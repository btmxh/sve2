#include "audio.h"

#include <libavutil/samplefmt.h>

#include "sve2/media/audio_pcm.h"
#include "sve2/media/ffmpeg_audio_stream.h"

bool audio_open(context_t *ctx, audio_t *a, const char *path,
                stream_index_t index, audio_format_t format) {
  switch (a->format = format) {
  case AUDIO_FORMAT_FFMPEG_STREAM:
    return ffmpeg_audio_stream_open(ctx, &a->ffmpeg, path, index);
  case AUDIO_FORMAT_PCM_SAMPLES:
    return audio_pcm_open(ctx, &a->pcm, path, index);
  }

  return false;
}

void audio_close(audio_t *a) {
  switch (a->format) {
  case AUDIO_FORMAT_FFMPEG_STREAM:
    ffmpeg_audio_stream_close(&a->ffmpeg);
    break;
  case AUDIO_FORMAT_PCM_SAMPLES:
    audio_pcm_close(&a->pcm);
    break;
  }
}

void audio_seek(audio_t *a, i64 time) {
  switch (a->format) {
  case AUDIO_FORMAT_FFMPEG_STREAM:
    ffmpeg_audio_stream_seek(&a->ffmpeg, time);
    break;
  case AUDIO_FORMAT_PCM_SAMPLES:
    audio_pcm_seek(&a->pcm, time);
    break;
  }
}

void audio_get_samples(audio_t *a, i32 num_samples[static 1], u8 *samples) {
  switch (a->format) {
  case AUDIO_FORMAT_FFMPEG_STREAM:
    ffmpeg_audio_stream_get_samples(&a->ffmpeg, num_samples, samples);
    break;
  case AUDIO_FORMAT_PCM_SAMPLES:
    audio_pcm_get_samples(&a->pcm, num_samples, samples);
    break;
  }
}
