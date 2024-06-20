#include "muxer.h"

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <log.h>

#include "sve2/ffmpeg/encoder.h"
#include "sve2/utils/runtime.h"

void muxer_init(muxer_t *m, const char *path) {
  nassert(avformat_alloc_output_context2(&m->fc, NULL, NULL, path) >= 0);
  m->fc->nb_streams = 0;
  m->encoders = NULL;

  if (!(m->fc->flags & AVFMT_NOFILE)) {
    nassert(avio_open(&m->fc->pb, path, AVIO_FLAG_WRITE) >= 0);
  }

  nassert(m->fc->oformat);
}

i32 muxer_new_stream(muxer_t *m, context_t *c, const AVCodec *codec,
                     bool hwaccel, encoder_config_fn config_fn, void *userptr) {
  AVStream *stream = avformat_new_stream(m->fc, codec);
  nassert(stream);
  stream->id = (i32)m->fc->nb_streams - 1;
  m->encoders =
      sve2_realloc(m->encoders, m->fc->nb_streams * sizeof *m->encoders);
  encoder_init(&m->encoders[stream->id], c, codec, hwaccel, config_fn, userptr);
  if (m->fc->oformat->flags & AVFMT_GLOBALHEADER) {
    m->encoders[stream->id].c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  stream->time_base = m->encoders[stream->id].c->time_base;
  nassert(avcodec_parameters_from_context(stream->codecpar,
                                          m->encoders[stream->id].c) >= 0);
  return stream->id;
}

void muxer_begin(muxer_t *m) {
  nassert_ffmpeg(avformat_write_header(m->fc, NULL));
}

static void muxer_flush(muxer_t *m, i32 stream_index) {
  AVPacket *packet;
  while ((packet = encoder_receive_packet(&m->encoders[stream_index]))) {
    av_packet_rescale_ts(packet, m->encoders[stream_index].c->time_base,
                         m->fc->streams[stream_index]->time_base);
    packet->stream_index = stream_index;
    av_interleaved_write_frame(m->fc, packet);
    av_packet_free(&packet);
  }
}

void muxer_end(muxer_t *m) {
  for (i32 i = 0; i < (i32) m->fc->nb_streams; ++i) {
    encoder_submit_frame(&m->encoders[i], NULL);
    muxer_flush(m, i);
  }
  nassert(av_write_trailer(m->fc) >= 0);
}

void muxer_submit_frame(muxer_t *m, const AVFrame *frame, i32 stream_index) {
  nassert(stream_index < (i32)m->fc->nb_streams && stream_index >= 0);
  do {
    muxer_flush(m, stream_index);
  } while (!encoder_submit_frame(&m->encoders[stream_index], frame));
  muxer_flush(m, stream_index);
}

void muxer_free(muxer_t *m) {
  for (i32 i = 0; i < (i32)m->fc->nb_streams; ++i) {
    encoder_free(&m->encoders[i]);
  }
  free(m->encoders);

  if (!(m->fc->flags & AVFMT_NOFILE)) {
    avio_closep(&m->fc->pb);
  }

  avformat_free_context(m->fc);
}
