#include "sve2/context/context.h"
#include "sve2/ffmpeg/media.h"
#include "sve2/gl/shader.h"
#include "sve2/log/logging.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    raw_log_panic("usage: %s <media file>\n", argv[0]);
  }

  AVChannelLayout ch_layout = AV_CHANNEL_LAYOUT_STEREO;

  context_t *c;
  nassert(c = context_init(&(context_init_t){.mode = CONTEXT_MODE_PREVIEW,
                                             .width = 1920,
                                             .height = 1080,
                                             .fps = 75,
                                             .output_path = "output.mkv",
                                             .sample_rate = 48000,
                                             .sample_fmt = AV_SAMPLE_FMT_S16,
                                             .ch_layout = &ch_layout}));

  shader_t *shader = shader_new_vf(c, "y_uv.vert.glsl", "y_uv.frag.glsl");

  media_t media;
  nassert(media_open(&media, c, argv[1]));
  media_seek(&media, 10 * SVE2_NS_PER_SEC);

  hw_texture_t texture;
  for (i32 j = 0;
       j < 100 && media_map_video_texture(&media, &texture) == DECODE_SUCCESS;
       ++j) {
    context_begin_frame(c);

    if (shader_use(shader) >= 0) {
      for (i32 i = 0; i < sve2_arrlen(texture.textures); ++i) {
        if (!texture.textures[i]) {
          continue;
        }
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, texture.textures[i]);
        log_trace("binding texture %u to bind slot %" PRIi32,
                  texture.textures[i], i);
      }

      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      hw_texmap_unmap(&texture, true);
    }

    media_unmap_video_texture(&media, &texture);
    context_end_frame(c);
  }

  AVFrame *audio_frame = av_frame_alloc();
  nassert(audio_frame);
  for (i32 i = 0;
       i < 500 && media_get_audio_frame(&media, audio_frame) == DECODE_SUCCESS;
       ++i) {
    context_submit_audio(c, audio_frame);
  }

  av_frame_free(&audio_frame);

  media_close(&media);
  context_free(c);

  return 0;
}
