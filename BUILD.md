= Build instructions

Requires C11 and dependencies:
- [glfw](https://github.com/glfw/glfw)
- [glad](https://gen.glad.sh)
    - GLES 3.2+, with extensions GL_OES_EGL_image, GL_OES_EGL_image_external
    - EGL=1.5+ with extensions EGL_EXT_image_dma_buf_import, EGL_KHR_image_base, EGL_MESA_image_dma_buf_export
- [OpenAL](https://www.openal.org) or [OpenAL Soft](https://github.com/kcat/openal-soft)
- [NanoVG](https://github.com/memononen/nanovg)
- [ffmpeg](https://ffmpeg.org)

Define `SVE2_NO_NONSTD` to disable non-standard features

Currently only support Linux for the time being.

