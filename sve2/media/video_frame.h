#pragma once

#include <glad/gl.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixfmt.h>

#include "sve2/utils/types.h"

/**
 * @brief OpenGL textures containing video frame pixel data. There are two
 * types:
 *
 * (Possibly multi-planar) streamed texture:
 * - indicated by texture_array_index < 0.
 * - textures can have multiple planes (each is a GL_TEXTURE_2D target).
 *
 * A layer from a texture array:
 * - indicated by texture_array_index >= 0.
 * - only textures[0] is used (and is a GL_TEXTURE_2D_ARRAY target). The rest is
 * 0.
 */
typedef struct {
  enum AVPixelFormat sw_format;
  i32 texture_array_index;
  GLuint textures[AV_DRM_MAX_PLANES];
} video_frame_t;
