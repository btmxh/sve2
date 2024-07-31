#pragma once

#include <libavutil/hwcontext_drm.h>
#include <libswresample/swresample.h>

#include "sve2/media/ffmpeg_stream.h"

/**
 * @brief An audio_t implementation based on FFmpeg demuxer and decoder. This
 * streams the audio, which is more efficient (memory-wise) at the cost of
 * latency (I/O) and being more error-prone in general.
 */
typedef struct {
  ffmpeg_stream_t base;
  /**
   * @brief An audio resampler that converts from the source audio format to the
   * common audio format specified by the context. This is required for mixing.
   */
  SwrContext *audio_resampler;
} ffmpeg_audio_stream_t;

// this is the same API as in audio.h
bool ffmpeg_audio_stream_open(context_t *ctx, ffmpeg_audio_stream_t *a,
                              const char *path, stream_index_t index);
void ffmpeg_audio_stream_close(ffmpeg_audio_stream_t *a);
void ffmpeg_audio_stream_seek(ffmpeg_audio_stream_t *a, i64 time);
void ffmpeg_audio_stream_get_samples(ffmpeg_audio_stream_t *a,
                                     i32 num_samples[static 1], u8 *samples);
