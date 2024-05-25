#include "decoder.h"

#include <assert.h>
#include <stdbit.h>

#include <glad/egl.h>
#include <glad/gles2.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/common.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libdrm/drm_fourcc.h>
#include <log.h>
#include <unistd.h>

#include "sve2/ffmpeg/demuxer.h"
#include "sve2/utils/runtime.h"

static enum AVPixelFormat get_format_vaapi(struct AVCodecContext *s,
                                           const enum AVPixelFormat *fmt) {
  (void)s;
  (void)fmt;
  return AV_PIX_FMT_VAAPI;
}

bool decoder_init(decoder_t *d, AVFormatContext *fc, demuxer_stream_t *stream,
                  bool hwaccel) {
  d->stream = stream;

  assert(stream->index >= 0);
  enum AVCodecID codec_id = fc->streams[stream->index]->codecpar->codec_id;
  const AVCodec *codec = avcodec_find_decoder(codec_id);
  if (!codec) {
    log_warn("unable to find decoder for codec %s", avcodec_get_name(codec_id));
    return false;
  }

  nassert(d->cc = avcodec_alloc_context3(codec));
  nassert(avcodec_parameters_to_context(
              d->cc, fc->streams[stream->index]->codecpar) >= 0);

  if (hwaccel) {
    if (fc->streams[stream->index]->codecpar->codec_type !=
        AVMEDIA_TYPE_VIDEO) {
      log_warn("HW accelerated decoding is only supported for video codecs");
    }

    int err = av_hwdevice_ctx_create(&d->cc->hw_device_ctx,
                                     AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0);
    if (err < 0) {
      log_warn("unable to create HW acceleration context: %s", av_err2str(err));
    }

    d->cc->get_format = get_format_vaapi;
    d->cc->pix_fmt = AV_PIX_FMT_VAAPI;
  }

  int err = avcodec_open2(d->cc, codec, NULL);
  if (err < 0) {
    log_warn("unable to open codec context for decoding: %s", av_err2str(err));
    avcodec_free_context(&d->cc);
    return false;
  }

  log_info("AVCodecContext %p initialized for stream %s of AVFormatContext %p",
           (const void *)d->cc, sve2_si2str(stream->index), (const void *)fc);

  return true;
}

static inline bool is_eof_or_eagain(int e) {
  return e == AVERROR_EOF || e == AVERROR(EAGAIN);
}

decode_result_t decoder_decode(decoder_t *d, AVFrame *frame, i64 deadline) {
  int err;
  while (is_eof_or_eagain(err = avcodec_receive_frame(d->cc, frame))) {
    packet_msg_t pkt;
    if (!mpmc_recv(&d->stream->packet_channel, &pkt, deadline)) {
      return DECODE_TIMEOUT;
    }

    if (pkt.regular || pkt.eof) {
      AVPacket *p = pkt.packet;
      log_trace("send packet %p to AVCodecContext %p: pts=%" PRId64
                ", dts=%" PRId64 ", duration=%" PRId64
                ", si=%d, size=%d, flags=%d",
                (const void *)p, (const void *)d->cc, p->pts, p->dts,
                p->duration, p->stream_index, p->size, p->flags);
      err = avcodec_send_packet(d->cc, p);
      if (err < 0) {
        log_warn("unable to send packet: %s", av_err2str(err));
      }

      av_packet_free(&p);
      continue;
    }

    if (pkt.seek) {
      log_info("seek message caught from demuxer for AVCodecContext %p",
               (const void *)d->cc);
      avcodec_flush_buffers(d->cc);
      continue;
    }

    if (pkt.error) {
      log_warn("decoder fails because of an error in the demuxer thread");
      return DECODE_ERROR;
    }
  }

  if (err >= 0) {
    return DECODE_SUCCESS;
  } else if (err == AVERROR_EOF) {
    return DECODE_EOF;
  } else {
    log_warn("error while decoding: %s", av_err2str(err));
    return DECODE_ERROR;
  }
}

void decoder_free(decoder_t *d) { avcodec_free_context(&d->cc); }

static void map_nv12(AVFrame *frame, enum AVPixelFormat format,
                     decode_texture_t *texture) {
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
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, texture->images[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

decode_texture_t decoder_blank_texture() {
  decode_texture_t t;
  memset(&t, 0, sizeof t);
  for (i32 i = 0; i < sve2_arrlen(t.vaapi_fds); ++i) {
    t.vaapi_fds[i] = -1;
  }
  return t;
}

void decoder_map_texture(decoder_t *d, const AVFrame *frame,
                         AVFrame *mapped_frame, decode_texture_t *texture) {
  decoder_unmap_texture(texture);
  mapped_frame->format = AV_PIX_FMT_DRM_PRIME;
  nassert(av_hwframe_map(mapped_frame, frame,
                         AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT) >= 0);
  enum AVPixelFormat format =
      ((AVHWFramesContext *)d->cc->hw_frames_ctx->data)->sw_format;
  map_nv12(mapped_frame, format, texture);
  av_frame_unref(mapped_frame);
}

void decoder_unmap_texture(decode_texture_t *texture) {
  for (i32 i = 0; i < sve2_arrlen(texture->textures); ++i) {
    if (texture->textures[i]) {
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
