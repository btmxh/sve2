#pragma once

#include <libavcodec/avcodec.h>

#include "sve2/ffmpeg/demuxer.h"

typedef struct {
  demuxer_stream_t *stream;
  AVCodecContext *cc;
} decoder_t;

typedef enum {
  DECODE_SUCCESS,
  DECODE_TIMEOUT,
  DECODE_ERROR,
  DECODE_EOF,
} decode_result_t;

bool decoder_init(decoder_t *d, AVFormatContext *fc, demuxer_stream_t *stream,
                  bool hwaccel);
decode_result_t decoder_decode(decoder_t *d, AVFrame *frame, i64 deadline);
void decoder_free(decoder_t *d);
