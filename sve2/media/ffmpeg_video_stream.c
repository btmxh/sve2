#include "ffmpeg_video_stream.h"

#include <glad/egl.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixdesc.h>
#include <libdrm/drm_fourcc.h>
#include <unistd.h>

#include "sve2/media/ffmpeg_stream.h"
#include "sve2/utils/runtime.h"

static void map_hw_texture(ffmpeg_video_stream_t *v, const AVFrame *vaapi_frame,
                           AVFrame *prime_frame) {
  prime_frame->format = AV_PIX_FMT_DRM_PRIME;
  nassert_ffmpeg(av_hwframe_map(prime_frame, vaapi_frame,
                                AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT));
  const AVDRMFrameDescriptor *prime =
      (const AVDRMFrameDescriptor *)prime_frame->data[0];
  for (i32 i = 0; i < prime->nb_objects; ++i) {
    v->prime_fds[i] = prime->objects[i].fd;
  }

  enum AVPixelFormat sw_format = v->base.cdc_ctx->sw_pix_fmt;
  const AVPixFmtDescriptor *fmt_desc = av_pix_fmt_desc_get(sw_format);
  log_trace("mapping texture with pixel format %s (aka %s)", fmt_desc->name,
            fmt_desc->alias ? fmt_desc->alias : "none");
  // endianness check
#ifdef __STDC_ENDIAN_LITTLE__
  assert(!(pix_fmt->flags & AV_PIX_FMT_FLAG_BE));
#elif defined(__STDC_ENDIAN_BIG__)
  assert(pix_fmt->flags & AV_PIX_FMT_FLAG_BE);
#endif
  v->cur_frame.sw_format = sw_format;
  v->cur_frame.texture_array_index = -1;

  assert(prime->nb_layers <= AV_DRM_MAX_PLANES);
  glCreateTextures(GL_TEXTURE_2D, prime->nb_layers, v->cur_frame.textures);

  for (i32 i = 0; i < prime->nb_layers; ++i) {
    const AVDRMLayerDescriptor *layer = &prime->layers[i];

    // dimension shifting
    // in NV12 for example, some planes (chroma) may have different dimensions
    // with the original frame
    // FFmpeg provides us with this information in fmt_desc->log2_w
    i32 width = vaapi_frame->width, height = vaapi_frame->height;

    // we only shift chroma planes (because the fields is named like that),
    // which means:
    // - sw_format is not RGB-based, and it must contain chroma (so no RGB,
    // grayscale formats)
    // - this plane contains either chroma data (or both)
    // - this plane does not contain luma data
    // the last condition is important, in formats like YUYV422 where the
    // dimensions are kept the same because luma and chroma are packed into
    // one plane
    bool shift = !(fmt_desc->flags & AV_PIX_FMT_FLAG_RGB) &&
                 fmt_desc->nb_components >= 3 &&
                 i != fmt_desc->comp[0 /* Y */].plane &&
                 (i == fmt_desc->comp[1 /* U */].plane ||
                  i != fmt_desc->comp[2 /* V */].plane);
    if (shift) {
      width = AV_CEIL_RSHIFT(width, fmt_desc->log2_chroma_w);
      height = AV_CEIL_RSHIFT(height, fmt_desc->log2_chroma_h);
    }

    // fill in the EGL attributes
    i32 attr_index = 0;
    nassert(layer->nb_planes <= AV_DRM_MAX_PLANES);
    EGLAttrib attrs[7 + AV_DRM_MAX_PLANES * 6];
    attrs[attr_index++] = EGL_LINUX_DRM_FOURCC_EXT;
    attrs[attr_index++] = layer->format;
    attrs[attr_index++] = EGL_WIDTH;
    attrs[attr_index++] = width;
    attrs[attr_index++] = EGL_HEIGHT;
    attrs[attr_index++] = height;
    for (i32 j = 0; j < layer->nb_planes; ++j) {
      const AVDRMPlaneDescriptor *plane = &layer->planes[j];
      const AVDRMObjectDescriptor *object =
          &prime->objects[plane->object_index];
      attrs[attr_index++] = EGL_DMA_BUF_PLANE0_FD_EXT + j * 3;
      attrs[attr_index++] = object->fd;
      attrs[attr_index++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT + j * 3;
      attrs[attr_index++] = plane->offset;
      attrs[attr_index++] = EGL_DMA_BUF_PLANE0_PITCH_EXT + j * 3;
      attrs[attr_index++] = plane->pitch;
    }
    attrs[attr_index++] = EGL_NONE;

    nassert((v->prime_images[i] = eglCreateImage(
                 eglGetCurrentDisplay(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                 NULL, attrs)) != EGL_NO_IMAGE);
    glBindTexture(GL_TEXTURE_2D, v->cur_frame.textures[i]);
    glEGLImageTargetTexStorageEXT(GL_TEXTURE_2D, v->prime_images[i], NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

static void unmap_hw_texture(ffmpeg_video_stream_t *v) {
  if (v->cur_frame.sw_format != AV_PIX_FMT_NONE) {
    for (int i = 0; i < AV_DRM_MAX_PLANES; ++i) {
      if (v->cur_frame.textures[i]) {
        glDeleteTextures(1, &v->cur_frame.textures[i]);
      }
      if (v->prime_images[i] != EGL_NO_IMAGE) {
        eglDestroyImage(eglGetCurrentDisplay(), v->prime_images[i]);
      }
      if (v->prime_fds[i] >= 0) {
        close(v->prime_fds[i]);
      }
    }
  }

  v->cur_frame.sw_format = AV_PIX_FMT_NONE;
  for (int i = 0; i < AV_DRM_MAX_PLANES; ++i) {
    v->cur_frame.textures[i] = 0;
    v->prime_images[i] = EGL_NO_IMAGE;
    v->prime_fds[i] = -1;
  }
}

bool ffmpeg_video_stream_open(context_t *ctx, ffmpeg_video_stream_t *v,
                              const char *path, stream_index_t index,
                              bool hw_accel) {
  if (!ffmpeg_stream_open(ctx, &v->base, path, index, hw_accel)) {
    return false;
  }

  v->next_frame_pts = -1;
  v->cur_frame.sw_format = AV_PIX_FMT_NONE;
  unmap_hw_texture(v);

  return true;
}

void ffmpeg_video_stream_close(ffmpeg_video_stream_t *v) {
  unmap_hw_texture(v);
  ffmpeg_stream_close(&v->base);
}

void ffmpeg_video_stream_seek(ffmpeg_video_stream_t *v, i64 time) {
  ffmpeg_stream_seek(&v->base, time);

  AVFrame *vaapi_frame = v->base.ctx->temp_frames[0],
          *prime_frame = v->base.ctx->temp_frames[1];
  do {
    if (!ffmpeg_stream_get_frame(&v->base, vaapi_frame)) {
      return;
    }
    v->next_frame_pts = vaapi_frame->pts + vaapi_frame->duration;
  } while (v->next_frame_pts < time);

  map_hw_texture(v, vaapi_frame, prime_frame);
  av_frame_unref(vaapi_frame);
  av_frame_unref(prime_frame);
}
bool ffmpeg_video_stream_get_texture(ffmpeg_video_stream_t *v, i64 time,
                                     video_frame_t *tex) {
  AVFrame *vaapi_frame = v->base.ctx->temp_frames[0],
          *prime_frame = v->base.ctx->temp_frames[1];
  bool updated = false;
  while (v->next_frame_pts < time) {
    updated = true;
    if (!ffmpeg_stream_get_frame(&v->base, vaapi_frame)) {
      return false;
    }
    v->next_frame_pts = vaapi_frame->pts + vaapi_frame->duration;
  }

  if (updated) {
    unmap_hw_texture(v);
    map_hw_texture(v, vaapi_frame, prime_frame);
    av_frame_unref(vaapi_frame);
    av_frame_unref(prime_frame);
  }

  if (tex) {
    *tex = v->cur_frame;
  }

  return true;
}
