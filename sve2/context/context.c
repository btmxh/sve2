#include "context.h"

#include <stdlib.h>

#include <GLFW/glfw3.h>
#include <glad/egl.h>
#include <log.h>

#include "sve2/gl/shader.h"

#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>

#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

struct context_t {
  context_init_t info;
  GLFWwindow *window;
  shader_manager_t sman;
  i32 frame_num;
  f32 xscale, yscale;
  void *user_ptr;
};

void glfw_error_callback(int error_code, const char *description) {
  log_warn("GLFW error %d: %s", error_code, description);
}

static void GLAD_API_PTR gl_debug_callback(GLenum source, GLenum type,
                                           GLuint id, GLenum severity,
                                           GLsizei length,
                                           const GLchar *message,
                                           const void *userParam) {
  (void)length;
  (void)userParam;
  i32 level;
  const char *src_str;
  const char *type_str;
  switch (source) {
  case GL_DEBUG_SOURCE_API:
    src_str = "API";
    break;
  case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
    src_str = "WINDOW SYSTEM";
    break;
  case GL_DEBUG_SOURCE_SHADER_COMPILER:
    src_str = "SHADER COMPILER";
    break;
  case GL_DEBUG_SOURCE_THIRD_PARTY:
    src_str = "THIRD PARTY";
    break;
  case GL_DEBUG_SOURCE_APPLICATION:
    src_str = "APPLICATION";
    break;
  default:
    src_str = "OTHER";
    break;
  }

  switch (type) {
  case GL_DEBUG_TYPE_ERROR:
    type_str = "ERROR";
    break;
  case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    type_str = "DEPRECATED_BEHAVIOR";
    break;
  case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    type_str = "UNDEFINED_BEHAVIOR";
    break;
  case GL_DEBUG_TYPE_PORTABILITY:
    type_str = "PORTABILITY";
    break;
  case GL_DEBUG_TYPE_PERFORMANCE:
    type_str = "PERFORMANCE";
    break;
  case GL_DEBUG_TYPE_MARKER:
    type_str = "MARKER";
    break;
  default:
    type_str = "OTHER";
    break;
  }

  switch (severity) {
  case GL_DEBUG_SEVERITY_NOTIFICATION:
    level = LOG_INFO;
    break;
  case GL_DEBUG_SEVERITY_LOW:
    level = LOG_DEBUG;
    break;
  case GL_DEBUG_SEVERITY_MEDIUM:
    level = LOG_WARN;
    break;
  case GL_DEBUG_SEVERITY_HIGH:
    level = LOG_ERROR;
    break;
  default:
    level = LOG_FATAL;
    break;
  }

  log_log(level, __FILE__, __LINE__, "GL: %s (source: %s, type: %s, id: %d)",
          message, src_str, type_str, (int)id);
}

static void glfw_framebuffer_callback(GLFWwindow *w, int width, int height) {
  context_t *c = context_get_from_window(w);
  c->info.width = width;
  c->info.height = height;
}

static void glfw_content_scale_callback(GLFWwindow *w, float xscale,
                                        float yscale) {
  context_t *c = context_get_from_window(w);
  c->xscale = xscale;
  c->yscale = yscale;
}

context_t *context_init(const context_init_t *info) {
  context_t *c = sve2_calloc(1, sizeof *c);
  c->info = *info;
  c->frame_num = 0;
  c->xscale = 1.0;
  c->yscale = 1.0;

  nassert(
      !glfwSetErrorCallback(glfw_error_callback) &&
      "Existing GLFW error callback is overriden. Consider setting the GLFW "
      "error callback after context initialization.");
  nassert(glfwInit() && "GLFW initialization failed");

  glfwDefaultWindowHints();
  // off-screen rendering if not in preview mode
  glfwWindowHint(GLFW_VISIBLE, info->mode == CONTEXT_MODE_PREVIEW);
  // we require EGL + OpenGL ES for VAAPI integration extensions
  glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
  glfwWindowHint(GLFW_CONTEXT_DEBUG, GLFW_TRUE);
  nassert((c->window = glfwCreateWindow(info->width, info->height,
                                        "sve2 window", NULL, NULL)));
  glfwMakeContextCurrent(c->window);
  glfwSetWindowUserPointer(c->window, c);
  nassert(gladLoadGL(glfwGetProcAddress));
  nassert(gladLoadEGL(glfwGetEGLDisplay(), glfwGetProcAddress));

  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(gl_debug_callback, NULL);

  int width, height;
  glfwGetFramebufferSize(c->window, &width, &height);
  c->info.width = width;
  c->info.height = height;

  glfwSetFramebufferSizeCallback(c->window, glfw_framebuffer_callback);
  glfwSetWindowContentScaleCallback(c->window, glfw_content_scale_callback);

  shader_manager_init(&c->sman, "shaders/out");

  return c;
}

void context_free(context_t *c) {
  shader_manager_free(&c->sman);
  free(c);
  glfwTerminate(); // free all windowing + OpenGL stuff,
                   // no need to manually free every resource
}

void context_set_should_close(context_t *c, bool should_close) {
  glfwSetWindowShouldClose(c->window, should_close);
}

bool context_get_should_close(context_t *c) {
  return glfwWindowShouldClose(c->window);
}

i64 context_get_time(context_t *c) {
  switch (c->info.mode) {
  case CONTEXT_MODE_PREVIEW:
    return threads_timer_now();
  case CONTEXT_MODE_RENDER:
    return (i64)c->frame_num * SVE2_NS_PER_SEC / c->info.fps;
  default:
    unreachable();
  }
}

void context_get_framebuffer_info(context_t *c, i32 *w, i32 *h, f32 *xscale,
                                  f32 *yscale) {
  // clang-format off
  if(w) *w = c->info.width;
  if(h) *h = c->info.height;
  if(xscale) *xscale = c->xscale;
  if(yscale) *yscale = c->yscale;
  // clang-format on
}

void context_begin_frame(context_t *c) {
  glfwPollEvents();
  log_debug("frame %" PRIi32 " started", c->frame_num);
  shader_manager_update(&c->sman);
}

void context_end_frame(context_t *c) {
  glfwSwapBuffers(c->window);
  ++c->frame_num;
}

GLuint context_default_framebuffer(context_t *c) {
  (void)c;
  return 0; // temporary
}

void context_set_user_pointer(context_t *c, void *u) { c->user_ptr = u; }

void *context_get_user_pointer(GLFWwindow *window) {
  return context_get_from_window(window)->user_ptr;
}

context_t *context_get_from_window(GLFWwindow *window) {
  return glfwGetWindowUserPointer(window);
}

shader_manager_t *context_get_shader_manager(context_t *c) {
  return &c->sman;
}

void context_set_key_callback(context_t *c, GLFWkeyfun key) {
  glfwSetKeyCallback(c->window, key);
}
