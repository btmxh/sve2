#include <libswresample/swresample.h>

#include "sve2/context/context.h"
#include "sve2/ffmpeg/decoder.h"
#include "sve2/ffmpeg/demuxer.h"
#include "sve2/ffmpeg/encoder.h"
#include "sve2/ffmpeg/hw_texmap.h"
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
              .fps = 75,
              .output_path = "output.mkv",
              .sample_rate = 48000,
          }));

  shader_t *shader = shader_new_vf(c, "y_uv.vert.glsl", "y_uv.frag.glsl");

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

  AVFrame *decode_frame = av_frame_alloc();
  AVFrame *transfered_frame = av_frame_alloc();
  assert(decode_frame && transfered_frame);

  for (i32 i = 0;
       i < 100 && (err = decoder_decode(&vdec, decode_frame,
                                        SVE_DEADLINE_INF)) != DECODE_EOF;
       ++i) {
    nassert(err == DECODE_SUCCESS);
    context_begin_frame(c);

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

      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      hw_texmap_unmap(&texture, true);
    }

    av_frame_unref(decode_frame);
    av_frame_unref(transfered_frame);

    context_end_frame(c);
  }

  i64 next_pts = 0;
  for (i32 i = 0; i < 500; ++i) {
    nassert(decoder_decode(&adec, decode_frame, SVE_DEADLINE_INF) ==
            DECODE_SUCCESS);
    av_channel_layout_copy(&transfered_frame->ch_layout,
                           &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO);
    transfered_frame->sample_rate = 48000;
    transfered_frame->format = AV_SAMPLE_FMT_S16;
    nassert(swr_convert_frame(swr, transfered_frame, decode_frame) >= 0);
    transfered_frame->pts = next_pts;
    next_pts += transfered_frame->nb_samples;
    context_submit_audio(c, transfered_frame);
  }

  av_frame_free(&decode_frame);
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
