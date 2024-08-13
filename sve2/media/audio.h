#pragma once

#include "sve2/context/context.h"
#include "sve2/media/audio_pcm.h"
#include "sve2/media/ffmpeg_audio_stream.h"
#include "sve2/media/stream_index.h"
#include "sve2/utils/types.h"

/**
 * Internal format of audio_t.
 *
 * AUDIO_FORMAT_PCM_SAMPLES: load the entire file and decode into memory. This
 * is save computing power but consumes a lot of memory if the audio file is
 * large.
 *
 * AUDIO_FORMAT_FFMPEG_STREAM: stream the audio file from disk, might cause lag
 * due to the disk I/O and on-the-fly decoding.
 */
typedef enum {
  AUDIO_FORMAT_FFMPEG_STREAM,
  AUDIO_FORMAT_PCM_SAMPLES,
} audio_format_t;

typedef struct {
  audio_format_t format;
  union {
    ffmpeg_audio_stream_t ffmpeg;
    audio_pcm_t pcm;
  };
} audio_t;

/**
 * @brief Open an audio object
 *
 * @param v Destination audio_t object
 * @param path Path to the audio file
 * @param stream_index Audio stream index
 * @param format Audio format
 * @return Whether the operation succeeded or failed (media file not exists)
 */
bool audio_open(context_t *ctx, audio_t *a, const char *path,
                stream_index_t index, audio_format_t format);
/**
 * @brief Close an audio object
 *
 * @param a An opened audio object
 */
void audio_close(audio_t *a);
/**
 * @brief Seek to the specified timestamp in an audio object
 *
 * @param a An opened audio object
 * @param time The seek timestamp
 */
void audio_seek(audio_t *a, i64 time);
/**
 * @brief Retrieve sample data from the audio object.
 *
 * @param a An opened audio object
 * @param num_samples in: Maximum number of samples (per channel) that `samples`
 * can hold. out: The actual number of samples (per channel) actually retrieved.
 * @param samples Sample data buffer
 */
void audio_get_samples(audio_t *a, i32 num_samples[static 1],
                       u8 samples[*num_samples]);

/**
 * @brief heap-allocate an audio_t object, used in lua API
 *
 * @return sve2_malloc(sizeof(audio_t))
 */
audio_t* audio_alloc();
