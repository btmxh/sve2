#include "audio_pcm.h"

#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#include "sve2/media/ffmpeg_stream.h"
#include "sve2/utils/minmax.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

bool audio_pcm_open(context_t *ctx, audio_pcm_t *a, const char *path,
                    stream_index_t index) {
  a->ctx = ctx;
  a->cur_index = 0;

  // we load the data using FFmpeg, and we will do the resampling manually, so
  // we use the base ffmpeg_stream_t type
  ffmpeg_stream_t stream;
  if (!ffmpeg_stream_open(ctx, &stream, path, index, false)) {
    return false;
  }

  struct SwrContext *resampler = NULL;
  nassert_ffmpeg(swr_alloc_set_opts2(
      &resampler, ctx->info.ch_layout, ctx->info.sample_fmt,
      ctx->info.sample_rate, &stream.cdc_ctx->ch_layout,
      stream.cdc_ctx->sample_fmt, stream.cdc_ctx->sample_rate, 0, NULL));
  nassert_ffmpeg(swr_init(resampler));

  AVFrame *frame = ctx->temp_frames[0];
  while (ffmpeg_stream_get_frame(&stream, frame)) {
    // this submits the frame to the resampler context (which acts as an audio
    // FIFO buffer). We can retrieve the audio PCM data in one go later.
    swr_convert_frame(resampler, NULL, frame);
    av_frame_unref(frame);
  }

  a->sample_size = av_get_bytes_per_sample(ctx->info.sample_fmt) *
                   ctx->info.ch_layout->nb_channels;
  // this might be incorrect, so we will reassign it below
  a->num_samples = swr_get_out_samples(resampler, 0);
  a->buffer = (u8 *)sve2_malloc(a->num_samples * a->sample_size);
  nassert_ffmpeg(a->num_samples =
                     swr_convert(resampler, (u8 *const[]){a->buffer},
                                 a->num_samples, NULL, 0));

  ffmpeg_stream_close(&stream);
  swr_free(&resampler);

  return true;
}

void audio_pcm_close(audio_pcm_t *a) { free(a->buffer); }

void audio_pcm_seek(audio_pcm_t *a, i64 time) {
  // convert from ns to 1/sample_rate unit
  a->cur_index = time * a->ctx->info.sample_rate / SVE2_NS_PER_SEC;
}
void audio_pcm_get_samples(audio_pcm_t *a, i32 num_samples[static 1],
                           u8 *samples) {
  *num_samples = sve2_min_i32(*num_samples, a->num_samples - a->cur_index);
  memcpy(samples, a->buffer + a->cur_index * a->sample_size,
         *num_samples * a->sample_size);
  a->cur_index += *num_samples;
}
