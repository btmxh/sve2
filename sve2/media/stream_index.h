#pragma once

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#include "sve2/utils/types.h"

/**
 * @brief Stream index, there are two types:
 *
 * Canonical stream index (:2 -> second stream). type is set to
 * AVMEDIA_TYPE_UNKNOWN.
 *
 * Typed stream index (v:0 -> first video stream, a:1 ->
 * second audio stream, etc.), type is set to the corresponding media type.
 */
typedef struct {
  enum AVMediaType type;
  i32 offset;
} stream_index_t;

#define SVE2_CONCAT(a, b) a##b
// shorthand to create stream index objects
#define SVE2_SI(type, offset)                                                  \
  (stream_index_t) { SVE2_CONCAT(AVMEDIA_TYPE_, type), (offset) }

char *sve2_si2str_helper(i32 bufsize, char buffer[bufsize],
                         stream_index_t index);

/**
 * @brief Convert a typed/canonical stream index to a canonical one, with
 * respect to a media file.
 *
 * @param index A stream_index_t object. It will be converted to a canonical
 * stream index if the operation succeeded
 * @param num_streams The number of streams in a media file
 * @param streams An array of AVStream in the media file
 * @return true if the stream is found and the stream index is resolved, false
 * otherwise.
 */
bool stream_index_make_canonical(stream_index_t *index, i32 num_streams,
                                 AVStream *streams[num_streams]);

// convert a stream index to a string (on the stack)
#define SVE2_SI2STR(si) sve2_si2str_helper(32, (char[32]){}, (si))
