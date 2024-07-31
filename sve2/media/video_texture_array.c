#include "video_texture_array.h"

#include <stdbit.h>

#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <stb/stb_ds.h>
#include <stb/stb_image_write.h>
#include <webp/demux.h>
#include <webp/mux_types.h>

#include "sve2/media/ffmpeg_stream.h"
#include "sve2/media/video_frame.h"
#include "sve2/utils/runtime.h"

// OpenGL texture formats and FFmpeg pixel formats have some overlaps
// this struct contains information to map from a FFmpeg pixel format to the
// corresponding OpenGL texture format.
// of course, not every FFmpeg texture format is supported, so we might still
// have to do format conversion. get_video_texture_array_best_format() is used
// to retrieve the best format (that has a corresponding OpenGL texture format)
// to convert to for a specific pixel format.
// e.g. YUV -> RGB (RGBA is a waste due to the unused alpha channel)
typedef struct {
  // the param of glTexStorage3D
  GLenum internal_format;
  // the params of glTexSubImage3D
  GLenum upload_format, upload_elem_type;
  // whether to swizzle, this allows us to swizzle GL_RED texture as a grayscale
  // texture.
  bool swizzle;
  // the swizzle mask, see below for usage
  GLint swizzle_mask[4];
} pix_fmt_mapping_t;

static const pix_fmt_mapping_t mappings[] = {
// the value can contain commas, so  we use variadic macro to preserve that
#define X(key, ...) [key] = __VA_ARGS__,
#include "video_texture_formats.inc"
#undef X
};

static const enum AVPixelFormat supported_formats[] = {
#define X(key, ...) key,
#include "video_texture_formats.inc"
#undef X
};

static void get_video_texture_array_best_format(enum AVPixelFormat *format) {
  enum AVPixelFormat src_fmt = *format;
  bool has_alpha = av_pix_fmt_desc_get(src_fmt)->flags & AV_PIX_FMT_FLAG_ALPHA;
  *format = AV_PIX_FMT_NONE;
  for (i32 i = 0; i < sve2_arrlen(supported_formats); ++i) {
    *format = av_find_best_pix_fmt_of_2(*format, supported_formats[i], src_fmt,
                                        has_alpha, NULL);
  }
}

bool video_texture_array_new(context_t *ctx, video_texture_array_t *t,
                             const char *path, stream_index_t index) {
  ffmpeg_stream_t stream;
  if (!ffmpeg_stream_open(ctx, &stream, path, index, false)) {
    return false;
  }

  enum AVPixelFormat format = stream.cdc_ctx->pix_fmt;
  if (format == AV_PIX_FMT_NONE &&
      strcmp(stream.fmt_ctx->iformat->name, "webp_pipe") == 0) {
    ffmpeg_stream_close(&stream);
    // manually load WEBP images using libwebp
    FILE *f = fopen(path, "rb");
    if (!f) {
      return false;
    }

    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = sve2_malloc(len);
    nassert(fread(content, 1, len, f) == len);
    nassert(!ferror(f));
    nassert(!fclose(f));

    WebPData data = {(const u8 *)content, len};
    WebPAnimDecoderOptions dec_options;
    WebPAnimDecoderOptionsInit(&dec_options);
    WebPAnimDecoder *decoder = WebPAnimDecoderNew(&data, &dec_options);
    WebPAnimInfo anim_info;
    WebPAnimDecoderGetInfo(decoder, &anim_info);

    t->sw_format = AV_PIX_FMT_RGBA;
    t->num_frames = anim_info.frame_count;
    t->next_frame_timestamps =
        sve2_malloc(t->num_frames * sizeof *t->next_frame_timestamps);
    i32 width = anim_info.canvas_width, height = anim_info.canvas_height;

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &t->texture);
    glTextureStorage3D(t->texture, 1, GL_RGBA8, width, height, t->num_frames);

    glTextureParameteri(t->texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(t->texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    for (i32 i = 0; WebPAnimDecoderHasMoreFrames(decoder); ++i) {
      u8 *pixels;
      int timestamp;
      WebPAnimDecoderGetNext(decoder, &pixels, &timestamp);
      t->next_frame_timestamps[i] = timestamp * 1000000; // ms to ns
      glTextureSubImage3D(t->texture, 0, 0, 0, i, width, height, 1, GL_RGBA,
                          GL_UNSIGNED_BYTE, pixels);
    }

    free(content);
    WebPAnimDecoderDelete(decoder);

    return true;
  }

  struct SwsContext *rescaler = NULL;
  get_video_texture_array_best_format(&format);
  bool rescale = format != stream.cdc_ctx->pix_fmt;

  AVFrame *in_frame, *out_frame = NULL, **out_frames = NULL;
  nassert(in_frame = av_frame_alloc());
  while (ffmpeg_stream_get_frame(&stream, in_frame)) {
    if (rescale) {
      nassert(out_frame = av_frame_alloc());
      nassert(rescaler = sws_getCachedContext(
                  rescaler, in_frame->width, in_frame->height, in_frame->format,
                  in_frame->width, in_frame->height, format, SWS_FAST_BILINEAR,
                  NULL, NULL, NULL));
      out_frame->pts = in_frame->pts;
      out_frame->duration = in_frame->duration;
      sws_scale_frame(rescaler, out_frame, in_frame);
      stbds_arrput(out_frames, out_frame);
      out_frame = NULL;
      av_frame_unref(in_frame);
    } else {
      stbds_arrput(out_frames, in_frame);
      nassert(in_frame = av_frame_alloc());
    }
  }

  t->num_frames = stbds_arrlen(out_frames);
  t->next_frame_timestamps =
      sve2_malloc(t->num_frames * sizeof *t->next_frame_timestamps);
  t->sw_format = out_frames[0]->format;
  glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &t->texture);

  pix_fmt_mapping_t mapping = mappings[out_frames[0]->format];
  glTextureStorage3D(t->texture, 1, mapping.internal_format,
                     out_frames[0]->width, out_frames[0]->height,
                     t->num_frames);
  glTextureParameteri(t->texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(t->texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  if (mapping.swizzle) {
    glTextureParameteriv(t->texture, GL_TEXTURE_SWIZZLE_RGBA,
                         mapping.swizzle_mask);
  }

  for (i32 i = 0; i < t->num_frames; ++i) {
    nassert(out_frames[i]->width == out_frames[0]->width);
    nassert(out_frames[i]->height == out_frames[0]->height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage3D(t->texture, 0, 0, 0, i, out_frames[0]->width,
                        out_frames[0]->height, 1, mapping.upload_format,
                        mapping.upload_elem_type, out_frames[i]->data[0]);
    t->next_frame_timestamps[i] = out_frames[i]->pts + out_frames[i]->duration;
  }

  av_frame_free(&in_frame);
  av_frame_free(&out_frame);
  for (i32 i = 0; i < t->num_frames; ++i) {
    av_frame_free(&out_frames[i]);
  }
  stbds_arrfree(out_frames);
  ffmpeg_stream_close(&stream);
  sws_freeContext(rescaler);

  return true;
}

void video_texture_array_free(video_texture_array_t *t) {
  glDeleteTextures(1, &t->texture);
  free(t->next_frame_timestamps);
}

// binary search is simple but implementing it is hard (due to the off-by-1
// pitfalls). here is the implementation of std::upper_bound
static i32 frame_binary_search(video_texture_array_t *t, i64 time,
                               i32 start_idx) {
  i32 count = t->num_frames - start_idx;
  while (count) {
    i32 mid = start_idx + count / 2;

    if (time >= t->next_frame_timestamps[mid]) {
      start_idx = mid + 1;
      count -= count / 2 + 1;
    } else {
      count /= 2;
    }
  }

  return start_idx;
}

bool video_texture_array_get_texture(video_texture_array_t *t, i64 time,
                                     video_frame_t *tex) {
  // temporary hack for repeat
  // time %= t->next_frame_timestamps[t->num_frames - 1];
  tex->textures[0] = t->texture;
  tex->sw_format = t->sw_format;
  tex->texture_array_index = frame_binary_search(t, time, 0);

  return tex->texture_array_index < t->num_frames;
}
