#pragma once

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "sve2/context/context.h"
#include "sve2/media/stream_index.h"
#include "sve2/utils/types.h"

/**
 * @brief A FFmpeg stream stored in a media file. Streams within the same media
 * have separated demuxers for extra flexibility.
 */
typedef struct {
  context_t *ctx;
  AVFormatContext *fmt_ctx;
  AVCodecContext *cdc_ctx;
  stream_index_t index;
} ffmpeg_stream_t;

/**
 * @brief Open a FFmpeg stream at the specified file and with the specified
 * stream index
 *
 * @param stream Destination ffmpeg_stream_t object
 * @param path Media file path
 * @param stream_index Media stream index
 * @param hw_accel Whether to use hardware-acceleration for decoding or not
 * @return Whether the operation succeeded or failed (file not found)
 */
bool ffmpeg_stream_open(context_t *ctx, ffmpeg_stream_t *stream,
                        const char *path, stream_index_t index, bool hw_accel);
/**
 * @brief Close a FFmpeg stream
 *
 * @param stream An opened FFmpeg stream
 */
void ffmpeg_stream_close(ffmpeg_stream_t *stream);

/**
 * @brief Seek a FFmpeg stream to the specified timestamp
 *
 * @param stream The FFmpeg stream
 * @param timestamp The timestamp to seek to, in nanoseconds
 */
void ffmpeg_stream_seek(ffmpeg_stream_t *stream, i64 timestamp);

/**
 * @brief Get the next frame within the FFmpeg stream
 *
 * @param stream The FFmpeg stream
 * @param frame Destination frame
 * @return Whether the operation succeeded or failed (no more frames to decode)
 */
bool ffmpeg_stream_get_frame(ffmpeg_stream_t *stream, AVFrame *frame);

/**
 * @brief Convert an AVFrame's PTS and duration to the common SVE2 timebase
 * (nanoseconds). orig_time_base_num and orig_time_base_den can be 64-bit
 * integers.
 *
 * @param frame The AVFrame
 * @param orig_time_base_num Original timebase numerator
 * @param orig_time_base_den Original timebase denominator
 */
void avframe_convert_pts(AVFrame *frame, i64 orig_time_base_num,
                         i64 orig_time_base_den);
