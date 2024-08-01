#include "ffmpeg_stream.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <log.h>

#include "sve2/media/stream_index.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

static enum AVPixelFormat get_format_vaapi(struct AVCodecContext *s,
                                           const enum AVPixelFormat *fmt) {
  (void)s;
  (void)fmt;
  return AV_PIX_FMT_VAAPI;
}

bool ffmpeg_stream_open(context_t *ctx, ffmpeg_stream_t *stream,
                        const char *path, stream_index_t index, bool hw_accel) {
  stream->ctx = ctx;
  stream->index = index;

  int err;
  if ((err = avformat_open_input(&stream->fmt_ctx, path, NULL, NULL)) < 0) {
    log_error("unable to open media file '%s': '%s'", path, av_err2str(err));
    return false;
  }

  log_trace("media file '%s' opened with AVFormatContext %p", path,
            (void *)stream->fmt_ctx);

  nassert_ffmpeg(avformat_find_stream_info(stream->fmt_ctx, NULL));
  av_dump_format(stream->fmt_ctx, 0, path, false);

  if (!stream_index_make_absolute(&stream->index, stream->fmt_ctx->nb_streams,
                                  stream->fmt_ctx->streams)) {
    log_error("stream %s not found in media file '%s'", SVE2_SI2STR(index),
              path);
  }
  log_info("resolved stream index %s in media '%s' to be '%s'",
           SVE2_SI2STR(index), path, SVE2_SI2STR(stream->index));

  assert(stream->index.type == AVMEDIA_TYPE_UNKNOWN);
  const AVStream *ff_stream = stream->fmt_ctx->streams[stream->index.offset];
  const AVCodec *codec = avcodec_find_decoder(ff_stream->codecpar->codec_id);
  nassert(stream->cdc_ctx = avcodec_alloc_context3(codec));
  nassert_ffmpeg(
      avcodec_parameters_to_context(stream->cdc_ctx, ff_stream->codecpar));
  if (hw_accel) {
    if (ff_stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
      log_warn("hardware accelerated decoding is only supported for video");
    } else {
      nassert_ffmpeg(av_hwdevice_ctx_create(&stream->cdc_ctx->hw_device_ctx,
                                            AV_HWDEVICE_TYPE_VAAPI, NULL, NULL,
                                            0));
      stream->cdc_ctx->get_format = get_format_vaapi;
      stream->cdc_ctx->pix_fmt = AV_PIX_FMT_VAAPI;
    }
  }

  nassert_ffmpeg(avcodec_open2(stream->cdc_ctx, codec, NULL));
  log_info("AVCodecContext %p initialized for stream %s (%s) of media '%s'",
           (void *)stream->cdc_ctx, SVE2_SI2STR(index),
           SVE2_SI2STR(stream->index), path);
  // this is unused but we copy to make accessing this easier
  stream->cdc_ctx->time_base = ff_stream->time_base;
  return true;
}

void ffmpeg_stream_close(ffmpeg_stream_t *stream) {
  avcodec_free_context(&stream->cdc_ctx);
  avformat_close_input(&stream->fmt_ctx);
}

void ffmpeg_stream_seek(ffmpeg_stream_t *stream, i64 timestamp) {
  nassert_ffmpeg(av_seek_frame(stream->fmt_ctx, -1,
                               timestamp / (SVE2_NS_PER_SEC / AV_TIME_BASE),
                               AVSEEK_FLAG_BACKWARD));
}

void convert_pts(AVFrame *frame, i64 orig_time_base_num,
                 i64 orig_time_base_den) {
  // basically
  // frame->pts = av_rescale_q(frame->pts, orig_time_base,
  //                           (AVRational){1, SVE2_NS_PER_SEC});
  // frame->duration = av_rescale_q(frame->duration, orig_time_base,
  //                                (AVRational){1, SVE2_NS_PER_SEC});
  // but without overflowing
  i64 b = orig_time_base_num * SVE2_NS_PER_SEC;
  i64 c = orig_time_base_den;
  frame->pts = av_rescale(frame->pts, b, c);
  frame->duration = av_rescale(frame->duration, b, c);
}

bool ffmpeg_stream_get_frame(ffmpeg_stream_t *stream, AVFrame *frame) {
  AVPacket *packet = stream->ctx->temp_packet;
  int err;
  while ((err = avcodec_receive_frame(stream->cdc_ctx, frame)) ==
         AVERROR(EAGAIN)) {
    do {
      av_packet_unref(packet);
      err = av_read_frame(stream->fmt_ctx, packet);
      if (err == AVERROR_EOF) {
        return false;
      }
      nassert_ffmpeg(err);
    } while (packet->stream_index != stream->index.offset);

    nassert_ffmpeg(avcodec_send_packet(stream->cdc_ctx, packet));
    av_packet_unref(packet);
  }

  if (err != AVERROR_EOF && err < 0) {
    log_warn("decoding error: %s", av_err2str(err));
  }

  if (err >= 0) {
    convert_pts(frame, stream->cdc_ctx->time_base.num,
                stream->cdc_ctx->time_base.den);
  }

  return err >= 0;
}
