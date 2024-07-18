#include "media.h"

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

bool media_open(media_t *media, context_t *context, const char *path) {
  media->context = context;
  media->streams[MEDIA_STREAM_VIDEO].index = stream_index_video(0);
  media->streams[MEDIA_STREAM_AUDIO].index = stream_index_audio(0);
  media->streams[MEDIA_STREAM_SUBS].index = stream_index_subs(0);
  media->next_video_pts = -1;
  if (!demuxer_init(&media->demuxer,
                    &(demuxer_init_t){
                        .path = path,
                        .streams = media->streams,
                        .num_streams = sve2_arrlen(media->streams),
                        .num_buffered_packets = 8,
                    })) {
    log_warn("unable to open demuxer for media '%s'", path);
  }

  media->audio_resampler = NULL;

  for (i32 i = 0; i < MEDIA_NUM_STREAMS_MAX; ++i) {
    if (!media_get_stream(media, i)) {
      continue;
    }

    nassert(decoder_init(&media->decoder[i], &media->demuxer, i,
                         i == MEDIA_STREAM_VIDEO));

    if (i == MEDIA_STREAM_AUDIO) {
      nassert(media->audio_resampler = swr_alloc());
    }
  }

  nassert(media->hw_frame = av_frame_alloc());
  nassert(media->transfer_frame = av_frame_alloc());

  return true;
}

void media_close(media_t *media) {
  av_frame_free(&media->hw_frame);
  av_frame_free(&media->transfer_frame);
  swr_free(&media->audio_resampler);
  for (i32 i = 0; i < MEDIA_NUM_STREAMS_MAX; ++i) {
    if (media_get_stream(media, i)) {
      decoder_free(&media->decoder[i]);
    }
  }
  demuxer_free(&media->demuxer);
}

AVStream *media_get_stream(const media_t *media, media_stream_index_t index) {
  i32 i = media->streams[index].index;
  if (i < 0) {
    return NULL;
  }

  return media->demuxer.fc->streams[i];
}

void media_seek(media_t *media, i64 timestamp) {
  demuxer_cmd_seek(&media->demuxer, -1,
                   timestamp / (SVE2_NS_PER_SEC / AV_TIME_BASE),
                   AVSEEK_FLAG_BACKWARD);
  media->next_video_pts = -1;
  for (i32 i = 0; i < MEDIA_NUM_STREAMS_MAX; ++i) {
    if (media_get_stream(media, i)) {
      decoder_wait_for_seek(&media->decoder[i], SVE_DEADLINE_INF);
    }
  }
}

decode_result_t media_get_video_texture(media_t *media, hw_texture_t *texture,
                                        i64 time) {
  nassert(media_get_stream(media, MEDIA_STREAM_VIDEO));
  // TODO: base this on timing idk
  decoder_t *dec = &media->decoder[MEDIA_STREAM_VIDEO];
  const AVStream *stream =
      media->demuxer.fc->streams[media->streams[MEDIA_STREAM_VIDEO].index];

  decode_result_t err;
  bool updated = false;
  while (media->next_video_pts < time) {
    err = decoder_decode(dec, media->hw_frame, SVE_DEADLINE_INF);
    if (err != DECODE_SUCCESS) {
      return err;
    }

    updated = true;
    i64 current_end_pts = media->hw_frame->pts + media->hw_frame->duration;
    media->next_video_pts = av_rescale_q(current_end_pts, stream->time_base,
                                         (AVRational){1, SVE2_NS_PER_SEC});
  }

  if (updated) {
    hw_texmap_unmap(texture, true);
    *texture = hw_texture_blank(decoder_get_sw_format(dec));
    hw_texmap_to_gl(media->hw_frame, media->transfer_frame, texture);
    av_frame_unref(media->hw_frame);
    av_frame_unref(media->transfer_frame);
  }
  return DECODE_SUCCESS;
}

static decode_result_t media_next_audio_frame(media_t *media, AVFrame *frame) {
  const AVStream *stream;
  nassert(stream = media_get_stream(media, MEDIA_STREAM_AUDIO));
  decode_result_t err = decoder_decode(&media->decoder[MEDIA_STREAM_AUDIO],
                                       media->transfer_frame, SVE_DEADLINE_INF);
  if (err == DECODE_SUCCESS) {
    av_channel_layout_copy(&frame->ch_layout, media->context->info.ch_layout);
    frame->sample_rate = media->context->info.sample_rate;
    frame->format = media->context->info.sample_fmt;
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
    nassert_ffmpeg(swr_convert_frame(media->audio_resampler, frame,
                                     media->transfer_frame));
    i64 out_pts = swr_next_pts(media->audio_resampler, in_pts);
    {
      // this overflows (time_base can't fit in an `int`)
      // frame->pts = av_rescale_q(out_pts, (AVRational){1, time_base},
      //                           (AVRational){1, SVE2_NS_PER_SEC});
      i64 b = 1 * SVE2_NS_PER_SEC;
      i64 c = time_base * 1;
      frame->pts = av_rescale(out_pts, b, c);
    }

    frame->duration = (i64)frame->nb_samples * SVE2_NS_PER_SEC /
                      media->context->info.sample_rate;
    av_frame_unref(media->transfer_frame);
  }

  return err;
}

decode_result_t media_get_audio_frame(media_t *media, AVFrame *frame,
                                      i64 time) {
  decode_result_t result;
  i64 frame_end_pts;
  do {
    if ((result = media_next_audio_frame(media, frame)) != DECODE_SUCCESS) {
      return result;
    }
    frame_end_pts = frame->pts + frame->duration;
  } while (frame_end_pts < time);
  return DECODE_SUCCESS;
}
