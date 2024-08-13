#include "context.h"

#include <stdlib.h>
#include <threads.h>

#include <GLFW/glfw3.h>
#include <glad/egl.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/samplefmt.h>
#include <libdrm/drm.h>
#include <libdrm/drm_fourcc.h>
#include <log.h>
#include <time.h>

#include "sve2/gl/shader.h"
#include "sve2/log/logging.h"
#include "sve2/media/output_ctx.h"
#include "sve2/utils/minmax.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"
#include "sve2/utils/types.h"

#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>

void glfw_error_callback(int error_code, const char *description) {
  log_warn("GLFW error %d: %s", error_code, description);
}

static void GLAD_API_PTR gl_debug_callback(GLenum source, GLenum type,
                                           GLuint id, GLenum severity,
                                           GLsizei length,
                                           const GLchar *message,
                                           const void *userParam) {
  (void)length;
  (void)userParam;
  i32 level;
  const char *src_str;
  const char *type_str;
  switch (source) {
  case GL_DEBUG_SOURCE_API:
    src_str = "API";
    break;
  case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
    src_str = "WINDOW SYSTEM";
    break;
  case GL_DEBUG_SOURCE_SHADER_COMPILER:
    src_str = "SHADER COMPILER";
    break;
  case GL_DEBUG_SOURCE_THIRD_PARTY:
    src_str = "THIRD PARTY";
    break;
  case GL_DEBUG_SOURCE_APPLICATION:
    src_str = "APPLICATION";
    break;
  default:
    src_str = "OTHER";
    break;
  }

  switch (type) {
  case GL_DEBUG_TYPE_ERROR:
    type_str = "ERROR";
    break;
  case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    type_str = "DEPRECATED_BEHAVIOR";
    break;
  case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    type_str = "UNDEFINED_BEHAVIOR";
    break;
  case GL_DEBUG_TYPE_PORTABILITY:
    type_str = "PORTABILITY";
    break;
  case GL_DEBUG_TYPE_PERFORMANCE:
    type_str = "PERFORMANCE";
    break;
  case GL_DEBUG_TYPE_MARKER:
    type_str = "MARKER";
    break;
  default:
    type_str = "OTHER";
    break;
  }

  switch (severity) {
  case GL_DEBUG_SEVERITY_NOTIFICATION:
    level = LOG_INFO;
    break;
  case GL_DEBUG_SEVERITY_LOW:
    level = LOG_DEBUG;
    break;
  case GL_DEBUG_SEVERITY_MEDIUM:
    level = LOG_WARN;
    break;
  case GL_DEBUG_SEVERITY_HIGH:
    level = LOG_ERROR;
    break;
  default:
    level = LOG_FATAL;
    break;
  }

  log_log(level, __FILE__, __LINE__, "GL: %s (source: %s, type: %s, id: %d)",
          message, src_str, type_str, (int)id);
}

static void glfw_framebuffer_callback(GLFWwindow *w, int width, int height) {
  context_t *c = context_get_from_window(w);
  c->info.width = width;
  c->info.height = height;
}

static void glfw_content_scale_callback(GLFWwindow *w, float xscale,
                                        float yscale) {
  context_t *c = context_get_from_window(w);
  c->xscale = xscale;
  c->yscale = yscale;
}

enum {
  OUT_VIDEO_SI = 0,
  OUT_AUDIO_SI = 1,
};

void ma_data_callback(ma_device *device, void *output, const void *input,
                      u32 nb_frames) {
  (void)input;
  preview_context_t *c = &((context_t *)device->pUserData)->pctx;
  sve2_mtx_lock(&c->audio_fifo_mutex);
  i32 num_read = sve2_min_i32(nb_frames, av_audio_fifo_size(c->audio_fifo));
  nassert_ffmpeg(av_audio_fifo_read(c->audio_fifo, &output, num_read));
  sve2_mtx_unlock(&c->audio_fifo_mutex);
}

static ma_format get_ma_sample_format(enum AVSampleFormat format) {
  switch (format) {
  case AV_SAMPLE_FMT_U8:
    return ma_format_u8;
  case AV_SAMPLE_FMT_S16:
    return ma_format_s16;
  case AV_SAMPLE_FMT_S32:
    return ma_format_s32;
  case AV_SAMPLE_FMT_FLT:
    return ma_format_f32;
  default:
    return ma_format_unknown;
  }
}

typedef struct {
  struct {
    i32 x, y;
  } offset_y, offset_uv;
  i32 tex_width, tex_height;
} nv12_output_frame_offsets_t;

static i32 round_up_to_multiple_of(i32 x, i32 po2) {
  return (i32)((u32)(x + po2 - 1) & ~(u32)(po2 - 1));
}

// this is how stuff is aligned on intel GPUs
// https://github.com/intel/hwc/blob/master/lib/ufo/graphics.h
void hw_align_size(i32 *width, i32 *height) {
  if (width) {
    *width = round_up_to_multiple_of(*width, 128);
  }
  if (height) {
    *height = round_up_to_multiple_of(*height, 64);
  }
}

static nv12_output_frame_offsets_t calc_out_frame_offsets(i32 width,
                                                          i32 height) {
  // NV12 is structured (vertically, horizontal layout is straightforward)
  // similar to a C struct like so: struct NV12 {
  //   Region y_plane;
  //   Region uv_plane; // interleaved
  // }
  // Here, offsetof(y_plane) = 0
  // Here, alignof(Region) can be retrieved by hw_align_size, we are only
  // intere
  // TODO: NV12-specific hack
  // trivial offsets
  nv12_output_frame_offsets_t offsets = {
      .offset_y = {0, 0},
      .offset_uv = {0, height},
      .tex_width = width,
      .tex_height = height,
  };

  // we ignores the zero offsets
  hw_align_size(NULL, &offsets.offset_uv.y);
  offsets.tex_height = offsets.offset_uv.y + height / 2;
  hw_align_size(&offsets.tex_width, &offsets.tex_height);
  return offsets;
}

static void remap_drm_prime(render_context_t *r, EGLImage *image, i32 width,
                            i32 height) {
  nv12_output_frame_offsets_t offsets = calc_out_frame_offsets(width, height);
  nassert((*image = eglCreateImage(eglGetCurrentDisplay(),
                                   eglGetCurrentContext(), EGL_GL_TEXTURE_2D,
                                   (EGLClientBuffer)(size_t)r->output_texture,
                                   NULL)));

  EGLint num_planes;
  EGLuint64KHR mods;
  eglExportDMABUFImageQueryMESA(eglGetCurrentDisplay(), *image, NULL,
                                &num_planes, &mods);
  assert(num_planes == 1);

  EGLint fd, stride, offset;
  eglExportDMABUFImageMESA(eglGetCurrentDisplay(), *image, &fd, &stride,
                           &offset);

  AVFrame *prime_frame = r->output_video_texture_prime;
  AVDRMFrameDescriptor *prime =
      (AVDRMFrameDescriptor *)prime_frame->buf[0]->data;
  // DRM PRIME with one object:
  prime->nb_objects = 1;
  prime->objects[0] = (AVDRMObjectDescriptor){
      .fd = fd,
      .size = offsets.tex_width * offsets.tex_height,
      .format_modifier = mods,
  };
  // Two layers, each with one plane:
  prime->nb_layers = 2;
  prime->layers[0] = (AVDRMLayerDescriptor){
      .nb_planes = 1,
      .planes =
          {
              {
                  .pitch = stride,
                  .offset =
                      offset + offsets.offset_y.y * stride + offsets.offset_y.x,
                  .object_index = 0,
              },
          },
      .format = DRM_FORMAT_R8,
  };
  prime->layers[1] = (AVDRMLayerDescriptor){
      .nb_planes = 1,
      .planes =
          {
              {
                  .pitch = stride,
                  .offset = offset + offsets.offset_uv.y * stride +
                            offsets.offset_uv.x,
                  .object_index = 0,
              },
          },
      .format = DRM_FORMAT_RG88,
  };
}

context_t *context_init(const context_init_t *info) {
  // initialize core libraries
  init_logging();
  init_threads_timer();

  context_t *c = sve2_calloc(1, sizeof *c);
  c->info = *info;
  c->xscale = 1.0;
  c->yscale = 1.0;

  // GLFW initialization
  nassert(
      !glfwSetErrorCallback(glfw_error_callback) &&
      "Existing GLFW error callback is overriden. Consider setting the GLFW "
      "error callback after context initialization.");
  nassert(glfwInit() && "GLFW initialization failed");

  glfwDefaultWindowHints();
  // off-screen rendering if not in preview mode
  glfwWindowHint(GLFW_VISIBLE, info->mode == CONTEXT_MODE_PREVIEW);
  // we require EGL + OpenGL ES for VAAPI integration extensions
  glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
  glfwWindowHint(GLFW_CONTEXT_DEBUG, GLFW_TRUE);
  nassert((c->window = glfwCreateWindow(info->width, info->height,
                                        "sve2 window", NULL, NULL)));
  glfwMakeContextCurrent(c->window);
  glfwSetWindowUserPointer(c->window, c);

  // GLAD function loading
  nassert(gladLoadGL(glfwGetProcAddress));
  nassert(gladLoadEGL(glfwGetEGLDisplay(), glfwGetProcAddress));

  // enable debug output
  // comment these lines (and set GLFW_CONTEXT_DEBUG to GLFW_FALSE above to
  // disable this)
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(gl_debug_callback, NULL);

  // get framebuffer size
  // the window size might be different to the size specified in
  // glfwCreateWindow (e.g. if one is using a tiling WM (me))
  int width, height;
  glfwGetFramebufferSize(c->window, &width, &height);
  c->info.width = width;
  c->info.height = height;

  // GLFW callback
  glfwSetFramebufferSizeCallback(c->window, glfw_framebuffer_callback);
  glfwSetWindowContentScaleCallback(c->window, glfw_content_scale_callback);

  shader_manager_init(&c->sman, "shaders/out");

  for (i32 i = 0; i < sve2_arrlen(c->temp_frames); ++i) {
    nassert(c->temp_frames[i] = av_frame_alloc());
  }
  nassert(c->temp_packet = av_packet_alloc());

  // mode-specific initialization
  if (c->info.mode == CONTEXT_MODE_RENDER) {
    nassert(c->rctx.audio_mapping_frame = av_frame_alloc());

    const AVCodec *video_codec, *audio_codec;
    nassert(video_codec = avcodec_find_encoder_by_name("hevc_vaapi"));
    nassert(audio_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE));
    output_ctx_open(c, &c->rctx.output_ctx, c->info.output_path, 2,
                    (const AVCodec *[]){video_codec, audio_codec});

    nassert(c->rctx.output_video_texture_prime = av_frame_alloc());
    AVFrame *prime_frame = c->rctx.output_video_texture_prime;
    prime_frame->width = width;
    prime_frame->height = height;
    prime_frame->format = AV_PIX_FMT_DRM_PRIME;
    prime_frame->buf[0] = av_buffer_alloc(sizeof(AVDRMFrameDescriptor));
    prime_frame->data[0] = prime_frame->buf[0]->data;
    // TODO: this only supports RGB -> NV12 conversion
    nassert(c->rctx.color_convert_shader =
                shader_new_c(c, "encode_nv12.comp.glsl"));

    // create OpenGL capturing objects
    i32 width = c->info.width, height = c->info.height;
    glCreateFramebuffers(1, &c->rctx.fbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &c->rctx.fbo_color_attachment);
    glTextureStorage2D(c->rctx.fbo_color_attachment, 1, GL_RGBA32F, width,
                       height);
    glTextureParameteri(c->rctx.fbo_color_attachment, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR);
    glTextureParameteri(c->rctx.fbo_color_attachment, GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
    glNamedFramebufferTexture(c->rctx.fbo, GL_COLOR_ATTACHMENT0,
                              c->rctx.fbo_color_attachment, 0);
    nassert(glCheckNamedFramebufferStatus(c->rctx.fbo, GL_FRAMEBUFFER) ==
            GL_FRAMEBUFFER_COMPLETE);
    glCreateTextures(GL_TEXTURE_2D, 1, &c->rctx.output_texture);
    nv12_output_frame_offsets_t offsets = calc_out_frame_offsets(width, height);
    glTextureStorage2D(c->rctx.output_texture, 1, GL_R8, offsets.tex_width,
                       offsets.tex_height);
    c->rctx.offset_uv_y = offsets.offset_uv.y;
  } else {
    // allocate audio playback buffer and other things
    i32 num_samples =
        c->info.sample_rate * c->info.num_buffered_audio_frames / c->info.fps;
    nassert(
        c->pctx.audio_fifo = av_audio_fifo_alloc(
            c->info.sample_fmt, c->info.ch_layout->nb_channels, num_samples));
    // throughout this, we assumed that audio samples are interleaved (to
    // advance the pointer), therefore we require the sample format to be
    // non-planar
    c->pctx.audio_staging_buffer =
        sve2_malloc(num_samples * c->info.ch_layout->nb_channels *
                    av_get_bytes_per_sample(c->info.sample_fmt));
    sve2_mtx_init(&c->pctx.audio_fifo_mutex, mtx_plain);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    if ((config.playback.format = get_ma_sample_format(c->info.sample_fmt)) ==
        ma_format_unknown) {
      log_error("AVSampleFormat %s is not supported as output audio format",
                av_get_sample_fmt_name(c->info.sample_fmt));
      panic();
    }
    config.playback.channels = c->info.ch_layout->nb_channels;
    config.sampleRate = c->info.sample_rate;
    config.dataCallback = ma_data_callback;
    config.pUserData = c;
    // the period size is 1 frame
    config.periodSizeInFrames = c->info.sample_rate / c->info.fps;
    nassert(ma_device_init(NULL, &config, &c->pctx.audio_device) == MA_SUCCESS);
    nassert(ma_device_start(&c->pctx.audio_device) == MA_SUCCESS);
  }

  return c;
}

void context_free(context_t *c) {
  if (c->info.mode == CONTEXT_MODE_RENDER) {
    output_ctx_close(&c->rctx.output_ctx);
    eglDestroyImage(eglGetCurrentDisplay(), c->rctx.output_texture_image);
    av_frame_free(&c->rctx.output_video_texture_prime);
    glDeleteTextures(1, &c->rctx.output_texture);
    glDeleteTextures(1, &c->rctx.fbo_color_attachment);
    glDeleteFramebuffers(1, &c->rctx.fbo);
    av_frame_free(&c->rctx.audio_mapping_frame);
  } else {
    ma_device_uninit(&c->pctx.audio_device);
    mtx_destroy(&c->pctx.audio_fifo_mutex);
    av_audio_fifo_free(c->pctx.audio_fifo);
    sve2_freep(&c->pctx.audio_staging_buffer);
  }

  av_packet_free(&c->temp_packet);
  for (i32 i = 0; i < sve2_arrlen(c->temp_frames); ++i) {
    av_frame_free(&c->temp_frames[i]);
  }

  shader_manager_free(&c->sman);
  free(c);
  glfwTerminate(); // free all windowing + OpenGL stuff,
                   // no need to manually free every resource

  done_logging();
}

// this is baked-in to GLFW, so nice
void context_set_should_close(context_t *c, bool should_close) {
  glfwSetWindowShouldClose(c->window, should_close);
}

bool context_get_should_close(context_t *c) {
  return glfwWindowShouldClose(c->window);
}

void context_get_framebuffer_info(context_t *c, i32 *w, i32 *h, f32 *xscale,
                                  f32 *yscale) {
  // clang-format off
  if(w) *w = c->info.width;
  if(h) *h = c->info.height;
  if(xscale) *xscale = c->xscale;
  if(yscale) *yscale = c->yscale;
  // clang-format on
}

void context_begin_frame(context_t *c) {
  glfwPollEvents();
  log_trace("frame %" PRIi32 " started", c->frame_num);
  shader_manager_update(&c->sman);

  if (c->info.mode == CONTEXT_MODE_RENDER) {
    glBindFramebuffer(GL_FRAMEBUFFER, c->rctx.fbo);
  }

  i32 width, height;
  context_get_framebuffer_info(c, &width, &height, NULL, NULL);
  glViewport(0, 0, width, height);

  c->num_frame_samples = 0;
}

void context_pause_audio(context_t *c) {
  if (c->info.mode == CONTEXT_MODE_PREVIEW) {
    ma_device_stop(&c->pctx.audio_device);
  }
}
void context_play_audio(context_t *c) {
  if (c->info.mode == CONTEXT_MODE_PREVIEW) {
    ma_device_start(&c->pctx.audio_device);
  }
}

void context_end_frame(context_t *c) {
  if (c->info.mode == CONTEXT_MODE_RENDER) {
    // do color-conversion via compute shader
    i32 width = c->info.width, height = c->info.height;
    nassert(shader_use(c->rctx.color_convert_shader) >= 0);
    glUniform1i(0, c->rctx.offset_uv_y);
    glUniform1i(1, true);
    glBindImageTexture(0, c->rctx.fbo_color_attachment, 0, GL_FALSE, 0,
                       GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, c->rctx.output_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                       GL_R8);
    glDispatchCompute(width / 2, height / 2, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // submit frame to hardware-accelerated video encoder
    AVFrame *hw_frame = c->temp_frames[0];
    output_ctx_init_hwframe(&c->rctx.output_ctx, hw_frame, OUT_VIDEO_SI);
    hw_frame->pts = c->frame_num;
    EGLImage image;
    remap_drm_prime(&c->rctx, &image, width, height);
    nassert_ffmpeg(av_hwframe_map(hw_frame, c->rctx.output_video_texture_prime,
                                  AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT));
    output_ctx_submit_frame(&c->rctx.output_ctx, hw_frame, OUT_VIDEO_SI);
    eglDestroyImage(eglGetCurrentDisplay(), image);

    av_frame_unref(hw_frame);
  } else {
    glfwSwapBuffers(c->window);
  }
  ++c->frame_num;
}

GLuint context_default_framebuffer(context_t *c) {
  return c->info.mode == CONTEXT_MODE_RENDER ? c->rctx.fbo : 0;
}

void context_set_audio_timer(context_t *c, i64 time) {
  // this should make sense from seeing the implementation of
  // context_get_audio_timer()
  c->num_samples_from_last_seek = 0;
  c->audio_timer_offset = time;
}

i64 context_get_audio_timer(context_t *c) {
  i32 audio_fifo_size = 0;
  // in preview mode, there is a little difference:
  // samples submitted to the context might not have been played
  // the number of such samples can be found by getting the size of the audio
  // fifo buffer
  if (c->info.mode == CONTEXT_MODE_PREVIEW) {
    sve2_mtx_lock(&c->pctx.audio_fifo_mutex);
    audio_fifo_size = av_audio_fifo_size(c->pctx.audio_fifo);
    sve2_mtx_unlock(&c->pctx.audio_fifo_mutex);
  }
  //                             _______________ < this is `audio_fifo_size`
  //                             samples
  //      we need to count this v        |
  // ____________________________        v
  // XXXXXXXXXXXXXXXXXXXXXXXXXXXXOOOOOOOOOOOOOOO: c->num_samples_from_last_seek
  // samples in total
  //                            | --- samples submitted that has not been played
  i32 time = c->num_samples_from_last_seek - audio_fifo_size;
  // we add with the seek offset, and convert `time` (in samples unit) to
  // nanoseconds
  return c->audio_timer_offset + time * SVE2_NS_PER_SEC / c->info.sample_rate;
}

bool context_map_audio(context_t *c, u8 *staging_buffer[static 1],
                       i32 nb_samples[static 1]) {
  switch (c->info.mode) {
  case CONTEXT_MODE_PREVIEW:
    // in preview mode, we use the staging buffer directly
    // the number of samples is the remaining space in the audio fifo to not let
    // the fifo get too big
    sve2_mtx_lock(&c->pctx.audio_fifo_mutex);
    *nb_samples = av_audio_fifo_space(c->pctx.audio_fifo);
    *staging_buffer = c->pctx.audio_staging_buffer;
    sve2_mtx_unlock(&c->pctx.audio_fifo_mutex);
    break;
  case CONTEXT_MODE_RENDER:
    AVFrame *frame = c->rctx.audio_mapping_frame;
    // allocate memory for samples
    av_channel_layout_copy(&frame->ch_layout, c->info.ch_layout);
    frame->sample_rate = c->info.sample_rate;
    frame->nb_samples = frame->sample_rate / c->info.fps - c->num_frame_samples;
    frame->format = c->info.sample_fmt;
    if (frame->nb_samples <= 0) {
      return false;
    }
    nassert_ffmpeg(av_frame_get_buffer(frame, 0));

    *nb_samples = frame->nb_samples;
    *staging_buffer = frame->data[0];
    break;
  }

  // we only map samples if the number of writable samples is positive
  return *nb_samples > 0;
}

void context_unmap_audio(context_t *c, i32 nb_samples) {
  switch (c->info.mode) {
  case CONTEXT_MODE_PREVIEW:
    // write from staging buffer to audio fifo
    sve2_mtx_lock(&c->pctx.audio_fifo_mutex);
    av_audio_fifo_write(c->pctx.audio_fifo,
                        (void *[]){c->pctx.audio_staging_buffer}, nb_samples);
    sve2_mtx_unlock(&c->pctx.audio_fifo_mutex);
    break;
  case CONTEXT_MODE_RENDER:
    AVFrame *frame = c->rctx.audio_mapping_frame;
    frame->nb_samples = nb_samples;
    frame->pts = c->num_total_samples;
    // submit allocated frame to output context directly
    output_ctx_submit_frame(&c->rctx.output_ctx, frame, OUT_AUDIO_SI);
    av_frame_unref(frame);
    break;
  }

  // update sample counters
  c->num_total_samples += nb_samples;
  c->num_frame_samples += nb_samples;
  c->num_samples_from_last_seek += nb_samples;
}

void context_set_user_pointer(context_t *c, void *u) { c->user_ptr = u; }

void *context_get_user_pointer(GLFWwindow *window) {
  return context_get_from_window(window)->user_ptr;
}

context_t *context_get_from_window(GLFWwindow *window) {
  return glfwGetWindowUserPointer(window);
}

shader_manager_t *context_get_shader_manager(context_t *c) { return &c->sman; }

void context_set_key_callback(context_t *c, GLFWkeyfun key) {
  glfwSetKeyCallback(c->window, key);
}
