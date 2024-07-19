#include "sve2/context/context.h"
#include "sve2/ffmpeg/media_stream.h"
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
                                             .fps = 60,
                                             .output_path = "output.mkv",
                                             .sample_rate = 48000,
                                             .sample_fmt = AV_SAMPLE_FMT_S16,
                                             .num_buffered_audio_frames = 4,
                                             .ch_layout = &ch_layout}));

  shader_t *shader = shader_new_vf(c, "y_uv.vert.glsl", "y_uv.frag.glsl");

  media_stream_t video, audio;
  nassert(media_stream_open(&video, c, argv[1], stream_index_video(0)));
  nassert(media_stream_open(&audio, c, argv[1], stream_index_audio(0)));

  i64 seek_time = 115 * SVE2_NS_PER_SEC;
  media_stream_seek(&video, seek_time);
  media_stream_seek(&audio, seek_time);

  for (i32 j = 0; !context_get_should_close(c) && !media_stream_eof(&video) &&
                  !media_stream_eof(&audio);
       ++j) {
    context_begin_frame(c);
    i64 time = context_get_audio_timer(c) + seek_time;
    {
      hw_texture_t texture;
      decode_result_t result = media_get_video_texture(&video, &texture, time);
      if (result == DECODE_SUCCESS) {
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
        }
      } else if (result == DECODE_ERROR) {
        log_error("error decoding video");
      }
    }
    {
      AVFrame *audio_frame;
      decode_result_t result;
      while (!context_audio_full(c) &&
             (result = media_get_audio_frame(&audio, &audio_frame, time)) !=
                 DECODE_EOF) {
        nassert(result != DECODE_ERROR && result != DECODE_TIMEOUT);
        context_submit_audio(c, audio_frame);
      }
    }
    context_end_frame(c);
  }

  media_stream_close(&video);
  media_stream_close(&audio);
  context_free(c);

  return 0;
}
