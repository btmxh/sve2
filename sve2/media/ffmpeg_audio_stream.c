#include "ffmpeg_audio_stream.h"

#include <libswresample/swresample.h>

#include "sve2/utils/minmax.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

bool ffmpeg_audio_stream_open(context_t *c, ffmpeg_audio_stream_t *a,
                              const char *path, stream_index_t index) {
  if (!ffmpeg_stream_open(c, &a->base, path, index, false)) {
    return false;
  }

  nassert_ffmpeg(swr_alloc_set_opts2(
      &a->audio_resampler, c->info.ch_layout, c->info.sample_fmt,
      c->info.sample_rate, &a->base.cdc_ctx->ch_layout,
      a->base.cdc_ctx->sample_fmt, a->base.cdc_ctx->sample_rate, 0, NULL));
  nassert_ffmpeg(swr_init(a->audio_resampler));

  return true;
}

void ffmpeg_audio_stream_close(ffmpeg_audio_stream_t *a) {
  swr_free(&a->audio_resampler);
  ffmpeg_stream_close(&a->base);
}

void ffmpeg_audio_stream_seek(ffmpeg_audio_stream_t *a, i64 time) {
  ffmpeg_stream_seek(&a->base, time);

  // flush audio buffer
  nassert_ffmpeg(swr_convert(a->audio_resampler, NULL, 0, NULL, 0));

  AVFrame *audio_frame = a->base.ctx->temp_frames[0];
  do {
    av_frame_unref(audio_frame);
    if (!ffmpeg_stream_get_frame(&a->base, audio_frame)) {
      return;
    }
  } while (audio_frame->pts + audio_frame->duration < time);

  nassert_ffmpeg(swr_convert(a->audio_resampler, NULL, 0,
                             (const u8 *const *)audio_frame->data,
                             audio_frame->nb_samples));

  i32 num_dropped = (time - audio_frame->pts) * SVE2_NS_PER_SEC /
                    a->base.cdc_ctx->sample_rate;
  num_dropped = sve2_min_i32(num_dropped, audio_frame->nb_samples);
  num_dropped = sve2_max_i32(num_dropped, 0);
  nassert_ffmpeg(swr_drop_output(a->audio_resampler, num_dropped));

  av_frame_unref(audio_frame);
}

static bool convert_samples(SwrContext *swr, AVFrame *frame,
                            i32 *num_samples_left, u8 **samples,
                            i32 sample_size) {
  const u8 *const *data = frame ? (const u8 *const *)frame->data : NULL;
  i32 num_samples_in = frame ? frame->nb_samples : 0;
  i32 num_samples_read;
  nassert_ffmpeg(num_samples_read =
                     swr_convert(swr, (u8 *const[]){*samples},
                                 *num_samples_left, data, num_samples_in));
  *num_samples_left -= num_samples_read;
  *samples += sample_size * num_samples_read;
  return *num_samples_left > 0;
}

void ffmpeg_audio_stream_get_samples(ffmpeg_audio_stream_t *a,
                                     i32 num_samples[static 1], u8 *samples) {
  i32 sample_size = av_get_bytes_per_sample(a->base.ctx->info.sample_fmt) *
                    a->base.cdc_ctx->ch_layout.nb_channels;
  i32 num_samples_left = *num_samples;
  AVFrame *frame = NULL;
  while (convert_samples(a->audio_resampler, frame, &num_samples_left, &samples,
                         sample_size)) {
    av_frame_unref(frame);
    frame = a->base.ctx->temp_frames[0];
    if (!ffmpeg_stream_get_frame(&a->base, frame)) {
      break;
    }
  }

  av_frame_unref(frame);
  *num_samples = *num_samples - num_samples_left;
}
