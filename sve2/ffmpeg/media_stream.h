#pragma once

#include "sve2/context/context.h"
#include "sve2/ffmpeg/decoder.h"
#include "sve2/ffmpeg/demuxer.h"
#include "sve2/ffmpeg/hw_texmap.h"
#include "sve2/utils/types.h"

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

bool media_stream_open(media_stream_t *media, context_t *context,
                       const char *path, i32 stream_index);
void media_stream_close(media_stream_t *media);
bool media_stream_eof(const media_stream_t *media);
void media_stream_seek(media_stream_t *media, i64 timestamp);

decode_result_t media_get_video_texture(media_stream_t *media,
                                        hw_texture_t *texture, i64 time);

decode_result_t media_get_audio_frame(media_stream_t *media, void *samples,
                                      i32 nb_samples[static 1]);
