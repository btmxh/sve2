#pragma once

#include <glad/gl.h>
#include <libavutil/frame.h>

#include "sve2/context/context.h"
#include "sve2/media/stream_index.h"
#include "sve2/media/video_frame.h"
#include "sve2/utils/types.h"

/**
 * @brief A video_t implementation which stores all video content on a OpenGL
 * texture array. This reduces CPU-GPU latency (on playback), but at the cost of
 * memory usage.
 *
 * Implementation details: Textures are loaded with FFmpeg, except when it fails
 * for animated WebP images, where libwebp is used instead. This makes it
 * possible for sve2 to support WebP animated images (twitch emotes).
 */
typedef struct {
  GLuint texture;
  enum AVPixelFormat sw_format;
  i32 num_frames;
  /**
   * @brief An array of next frame timestamp (size is num_frames).
   */
  i64 *next_frame_timestamps;
} video_texture_array_t;

bool video_texture_array_new(context_t *ctx, video_texture_array_t *t,
                             const char *path, stream_index_t index);
void video_texture_array_free(video_texture_array_t *t);
bool video_texture_array_get_texture(video_texture_array_t *t, i64 time,
                                     video_frame_t *tex);
