#pragma once

#include <glad/egl.h>
#include <glad/gl.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>

#include "sve2/ffmpeg/demuxer.h"

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

bool decoder_init(decoder_t *d, AVFormatContext *fc, demuxer_stream_t *stream,
                  bool hwaccel);
decode_result_t decoder_decode(decoder_t *d, AVFrame *frame, i64 deadline);
void decoder_free(decoder_t *d);

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
} decode_texture_t;

decode_texture_t decoder_blank_texture();
void decoder_map_texture(decoder_t *d, const AVFrame *frame,
                         AVFrame *mapped_frame, decode_texture_t *texture);
void decoder_unmap_texture(decode_texture_t *texture);
