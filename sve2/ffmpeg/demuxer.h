#pragma once

#include <libavformat/avformat.h>

#include "sve2/utils/mpmc.h"
#include "sve2/utils/types.h"

typedef struct {
  i32 index;
  mpmc_t packet_channel;
} demuxer_stream_t;

typedef struct {
  const char *path;
  demuxer_stream_t *streams;
  i32 num_streams;
  i32 num_buffered_packets;
} demuxer_init_t;

typedef struct {
  AVFormatContext *fc;
  demuxer_init_t init;
  thrd_t thread;
  mpmc_t cmd;
} demuxer_t;

typedef struct {
  AVPacket *packet;
  bool regular : 1;
  bool error : 1;
  bool eof : 1;
  bool seek : 1;
} packet_msg_t;

i32 stream_index_regular(i16 index);
i32 stream_index_video(i16 index);
i32 stream_index_audio(i16 index);
i32 stream_index_subs(i16 index);

#define STREAM_INDEX_STRING_MAX_LENGTH 16 // including \0
char *stream_index_to_string(i32 index, char *str, i32 bufsize);
#define sve2_si2str(index)                                                     \
  stream_index_to_string(index, (char[STREAM_INDEX_STRING_MAX_LENGTH]){0},     \
                         STREAM_INDEX_STRING_MAX_LENGTH)

bool demuxer_init(demuxer_t *d, const demuxer_init_t *info);
void demuxer_cmd_exit(demuxer_t *d);
void demuxer_cmd_late_packet(demuxer_t *d);
void demuxer_cmd_seek(demuxer_t *d, i32 stream_index, i64 offset, int flags);
void demuxer_free(demuxer_t *d);
