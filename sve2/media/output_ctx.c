#include "output_ctx.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>

#include "sve2/context/context.h"
#include "sve2/utils/runtime.h"

void config_stream(context_t *ctx, output_ctx_t *o, AVStream *stream,
                   AVCodecContext *codec_ctx) {
  switch (codec_ctx->codec->type) {
  case AVMEDIA_TYPE_VIDEO:
    codec_ctx->width = ctx->info.width;
    codec_ctx->height = ctx->info.height;
    codec_ctx->time_base = (AVRational){1, ctx->info.fps};
    codec_ctx->framerate = (AVRational){ctx->info.fps, 1};
    codec_ctx->sample_aspect_ratio = (AVRational){1, 1};
    codec_ctx->pix_fmt = AV_PIX_FMT_VAAPI;
    codec_ctx->sw_pix_fmt = AV_PIX_FMT_NV12;
    codec_ctx->bit_rate =
        (i64)(ctx->info.width * ctx->info.height * ctx->info.fps * 1.0);
    codec_ctx->max_b_frames = 0;
    log_info("initializing video encoder with w=%d, h=%d, fr=%f, br=%" PRIi64
             ", pixfmt=%s, sw_pixfmt=%s",
             codec_ctx->width, codec_ctx->height, av_q2d(codec_ctx->framerate),
             codec_ctx->bit_rate, av_get_pix_fmt_name(codec_ctx->pix_fmt),
             av_get_pix_fmt_name(codec_ctx->sw_pix_fmt));
    break;
  case AVMEDIA_TYPE_AUDIO:
    codec_ctx->sample_fmt = ctx->info.sample_fmt;
    codec_ctx->sample_rate = ctx->info.sample_rate;
    codec_ctx->bit_rate = 320000;
    codec_ctx->time_base = (AVRational){1, codec_ctx->sample_rate};
    nassert_ffmpeg(
        av_channel_layout_copy(&codec_ctx->ch_layout, ctx->info.ch_layout));
    log_info("initializing audio encoder with sr=%d, br=%" PRIi64
             ", samplefmt=%s",
             codec_ctx->sample_rate, codec_ctx->bit_rate,
             av_get_sample_fmt_name(codec_ctx->sample_fmt));
    break;
  default:
    break;
  }

  if (o->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
}

void output_ctx_open(context_t *ctx, output_ctx_t *o, const char *path,
                     i32 num_streams,
                     const AVCodec *stream_codecs[num_streams]) {
  o->ctx = ctx;

  nassert_ffmpeg(avformat_alloc_output_context2(&o->fmt_ctx, NULL, NULL, path));
  if (!(o->fmt_ctx->flags & AVFMT_NOFILE)) {
    nassert(avio_open(&o->fmt_ctx->pb, path, AVIO_FLAG_WRITE) >= 0);
  }

  o->cdc_ctx = sve2_malloc(num_streams * sizeof *o->cdc_ctx);
  for (i32 i = 0; i < num_streams; ++i) {
    AVStream *stream = avformat_new_stream(o->fmt_ctx, stream_codecs[i]);
    stream->id = i;
    nassert(o->cdc_ctx[i] = avcodec_alloc_context3(stream_codecs[i]));
    if (stream_codecs[i]->type == AVMEDIA_TYPE_VIDEO) {
      nassert_ffmpeg(av_hwdevice_ctx_create(&o->cdc_ctx[i]->hw_device_ctx,
                                            AV_HWDEVICE_TYPE_VAAPI, NULL, NULL,
                                            0));
      nassert(o->cdc_ctx[i]->hw_frames_ctx =
                  av_hwframe_ctx_alloc(o->cdc_ctx[i]->hw_device_ctx));
      AVHWFramesContext *hw_frames_ctx =
          (AVHWFramesContext *)o->cdc_ctx[i]->hw_frames_ctx->data;
      hw_frames_ctx->format = AV_PIX_FMT_VAAPI;
      hw_frames_ctx->sw_format = AV_PIX_FMT_NV12;
      hw_frames_ctx->width = ctx->info.width;
      hw_frames_ctx->height = ctx->info.height;

      nassert_ffmpeg(av_hwframe_ctx_init(o->cdc_ctx[i]->hw_frames_ctx));
    }

    config_stream(ctx, o, stream, o->cdc_ctx[i]);
    nassert_ffmpeg(avcodec_open2(o->cdc_ctx[i], stream_codecs[i], NULL));

    stream->time_base = o->cdc_ctx[i]->time_base;
    stream->avg_frame_rate = o->cdc_ctx[i]->framerate;
    nassert_ffmpeg(
        avcodec_parameters_from_context(stream->codecpar, o->cdc_ctx[i]));
  }

  nassert_ffmpeg(avformat_write_header(o->fmt_ctx, NULL));
}

void output_ctx_init_hwframe(output_ctx_t *o, AVFrame *hw_frame,
                             i32 stream_idx) {
  AVCodecContext *cc = o->cdc_ctx[stream_idx];
  hw_frame->hw_frames_ctx = av_buffer_ref(cc->hw_frames_ctx);
  hw_frame->width = cc->width;
  hw_frame->height = cc->height;
  hw_frame->format = AV_PIX_FMT_VAAPI;
}

static bool is_eof_or_eagain(int err) {
  return err == AVERROR_EOF || err == AVERROR(EAGAIN);
}

void flush_packets(output_ctx_t *o, i32 stream_idx) {
  AVPacket *packet = o->ctx->temp_packet;
  int err;
  while (!is_eof_or_eagain(
      err = avcodec_receive_packet(o->cdc_ctx[stream_idx], packet))) {
    nassert_ffmpeg(err);
    av_packet_rescale_ts(packet, o->cdc_ctx[stream_idx]->time_base,
                         o->fmt_ctx->streams[stream_idx]->time_base);
    packet->stream_index = stream_idx;
    nassert_ffmpeg(av_interleaved_write_frame(o->fmt_ctx, packet));
    av_packet_unref(packet);
  }
}

void output_ctx_submit_frame(output_ctx_t *o, AVFrame *frame, i32 stream_idx) {
  int err;
  do {
    flush_packets(o, stream_idx);
  } while ((err = avcodec_send_frame(o->cdc_ctx[stream_idx], frame)) ==
           AVERROR(EAGAIN));

  nassert_ffmpeg(err);
  flush_packets(o, stream_idx);
}

void output_ctx_close(output_ctx_t *o) {
  for (i32 i = 0; i < (i32)o->fmt_ctx->nb_streams; ++i) {
    output_ctx_submit_frame(o, NULL, i);
    flush_packets(o, i);
    avcodec_free_context(&o->cdc_ctx[i]);
  }

  free(o->cdc_ctx);

  nassert_ffmpeg(av_write_trailer(o->fmt_ctx));
  if (!(o->fmt_ctx->flags & AVFMT_NOFILE)) {
    avio_closep(&o->fmt_ctx->pb);
  }

  avformat_free_context(o->fmt_ctx);
}
