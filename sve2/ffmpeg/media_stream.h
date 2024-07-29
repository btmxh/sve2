#pragma once

#include "sve2/context/context.h"
#include "sve2/ffmpeg/decoder.h"
#include "sve2/ffmpeg/demuxer.h"
#include "sve2/ffmpeg/hw_texmap.h"
#include "sve2/utils/types.h"

/**
 * @brief High-level media decoding API.
 *
 * media_stream_t contains the demuxer and the decoder of a single media stream.
 * The demuxer and decoder object are owned and not shared to any other
 * media_stream_t, which allows one to freely seek different media streams by
 * different offsets.
 */
typedef struct {
  context_t *context;
  demuxer_t demuxer;
  demuxer_stream_t stream;
  decoder_t decoder;
  // decoding pipeline:
  // decoder_t (wraps AVCodecContext)
  //               v
  // decoded_frame (same format as input stream)
  //          _____|_____
  // (audio) |          | (video + hwaccel)
  //         v          v
  // audio_resampler   transfer_frame (DRM prime)
  //       v                     v
  // raw PCM data        texture (GL texture)
  AVFrame *decoded_frame, *transfer_frame;
  bool eof;
  // audio
  SwrContext *audio_resampler;
  // video
  hw_texture_t texture;
} media_stream_t;

/**
 * @brief Open a media stream from a file
 *
 * @param media Destination media_stream_t object
 * @param context The current context
 * @param path Path to the media file
 * @param stream_index Relative/Absolute stream index of the media stream
 * @return Whether the operation succeed or not. This fails mostly when the file
 * does not exist.
 */
bool media_stream_open(media_stream_t *media, context_t *context,
                       const char *path, i32 stream_index);
/**
 * @brief Close a media stream
 *
 * @param media An initialized media_stream_t object
 */
void media_stream_close(media_stream_t *media);
bool media_stream_eof(const media_stream_t *media);
/**
 * @brief Seek within a media stream
 *
 * @param media An initialized media_stream_t object
 * @param timestamp The seek timestamp, in nanoseconds
 */
void media_stream_seek(media_stream_t *media, i64 timestamp);

/**
 * @brief Get the current video texture with relative to the time time.
 *
 * @param media An initialized video media_stream_t object
 * @param texture A hw_texture_t object to store the mapped texture
 * @param time The current time of the video
 * @return The decoding result of the video stream
 */
decode_result_t media_get_video_texture(media_stream_t *media,
                                        hw_texture_t *texture, i64 time);

/**
 * @brief Retrieve next audio samples from the audio stream.
 *
 * @param media An initialized audio media_stream_t object
 * @param samples A buffer containing at least *nb_samples samples (per
 * channel).
 * @param nb_samples Input: the upper bound on the number of samples (per
 * channel) in buffer. Output: the actual number of samples (per channel)
 * filled.
 * @return The decoding result of the audio stream
 */
decode_result_t media_get_audio_frame(media_stream_t *media, void *samples,
                                      i32 nb_samples[static 1]);
