#include <assert.h>

#include <glad/gl.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <log.h>

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
              .mode = CONTEXT_MODE_PREVIEW,
              .width = 1920,
              .height = 1080,
              .fps = 60,
          }));

  shader_t *shader = shader_new_vf(c, "y_uv.vert.glsl", "y_uv.frag.glsl");
  i32 shader_version = -1;

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

  demuxer_cmd_seek(&d, -1, 5 * AV_TIME_BASE, 0);
  sve2_sleep_for(SVE2_NS_PER_SEC / 10);
  glfwSwapInterval(0);

  struct SwsContext *sws;
  struct SwrContext *swr;
  sws = sws_getContext(vdec.cc->width, vdec.cc->height, AV_PIX_FMT_NV12,
                       vdec.cc->width, vdec.cc->height, AV_PIX_FMT_RGB24,
                       SWS_FAST_BILINEAR, NULL, NULL, NULL);
  int err =
      swr_alloc_set_opts2(&swr, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO,
                          AV_SAMPLE_FMT_S16, 48000, &adec.cc->ch_layout,
                          adec.cc->sample_fmt, adec.cc->sample_rate, 0, NULL);
  nassert(sws && swr && err >= 0 && swr_init(swr) >= 0);

  AVFrame *decode_frame = av_frame_alloc();
  AVFrame *transfered_frame = av_frame_alloc();
  assert(decode_frame && transfered_frame);

  i64 start = threads_timer_now() - 5 * SVE2_NS_PER_SEC;

  for (i32 i = 0; i < 100000; ++i) {
    decode_result_t err = decoder_decode(&vdec, decode_frame, SVE_DEADLINE_INF);
    if (err == DECODE_EOF) {
      break;
    }

    nassert(err == DECODE_SUCCESS);

    decode_texture_t texture = decoder_blank_texture();
    decoder_map_texture(&vdec, decode_frame, transfered_frame, &texture);

    i64 deadline = start + decode_frame->pts * fc->streams[0]->time_base.num *
                               SVE2_NS_PER_SEC / fc->streams[0]->time_base.den;
    /* sve2_sleep_until(deadline); */
    context_begin_frame(c);

    i32 width, height;
    context_get_framebuffer_info(c, &width, &height, NULL, NULL);
    glViewport(0, 0, width, height);

    i32 version = shader_use(shader);
    const AVPixFmtDescriptor *format_desc = av_pix_fmt_desc_get(texture.format);
    if (version >= 0) {
      for (i32 i = 0; i < sve2_arrlen(texture.textures); ++i) {
        if (!texture.textures[i]) {
          continue;
        }
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, texture.textures[i]);
        log_trace("binding texture %u to bind slot %" PRIi32,
                  texture.textures[i], i);
      }
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      shader_version = version;
    }

    decoder_unmap_texture(&texture);
    context_end_frame(c);

    av_frame_unref(decode_frame);
    av_frame_unref(transfered_frame);
  }

  av_frame_free(&decode_frame);
  av_frame_free(&transfered_frame);
  swr_free(&swr);
  sws_freeContext(sws);
  decoder_free(&vdec);
  decoder_free(&adec);
  demuxer_free(&d);
  avformat_close_input(&fc);

  context_free(c);

  return 0;
}
