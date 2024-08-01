#pragma once

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "sve2/utils/types.h"

typedef struct context_t context_t;

/**
 * @brief Output context. This wraps a muxer and several encoders for the output
 * media file in render mode.
 */
typedef struct {
  context_t *ctx;
  AVFormatContext *fmt_ctx;
  AVCodecContext **cdc_ctx;
} output_ctx_t;

/**
 * @brief Open an output context.
 *
 * @param ctx The context which output streams information will be based on
 * @param o The output context
 * @param path Path to the output file
 * @param num_streams Number of streams in the output media file
 * @param stream_codecs An array of stream codecs used to encode the output
 * media streams
 */
void output_ctx_open(context_t *ctx, output_ctx_t *o, const char *path,
                     i32 num_streams,
                     const AVCodec *stream_codecs[num_streams]);
/**
 * @brief Create a hardware-acceleration-based AVFrame for video encoding. The
 * data could be mapped to this frame using av_hwframe_map().
 *
 * @param o The output context
 * @param hw_frame An uninitialized AVFrame object
 * @param stream_idx The stream index of the video stream
 */
void output_ctx_init_hwframe(output_ctx_t *o, AVFrame *hw_frame,
                             i32 stream_idx);
/**
 * @brief Submit a frame to the output context for encoding.
 *
 * @param o The output context
 * @param frame The frame
 * @param stream_idx The stream index this frame belongs to
 */
void output_ctx_submit_frame(output_ctx_t *o, AVFrame *frame, i32 stream_idx);
/**
 * @brief Close (and flush) the output context.
 *
 * @param o An opened output context.
 */
void output_ctx_close(output_ctx_t *o);
