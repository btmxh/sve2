#pragma once

#include <threads.h>

#include <glad/gl.h>
#include <libavutil/audio_fifo.h>
#include <miniaudio/miniaudio.h>

#include "sve2/ffmpeg/muxer.h"
#include "sve2/gl/shader.h"
#include "sve2/utils/types.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// sve2 context provides a graphics context (OpenGL ES) and an audio context
// e.g. when in preview mode, a GLFW window will be created, and audio will be
// played by OpenAL, but in render mode, both video and audio will be "piped" to
// an output context. An OpenGL context is still provided for
// hardware-accelerated rendering
typedef enum { CONTEXT_MODE_PREVIEW, CONTEXT_MODE_RENDER } context_mode_t;

typedef struct {
  context_mode_t mode;
  i32 width, height, fps, sample_rate, num_buffered_audio_frames;
  const AVChannelLayout *ch_layout;
  enum AVSampleFormat sample_fmt;
  const char *output_path;
} context_init_t;

typedef struct {
  muxer_t muxer;
  AVFrame *hw_frame;
  AVFrame *transfer_frame;
  shader_t *color_convert_shader;
  i32 video_si, audio_si;
  i32 uv_offset_y;
  GLuint fbo, fbo_color_attachment, output_texture;
} render_context_t;

typedef struct context_t {
  context_init_t info;
  GLFWwindow *window;
  shader_manager_t sman;
  i32 frame_num;
  // num_samples is the total number of samples:
  // preview mode: total samples played since last seek
  // render mode: total samples played since last frame
  i32 num_samples;
  f32 xscale, yscale;
  void *user_ptr;
  render_context_t rctx;
  ma_device audio_device;
  mtx_t audio_fifo_mutex;
  bool audio_eof;
  AVAudioFifo *audio_fifo;
  i64 audio_timer_offset;
} context_t;

context_t *context_init(const context_init_t *info);
void context_free(context_t *c);

void context_set_should_close(context_t *c, bool should_close);
bool context_get_should_close(context_t *c);

i64 context_get_time(context_t *c);
void context_get_framebuffer_info(context_t *c, i32 *w, i32 *h, f32 *xscale,
                                  f32 *yscale);
void context_begin_frame(context_t *c);
void context_end_frame(context_t *c);
GLuint context_default_framebuffer(context_t *c);

void context_set_audio_timer(context_t *c, i64 time);
i64 context_get_audio_timer(context_t *c);
bool context_audio_full(context_t *c);
void context_submit_audio(context_t *c, AVFrame *audio_frame);
void context_submit_audio_eof(context_t *c);

void context_set_user_pointer(context_t *c, void *u);
void *context_get_user_pointer(GLFWwindow *window);
context_t *context_get_from_window(GLFWwindow *window);
shader_manager_t *context_get_shader_manager(context_t *c);

void context_set_key_callback(context_t *c, GLFWkeyfun key);
