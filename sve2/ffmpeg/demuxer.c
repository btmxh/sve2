#include "demuxer.h"

#include <string.h>
#include <threads.h>

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <log.h>

#include "sve2/utils/mpmc.h"
#include "sve2/utils/rb.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"
#include "sve2/utils/types.h"

AVFormatContext *open_media(const char *path) {
  AVFormatContext *fc = NULL;
  int err = avformat_open_input(&fc, path, NULL, NULL);
  if (err < 0) {
    log_error("unable to open media at '%s': %s", path, av_err2str(err));
    return NULL;
  }

  log_info("opened media '%s' with AVFormatContext %p", path, (const void *)fc);

  err = avformat_find_stream_info(fc, NULL);
  if (err < 0) {
    log_error("unable to find stream info of media '%s': %s", path,
              av_err2str(err));
    avformat_close_input(&fc);
    return NULL;
  }

  /* av_dump_format(fc, 0, path, NULL); */

  return fc;
}

typedef enum {
  VIDEO = (u32)1u << 20,
  AUDIO = (u32)1u << 21,
  SUBS = (u32)1u << 22,
} stream_index_bit_t;

i32 bitfield_index(stream_index_bit_t type, i16 index) {
  assert(index >= 0);
  u32 bitfield_index = type | (u32)index;
  return -(i32)(bitfield_index + 1);
}

i32 stream_index_video(i16 index) { return bitfield_index(VIDEO, index); }
i32 stream_index_audio(i16 index) { return bitfield_index(AUDIO, index); }
i32 stream_index_subs(i16 index) { return bitfield_index(SUBS, index); }

static bool decompose_stream_index(i32 index, enum AVMediaType *type,
                                   i16 *relative_index) {
  type = type ? type : &(enum AVMediaType){AVMEDIA_TYPE_UNKNOWN};
  relative_index = relative_index ? relative_index : &(i16){0};

  if (index >= 0) {
    *type = AVMEDIA_TYPE_UNKNOWN;
    *relative_index = index;
    return index <= (i32)INT16_MAX;
  }

  u32 bitfield_index = (u32) - (index + 1);
  if (bitfield_index & VIDEO) {
    *type = AVMEDIA_TYPE_VIDEO;
  } else if (bitfield_index & AUDIO) {
    *type = AVMEDIA_TYPE_AUDIO;
  } else if (bitfield_index & SUBS) {
    *type = AVMEDIA_TYPE_SUBTITLE;
  } else {
    log_warn("invalid stream bitfield index %x", (unsigned)bitfield_index);
    return false;
  }

  i32 rel_index = (i32)(bitfield_index & ~(VIDEO | AUDIO | SUBS));
  *relative_index = rel_index;
  return rel_index >= 0 && rel_index <= (i32)INT16_MAX;
}

char *stream_index_to_string(i32 index, char *str, i32 bufsize) {
  if (index >= 0) {
    nassert(snprintf(str, bufsize, ":%" PRIi32, index));
    return str;
  }

  enum AVMediaType type;
  i16 rel_index;
  if (!decompose_stream_index(index, &type, &rel_index)) {
    strncpy(str, ":?", bufsize - 1);
    return str;
  }

  nassert(type != AVMEDIA_TYPE_UNKNOWN);
  nassert(snprintf(str, bufsize, "%c:%" PRIi32,
                   av_get_media_type_string(type)[0], index));
  return str;
}

typedef struct {
  // seek args
  i64 seek_offset;
  i32 seek_stream_index;
  int seek_flags;
  bool exit : 1;
  bool late_packet : 1;
  bool seek : 1;
} cmd_t;

// return true if exit cmd received
bool demuxer_handle_cmd(demuxer_t *d, bool *late_packet, bool *error,
                        i64 timeout, AVPacket **packet,
                        i32 *packet_stream_index) {
  i64 deadline =
      timeout == 0 ? SVE_DEADLINE_NOW : (threads_timer_now() + timeout);
  cmd_t cmd;
  while (mpmc_recv(&d->cmd, &cmd, deadline)) {
    if (cmd.exit) {
      log_info("demuxer thread received exit command");
      return true;
    } else if (cmd.late_packet) {
      log_info("demuxer thread received late packet command");
      *late_packet = true;
    } else if (cmd.seek) {
      log_info("demuxer thread received seek command");
      int err = av_seek_frame(d->init.fc, cmd.seek_stream_index,
                              cmd.seek_offset, cmd.seek_flags);
      if (err < 0) {
        *error = true;
        log_warn("unable to seek media: %s", av_err2str(err));
        return true;
      }

      av_packet_free(packet);
      *packet_stream_index = -1;

      for (i32 i = 0; i < d->init.num_streams; ++i) {
        log_trace("sending seek packet message to stream %s",
                  sve2_si2str(d->init.streams[i].index));
        nassert(mpmc_send(&d->init.streams[i].packet_channel,
                          &(packet_msg_t){.seek = true}, SVE_DEADLINE_INF));
      }
    }
  }

  return false;
}

bool demuxer_should_send(demuxer_t *d) {
  for (i32 i = 0; i < d->init.num_streams; ++i) {
    if (mpmc_len(&d->init.streams[i].packet_channel) <
        d->init.num_buffered_packets) {
      return true;
    }
  }

  return false;
}

static void dump_avpacket_info(AVPacket *pkt) {
  log_trace("read packet %p: pts=%" PRId64 ", dts=%" PRId64
            ", duration=%" PRId64 ", si=%d, size=%d, flags=%d",
            (const void *)pkt, pkt->pts, pkt->dts, pkt->duration,
            pkt->stream_index, pkt->size, pkt->flags);
}

int demuxer_thread(void *u) {
  demuxer_t *d = (demuxer_t *)u;
  bool late_packet = false;
  bool error = false;
  i64 timeout = 0;
  AVPacket *packet = NULL;
  i32 packet_stream_index = -1;

  while (!demuxer_handle_cmd(d, &late_packet, &error, timeout, &packet,
                             &packet_stream_index)) {
    if (packet_stream_index >= 0) {
      if (!late_packet && !demuxer_should_send(d)) {
        timeout = 10 * SVE2_NS_PER_SEC / 1000; // 10ms
        continue;
      }

      late_packet = false;
      assert(packet_stream_index < d->init.num_streams);
      nassert(mpmc_send(&d->init.streams[packet_stream_index].packet_channel,
                        &(packet_msg_t){
                            .packet = packet,
                            .regular = true,
                        },
                        SVE_DEADLINE_INF));
      packet = NULL;
      packet_stream_index = -1;
    }

    if (!packet) {
      packet = av_packet_alloc();
      nassert(packet);
    }

    while (packet_stream_index < 0) {
      int err = av_read_frame(d->init.fc, packet);
      if (err < 0) {
        error = err != AVERROR_EOF;
        break;
      }

      dump_avpacket_info(packet);

      for (i32 i = 0; i < d->init.num_streams; ++i) {
        if (d->init.streams[i].index == packet->stream_index) {
          packet_stream_index = i;
          break;
        }
      }

      if (packet_stream_index < 0) {
        av_packet_unref(packet);
      }
    }
  }

  av_packet_free(&packet);
  for (i32 i = 0; i < d->init.num_streams; ++i) {
    mpmc_send(&d->init.streams[i].packet_channel,
              &(packet_msg_t){
                  .error = error,
                  .eof = !error,
              },
              SVE_DEADLINE_INF);
  }

  return 0;
}

static i32 find_stream_index(AVFormatContext *fc, i32 index) {
  if (index >= 0) {
    return index < (i32)fc->nb_streams ? index : -1;
  }

  enum AVMediaType type;
  i16 rel_index;
  if (!decompose_stream_index(index, &type, &rel_index)) {
    return -1;
  }

  i32 counter = 0;
  for (i32 i = 0; i < (i32)fc->nb_streams; ++i) {
    if (fc->streams[i]->codecpar->codec_type != type) {
      continue;
    }

    if (rel_index == counter++) {
      return i;
    }
  }

  log_warn("media file only has %" PRIi32
           " %s streams, requested stream of index %" PRIi16,
           counter, av_get_media_type_string(type), index);
  return -1;
}

static void init_stream(AVFormatContext *fc, demuxer_stream_t *stream,
                        i32 initial_cap) {
  i32 index = find_stream_index(fc, stream->index);
  log_info("mapped stream %s of AVFormatContext %p to %s",
           sve2_si2str(stream->index), (const void *)fc, sve2_si2str(index));
  stream->index = index;

  if (stream->index < 0) {
    return;
  }

  mpmc_init(&stream->packet_channel, initial_cap, SVE2_RB_DEFAULT_GROW,
            sizeof(packet_msg_t));
}

void demuxer_init(demuxer_t *d, const demuxer_init_t *info) {
  d->init = *info;
  mpmc_init(&d->cmd, 0, SVE2_RB_DEFAULT_GROW, sizeof(cmd_t));
  for (i32 i = 0; i < info->num_streams; ++i) {
    init_stream(d->init.fc, &d->init.streams[i], info->num_buffered_packets);
  }

  sve2_thrd_create(&d->thread, demuxer_thread, d);
}

void demuxer_cmd_exit(demuxer_t *d) {
  nassert(mpmc_send(&d->cmd, &(cmd_t){.exit = true}, SVE_DEADLINE_INF));
}

void demuxer_cmd_late_packet(demuxer_t *d) {
  nassert(mpmc_send(&d->cmd, &(cmd_t){.late_packet = true}, SVE_DEADLINE_INF));
}

void demuxer_cmd_seek(demuxer_t *d, i32 stream_index, i64 offset, int flags) {
  nassert(mpmc_send(&d->cmd,
                    &(cmd_t){.seek = true,
                             .seek_stream_index = stream_index,
                             .seek_offset = offset,
                             .seek_flags = flags},
                    SVE_DEADLINE_INF));
}

void demuxer_free(demuxer_t *d) {
  demuxer_cmd_exit(d);
  int code;
  nassert(thrd_join(d->thread, &code) == thrd_success);
  if (code != 0) {
    log_warn("demuxer thread exited with code %d", code);
  }

  mpmc_free(&d->cmd);
  for (i32 i = 0; i < d->init.num_streams; ++i) {
    demuxer_stream_t *s = &d->init.streams[i];
    if (s->index < 0) {
      continue;
    }

    packet_msg_t msg;
    while (mpmc_recv(&s->packet_channel, &msg, SVE_DEADLINE_NOW)) {
      if (msg.regular) {
        log_trace("freeing packet %p", (const void *)msg.packet);
        av_packet_free(&msg.packet);
      }
    }
    mpmc_free(&s->packet_channel);
  }
}
