#pragma once

#include <arena.h>
#include <glad/gles2.h>

#include "sve2/utils/filewatch.h"
#include "sve2/utils/types.h"

#define SVE2_MAX_SHADERS 4

typedef struct context_t context_t;
typedef struct shader_t shader_t;

typedef struct {
  char *shader_dir;
  filewatch_t *fw;
  shader_t *head;
} shader_manager_t;

// shaders are stored in a (doubly) linked list
// this is to add and delete shaders in O(1)
// shaders are loaded directly from a
struct shader_t {
  shader_manager_t *manager;
  struct shader_t *prev, *next;
  GLuint program;
  i32 num_shaders;
  i32 version;
  bool updated;
  GLenum shader_types[SVE2_MAX_SHADERS];
  char *shader_paths[SVE2_MAX_SHADERS];
};

// shader managers are directly managed by the context
// these functions should not be used
void shader_manager_init(shader_manager_t *sm, const char *shader_dir);
void shader_manager_update(shader_manager_t *sm);
void shader_manager_free(shader_manager_t *sm);

shader_t *shader_new(context_t *c, GLenum shader_types[],
                     const char *shader_paths[],
                     i32 num_shaders);
// since shaders are managed by the context shader manager, freeing the context
// (and therefore the shader manager) will also free this shader
void shader_free(shader_t *s);

// basically glUseProgram, but returns a numeric value indicating the "version"
// of the shader program
//
// if version == -1 (or < 0 in general) indicates that the shader program has
// not been initialized (due to missing files, syntax errors, link errors, etc.)
//
// otherwise (if version >= 0), then shader program initialization is successful
// and one can use this program to render OpenGL stuff, however, due to hot
// reloading, some state previously set for the old shader may not be available
// for the new shader. Hence, it is crucial to compare the version to know
// whether the program has been reloaded or not
//
// Usage:
// shader_t* s = shader_new(...);
// i32 last_version = -1;
// while(true) {
//    i32 version = shader_use(s);
//    if(version < 0) {
//      // error or skip rendering
//    } else if(version != last_version) {
//      // program is reloaded, store uniform that does not change
//      glUniform1i(glGetUniformLocation(s->program, "texture"), 0);
//    }
//
//    glUniform1i(glGetUniformLocation(s->program, "time"), glfwGetTime());
// }
i32 shader_use(shader_t *s);

// utility function for vertex-fragment shaders
#define shader_new_vf(c, vpath, fpath)                                         \
  shader_new(c, (GLenum[]){GL_VERTEX_SHADER, GL_FRAGMENT_SHADER},              \
             (const char *[]){vpath, fpath}, 2)
