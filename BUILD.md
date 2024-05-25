# Build instructions

Build files (`CMakeLists.txt`, `Makefile`) are not provided (and even gitignored) due to multiple reasons:
- I do not want to force everyone else to use Makefile, which is what I use to build this project. And I also do not want bloat the codebase by supporting like 10 other compilers.
- I do not want to force everyone else to use system libraries, which is how I manage dependencies on my machine.
- I do not want to force everyone to use my build configurations, which may contains things like sanitizers, static analysis, etc.
- Maintaining these build files takes effort.

In my opinion, the reposistory is just a place to store code, not build files. Those can live on other branches, forks, releases, etc.

To build this project, use your favorite compiler to build ALL `*.c` files in this reposistory. You may enable any flags or use any version of the dependencies you want.

The project requires C11 (+ some C23 features) and dependencies:
- [glfw](https://github.com/glfw/glfw)
- [glad](https://gen.glad.sh)
    - GL=4.3+, with extensions GL_EXT_EGL_image_storage
    - EGL=1.5+ with extensions EGL_EXT_image_dma_buf_import, EGL_KHR_image_base
- [OpenAL](https://www.openal.org) or [OpenAL Soft](https://github.com/kcat/openal-soft)
- [NanoVG](https://github.com/memononen/nanovg)
- [ffmpeg](https://ffmpeg.org)
- [log.c](https://github.com/innerout/log.c)
- [arena](https://github.com/tsoding/arena)

Define `SVE2_NO_NONSTD` to disable non-standard features

Currently only support Linux for the time being.

