#include <assert.h>

#include <libavformat/avformat.h>

#include "sve2/ffmpeg/demuxer.h"
#include "sve2/log/logging.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    raw_log_panic("usage: %s <media file>\n", argv[0]);
  }

  init_logging();
  AVFormatContext *fc = open_media(argv[1]);
  assert(fc);

  demuxer_t d;
  demuxer_stream_t streams[] = {
      (demuxer_stream_t){.index = stream_index_video(0)},
      (demuxer_stream_t){.index = stream_index_audio(0)},
  };
  demuxer_init(&d, &(demuxer_init_t){.fc = fc,
                                     .streams = streams,
                                     .num_streams = sve2_arrlen(streams),
                                     .num_buffered_packets = 8});
  nassert(streams[0].index == 0);
  nassert(streams[1].index == 1);
  sve2_sleep_for(SVE2_NS_PER_SEC);
  demuxer_free(&d);
  avformat_close_input(&fc);

  return 0;
}
