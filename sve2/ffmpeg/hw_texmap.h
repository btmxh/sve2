#pragma once

#include <glad/egl.h>
#include <glad/gl.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>

#include "sve2/utils/types.h"

// HW accelerated API
typedef struct {
  // if format is AV_PIX_FMT_NONE, then this texture is blank
  // unmapping a texture always reset it to this state, but to get a fresh
  // blank texture, the user must use decoder_blank_texture()
  //
  // otherwise, this is a (software) pixel format of this texture
  // the header libavutil/pixdesc.h provides description of pixel data is stored
  // however, supporting all formats is probably overkill, so it is recommended
  // that one should just support the most common formats like NV12
  enum AVPixelFormat format;
  // format can be planar, which corresponds to multiple texture layers
  // the number of textures are not provided, but it could be easily
  // obtained by counting the number of i such that textures[i] != 0
  GLuint textures[AV_DRM_MAX_PLANES];
  // backing image for the textures
  // these are only used to unmap the texture
  // the number of images are not provided, but it could be easily
  // obtained by counting the number of i such that images[i] != EGL_MO_IMAGE
  // currently there is a one-to-one mapping from textures to images (same
  // index), but this is not guaranteed
  EGLImage images[AV_DRM_MAX_PLANES];
  // VAAPI file descriptors, these will be closed when the texture is unmapped
  int vaapi_fds[AV_DRM_MAX_PLANES];
} hw_texture_t;

hw_texture_t hw_texture_blank(enum AVPixelFormat format);
hw_texture_t hw_texture_from_gl(enum AVPixelFormat format, i32 num_textures,
                                GLuint textures[]);
void hw_texmap_to_gl(const AVFrame *src, AVFrame *prime, hw_texture_t *texture);
void hw_texmap_from_gl(hw_texture_t *texture, AVFrame *prime, AVFrame *dst);
void hw_texmap_unmap(hw_texture_t *texture, bool unmap_gl_textures);
