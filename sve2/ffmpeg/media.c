#include "media.h"

#include <libavutil/avutil.h>
#include <libavutil/frame.h>
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
    if (!media_has_stream(media, i)) {
      continue;
    }

    nassert(decoder_init(&media->decoder[i], &media->demuxer, i,
                         i == MEDIA_STREAM_VIDEO));

    if (i == MEDIA_STREAM_AUDIO) {
      nassert(swr_alloc_set_opts2(
                  &media->audio_resampler, context->info.ch_layout,
                  context->info.sample_fmt, context->info.sample_rate,
                  &media->decoder[i].cc->ch_layout,
                  media->decoder[i].cc->sample_fmt,
                  media->decoder[i].cc->sample_rate, 0, NULL) >= 0);
      nassert(media->audio_resampler);
      nassert(swr_init(media->audio_resampler) >= 0);
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
    if (media_has_stream(media, i)) {
      decoder_free(&media->decoder[i]);
    }
  }
  demuxer_free(&media->demuxer);
}

bool media_has_stream(const media_t *media, media_stream_index_t index) {
  return media->streams[index].index >= 0;
}

void media_seek(media_t *media, i64 timestamp) {
  demuxer_cmd_seek(&media->demuxer, -1,
                   timestamp / (SVE2_NS_PER_SEC / AV_TIME_BASE), 0);
  for (i32 i = 0; i < MEDIA_NUM_STREAMS_MAX; ++i) {
    if (media_has_stream(media, i)) {
      decoder_wait_for_seek(&media->decoder[i], SVE_DEADLINE_INF);
    }
  }
}

decode_result_t media_map_video_texture(media_t *media, hw_texture_t *texture) {
  nassert(media_has_stream(media, MEDIA_STREAM_VIDEO));
  // TODO: base this on timing idk
  decoder_t *dec = &media->decoder[MEDIA_STREAM_VIDEO];

  decode_result_t err = decoder_decode(dec, media->hw_frame, SVE_DEADLINE_INF);
  if (err == DECODE_SUCCESS) {
    *texture = hw_texture_blank(decoder_get_sw_format(dec));
    hw_texmap_to_gl(media->hw_frame, media->transfer_frame, texture);
  }

  return err;
}

void media_unmap_video_texture(media_t *media, hw_texture_t *texture) {
  av_frame_unref(media->hw_frame);
  av_frame_unref(media->transfer_frame);
  hw_texmap_unmap(texture, true);
}

decode_result_t media_get_audio_frame(media_t *media, AVFrame *frame) {
  nassert(media_has_stream(media, MEDIA_STREAM_AUDIO));
  decode_result_t err = decoder_decode(&media->decoder[MEDIA_STREAM_AUDIO],
                                       media->transfer_frame, SVE_DEADLINE_INF);
  if (err == DECODE_SUCCESS) {
    av_channel_layout_copy(&frame->ch_layout, media->context->info.ch_layout);
    frame->sample_rate = media->context->info.sample_rate;
    frame->format = media->context->info.sample_fmt;
    nassert(swr_convert_frame(media->audio_resampler, frame,
                              media->transfer_frame) >= 0);
    av_frame_unref(media->transfer_frame);
  }

  return err;
}
