#pragma once

#include <glad/egl.h>
#include <glad/gl.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libswresample/swresample.h>

#include "sve2/ffmpeg/demuxer.h"

/**
 * @brief decoder API: decode packets from demuxer_t to media frames
 */
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

/**
 * @brief Initialize a decoder
 *
 * @param d An uninitialized decoder_t object
 * @param dm demuxer_t object where packets are taken from
 * @param rel_stream_index Relative stream index in the streams of dm (okay the
 * naming is confusing but it's not the relative/absolute indices I'm sorry)
 * @param hwaccel Whether to use hardware-acceleration
 * @return Whether the initialization succeeded
 */
bool decoder_init(decoder_t *d, demuxer_t *dm, i32 rel_stream_index,
                  bool hwaccel);
/**
 * @brief Decode packets from demuxer to frames
 *
 * @param d The decoder
 * @param frame Output frame, allocated via av_frame_alloc() but empty
 * @param deadline Packet receive deadline
 * @return Decode result (self-explanatory)
 */
decode_result_t decoder_decode(decoder_t *d, AVFrame *frame, i64 deadline);
/**
 * @brief Wait for seek message from the demuxer. This is meant to be done right
 * after the seek command is sent to the demuxer. Also flushes internal decoding
 * buffer.
 *
 * @param d The decoder
 * @param deadline Message receive deadline
 * @return Whether the operation succeeded or not (timeout)
 */
bool decoder_wait_for_seek(decoder_t *d, i64 deadline);
/**
 * @brief Get software frame format of hardware-accelerated decoding context.
 * For example, if one decodes NV12 H264 using VAAPI, then this will return
 * AV_PIX_FMT_NV12, while accessing format via the AVCodecContext will yield
 * AV_PIX_FMT_VAAPI.
 *
 * @param d The decoder
 */
enum AVPixelFormat decoder_get_sw_format(const decoder_t *d);
/**
 * @brief Free a decoder
 *
 * @param d An initialized decoder
 */
void decoder_free(decoder_t *d);
