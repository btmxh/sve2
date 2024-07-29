#pragma once

#include <glad/egl.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavutil/hwcontext_drm.h>

typedef struct context_t context_t;

/**
 * @brief stream encoder, supports hardware acceleration
 */
typedef struct {
  AVCodecContext *c;
  AVBufferRef *hw_device_ctx;
} encoder_t;

typedef void (*encoder_config_fn)(AVCodecContext *ctx, void *userptr);

/**
 * @brief Initialize an encoder. To configure the output stream, modify the
 * source code directly. The justification is the fact that making a custom
 * configuration API is time-consuming and not worth it.
 *
 * @param e An uninitialized encoder_t object
 * @param c Context, default encoding settings will be based on this context
 * @param codec Encoding for this encoder
 * @param hwaccel Whether to use hardware-acceleration or not
 * @param config_fn Custom configure function for the AVCodecContext
 * @param userptr User pointer for config_fn
 */
void encoder_init(encoder_t *e, context_t *c, const AVCodec *codec,
                  bool hwaccel, encoder_config_fn config_fn, void *userptr);
/**
 * @brief Submit a frame to the encoder for encoding, basically a thin wrapper
 * for avcodec_send_frame()
 */
bool encoder_submit_frame(encoder_t *e, const AVFrame *frame);
/**
 * @brief Retrieve a encoded packet from the encoder, basically a thin wrapper
 * for avcodec_receive_packet()
 */
AVPacket *encoder_receive_packet(encoder_t *e);
/**
 * @brief Free an encoder
 *
 * @param e An initialized encoder_t object
 */
void encoder_free(encoder_t *e);
