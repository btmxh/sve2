#pragma once

#include <libavutil/hwcontext_drm.h>

#include "sve2/media/ffmpeg_stream.h"
#include "sve2/media/video_frame.h"

/**
 * @brief An video_t implementation based on FFmpeg demuxer and decoder. This
 * streams the video, which is more efficient (memory-wise) at the cost of
 * latency (I/O) and being more error-prone in general.
 */
typedef struct {
  ffmpeg_stream_t base;
  /**
   * @brief Current decoded video frame
   */
  video_frame_t cur_frame;
  /**
   * @brief PTS of next video frame (converted to nanoseconds)
   */
  i64 next_frame_pts;
  // We keep track of these just to free (destroy/close) them when changing
  // frames
  EGLImage prime_images[AV_DRM_MAX_PLANES];
  int prime_fds[AV_DRM_MAX_PLANES];
} ffmpeg_video_stream_t;

// this is the same API as in video.h
bool ffmpeg_video_stream_open(context_t *ctx, ffmpeg_video_stream_t *v,
                              const char *path, stream_index_t index,
                              bool hw_accel);
void ffmpeg_video_stream_close(ffmpeg_video_stream_t *v);
void ffmpeg_video_stream_seek(ffmpeg_video_stream_t *v, i64 time);
bool ffmpeg_video_stream_get_texture(ffmpeg_video_stream_t *v, i64 time,
                                     video_frame_t *tex);
