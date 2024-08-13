#include "video.h"

#include "sve2/utils/runtime.h"

bool video_open(context_t *ctx, video_t *v, const char *path,
                stream_index_t index, video_format_t format) {
  switch (v->format = format) {
  case VIDEO_FORMAT_FFMPEG_STREAM:
    return ffmpeg_video_stream_open(ctx, &v->ffmpeg, path, index, true);
  case VIDEO_FORMAT_TEXTURE_ARRAY:
    return video_texture_array_new(ctx, &v->tex_array, path, index);
  }

  return false;
}

void video_close(video_t *v) {
  switch (v->format) {
  case VIDEO_FORMAT_FFMPEG_STREAM:
    ffmpeg_video_stream_close(&v->ffmpeg);
    break;
  case VIDEO_FORMAT_TEXTURE_ARRAY:
    video_texture_array_free(&v->tex_array);
    break;
  }
}

void video_seek(video_t *v, i64 time) {
  if (v->format == VIDEO_FORMAT_FFMPEG_STREAM) {
    ffmpeg_video_stream_seek(&v->ffmpeg, time);
  }
}

bool video_get_texture(video_t *v, i64 time, video_frame_t *tex) {
  switch (v->format) {
  case VIDEO_FORMAT_FFMPEG_STREAM:
    return ffmpeg_video_stream_get_texture(&v->ffmpeg, time, tex);
  case VIDEO_FORMAT_TEXTURE_ARRAY:
    return video_texture_array_get_texture(&v->tex_array, time, tex);
  }

  return false;
}

video_t *video_alloc() { return sve2_malloc(sizeof(video_t)); }
