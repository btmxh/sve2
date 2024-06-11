#pragma once

#include <glad/egl.h>
#include <glad/gl.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>

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
bool decoder_wait_for_seek(decoder_t *d, i64 deadline);
enum AVPixelFormat decoder_get_sw_format(const decoder_t *d);
void decoder_free(decoder_t *d);
