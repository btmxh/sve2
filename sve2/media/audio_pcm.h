#pragma once

#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

#include "sve2/context/context.h"
#include "sve2/media/stream_index.h"
#include "sve2/utils/types.h"

/**
 * @brief Implementation of audio_t where raw audio PCM stored directly in
 * memory. Good for short audio segments (sound effects), and if you got enough
 * RAM, it might be better to use this for long audio tracks too. The memory
 * usage is basically the same as a WAV file (if you are using s16 sample
 * format, that is).
 *
 * A sample here is defined to be the amount of sound data per 1/sample_rate
 * seconds. For example, with stereo (2 channels) channel layout and s16 sample
 * format, a sample is 2 * sizeof(i16) = 4 bytes.
 *
 * This definition makes calculation easier (it is also what FFmpeg meant by
 * samples, and what miniaudio calls frames). The number of samples only depends
 * on the duration of an audio track and the audio sample rate, independent of
 * the channel layout.
 */
typedef struct {
  context_t *ctx;
  /**
   * @brief Buffer containing audio data. Total size (in bytes) of
   * `buffer` is `sample_size * num_samples`. Because context_t only allows
   * interleaved audio formats, this data is also guaranteed to be interleaved
   * audio.
   */
  u8 *buffer;
  /**
   * @brief Size (in bytes) of a sample (see definition in struct docs)
   */
  i32 sample_size;
  /**
   * @brief Total count of samples in `buffer`.
   */
  i32 num_samples;
  /**
   * @brief Current sample index. It will be increased by *num_samples
   * (out-value) after every call to audio_pcm_get_samples, and can be resetted
   * to a specific value using audio_pcm_seek.
   */
  i32 cur_index;
} audio_pcm_t;

// Exact same API as in audio.h
bool audio_pcm_open(context_t *ctx, audio_pcm_t *a, const char *path,
                    stream_index_t index);
void audio_pcm_close(audio_pcm_t *a);
void audio_pcm_seek(audio_pcm_t *a, i64 time);
void audio_pcm_get_samples(audio_pcm_t *a, i32 num_samples[static 1],
                           u8 *samples);
