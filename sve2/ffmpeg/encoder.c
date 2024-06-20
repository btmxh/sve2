#include "encoder.h"

#include <glad/egl.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <log.h>

#include "sve2/context/context.h"
#include "sve2/gl/shader.h"
#include "sve2/utils/runtime.h"

// video bit-per-pixel
#define VIDEO_BPP 1.0
#define VIDEO_PIX_FMT AV_PIX_FMT_VAAPI
#define VIDEO_SW_PIX_FMT AV_PIX_FMT_NV12
#define AUDIO_SAMPLE_FMT AV_SAMPLE_FMT_S16

static void default_encoder_config_fn(AVCodecContext *ctx, void *userptr) {
  context_t *c = (context_t *)userptr;
  ctx->codec_id = ctx->codec->id;
  ctx->codec_type = ctx->codec->type;

  i32 width, height;
  context_get_framebuffer_info(c, &width, &height, NULL, NULL);
  i32 frame_rate = c->info.fps;
  i32 sample_rate = c->info.sample_rate;

  switch (ctx->codec->type) {
  case AVMEDIA_TYPE_VIDEO:
    ctx->width = width;
    ctx->height = height;
    ctx->time_base = (AVRational){1, frame_rate};
    ctx->framerate = (AVRational){frame_rate, 1};
    ctx->sample_aspect_ratio = (AVRational){1, 1};
    ctx->pix_fmt = VIDEO_PIX_FMT;
    ctx->bit_rate = (i64)(width * height * frame_rate * VIDEO_BPP);
    ctx->sw_pix_fmt = VIDEO_SW_PIX_FMT;
    // stolen from ffmpeg demo (probably does not matter much)
    // ctx->gop_size = 12;
    log_info("initializing video encoder with w=%d, h=%d, fr=%d, br=%" PRIi64
             ", pixfmt=%s, sw_pixfmt=%s",
             width, height, frame_rate, ctx->bit_rate,
             av_get_pix_fmt_name(VIDEO_PIX_FMT),
             av_get_pix_fmt_name(VIDEO_SW_PIX_FMT));
    ctx->flags = AV_CODEC_FLAG_GLOBAL_HEADER;
    ctx->max_b_frames = 0;
    break;
  case AVMEDIA_TYPE_AUDIO:
    ctx->sample_fmt = AUDIO_SAMPLE_FMT;
    ctx->sample_rate = sample_rate;
    ctx->bit_rate = 320000;
    ctx->time_base = (AVRational){1, sample_rate};
    nassert(av_channel_layout_copy(
                &ctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO) >=
            0);
    log_info(
        "initializing audio encoder with sr=%d, br=%" PRIi64 ", samplefmt=%s",
        sample_rate, ctx->bit_rate, av_get_sample_fmt_name(AUDIO_SAMPLE_FMT));
    break;
  default:
    log_warn("initializing %s encoder with no parameters",
             av_get_media_type_string(ctx->codec->type));
    break;
  }
}

void encoder_init(encoder_t *e, context_t *c, const AVCodec *codec,
                  bool hwaccel, encoder_config_fn config_fn, void *userptr) {
  nassert(e && codec);
  nassert(e->c = avcodec_alloc_context3(codec));

  e->c->codec = codec;
  if (!config_fn) {
    config_fn = default_encoder_config_fn;
    userptr = c;
  }
  config_fn(e->c, userptr);

  if (hwaccel) {
    if (codec->type != AVMEDIA_TYPE_VIDEO) {
      log_warn("HW accelerated encoding is only supported for video codecs");
    }

    int err = av_hwdevice_ctx_create(&e->hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
                                     NULL, NULL, 0);
    if (err < 0) {
      log_warn("unable to create HW acceleration context: %s", av_err2str(err));
    }

    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(e->hw_device_ctx);
    nassert(hw_frames_ref);
    AVHWFramesContext *hw_frames_ctx = (AVHWFramesContext *)hw_frames_ref->data;
    hw_frames_ctx->format = VIDEO_PIX_FMT;
    hw_frames_ctx->sw_format = VIDEO_SW_PIX_FMT;
    hw_frames_ctx->width = e->c->width;
    hw_frames_ctx->height = e->c->height;
    /* hw_frames_ctx->initial_pool_size = 20; */
    nassert(av_hwframe_ctx_init(hw_frames_ref) >= 0);
    nassert(e->c->hw_frames_ctx = av_buffer_ref(hw_frames_ref));
    av_buffer_unref(&hw_frames_ref);
  } else {
    e->hw_device_ctx = NULL;
  }

  nassert_ffmpeg(avcodec_open2(e->c, codec, NULL));
}

bool encoder_submit_frame(encoder_t *e, const AVFrame *frame) {
  nassert(avcodec_is_open(e->c));
  nassert(av_codec_is_encoder(e->c->codec));
  int err = avcodec_send_frame(e->c, frame);
  assert(err >= 0 || err == AVERROR(EAGAIN));
  return err >= 0;
}

AVPacket *encoder_receive_packet(encoder_t *e) {
  AVPacket *packet = av_packet_alloc();
  assert(packet);
  int err = avcodec_receive_packet(e->c, packet);
  assert(err >= 0 || err == AVERROR(EAGAIN) || err == AVERROR_EOF);
  if (err < 0) {
    av_packet_free(&packet);
  }

  return packet;
}

void encoder_free(encoder_t *e) {
  av_buffer_unref(&e->c->hw_frames_ctx);
  av_buffer_unref(&e->hw_device_ctx);
  avcodec_free_context(&e->c);
}
