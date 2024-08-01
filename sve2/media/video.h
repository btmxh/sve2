#pragma once

#include "sve2/context/context.h"
#include "sve2/media/ffmpeg_video_stream.h"
#include "sve2/media/stream_index.h"
#include "sve2/media/video_frame.h"
#include "sve2/media/video_texture_array.h"
#include "sve2/utils/types.h"

/**
 * Internal format of video_t.
 *
 * VIDEO_FORMAT_TEXTURE_ARRAY: load the entire file, decode and upload the pixel
 * data to an OpenGL texture array. This reduces CPU-GPU latency (on playback),
 * but is memory (VRAM) inefficient due to having to store the entire decoded
 * content on VRAM.
 *
 * VIDEO_FORMAT_FFMPEG_STREAM: stream the video file from disk, might cause lag
 * due to the disk I/O and on-the-fly decoding.
 */
typedef enum {
  VIDEO_FORMAT_FFMPEG_STREAM,
  VIDEO_FORMAT_TEXTURE_ARRAY,
} video_format_t;

typedef struct {
  video_format_t format;
  union {
    ffmpeg_video_stream_t ffmpeg;
    video_texture_array_t tex_array;
  };
} video_t;

/**
 * @brief Open a video stream
 *
 * @param v Destination video_t object
 * @param path Path to the video file
 * @param stream_index Video stream index
 * @param format Video format
 * @return Whether the operation succeeded or not
 */
bool video_open(context_t *ctx, video_t *v, const char *path,
                stream_index_t index, video_format_t format);
/**
 * @brief Close a video stream
 *
 * @param v The video stream
 */
void video_close(video_t *v);
/**
 * @brief Seek a video stream to the desired time.
 *
 * @param v The video stream
 * @param time Timestamp to seek to, in nanoseconds
 */
void video_seek(video_t *v, i64 time);
/**
 * @brief Get the current video frame at time `time`
 *
 * @param v The video stream
 * @param time Current timestamp
 * @param tex Output texture containing the video textures
 */
bool video_get_texture(video_t *v, i64 time, video_frame_t *tex);
