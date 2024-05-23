#include <assert.h>

#include <glad/gles2.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include "sve2/context/context.h"
#include "sve2/ffmpeg/decoder.h"
#include "sve2/ffmpeg/demuxer.h"
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
  AVFrame *converted_frame = av_frame_alloc();
  assert(decode_frame && converted_frame);

  for (i32 i = 0; i < 10; ++i) {
    decode_result_t err = decoder_decode(&vdec, decode_frame, SVE_DEADLINE_INF);
    if (err == DECODE_EOF) {
      break;
    }

    nassert(err == DECODE_SUCCESS);

    transfered_frame->format = vdec.cc->sw_pix_fmt;
    nassert(av_hwframe_transfer_data(transfered_frame, decode_frame, 0) >= 0);

    nassert(sws_scale_frame(sws, converted_frame, transfered_frame) >= 0);
    char filename[100];
    nassert(snprintf(filename, sizeof filename, "frames/%02" PRIi32 ".jpg",
                     i + 1) >= 0);
    log_debug("writing frame %" PRIi32 " to '%s'", i + 1, filename);
    nassert(stbi_write_jpg(filename, converted_frame->width,
                           converted_frame->height, 3, converted_frame->data[0],
                           100));

    av_frame_unref(decode_frame);
    av_frame_unref(converted_frame);
    av_frame_unref(transfered_frame);
  }

  for (i32 i = 0; i < 5; ++i) {
    decode_result_t err = decoder_decode(&adec, decode_frame, SVE_DEADLINE_INF);
    if (err == DECODE_EOF) {
      break;
    }

    nassert(err == DECODE_SUCCESS);
    nassert(swr_convert(swr, NULL, 0, (const u8 **)decode_frame->data,
                        decode_frame->nb_samples) >= 0);
  }

  FILE *audio = fopen("audio.pcm", "w");
  i32 num_samples = swr_get_out_samples(swr, 0);
  u8 *buffer =
      sve2_calloc(num_samples * 2, av_get_bytes_per_sample(AV_SAMPLE_FMT_S16));
  nassert((num_samples = swr_convert(swr, &buffer, num_samples, NULL, 0)) >= 0);
  fwrite(buffer, av_get_bytes_per_sample(AV_SAMPLE_FMT_S16), num_samples * 2,
         audio);
  nassert(!ferror(audio));
  free(buffer);
  nassert(!fclose(audio));

  av_frame_free(&decode_frame);
  av_frame_free(&transfered_frame);
  av_frame_free(&converted_frame);
  swr_free(&swr);
  sws_freeContext(sws);
  decoder_free(&vdec);
  decoder_free(&adec);
  demuxer_free(&d);
  avformat_close_input(&fc);

  context_free(c);

  return 0;
}
