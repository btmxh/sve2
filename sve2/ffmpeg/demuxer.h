#pragma once

#include <libavformat/avformat.h>

#include "sve2/utils/mpmc.h"
#include "sve2/utils/types.h"

// demuxer API is a wrapper of AVFormatContext
// ngl the more i think about this the more i think this is obsolete xdd

/**
 * @brief acts as both input and output for demuxer initialization
 * input: specify which streams to demux from (streams that are not specified
 * will be ignored) output: return the absolute stream index and a packet
 * channel for said stream
 */
typedef struct {
  // stream index, set this before initialization to specify which streams to
  // demux from could be either relative or absolute stream index:
  // - absolute index: a positive value (e.g. 0 -> first stream, 1 -> second
  // stream, etc.) returned by stream_index_regular()
  // - relative index: a negative value, returned by the stream_index_*()
  // functions. This is more useful since most of the time, we want the first
  // video stream (stream_index_video(0)) and the first audio stream
  // (stream_index_audio(0)), etc.
  i32 index;
  // packet channel, used to receive packets from the demuxer thread. This is
  // initialized by demuxer initialization.
  mpmc_t packet_channel;
} demuxer_stream_t;

/**
 * @brief Initialization params for demuxer
 */
typedef struct {
  /**
   * @brief Input file path
   */
  const char *path;
  /**
   * @brief An array of streams that is going to be demuxed. Other streams'
   * packets will be discarded.
   */
  demuxer_stream_t *streams;
  /**
   * @brief Number of streams in the streams array
   */
  i32 num_streams;
  /**
   * @brief Number of buffered packets. Since packets are demuxed sequentially,
   * packets might be buffered in the packet channel. This is to limit the
   * channel size. A small value would work fine (something > 2).
   */
  i32 num_buffered_packets;
} demuxer_init_t;

/**
 * @brief Demuxer, responsible for reading packets from a media file and send it
 * to the correct packet channel.
 */
typedef struct {
  /**
   * @brief Initialization info of the demuxer
   */
  demuxer_init_t init;
  /**
   * @brief FFmpeg format context, reading packets from the file
   */
  AVFormatContext *fc;
  /**
   * @brief Thread handle of the demuxing thread. The demuxing process is done
   * in an external thread (ngl I think that there are better solutions now but
   * idk on-the-fly demuxing would be good for latency)
   */
  thrd_t thread;
  /**
   * @brief Command MPMC queue between demuxing thread and the main thread
   */
  mpmc_t cmd;
} demuxer_t;

/**
 * @brief Packet message, either:
 * - A regular packet.
 * - An error message, indicating demuxing failure.
 * - An EOF message, indicating the stream has no more packets.
 * - A seek message, indicating the stream has been seeked (and all packets
 * received prior to this can be discarded).
 */
typedef struct {
  AVPacket *packet;
  bool regular : 1;
  bool error : 1;
  bool eof : 1;
  bool seek : 1;
} packet_msg_t;

// Constructors for absolute/relative indices
i32 stream_index_regular(i16 index); // -> index-th stream of the file
i32 stream_index_video(i16 index);   // -> index-th video stream
i32 stream_index_audio(i16 index);   // -> index-th audio stream
i32 stream_index_subs(i16 index);    // -> index-th subtitle stream

// relative/absolute stream index to string conversion utilities

#define STREAM_INDEX_STRING_MAX_LENGTH 16 // including \0
/**
 * @brief Convert the stream index in index to string.
 *
 * @param index The stream index (absolute or relative)
 * @param str Storage buffer for returned string.
 * @param bufsize Buffer size of str.
 * @return The same value as str, this is needed for the implementation of
 * the sve2_si2str() macro.
 */
char *stream_index_to_string(i32 index, char *str, i32 bufsize);
#define sve2_si2str(index)                                                     \
  stream_index_to_string(index, (char[STREAM_INDEX_STRING_MAX_LENGTH]){0},     \
                         STREAM_INDEX_STRING_MAX_LENGTH)

/**
 * @brief Initialize a demuxer
 *
 * @param d Pointer to an uninitialized demuxer_t object
 * @param info Demuxer initialization parameters
 * @return Whether the operation succeeded. This fails (and not panic) only if
 * the input file is not found.
 */
bool demuxer_init(demuxer_t *d, const demuxer_init_t *info);
/**
 * @brief Send exit command to demuxer thread
 *
 * @param d The demuxer context
 */
void demuxer_cmd_exit(demuxer_t *d);
/**
 * @brief Send late-packet command to demuxer thread. This forces the demuxer to
 * send new packets instead of waiting for space.
 *
 * @param d The demuxer context
 */
void demuxer_cmd_late_packet(demuxer_t *d);
/**
 * @brief Send seek command to demuxer thread. Use this to seek in the media
 * file.
 *
 * @param d The demuxer context
 */
void demuxer_cmd_seek(demuxer_t *d, i32 stream_index, i64 offset, int flags);
/**
 * @brief Free the demuxer
 *
 * @param d An initialized demuxer_t object
 */
void demuxer_free(demuxer_t *d);
