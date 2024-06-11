#pragma once

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "sve2/context/context.h"
#include "sve2/ffmpeg/encoder.h"

typedef struct {
  AVFormatContext *fc;
  encoder_t *encoders; // fc->nb_streams encoders
} muxer_t;

void muxer_init(muxer_t *m, const char *path);
// return the new stream index
i32 muxer_new_stream(muxer_t *m, context_t *c, const AVCodec *codec,
                     bool hwaccel, encoder_config_fn config_fn, void *userptr);
void muxer_begin(muxer_t *m);
void muxer_end(muxer_t *m);
void muxer_submit_frame(muxer_t *m, const AVFrame *frame, i32 stream_index);
void muxer_free(muxer_t *m);
