#pragma once

#include <glad/egl.h>
#include <glad/gl.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>

#include "sve2/utils/types.h"

/**
 * @brief Hardware texture type
 */
typedef struct {
  /**
   * @brief Hardware texture format
   *
   * If format is AV_PIX_FMT_NONE, then the texture is currently blank (in the
   * null state). Unmapping a texture always reset it to this state. This null
   * state is useful for resource cleanup (much like how av_freep reset the
   * pointer to NULL after freeing it).
   *
   * Otherwise, format is the software format of the texture. For example, if
   * format is AV_PIX_FMT_NV12, then the texture contains three OpenGL textures
   * (for the Y, U and V planes). One could then render these textures to the
   * screen.
   */
  enum AVPixelFormat format;
  /**
   * @brief OpenGL texture planes
   *
   * In the case of multi-planar texture format, there could be multiple OpenGL
   * textures corresponding to a single hardware texture frame. One could freely
   * use these textures to render/process data in a traditional OpenGL manner
   * (binding to shader, etc.).
   *
   * The precise format of the textures are inferred from format. There are APIs
   * in FFmpeg that allows one to access generic information regarding the
   * texture formats, but in most cases one would need to write a different
   * shader/subroutine for every supported format.
   *
   * It could be possible that there are less than AV_DRM_MAX_PLANES textures.
   * Then, every unused texture slot in the textures array will be set to 0.
   */
  GLuint textures[AV_DRM_MAX_PLANES];
  /**
   * @brief Backing EGLImage for the OpenGL textures
   *
   * It could be possible that there are less than AV_DRM_MAX_PLANES images.
   * Then, every unused image slot in the images array will be set to
   * EGL_NO_IMAGE.
   *
   * These are only used for cleanup.
   */
  EGLImage images[AV_DRM_MAX_PLANES];
  /**
   * @brief VAAPI file descriptors for the hardware texture frame
   *
   * It could be possible that there are less than AV_DRM_MAX_PLANES file
   * descriptors. Then, every unused descriptor slot in the vaapi_fds array will
   * be set to -1.
   *
   * These are only used for cleanup.
   */
  int vaapi_fds[AV_DRM_MAX_PLANES];
} hw_texture_t;

/**
 * @brief Create a hw_texture_t object from OpenGL textures. This could be used
 * in conjunction with hw_texmap_from_gl to map these textures for
 * hardware-accelerated video encoding.
 *
 * @param texture Destination hw_texture_t object
 * @param num_planes Number of texture planes
 * @param textures An array of OpenGL texture handles, representing the planes
 * of the frame texture.
 */
hw_texture_t hw_texture_from_gl(enum AVPixelFormat format, i32 num_planes,
                                GLuint textures[num_planes]);
/**
 * @brief Map hardware-accelerated video frame to hw_texture_t, which could be
 * used as OpenGL textures for further processing.
 *
 * @param src Original hardware-accelerated video AVFrame
 * @param prime Temporary frame (internally used to store DRM PRIME data)
 * @param texture Destination hw_texture_t object
 * @param sw_format Software format of the frame texture.
 */
void hw_texmap_to_gl(const AVFrame *src, AVFrame *prime, hw_texture_t *texture,
                     enum AVPixelFormat sw_format);
/**
 * @brief Map a hw_texture_t object to a hardware-accelerated video frame, which
 * could be submitted to the encoding context for hardware-accelerated video
 * encoding.
 *
 * @param texture The hw_texture_t
 * @param prime Temporary frame (internally used to store DRM PRIME data)
 * @param dst Mapped hardware-accelerated video AVFrame
 */
void hw_texmap_from_gl(hw_texture_t *texture, AVFrame *prime, AVFrame *dst);
/**
 * @brief Unmap a hw_texture_t from memory. This will reset texture to the null
 * state (format == AV_PIX_FMT_NONE). Unmapping a null texture is a no-op.
 *
 * @param texture A valid (null or initialized) hw_texture_t object.
 * @param unmap_gl_textures Whether to delete OpenGL textures (using
 * glDeleteTextures) or not. This is useful if one wants to reuse the OpenGL
 * textures for further processing.
 */
void hw_texmap_unmap(hw_texture_t *texture, bool unmap_gl_textures);
/**
 * @brief Get texture dimension alignment of a hw_texture_t.
 *
 * In VAAPI, a hardware-accelerated must only be backed by one DRM PRIME file
 * descriptor. However, creating multiple textures for multiple planes violates
 * this requirement, thus forcing one to use one singular texture to store all
 * the texture planes. For example, in NV12, one could make a texture like this
 * (like a texture atlas in games):
 *
 *  ______________________
 * |                     |
 * |                     |
 * |          Y          |
 * |                     |
 * -----------------------
 * |          UV         | (interleaved U/V plane)
 * |                     |
 * -----------------------
 *
 * However, the borders needs to be aligned, or else it would yield undesirable
 * results.
 *
 * To understand how alignment works for the border of the texture, one can
 * think of the texture as a 2D struct/union. In X dimension, we have:
 *
 * union TexX {
 *   Y y; // Y at the top spans all the texture
 *   UV uv;
 * };
 *
 * For TexX, the alignment does not matter since the offset of both structs are
 * 0, regardless of any alignment.
 *
 * In Y dimension, we have:
 *
 * struct TexY {
 *   Y y;
 *   UV uv;
 * };
 *
 * Here, offsetof(TexY, y) is 0, but offsetof(TexY, uv) depends on alignof(Y)
 * (and sizeof(Y) of course).
 *
 * Hence, if we want to avoid "undesirable results", we must align the textures
 * properly, i.e. we must know the alignment of the "types" Y, U and V. Here,
 * the function assumed that every type has the same alignment (128x64 on Intel
 * CPUs), and will align (round up the input to the nearest multiple of 128/64)
 * the input values.
 *
 * @param width Pointer to the X align value, or NULL
 * @param height Pointer to the Y align value, or NULL
 */
void hw_align_size(i32 *width, i32 *height);
