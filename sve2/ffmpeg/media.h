#pragma once

#include "sve2/context/context.h"
#include "sve2/ffmpeg/decoder.h"
#include "sve2/ffmpeg/demuxer.h"
#include "sve2/ffmpeg/hw_texmap.h"
#include "sve2/utils/types.h"

typedef enum {
  MEDIA_STREAM_VIDEO = 0,
  MEDIA_STREAM_AUDIO = 1,
  MEDIA_STREAM_SUBS = 2,
  MEDIA_NUM_STREAMS_MAX = 3,
} media_stream_index_t;

typedef struct {
  context_t *context;
  demuxer_t demuxer;
  demuxer_stream_t streams[MEDIA_NUM_STREAMS_MAX];
  decoder_t decoder[MEDIA_NUM_STREAMS_MAX];

  // audio
  SwrContext *audio_resampler;
  AVFrame *hw_frame, *transfer_frame;
  i64 next_video_pts;
} media_t;

bool media_open(media_t *media, context_t *context, const char *path);
void media_close(media_t *media);
bool media_eof(const media_t *media);

AVStream *media_get_stream(const media_t *media, media_stream_index_t index);
void media_seek(media_t *media, i64 timestamp);

decode_result_t media_get_video_texture(media_t *media, hw_texture_t *texture,
                                        i64 time);

decode_result_t media_get_audio_frame(media_t *media, AVFrame *frame, i64 time);
