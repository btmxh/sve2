#pragma once

#include <glad/egl.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavutil/hwcontext_drm.h>

#include "sve2/context/context.h"

// Encoder API

typedef struct {
  AVCodecContext *c;
  AVBufferRef *hw_device_ctx;
} encoder_t;

typedef void (*encoder_config_fn)(AVCodecContext *ctx, void *userptr);

void encoder_init(encoder_t *e, context_t *c, const AVCodec *codec,
                  bool hwaccel, encoder_config_fn config_fn, void *userptr);
bool encoder_submit_frame(encoder_t *e, const AVFrame *frame);
AVPacket *encoder_receive_packet(encoder_t *e);
void encoder_free(encoder_t *e);
