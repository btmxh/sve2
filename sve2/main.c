#include <libavutil/pixdesc.h>

#include "sve2/context/context.h"
#include "sve2/gl/shader.h"
#include "sve2/log/logging.h"
#include "sve2/media/audio.h"
#include "sve2/media/video.h"
#include "sve2/media/video_frame.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    raw_log_panic("usage: %s <media file>\n", argv[0]);
  }

  AVChannelLayout ch_layout = AV_CHANNEL_LAYOUT_STEREO;

  context_t *c;
  char *output_path = getenv("OUTPUT_PATH");
  nassert(c = context_init(&(context_init_t){
              .mode = output_path ? CONTEXT_MODE_RENDER : CONTEXT_MODE_PREVIEW,
              .width = 1920,
              .height = 1080,
              .fps = 60,
              .output_path = output_path,
              .sample_rate = 48000,
              .sample_fmt = AV_SAMPLE_FMT_S16,
              .num_buffered_audio_frames = 4,
              .ch_layout = &ch_layout}));

  shader_t *yuv_shader = shader_new_vf(c, "quad.vert.glsl", "y_uv.frag.glsl");
  shader_t *rgb_shader =
      shader_new_vf(c, "quad.vert.glsl", "rgba_array.frag.glsl");

  video_t video;
  audio_t audio;
  nassert(video_open(c, &video, argv[1], SVE2_SI(VIDEO, 0),
                     VIDEO_FORMAT_FFMPEG_STREAM));
  nassert(audio_open(c, &audio, argv[1], SVE2_SI(AUDIO, 0),
                     AUDIO_FORMAT_FFMPEG_STREAM));

  i64 seek_time = 115 * SVE2_NS_PER_SEC;
  video_seek(&video, seek_time);
  audio_seek(&audio, seek_time);
  context_set_audio_timer(c, seek_time);

  for (i32 j = 0; !context_get_should_close(c); ++j) {
    context_begin_frame(c);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    i64 time = context_get_audio_timer(c);
    video_frame_t tex;
    if (video_get_texture(&video, time, &tex)) {
      shader_t *shader = NULL;
      if (av_pix_fmt_desc_get(tex.sw_format)->flags & AV_PIX_FMT_FLAG_RGB) {
        shader = rgb_shader;
      } else if (tex.sw_format == AV_PIX_FMT_NV12 ||
                 tex.sw_format == AV_PIX_FMT_YUV420P) {
        shader = yuv_shader;
      } else {
        log_error("unsupported pixel format: %s",
                  av_get_pix_fmt_name(tex.sw_format));
      }
      if (shader && shader_use(shader) >= 0) {
        for (i32 i = 0; i < sve2_arrlen(tex.textures); ++i) {
          if (!tex.textures[i]) {
            continue;
          }
          glActiveTexture(GL_TEXTURE0 + i);
          glBindTexture(tex.texture_array_index < 0 ? GL_TEXTURE_2D
                                                    : GL_TEXTURE_2D_ARRAY,
                        tex.textures[i]);
          log_trace("binding texture %u to bind slot %" PRIi32, tex.textures[i],
                    i);
        }
        glUniform1f(glGetUniformLocation(shader->program, "frame"),
                    tex.texture_array_index);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
    }
    u8 *samples;
    i32 num_samples;
    while (context_map_audio(c, &samples, &num_samples)) {
      audio_get_samples(&audio, &num_samples, samples);
      context_unmap_audio(c, num_samples);

      if (num_samples == 0) {
        break;
      }
    }
    context_end_frame(c);
  }

  video_close(&video);
  audio_close(&audio);
  context_free(c);

  return 0;
}
