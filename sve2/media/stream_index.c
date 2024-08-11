#include "stream_index.h"

#include <stdio.h>

#include <libavutil/avutil.h>

#include "sve2/utils/runtime.h"

char *sve2_si2str_helper(i32 bufsize, char buffer[bufsize], stream_index_t index) {
  const char *type_str = av_get_media_type_string(index.type);
  nassert(snprintf(buffer, bufsize, "%.*s:%" PRIi32, type_str ? 1 : 0,
                   type_str ? type_str : "", index.offset) <= bufsize);
  return buffer;
}

bool stream_index_make_canonical(stream_index_t *index, i32 num_streams,
                                AVStream *streams[num_streams]) {
  if (index->type == AVMEDIA_TYPE_UNKNOWN) {
    return index->offset < num_streams;
  }

  i32 rel_counter = 0;
  for (i32 i = 0; i < num_streams; ++i) {
    if (streams[i]->codecpar->codec_type == index->type) {
      if (rel_counter++ == index->offset) {
        index->type = AVMEDIA_TYPE_UNKNOWN;
        index->offset = i;
        return true;
      }
    }
  }

  return false;
}
