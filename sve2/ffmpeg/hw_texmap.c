#include "hw_texmap.h"

#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libdrm/drm_fourcc.h>
#include <log.h>
#include <unistd.h>
#include <va/va.h>

#include "sve2/utils/runtime.h"
#include "sve2/utils/types.h"

static void drm_prime_to_gl(AVFrame *frame, hw_texture_t *texture) {
  enum AVPixelFormat format = texture->format;
  const AVPixFmtDescriptor *pix_fmt = av_pix_fmt_desc_get(format);
  log_trace("mapping texture with pixel format %s (aka %s)",
            av_get_pix_fmt_name(format), pix_fmt->alias);
  texture->format = format;

  bool rgb = pix_fmt->flags & AV_PIX_FMT_FLAG_RGB;

  // endianness mismatch check
#ifdef __STDC_ENDIAN_LITTLE__
  nassert(!(pix_fmt->flags & AV_PIX_FMT_FLAG_BE));
#elif defined(__STDC_ENDIAN_BIG__)
  nassert(pix_fmt->flags & AV_PIX_FMT_FLAG_BE);
#endif

  const AVDRMFrameDescriptor *desc =
      (const AVDRMFrameDescriptor *)frame->data[0];
  for (i32 i = 0; i < desc->nb_objects; ++i) {
    texture->vaapi_fds[i] = desc->objects[i].fd;
  }

  glGenTextures(desc->nb_layers, texture->textures);
  for (i32 i = 0; i < desc->nb_layers; ++i) {
    const AVDRMLayerDescriptor *layer = &desc->layers[i];

    u32 w_shift = 0, h_shift = 0;
    // we only shift the dimensions if
    // - not RGB format
    // - this plane contains either U or V (chroma) data (or both)
    // - this plane does not contain Y (luma) data
    // the last condition is crucial: there exists formats such as YUYV422 which
    // interleaves everything into a plane. the width and height of such plane
    // should be the same as the original frame
    if (!rgb &&
        (i == pix_fmt->comp[1 /*U*/].plane ||
         i == pix_fmt->comp[2 /*V*/].plane) &&
        i != pix_fmt->comp[0 /*Y*/].plane) {
      w_shift = pix_fmt->log2_chroma_w;
      h_shift = pix_fmt->log2_chroma_h;
    }

    i32 attr_index = 0;
    nassert(layer->nb_planes <= AV_DRM_MAX_PLANES);
    EGLAttrib attrs[7 + AV_DRM_MAX_PLANES * 6];
    attrs[attr_index++] = EGL_LINUX_DRM_FOURCC_EXT;
    attrs[attr_index++] = layer->format;
    attrs[attr_index++] = EGL_WIDTH;
    attrs[attr_index++] = AV_CEIL_RSHIFT(frame->width, w_shift);
    attrs[attr_index++] = EGL_HEIGHT;
    attrs[attr_index++] = AV_CEIL_RSHIFT(frame->height, h_shift);
    for (i32 i = 0; i < layer->nb_planes; ++i) {
      attrs[attr_index++] = EGL_DMA_BUF_PLANE0_FD_EXT + i * 3;
      attrs[attr_index++] = desc->objects[layer->planes[i].object_index].fd;
      attrs[attr_index++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT + i * 3;
      attrs[attr_index++] = layer->planes[i].offset;
      attrs[attr_index++] = EGL_DMA_BUF_PLANE0_PITCH_EXT + i * 3;
      attrs[attr_index++] = layer->planes[i].pitch;
    }
    attrs[attr_index++] = EGL_NONE;
    nassert(attr_index <= sve2_arrlen(attrs));
    nassert((texture->images[i] = eglCreateImage(
                 eglGetCurrentDisplay(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                 NULL, attrs)) != EGL_NO_IMAGE);
    glBindTexture(GL_TEXTURE_2D, texture->textures[i]);
    glEGLImageTargetTexStorageEXT(GL_TEXTURE_2D, texture->images[i], NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

static void gl_to_drm_prime(hw_texture_t *texture, AVFrame *prime) {
  enum AVPixelFormat sw_format = texture->format;
  prime->format = AV_PIX_FMT_DRM_PRIME;
  prime->buf[0] = av_buffer_alloc(sizeof(AVDRMFrameDescriptor));
  AVDRMFrameDescriptor *frame = (AVDRMFrameDescriptor *)prime->buf[0]->data;
  frame->nb_objects = 0;
  frame->nb_layers = 0;
  for (int i = 0; i < sve2_arrlen(texture->images); ++i) {
    if (texture->textures[i] == 0) {
      continue;
    }

    texture->images[i] = eglCreateImage(
        eglGetCurrentDisplay(), eglGetCurrentContext(), EGL_GL_TEXTURE_2D,
        (EGLClientBuffer)(size_t)texture->textures[i], NULL);
    AVDRMLayerDescriptor *layer = &frame->layers[frame->nb_layers];
    EGLint format, nb_planes;
    EGLuint64KHR modifiers;
    EGLint fds[AV_DRM_MAX_PLANES], strides[AV_DRM_MAX_PLANES],
        offsets[AV_DRM_MAX_PLANES];
    eglExportDMABUFImageQueryMESA(eglGetCurrentDisplay(), texture->images[i],
                                  &format, &nb_planes, &modifiers);
    layer->format = format;
    layer->nb_planes = nb_planes;

    eglExportDMABUFImageMESA(eglGetCurrentDisplay(), texture->images[i], fds,
                             strides, offsets);
    i32 num_objects = 0;
    for (int j = 0; j < nb_planes; ++j) {
      if (fds[j] >= 0) {
        ++num_objects;
      }
    }

    for (int j = 0; j < nb_planes; ++j) {
      i32 object_index = frame->nb_objects++;
      layer->planes[j].object_index = object_index;
      layer->planes[j].offset = offsets[j];
      layer->planes[j].pitch = strides[j];
      frame->objects[object_index].fd = fds[j];
      frame->objects[object_index].size = 0;
      frame->objects[object_index].format_modifier = modifiers;
    }

    frame->nb_layers += 1;
  }

  i32 object_index = 0;
  for (i32 i = 0; i < frame->nb_objects; ++i) {
    int fd = frame->objects[i].fd;
    if (fd < 0) {
      continue;
    }

    texture->vaapi_fds[object_index++] = fd;
  }

  for (i32 i = object_index; i < sve2_arrlen(texture->vaapi_fds); ++i) {
    texture->vaapi_fds[i] = -1;
  }

  if (sw_format == AV_PIX_FMT_NV12) {
    // NV12-specific hacks
    i32 uv_offset_y = prime->height;
    hw_align_size(NULL, &uv_offset_y);
    frame->layers[1].nb_planes = 1;
    frame->layers[1].format = DRM_FORMAT_RG88;
    frame->layers[1].planes[0].object_index = 0;
    frame->layers[1].planes[0].offset =
        frame->layers[0].planes[0].pitch * uv_offset_y;
    frame->objects[0].size =
        frame->layers[0].planes[0].pitch *
        (prime->height / 2 + frame->layers[1].planes[0].offset);
    frame->layers[1].planes[0].pitch = frame->layers[0].planes[0].pitch;
    ++frame->nb_layers;
  }

  prime->data[0] = (u8 *)frame;
}

hw_texture_t hw_texture_blank(enum AVPixelFormat format) {
  return hw_texture_from_gl(format, 0, NULL);
}

hw_texture_t hw_texture_from_gl(enum AVPixelFormat format, i32 num_textures,
                                GLuint textures[]) {
  hw_texture_t t;
  memset(&t, 0, sizeof t);
  t.format = format;

  for (i32 i = 0; i < sve2_arrlen(t.vaapi_fds); ++i) {
    t.vaapi_fds[i] = -1;
  }

  for (i32 i = 0; i < num_textures; ++i) {
    t.textures[i] = textures[i];
  }

  return t;
}

void hw_texmap_to_gl(const AVFrame *src, AVFrame *prime,
                     hw_texture_t *texture) {
  prime->format = AV_PIX_FMT_DRM_PRIME;
  nassert_ffmpeg(
      av_hwframe_map(prime, src, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT));
  drm_prime_to_gl(prime, texture);
  av_frame_unref(prime);
}

void hw_texmap_from_gl(hw_texture_t *texture, AVFrame *prime, AVFrame *dst) {
  dst->format = AV_PIX_FMT_VAAPI;
  prime->width = dst->width;
  prime->height = dst->height;
  gl_to_drm_prime(texture, prime);
  nassert_ffmpeg(
      av_hwframe_map(dst, prime, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT));
  av_frame_unref(prime);
}

void hw_texmap_unmap(hw_texture_t *texture, bool unmap_gl_textures) {
  for (i32 i = 0; i < sve2_arrlen(texture->textures); ++i) {
    if (texture->textures[i] && unmap_gl_textures) {
      glDeleteTextures(1, &texture->textures[i]);
    }

    texture->textures[i] = 0;
  }
  for (i32 i = 0; i < sve2_arrlen(texture->images); ++i) {
    if (texture->images[i] != EGL_NO_IMAGE) {
      eglDestroyImage(eglGetCurrentDisplay(), texture->images[i]);
    }

    texture->images[i] = EGL_NO_IMAGE;
  }
  for (i32 i = 0; i < sve2_arrlen(texture->vaapi_fds); ++i) {
    if (texture->vaapi_fds[i] >= 0) {
      close(texture->vaapi_fds[i]);
    }

    texture->vaapi_fds[i] = -1;
  }
}

static i32 round_up_to_multiple_of(i32 x, i32 po2) {
  return (i32)((u32)(x + po2 - 1) & ~(u32)(po2 - 1));
}

// this is how stuff is aligned on intel GPUs
// https://github.com/intel/hwc/blob/master/lib/ufo/graphics.h
void hw_align_size(i32 *width, i32 *height) {
  if (width) {
    *width = round_up_to_multiple_of(*width, 128);
  }
  if (height) {
    *height = round_up_to_multiple_of(*height, 64);
  }
}
