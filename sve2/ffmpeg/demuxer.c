#include "demuxer.h"

#include <string.h>
#include <threads.h>

#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <log.h>

#include "sve2/utils/mpmc.h"
#include "sve2/utils/rb.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

AVFormatContext *open_media(const char *path) {
  AVFormatContext *fc = NULL;
  int err = avformat_open_input(&fc, path, NULL, NULL);
  if (err < 0) {
    log_error("unable to open media at '%s': %s", path, av_err2str(err));
    return NULL;
  }

  err = avformat_find_stream_info(fc, NULL);
  if (err < 0) {
    log_error("unable to find stream info of media '%s': %s", path,
              av_err2str(err));
    avformat_close_input(&fc);
    return NULL;
  }

  return fc;
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
                        i64 timeout) {
  i64 deadline =
      timeout == 0 ? SVE_DEADLINE_NOW : (threads_timer_now() + timeout);
  cmd_t cmd;
  while (mpmc_recv(&d->cmd, &cmd, deadline)) {
    if (cmd.exit) {
      return true;
    } else if (cmd.late_packet) {
      *late_packet = true;
    } else if (cmd.seek) {
      int err = av_seek_frame(d->init.fc, cmd.seek_stream_index,
                              cmd.seek_offset, cmd.seek_flags);
      if (err < 0) {
        *error = true;
        log_warn("unable to seek media: %s", av_err2str(err));
        return true;
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

int demuxer_thread(void *u) {
  demuxer_t *d = (demuxer_t *)u;
  bool late_packet = false;
  bool error = false;
  i64 timeout = 0;
  AVPacket *packet = NULL;
  i32 packet_stream_index = -1;

  while (!demuxer_handle_cmd(d, &late_packet, &error, timeout)) {
    if (packet_stream_index >= 0) {
      if (!late_packet && !demuxer_should_send(d)) {
        timeout = SVE2_NS_PER_SEC / 100; // 10ms
        continue;
      }

      late_packet = false;
      assert(packet_stream_index < d->init.num_streams);
      mpmc_send(&d->init.streams[packet_stream_index].packet_channel,
                &(packet_msg_t){
                    .packet = packet,
                    .regular = true,
                },
                SVE_DEADLINE_INF);
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

      for (i32 i = 0; i < d->init.num_streams; ++i) {
        if (d->init.streams->index == packet->stream_index) {
          packet_stream_index = i;
          break;
        }
      }
    }
  }

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

void demuxer_init(demuxer_t *d, const demuxer_init_t *info) {
  d->init = *info;
  mpmc_init(&d->cmd, 0, SVE2_RB_DEFAULT_GROW, sizeof(cmd_t));
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
}
