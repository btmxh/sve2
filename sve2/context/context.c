#include "context.h"

#include <stdlib.h>
#include <threads.h>

#include <GLFW/glfw3.h>
#include <glad/egl.h>
#include <libavcodec/codec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/frame.h>
#include <log.h>

#include "sve2/ffmpeg/hw_texmap.h"
#include "sve2/ffmpeg/muxer.h"
#include "sve2/gl/shader.h"
#include "sve2/log/logging.h"
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
  context_t *c = (context_t *)device->pUserData;
  sve2_mtx_lock(&c->audio_fifo_mutex);
  i32 num_read = sve2_min_i32(nb_frames, av_audio_fifo_size(c->audio_fifo));
  nassert_ffmpeg(av_audio_fifo_read(c->audio_fifo, &output, num_read));
  sve2_mtx_unlock(&c->audio_fifo_mutex);
}

context_t *context_init(const context_init_t *info) {
  init_logging();
  init_threads_timer();

  context_t *c = sve2_calloc(1, sizeof *c);
  c->info = *info;
  c->frame_num = 0;
  c->num_samples = 0;
  c->xscale = 1.0;
  c->yscale = 1.0;

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
  // nassert((c->window = glfwCreateWindow(info->width, info->height,
  //                                       "sve2 window", NULL, NULL)));
  c->window =
      glfwCreateWindow(info->width, info->height, "sve2 window", NULL, NULL);
  glfwMakeContextCurrent(c->window);
  glfwSetWindowUserPointer(c->window, c);
  nassert(gladLoadGL(glfwGetProcAddress));
  nassert(gladLoadEGL(glfwGetEGLDisplay(), glfwGetProcAddress));

  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(gl_debug_callback, NULL);

  int width, height;
  glfwGetFramebufferSize(c->window, &width, &height);
  c->info.width = width;
  c->info.height = height;

  glfwSetFramebufferSizeCallback(c->window, glfw_framebuffer_callback);
  glfwSetWindowContentScaleCallback(c->window, glfw_content_scale_callback);

  shader_manager_init(&c->sman, "shaders/out");

  if (c->info.mode == CONTEXT_MODE_RENDER) {
    nassert(c->rctx.hw_frame = av_frame_alloc());
    nassert(c->rctx.transfer_frame = av_frame_alloc());
    nassert(c->rctx.color_convert_shader =
                shader_new_c(c, "encode_nv12.comp.glsl"));
    muxer_init(&c->rctx.muxer, c->info.output_path);

    const AVCodec *video_codec, *audio_codec;
    nassert(video_codec = avcodec_find_encoder_by_name("h264_vaapi"));
    nassert(audio_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE));
    c->rctx.video_si =
        muxer_new_stream(&c->rctx.muxer, c, video_codec, true, NULL, NULL);
    c->rctx.audio_si =
        muxer_new_stream(&c->rctx.muxer, c, audio_codec, false, NULL, NULL);
    muxer_begin(&c->rctx.muxer);

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
    c->rctx.uv_offset_y = height;
    hw_align_size(NULL, &c->rctx.uv_offset_y);
    i32 nv12_width = width, nv12_height = c->rctx.uv_offset_y + height / 2;
    glCreateTextures(GL_TEXTURE_2D, 1, &c->rctx.output_texture);
    glTextureStorage2D(c->rctx.output_texture, 1, GL_R8, nv12_width,
                       nv12_height);
  } else {
    i32 num_samples =
        c->info.sample_rate / c->info.fps * c->info.num_buffered_audio_frames;
    nassert(c->audio_fifo = av_audio_fifo_alloc(c->info.sample_fmt,
                                                c->info.ch_layout->nb_channels,
                                                num_samples));
    sve2_mtx_init(&c->audio_fifo_mutex, mtx_plain);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = c->info.ch_layout->nb_channels;
    config.sampleRate = c->info.sample_rate;
    config.dataCallback = ma_data_callback;
    config.pUserData = c;
    nassert(ma_device_init(NULL, &config, &c->audio_device) == MA_SUCCESS);
    nassert(ma_device_start(&c->audio_device) == MA_SUCCESS);
  }

  return c;
}

void context_free(context_t *c) {
  if (c->info.mode == CONTEXT_MODE_RENDER) {
    muxer_end(&c->rctx.muxer);
    muxer_free(&c->rctx.muxer);
    av_frame_free(&c->rctx.hw_frame);
    av_frame_free(&c->rctx.transfer_frame);
    glDeleteTextures(1, &c->rctx.output_texture);
    glDeleteTextures(1, &c->rctx.fbo_color_attachment);
    glDeleteFramebuffers(1, &c->rctx.fbo);
  } else {
    context_submit_audio_eof(c);
    ma_device_uninit(&c->audio_device);

    mtx_destroy(&c->audio_fifo_mutex);
    av_audio_fifo_free(c->audio_fifo);
  }

  shader_manager_free(&c->sman);
  free(c);
  glfwTerminate(); // free all windowing + OpenGL stuff,
                   // no need to manually free every resource

  done_logging();
}

void context_set_should_close(context_t *c, bool should_close) {
  glfwSetWindowShouldClose(c->window, should_close);
}

bool context_get_should_close(context_t *c) {
  return glfwWindowShouldClose(c->window);
}

i64 context_get_time(context_t *c) {
  switch (c->info.mode) {
  case CONTEXT_MODE_RENDER:
    return (i64)c->frame_num * SVE2_NS_PER_SEC / c->info.fps;
  default:
    return threads_timer_now();
  }
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
  log_debug("frame %" PRIi32 " started", c->frame_num);
  shader_manager_update(&c->sman);

  if (c->info.mode == CONTEXT_MODE_RENDER) {
    glBindFramebuffer(GL_FRAMEBUFFER, c->rctx.fbo);
  }

  i32 width, height;
  context_get_framebuffer_info(c, &width, &height, NULL, NULL);
  glViewport(0, 0, width, height);
}

void context_end_frame(context_t *c) {
  if (c->info.mode == CONTEXT_MODE_RENDER) {
    i32 width = c->info.width, height = c->info.height;
    nassert(shader_use(c->rctx.color_convert_shader) >= 0);
    glUniform1i(0, c->rctx.uv_offset_y);
    glUniform1i(1, true);
    glBindImageTexture(0, c->rctx.fbo_color_attachment, 0, GL_FALSE, 0,
                       GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, c->rctx.output_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                       GL_R8);
    glDispatchCompute(width / 2, height / 2, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    c->rctx.hw_frame->hw_frames_ctx = av_buffer_ref(
        c->rctx.muxer.encoders[c->rctx.video_si].c->hw_frames_ctx);
    c->rctx.hw_frame->width = width;
    c->rctx.hw_frame->height = height;
    c->rctx.hw_frame->pts = c->frame_num;
    hw_texture_t texture = hw_texture_from_gl(
        AV_PIX_FMT_NV12, 1, (GLuint[]){c->rctx.output_texture});
    hw_texmap_from_gl(&texture, c->rctx.transfer_frame, c->rctx.hw_frame);
    muxer_submit_frame(&c->rctx.muxer, c->rctx.hw_frame, c->rctx.video_si);
    hw_texmap_unmap(&texture, false);

    av_frame_unref(c->rctx.hw_frame);
    av_frame_unref(c->rctx.transfer_frame);
  } else {
    glfwSwapBuffers(c->window);
  }
  ++c->frame_num;
}

GLuint context_default_framebuffer(context_t *c) {
  return c->info.mode == CONTEXT_MODE_RENDER ? c->rctx.fbo : 0;
}

i64 context_get_audio_timer(context_t *c) {
  sve2_mtx_lock(&c->audio_fifo_mutex);
  i32 num_buffered_samples = av_audio_fifo_size(c->audio_fifo);
  i32 time = c->num_samples - num_buffered_samples;
  sve2_mtx_unlock(&c->audio_fifo_mutex);
  return (i64)time * SVE2_NS_PER_SEC / c->info.sample_rate;
}

bool context_audio_full(context_t *c) {
  sve2_mtx_lock(&c->audio_fifo_mutex);
  i32 nb_samples =
      c->info.sample_rate / c->info.fps * c->info.num_buffered_audio_frames;
  bool full = av_audio_fifo_size(c->audio_fifo) >= nb_samples;
  sve2_mtx_unlock(&c->audio_fifo_mutex);
  return full;
}

void context_submit_audio(context_t *c, AVFrame *audio_frame) {
  if (c->info.mode == CONTEXT_MODE_RENDER) {
    audio_frame->pts = c->num_samples;
    muxer_submit_frame(&c->rctx.muxer, audio_frame, c->rctx.audio_si);
  } else {
    sve2_mtx_lock(&c->audio_fifo_mutex);
    av_audio_fifo_write(c->audio_fifo, (void *const *)audio_frame->data,
                        audio_frame->nb_samples);
    sve2_mtx_unlock(&c->audio_fifo_mutex);
  }

  c->num_samples += audio_frame->nb_samples;
  av_frame_unref(audio_frame);
}

void context_submit_audio_eof(context_t *c) {
  if (c->info.mode == CONTEXT_MODE_PREVIEW) {
    sve2_mtx_lock(&c->audio_fifo_mutex);
    c->audio_eof = true;
    sve2_mtx_unlock(&c->audio_fifo_mutex);
  }
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
