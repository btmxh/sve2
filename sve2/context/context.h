#pragma once

#include <threads.h>

#include <glad/gl.h>
#include <glad/egl.h>
#include <libavutil/audio_fifo.h>
#include <miniaudio/miniaudio.h>

#include "sve2/gl/shader.h"
#include "sve2/media/output_ctx.h"
#include "sve2/utils/types.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// sve2 context provides a graphics context (OpenGL ES) and an audio context
// e.g. when in preview mode, a GLFW window will be created, and audio will be
// played by miniaudio, but in render mode, both video and audio will be "piped"
// to a muxer. An OpenGL context is still provided for hardware-accelerated
// rendering.
typedef enum { CONTEXT_MODE_PREVIEW, CONTEXT_MODE_RENDER } context_mode_t;

typedef struct {
  /**
   * @brief Context mode, see the docs of context_mode_t for more details.
   */
  context_mode_t mode;
  /**
   * @brief Self-explanatory
   */
  i32 width, height, fps, sample_rate;
  /**
   * @brief Number of buffered frames for audio preview. This is ignored in
   * render mode.
   */
  i32 num_buffered_audio_frames;
  /**
   * @brief Pointer to the channel layout. Currently supported layout are mono
   * (AV_CHANNEL_LAYOUT_MONO) and stereo (AV_CHANNEL_LAYOUT_STEREO). Other
   * layout may or may not work.
   */
  const AVChannelLayout *ch_layout;
  /**
   * @brief Audio sample format. Must be a non-planar format.
   */
  enum AVSampleFormat sample_fmt;
  /**
   * @brief Path to the media file outputted in render mode. Ignored in preview
   * mode.
   */
  const char *output_path;
} context_init_t;

/**
 * @brief Preview context, only defined in preview mode
 */
typedef struct {
  /**
   * @brief miniaudio audio device
   */
  ma_device audio_device;
  /**
   * @brief Mutex for audio_fifo
   */
  mtx_t audio_fifo_mutex;
  /**
   * @brief Internal buffer for audio playback
   */
  AVAudioFifo *audio_fifo;
  /**
   * @brief Internal staging buffer returned by context_map_audio()
   */
  void *audio_staging_buffer;
} preview_context_t;

/**
 * @brief Render context, only defined in render mode
 */
typedef struct {
  output_ctx_t output_ctx;
  /**
   * @brief Color conversion shader (from RGB to YUV) for encoding.
   */
  shader_t *color_convert_shader;
  /**
   * @brief OpenGL objects for capturing rendering output.
   */
  GLuint fbo, fbo_color_attachment, output_texture;
  /**
   * @brief Audio mapping frame
   */
  AVFrame *audio_mapping_frame;
  AVFrame *output_video_texture_prime;
  EGLImage output_texture_image;
  i32 offset_uv_y;
} render_context_t;

typedef struct context_t {
  /**
   * @brief Context initialization info, copied from the struct specified in
   * context_init()
   */
  context_init_t info;
  /**
   * @brief GLFW window (wrapping both the native window and the GL context)
   */
  GLFWwindow *window;
  /**
   * @brief Global shader manager, managing all context GL shaders
   */
  shader_manager_t sman;
  /**
   * @brief Frame number counter, increased by 1 in every call to
   * context_begin_frame()
   */
  i32 frame_num;
  /**
   * @brief Offset of audio timer. This is used to implement seeking for this
   * timer.
   */
  i64 audio_timer_offset;
  /**
   * @brief Number of samples since last seek of audio timer. This is used to
   * implement seeking for this timer.
   */
  i32 num_samples_from_last_seek;
  /**
   * @brief Total number of samples played.
   */
  i32 num_total_samples;
  /**
   * @brief Total number of samples played in this frame.
   */
  i32 num_frame_samples;
  /**
   * @brief Content scaling. This is used to support HiDPI displays (not like
   * it's any useful for this type of application though).
   */
  f32 xscale, yscale;
  /**
   * @brief User pointer. Set/get using
   * context_set_user_pointer/context_get_user_pointer. This is used to retrieve
   * contexts in GLFW event callbacks.
   */
  void *user_ptr;

  /**
   * @brief Render-mode-specific context
   */
  render_context_t rctx;
  /**
   * @brief Preview-mode-specific context
   */
  preview_context_t pctx;

  /**
   * @brief Temporary AVFrames
   */
  AVFrame *temp_frames[2];

  /**
   * @brief Temporary AVPacket
   */
  AVPacket *temp_packet;
} context_t;

/**
 * @brief Initialize the context with parameters specified in info struct.
 * Using two contexts at the same time is undefined behavior. To use another
 * context, one must free the existing context (using context_free()). This
 * function can only be called on the main thread.
 *
 * @param info Context creation parameters, see the docs for context_init_t
 * for more details
 * @return Pointer to the heap-allocated context. The performance implication of
 * heap-allocation here is negligible since context initialization is not the
 * primary bottleneck (as it is usually done at most once every invocation).
 */
context_t *context_init(const context_init_t *info);

/**
 * @brief Free an initialized context. This frees up every resources owned by
 * the context.
 *
 * @param c The existing context previously initialized by context_init()
 */
void context_free(context_t *c);

/**
 * @brief Set whether the context should be closed (freed) or not. This does not
 * free the context (as it must be manually done via context_free).
 *
 * @param c The context
 * @param should_close A boolean indicating whether the context should be closed
 * or not.
 */
void context_set_should_close(context_t *c, bool should_close);
/**
 * @brief Get whether the context should be closed (freed) or not. This flag can
 * be set by an external source (e.g. user closing the preview window) or set by
 * context_set_should_close.
 *
 * This is meant to be used as following:
 * - First the user initialize a context c.
 * - Then, loops until context_get_should_close(c) is true, and free the context
 * after the loop finishes
 * - In the loop, render media and handle user events. If something happened and
 * the application is meant to exit after the current loop iteration, use
 * context_set_should_close to set the flag to true.
 *
 * @param c The context
 * @return A boolean indicating whether the context should be closed or not
 */
bool context_get_should_close(context_t *c);

/**
 * @brief Get information related to the main framebuffer for rendering. Pass a
 * pointer (or NULL if not interested in the information) as the function
 * arguments to retrieve the necessary parameters.
 *
 * @param c The context
 * @param w Framebuffer width
 * @param h Framebuffer height
 * @param xscale Horizontal content scale
 * @param yscale Vertical content scale
 */
void context_get_framebuffer_info(context_t *c, i32 *w, i32 *h, f32 *xscale,
                                  f32 *yscale);
/**
 * @brief Begin rendering (video and audio) for a new frame. To end rendering,
 * call context_end_frame(). Invocations to the two functions must match.
 *
 * @param c The context
 */
void context_begin_frame(context_t *c);
/**
 * @brief End rendering (video and audio) for a frame, started by
 * context_begin_frame(). Invocations to the two functions must match.
 *
 * @param c The context
 */
void context_end_frame(context_t *c);

/**
 * @brief Get the main (default) framebuffer for rendering. This is the 0
 * framebuffer in preview mode, and an internal framebuffer in rendering mode.
 * Graphics rendered to this framebuffer will be shown on the preview window
 * (preview mode) or encoded to the output media (render mode).
 *
 * @param c The context
 * @return The OpenGL handle of the main framebuffer
 */
GLuint context_default_framebuffer(context_t *c);

/**
 * @brief Set the context audio timer to time. This is used to seek the global
 * timer.
 *
 * @param c The context
 * @param time The new timer value, measured in nanoseconds
 */
void context_set_audio_timer(context_t *c, i64 time);

/**
 * @brief Get the current audio timer. This serves as the global timer, and is
 * based on the number of samples played. Video is expected to be synchronized
 * with this timer.
 *
 * @param c The context
 * @return The value of the audio timer in nanoseconds
 */
i64 context_get_audio_timer(context_t *c);

/**
 * @brief Begin transferring audio samples to audio device. At most *nb_samples
 * samples (per channel) can be written to *staging_buffer.
 *
 * @param c The context
 * @param staging_buffer Pointer to return the address of the audio staging
 * buffer. This must not be NULL.
 * @param nb_samples Pointer to return the maximum number of writable samples
 * (per channel) in *staging_buffer.
 * @return Whether the map operation succeeded. The operation fails when the
 * audio buffer is full.
 */
bool context_map_audio(context_t *c, u8 *staging_buffer[static 1],
                       i32 nb_samples[static 1]);
/**
 * @brief Submit audio samples to audio device. Samples are written to the
 * staging buffer returned by context_map_audio. This function could only be
 * called when the context_map_audio operation succeeded.
 *
 * @param c The context
 * @param nb_samples Number of samples (per channel) written to the staging
 * buffer. This must be at most the returned number of samples returned from
 * context_map_audio.
 */
void context_unmap_audio(context_t *c, i32 nb_samples);

/**
 * @brief Set user pointer to context
 *
 * @param c The context
 * @param u The user pointer
 */
void context_set_user_pointer(context_t *c, void *u);
/**
 * @brief Get user pointer of context owning window
 *
 * @param window GLFW window owned by the current context
 */
void *context_get_user_pointer(GLFWwindow *window);
/**
 * @brief Get the context owning a specific window
 *
 * @param window A GLFW window owned by a context
 * @return The context owning window
 */
context_t *context_get_from_window(GLFWwindow *window);
/**
 * @brief Get the shader manager of a context
 *
 * @param c The context
 * @return The shader manager of the context c.
 */
shader_manager_t *context_get_shader_manager(context_t *c);

/**
 * @brief Set GLFW key callback to window owned by context c
 *
 * @param c The context
 * @param key New GLFW key callback of the window
 */
void context_set_key_callback(context_t *c, GLFWkeyfun key);
