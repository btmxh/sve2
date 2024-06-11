#include "common.glsl"
#include "yuv.glsl"

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba32f, binding = 0) readonly uniform image2D rgba_output;
layout(r8, binding = 1) writeonly uniform image2D nv12;

void main() {
  ivec2 img_size = imageSize(rgba_output);

  // we process 4 pixels at once (a 2x2 region)
  ivec2 pos = ivec2(gl_GlobalInvocationID.xy) * 2;

  vec4 c00 = rgb2yuv(imageLoad(rgba_output, pos + ivec2(0, 0)));
  vec4 c01 = rgb2yuv(imageLoad(rgba_output, pos + ivec2(0, 1)));
  vec4 c10 = rgb2yuv(imageLoad(rgba_output, pos + ivec2(1, 0)));
  vec4 c11 = rgb2yuv(imageLoad(rgba_output, pos + ivec2(1, 1)));

  // 4 stores to the luma image
  imageStore(nv12, pos + ivec2(0, 0), c00.rrrr);
  imageStore(nv12, pos + ivec2(0, 1), c01.rrrr);
  imageStore(nv12, pos + ivec2(1, 0), c10.rrrr);
  imageStore(nv12, pos + ivec2(1, 1), c11.rrrr);

  // 1 store to the chroma image
  int height = img_size.y * 2 / 3;
  imageStore(nv12, pos + ivec2(0, height), ((c00 + c01 + c10 + c11) * 0.25).rrrr);
  imageStore(nv12, pos + ivec2(1, height), ((c00 + c01 + c10 + c11) * 0.25).gggg);
}

