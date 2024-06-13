#include <assert.h>

#include <glad/egl.h>
#include <glad/gl.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libdrm/drm_fourcc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <log.h>

#include "sve2/ffmpeg/encoder.h"
#include "sve2/ffmpeg/hw_texmap.h"
#include "sve2/ffmpeg/muxer.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "sve2/context/context.h"
#include "sve2/ffmpeg/decoder.h"
#include "sve2/ffmpeg/demuxer.h"
#include "sve2/gl/shader.h"
#include "sve2/log/logging.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    raw_log_panic("usage: %s <media file>\n", argv[0]);
  }

  init_logging();
  init_threads_timer();

  context_t *c;
  nassert(c = context_init(&(context_init_t){
              .mode = CONTEXT_MODE_RENDER,
              .width = 1920,
              .height = 1080,
              .fps = 60,
          }));

  shader_t *shader = shader_new_vf(c, "y_uv.vert.glsl", "y_uv.frag.glsl");
  shader_t *encode_shader = shader_new_c(c, "encode_nv12.comp.glsl");

  AVFormatContext *fc = open_media(argv[1]);
  assert(fc);

  demuxer_t d;
  demuxer_stream_t streams[] = {
      (demuxer_stream_t){.index = stream_index_video(0)},
      (demuxer_stream_t){.index = stream_index_audio(0)},
  };
  demuxer_init(&d, &(demuxer_init_t){.fc = fc,
                                     .streams = streams,
                                     .num_streams = sve2_arrlen(streams),
                                     .num_buffered_packets = 8});
  nassert(streams[0].index == 0);
  nassert(streams[1].index == 1);

  decoder_t vdec, adec;
  nassert(decoder_init(&vdec, fc, &streams[0], true));
  nassert(decoder_init(&adec, fc, &streams[1], false));

  demuxer_cmd_seek(&d, -1, 10 * AV_TIME_BASE, 0);
  decoder_wait_for_seek(&vdec, SVE_DEADLINE_INF);
  decoder_wait_for_seek(&adec, SVE_DEADLINE_INF);

  glfwSwapInterval(0);

  struct SwrContext *swr;
  int err =
      swr_alloc_set_opts2(&swr, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO,
                          AV_SAMPLE_FMT_S16, 48000, &adec.cc->ch_layout,
                          adec.cc->sample_fmt, adec.cc->sample_rate, 0, NULL);
  nassert(swr && err >= 0 && swr_init(swr) >= 0);

  muxer_t wav;
  muxer_init(&wav, "output.mkv");
  i32 out_video_si = muxer_new_stream(
      &wav, c, avcodec_find_encoder_by_name("h264_vaapi"), true, NULL, NULL);
  i32 out_audio_si = muxer_new_stream(
      &wav, c, avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE), false, NULL, NULL);

  AVFrame *decode_frame = av_frame_alloc();
  AVFrame *encode_frame = av_frame_alloc();
  AVFrame *transfered_frame = av_frame_alloc();
  assert(decode_frame && transfered_frame);

  GLuint fbo, fbo_color_attachment, nv12_texture;
  i32 fbo_width, fbo_height;
  context_get_framebuffer_info(c, &fbo_width, &fbo_height, NULL, NULL);
  glCreateFramebuffers(1, &fbo);
  glCreateTextures(GL_TEXTURE_2D, 1, &fbo_color_attachment);
  glTextureStorage2D(fbo_color_attachment, 1, GL_RGBA32F, fbo_width,
                     fbo_height);
  glTextureParameteri(fbo_color_attachment, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(fbo_color_attachment, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, fbo_color_attachment, 0);
  nassert(glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) ==
          GL_FRAMEBUFFER_COMPLETE);
  i32 uv_offset_y = fbo_height;
  hw_align_size(NULL, &uv_offset_y);
  assert(uv_offset_y == 1088);
  i32 nv12_width = fbo_width, nv12_height = uv_offset_y + fbo_height / 2;
  glCreateTextures(GL_TEXTURE_2D, 1, &nv12_texture);
  glTextureStorage2D(nv12_texture, 1, GL_R8, nv12_width, nv12_height);
  glTextureParameteri(nv12_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(nv12_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  muxer_begin(&wav);
  for (i32 i = 0; i < 100; ++i) {
    decode_result_t err = decoder_decode(&vdec, decode_frame, SVE_DEADLINE_INF);
    if (err == DECODE_EOF) {
      break;
    }

    nassert(err == DECODE_SUCCESS);

    context_begin_frame(c);

    i32 width, height;
    context_get_framebuffer_info(c, &width, &height, NULL, NULL);
    glViewport(0, 0, width, height);

    if (shader_use(shader) >= 0) {
      hw_texture_t texture = hw_texture_blank(decoder_get_sw_format(&vdec));
      hw_texmap_to_gl(decode_frame, transfered_frame, &texture);
      for (i32 i = 0; i < sve2_arrlen(texture.textures); ++i) {
        if (!texture.textures[i]) {
          continue;
        }
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, texture.textures[i]);
        log_trace("binding texture %u to bind slot %" PRIi32,
                  texture.textures[i], i);
      }

      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glViewport(0, 0, fbo_width, fbo_height);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      hw_texmap_unmap(&texture, true);
    }

    context_end_frame(c);

    av_frame_unref(decode_frame);
    av_frame_unref(transfered_frame);

    nassert(shader_use(encode_shader) >= 0);
    glUniform1i(0, uv_offset_y);
    glBindImageTexture(0, fbo_color_attachment, 0, GL_FALSE, 0, GL_READ_ONLY,
                       GL_RGBA32F);
    glBindImageTexture(1, nv12_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);
    glDispatchCompute(fbo_width / 2, fbo_height / 2, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    /* char *img = sve2_malloc(nv12_width * nv12_height); */
    /* glGetTextureImage(nv12_texture, 0, GL_RED, GL_UNSIGNED_BYTE, */
    /*                   nv12_width * nv12_height, img); */

    /* char *y = sve2_malloc(fbo_width * fbo_height), */
    /*      *uv = sve2_malloc(fbo_width * fbo_height / 2); */
    /* glPixelStorei(GL_UNPACK_ALIGNMENT, 1); */
    /* glPixelStorei(GL_PACK_ALIGNMENT, 1); */
    /* glGetTextureSubImage(nv12_texture, 0, 0, 0, 0, fbo_width, fbo_height, 1,
     */
    /*                      GL_RED, GL_UNSIGNED_BYTE, fbo_width * fbo_height,
     * y); */
    /* glGetTextureSubImage(nv12_texture, 0, 0, fbo_height, 0, fbo_width, */
    /*                      fbo_height / 2, 1, GL_RED, GL_UNSIGNED_BYTE, */
    /*                      fbo_width * fbo_height, uv); */
    /* for (i32 j = 0; j < fbo_width * fbo_height / 4; ++j) { */
    /*   uv[j * 2] = uv[j * 2]; */
    /*   uv[j * 2 + 1] = -1; */
    /* } */
    /* char filename[32]; */
    /* nassert(snprintf(filename, sizeof filename, "frames/y_%02d.png", i) >=
     * 0); */
    /* stbi_write_png(filename, nv12_width, nv12_height, 1, img, fbo_width); */
    /* free(img); */
    /* FILE* f = fopen("frames/yuv_%02d.raw", "w"); */
    /* fwrite(y, 1, fbo_width * fbo_height, f); */
    /* fwrite(uv, 1, fbo_width * fbo_height / 2, f); */
    /* fclose(f); */
    /* stbi_flip_vertically_on_write(true); */
    /* stbi_write_png(filename, fbo_width, fbo_height, 1, y, fbo_width); */
    /* nassert(snprintf(filename, sizeof filename, "frames/uv_%02d.png", i) >=
     * 0); */
    /* stbi_write_png(filename, fbo_width / 2, fbo_height / 2, 2, y, fbo_width);
     */
    /* free(y); */
    /* free(uv); */

    nassert_ffmpeg(av_hwframe_get_buffer(
        wav.encoders[out_video_si].c->hw_frames_ctx, encode_frame, 0));
    encode_frame->width = fbo_width;
    encode_frame->height = fbo_height;
    encode_frame->pts = i;
    hw_texture_t texture =
        hw_texture_from_gl(AV_PIX_FMT_NV12, 1, (GLuint[]){nv12_texture});
    hw_texmap_from_gl(&texture, transfered_frame, encode_frame);
    muxer_submit_frame(&wav, encode_frame, out_video_si);
    hw_texmap_unmap(&texture, false);

    av_frame_unref(encode_frame);
    av_frame_unref(transfered_frame);
    av_frame_unref(decode_frame);

  }

  i64 next_pts = 0;
  for (i32 i = 0; i < 10; ++i) {
    nassert(decoder_decode(&adec, decode_frame, SVE_DEADLINE_INF) ==
            DECODE_SUCCESS);
    av_channel_layout_copy(&transfered_frame->ch_layout,
                           &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO);
    transfered_frame->sample_rate = 48000;
    transfered_frame->format = AV_SAMPLE_FMT_S16;
    nassert(swr_convert_frame(swr, transfered_frame, decode_frame) >= 0);
    transfered_frame->pts = next_pts;
    next_pts += transfered_frame->nb_samples;
    muxer_submit_frame(&wav, transfered_frame, out_audio_si);
  }
  muxer_end(&wav);

  muxer_free(&wav);
  av_frame_free(&decode_frame);
  av_frame_free(&encode_frame);
  av_frame_free(&transfered_frame);
  swr_free(&swr);
  decoder_free(&vdec);
  decoder_free(&adec);
  demuxer_free(&d);
  avformat_close_input(&fc);

  context_free(c);
  done_logging();

  return 0;
}
