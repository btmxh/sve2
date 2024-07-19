#include "media_stream.h"

#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
#include <log.h>

#include "sve2/ffmpeg/decoder.h"
#include "sve2/ffmpeg/demuxer.h"
#include "sve2/ffmpeg/hw_texmap.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

bool media_stream_open(media_stream_t *media, context_t *context,
                       const char *path, i32 index) {
  media->context = context;
  media->stream.index = index;
  media->next_video_pts = -1;
  media->audio_resampler = NULL;

  if (!demuxer_init(&media->demuxer, &(demuxer_init_t){
                                         .path = path,
                                         .num_streams = 1,
                                         .streams = &media->stream,
                                         .num_buffered_packets = 8,
                                     })) {
    log_warn("unable to open demuxer for media '%s'", path);
    return false;
  }

  if (media->stream.index < 0) {
    log_warn("unable to find stream %s in media '%s'", sve2_si2str(index),
             path);
    return false;
  }

  enum AVMediaType type =
      media->demuxer.fc->streams[media->stream.index]->codecpar->codec_type;
  nassert(decoder_init(&media->decoder, &media->demuxer, 0,
                       type == AVMEDIA_TYPE_VIDEO));
  nassert(media->hw_frame = av_frame_alloc());
  nassert(media->transfer_frame = av_frame_alloc());
  nassert(media->out_frame = av_frame_alloc());
  if (type == AVMEDIA_TYPE_AUDIO) {
    nassert(media->audio_resampler = swr_alloc());
  }

  return true;
}

void media_stream_close(media_stream_t *media) {
  swr_free(&media->audio_resampler);
  av_frame_free(&media->hw_frame);
  av_frame_free(&media->transfer_frame);
  av_frame_free(&media->out_frame);
  decoder_free(&media->decoder);
  demuxer_free(&media->demuxer);
}

bool media_stream_eof(const media_stream_t *media) {
  return decoder_eof(&media->decoder);
}

const AVStream *media_get_stream(const media_stream_t *media) {
  return media->demuxer.fc->streams[media->stream.index];
}

void media_stream_seek(media_stream_t *media, i64 timestamp) {
  demuxer_cmd_seek(&media->demuxer, -1,
                   timestamp / (SVE2_NS_PER_SEC / AV_TIME_BASE),
                   AVSEEK_FLAG_BACKWARD);
  decoder_wait_for_seek(&media->decoder, SVE_DEADLINE_INF);
  media->next_video_pts = -1;
}

decode_result_t media_get_video_texture(media_stream_t *media,
                                        hw_texture_t *texture, i64 time) {
  const AVStream *stream = media_get_stream(media);
  nassert(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO);

  decode_result_t err;
  bool updated = false;
  while (media->next_video_pts < time) {
    err = decoder_decode(&media->decoder, media->hw_frame, SVE_DEADLINE_INF);
    if (err != DECODE_SUCCESS) {
      return err;
    }

    updated = true;
    i64 current_end_pts = media->hw_frame->pts + media->hw_frame->duration;
    media->next_video_pts = av_rescale_q(current_end_pts, stream->time_base,
                                         (AVRational){1, SVE2_NS_PER_SEC});
  }

  if (updated) {
    hw_texmap_unmap(&media->texture, true);
    media->texture = hw_texture_blank(decoder_get_sw_format(&media->decoder));
    hw_texmap_to_gl(media->hw_frame, media->transfer_frame, &media->texture);
    av_frame_unref(media->hw_frame);
    av_frame_unref(media->transfer_frame);
  }

  if (texture) {
    *texture = media->texture;
  }

  return DECODE_SUCCESS;
}

static decode_result_t get_next_audio_frame(media_stream_t *media) {
  const AVStream *stream = media_get_stream(media);
  nassert(stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO);

  decode_result_t err =
      decoder_decode(&media->decoder, media->transfer_frame, SVE_DEADLINE_INF);
  if (err == DECODE_SUCCESS) {
    av_channel_layout_copy(&media->out_frame->ch_layout,
                           media->context->info.ch_layout);
    media->out_frame->sample_rate = media->context->info.sample_rate;
    media->out_frame->format = media->context->info.sample_fmt;
    i64 time_base =
        (i64)stream->codecpar->sample_rate * media->context->info.sample_rate;
    i64 in_pts;
    {
      // this overflows (time_base can't fit in an `int`)
      // i64 in_pts = av_rescale_q(media->transfer_frame->pts,
      //        stream->time_base, (AVRational){1, time_base});
      i64 b = stream->time_base.num * time_base;
      i64 c = stream->time_base.den * 1;
      in_pts = av_rescale(media->transfer_frame->pts, b, c);
    }
    nassert_ffmpeg(swr_convert_frame(media->audio_resampler, media->out_frame,
                                     media->transfer_frame));
    i64 out_pts = swr_next_pts(media->audio_resampler, in_pts);
    {
      // this overflows (time_base can't fit in an `int`)
      // frame->pts = av_rescale_q(out_pts, (AVRational){1, time_base},
      //                           (AVRational){1, SVE2_NS_PER_SEC});
      i64 b = 1 * SVE2_NS_PER_SEC;
      i64 c = time_base * 1;
      media->out_frame->pts = av_rescale(out_pts, b, c);
    }

    media->out_frame->duration = (i64)media->out_frame->nb_samples *
                                 SVE2_NS_PER_SEC /
                                 media->context->info.sample_rate;
    av_frame_unref(media->transfer_frame);
  }

  return err;
}

decode_result_t media_get_audio_frame(media_stream_t *media, AVFrame **frame,
                                      i64 time) {
  av_frame_unref(media->out_frame);
  decode_result_t result;
  i64 frame_end_pts;

  do {
    if ((result = get_next_audio_frame(media)) != DECODE_SUCCESS) {
      return result;
    }
    frame_end_pts = media->out_frame->pts + media->out_frame->duration;
  } while (frame_end_pts < time);

  if (frame) {
    *frame = media->out_frame;
  }
  return DECODE_SUCCESS;
}
