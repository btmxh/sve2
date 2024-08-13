#include "shader.h"

#include <stdlib.h>

#include <arena.h>
#include <log.h>

#include "sve2/context/context.h"
#include "sve2/utils/asprintf.h"
#include "sve2/utils/filewatch.h"
#include "sve2/utils/runtime.h"

#ifndef SVE2_NO_NONSTD // TODO: use the right preprocessor macros
#include <sys/stat.h>
static bool same_file(const char *p1, const char *p2) {
  struct stat s1, s2;
  if (stat(p1, &s1) < 0 || stat(p2, &s2) < 0) {
    return false;
  }
  // two files are the same if they have the same inode and belong on the same
  // device
  return s1.st_ino == s2.st_ino && s1.st_dev == s2.st_dev;
}
#else
static bool same_file(const char *p1, const char *p2) {
  return strcmp(p1, p2) == 0;
}
#endif

void shader_manager_init(shader_manager_t *sm, const char *shader_dir) {
  sm->head = NULL;
  sm->shader_dir = sve2_strdup(shader_dir);
  nassert(sm->fw = filewatch_init(shader_dir));
}

void shader_manager_update(shader_manager_t *sm) {
  filewatch_event event;
  while (filewatch_poll(sm->fw, &event)) {
    for (shader_t *s = sm->head; s; s = s->next) {
      for (i32 i = 0; i < s->num_shaders; ++i) {
        if (same_file(s->shader_paths[i], event.name)) {
          s->updated = true;
        }
      }
    }
  }
}

void shader_manager_free(shader_manager_t *sm) {
  filewatch_free(sm->fw);
  free(sm->shader_dir);
  while (sm->head) {
    shader_free(sm->head);
  }
}

shader_t *shader_new(context_t *c, GLenum shader_types[],
                     const char *shader_paths[], i32 num_shaders) {
  shader_manager_t *sm = context_get_shader_manager(c);
  shader_t *shader = sve2_malloc(sizeof *shader);
  shader->manager = sm;
  shader->program = 0;
  shader->num_shaders = num_shaders;
  shader->version = -1;
  shader->updated = true;
  for (i32 i = 0; i < num_shaders; ++i) {
    shader->shader_types[i] = shader_types[i];
    shader->shader_paths[i] =
        sve2_asprintf("%s/%s", sm->shader_dir, shader_paths[i]);
  }

  // this shader become the new head of the shader linked list
  shader->prev = NULL;
  shader->next = sm->head;
  if (sm->head) {
    sm->head->prev = shader;
  }
  sm->head = shader;
  return shader;
}

void shader_free(shader_t *s) {
  // clang-format off
  if(s->prev) s->prev->next = s->next;
  if(s->next) s->next->prev = s->prev;
  if(s->manager->head == s) s->manager->head = s->next;
  // clang-format on

  for (i32 i = 0; i < s->num_shaders; ++i) {
    free(s->shader_paths[i]);
  }

  if (s->program) {
    glDeleteProgram(s->program);
  }

  free(s);
}

static char *read_file(Arena *a, const char *path, i32 *len) {
  len = len ? len : &(i32){0};
  FILE *f = fopen(path, "rb");
  if (!f) {
    log_warn("shader file not found: %s", path);
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  *len = (i32)ftell(f);
  fseek(f, 0, SEEK_SET);

  char *content = arena_alloc(a, (size_t)*len);
  nassert(content);

  nassert(fread(content, 1, (size_t)*len, f) == (size_t)*len);
  nassert(!ferror(f));
  nassert(!fclose(f));
  return content;
}

GLuint init_shader(shader_t *s) {
  Arena a = {0};
  GLuint program = glCreateProgram();
  bool init = true;
  nassert(program);
  for (i32 i = 0; i < s->num_shaders; ++i) {
    GLuint shader = glCreateShader(s->shader_types[i]);
    nassert(shader);

    i32 len;
    char *content = read_file(&a, s->shader_paths[i], &len);
    if (!content) {
      init = false;
      break;
    }

    glShaderSource(shader, 1, (const char *[]){content}, (GLint[]){len});
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      char info_log[256];
      glGetShaderInfoLog(shader, sizeof info_log, NULL, info_log);
      log_warn("shader compilation error at file '%s': %s", s->shader_paths[i],
               info_log);
      init = false;
      break;
    }

    glAttachShader(program, shader);
  }

  if (init) {
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
      char info_log[256];
      glGetProgramInfoLog(program, sizeof info_log, NULL, info_log);
      log_warn("program linker error: %s", info_log);
      init = false;
    }
  }

  GLint num_shaders;
  glGetProgramiv(program, GL_ATTACHED_SHADERS, &num_shaders);
  GLuint shaders[SVE2_MAX_SHADERS];
  glGetAttachedShaders(program, num_shaders, NULL, shaders);
  for (i32 i = 0; i < num_shaders; ++i) {
    glDetachShader(program, shaders[i]);
    glDeleteShader(shaders[i]);
  }

  arena_free(&a);
  if (init) {
    return program;
  }

  glDeleteProgram(program);
  return 0;
}

i32 shader_use(shader_t *s) {
  if (s->updated) {
    GLuint new_program = init_shader(s);
    if (new_program) {
      if (s->program) {
        glDeleteProgram(s->program);
      }

      s->program = new_program;
      ++s->version;
    }

    s->updated = false;
  }

  if (s->program) {
    glUseProgram(s->program);
  }
  return s->version;
}

GLuint shader_get_program(shader_t *s) { return s->program; }
