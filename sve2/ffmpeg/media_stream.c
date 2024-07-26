#include "media_stream.h"

#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <log.h>

#include "sve2/ffmpeg/decoder.h"
#include "sve2/ffmpeg/demuxer.h"
#include "sve2/ffmpeg/hw_texmap.h"
#include "sve2/utils/minmax.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

static const AVStream *media_get_stream(const media_stream_t *media) {
  return media->demuxer.fc->streams[media->stream.index];
}

static enum AVMediaType media_type(const media_stream_t *media) {
  return media_get_stream(media)->codecpar->codec_type;
}

bool media_stream_open(media_stream_t *media, context_t *context,
                       const char *path, i32 index) {
  media->context = context;
  media->stream.index = index;
  media->audio_resampler = NULL;
  media->eof = false;

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

  const AVStream *stream = media_get_stream(media);
  enum AVMediaType type = media_type(media);
  nassert(decoder_init(&media->decoder, &media->demuxer, 0,
                       type == AVMEDIA_TYPE_VIDEO));
  nassert(media->decoded_frame = av_frame_alloc());
  nassert(media->transfer_frame = av_frame_alloc());
  if (type == AVMEDIA_TYPE_AUDIO) {
    nassert_ffmpeg(swr_alloc_set_opts2(
        &media->audio_resampler, context->info.ch_layout,
        context->info.sample_fmt, context->info.sample_rate,
        &stream->codecpar->ch_layout, stream->codecpar->format,
        stream->codecpar->sample_rate, 0, NULL));
    nassert_ffmpeg(swr_init(media->audio_resampler));
  }

  return true;
}

void media_stream_close(media_stream_t *media) {
  swr_free(&media->audio_resampler);
  av_frame_free(&media->decoded_frame);
  av_frame_free(&media->transfer_frame);
  decoder_free(&media->decoder);
  demuxer_free(&media->demuxer);
}

bool media_stream_eof(const media_stream_t *media) { return media->eof; }

// here we won't use AVRational because the numerator/denomurator can overflow
// an i32
static void convert_pts(AVFrame *frame, i64 orig_time_base_num,
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

static decode_result_t next_frame(media_stream_t *media) {
  decode_result_t err =
      decoder_decode(&media->decoder, media->decoded_frame, SVE_DEADLINE_INF);
  media->eof = media->eof || err == DECODE_EOF;

  // convert pts and duration accordingly
  if (err == DECODE_SUCCESS) {
    AVRational time_base = media_get_stream(media)->time_base;
    convert_pts(media->decoded_frame, time_base.num, time_base.den);
  }

  return err;
}

static void unref_frames(media_stream_t *media) {
  av_frame_unref(media->decoded_frame);
  av_frame_unref(media->transfer_frame);
}

static void map_hw_frame(media_stream_t *media) {
  // asserting we are using hw acceleration
  assert(media->decoder.cc->hw_device_ctx);
  assert(media_type(media) == AVMEDIA_TYPE_VIDEO);
  hw_texmap_unmap(&media->texture, true);
  media->texture = hw_texture_blank(decoder_get_sw_format(&media->decoder));
  hw_texmap_to_gl(media->decoded_frame, media->transfer_frame, &media->texture);
}

void media_stream_seek(media_stream_t *media, i64 timestamp) {
  demuxer_cmd_seek(&media->demuxer, -1,
                   timestamp / (SVE2_NS_PER_SEC / AV_TIME_BASE),
                   AVSEEK_FLAG_BACKWARD);
  decoder_wait_for_seek(&media->decoder, SVE_DEADLINE_INF);

  media->eof = false;

  do {
    decode_result_t err;
    if ((err = next_frame(media)) != DECODE_SUCCESS) {
      goto end;
    }
  } while (media->decoded_frame->pts + media->decoded_frame->duration <
           timestamp);

  switch (media_type(media)) {
  case AVMEDIA_TYPE_VIDEO:
    if (media->decoder.cc->hw_device_ctx) {
      map_hw_frame(media);
    }
    break;
  case AVMEDIA_TYPE_AUDIO:
    // flush remaining samples from before seek
    nassert_ffmpeg(swr_convert(media->audio_resampler, NULL, 0, NULL, 0));
    // submit next frame
    nassert_ffmpeg(swr_convert(media->audio_resampler, NULL, 0,
                               (const u8 *const *)media->decoded_frame->data,
                               media->decoded_frame->nb_samples));
    // skip some samples
    i32 num_skip_samples =
        sve2_min_i32((timestamp - media->decoded_frame->pts) *
                         media->context->info.sample_rate / SVE2_NS_PER_SEC,
                     media->decoded_frame->nb_samples);
    nassert_ffmpeg(swr_drop_output(media->audio_resampler, num_skip_samples));
    break;
  default:
  }

end:
  unref_frames(media);
}

decode_result_t media_get_video_texture(media_stream_t *media,
                                        hw_texture_t *texture, i64 time) {
  const AVStream *stream = media_get_stream(media);
  nassert(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO);

  decode_result_t err;
  bool updated = false;
  while (media->decoded_frame->pts + media->decoded_frame->duration < time) {
    if ((err = next_frame(media)) != DECODE_SUCCESS) {
      return err;
    }
    updated = true;
  }

  if (updated) {
    map_hw_frame(media);
  }

  if (texture) {
    *texture = media->texture;
  }

  return DECODE_SUCCESS;
}

decode_result_t media_get_audio_frame(media_stream_t *media, void *samples,
                                      i32 nb_samples[static 1]) {
  assert(!av_sample_fmt_is_planar(media->context->info.sample_fmt));
  i32 sample_size = av_get_bytes_per_sample(media->context->info.sample_fmt) *
                    media->context->info.ch_layout->nb_channels;
  i32 total_nb_samples = *nb_samples;
  decode_result_t err = DECODE_SUCCESS;
  do {
    i32 num_samples_read = swr_convert(media->audio_resampler,
                                       (u8 *[]){samples}, *nb_samples, NULL, 0);
    nassert_ffmpeg(num_samples_read);
    // *nb_samples here is the number of
    // leftover samples to read, so it decreases
    *nb_samples -= num_samples_read;
    samples = (char *)samples + sample_size * num_samples_read;

    if (*nb_samples == 0) {
      break;
    }

    if ((err = next_frame(media)) != DECODE_SUCCESS) {
      break;
    }

    nassert_ffmpeg(swr_convert(media->audio_resampler, NULL, 0,
                               (const u8 *const *)media->decoded_frame->data,
                               media->decoded_frame->nb_samples));
  } while (true);

  *nb_samples = total_nb_samples - *nb_samples;
  return err;
}
