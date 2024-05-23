#include "decoder.h"

#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/error.h>
#include <log.h>

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
