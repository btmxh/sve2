#include "common.glsl"
#include "yuv.glsl"

layout(local_size_x = 1, local_size_y = 1) in;

layout(rgba32f, binding = 0) readonly uniform image2D rgba_output;
layout(r8, binding = 1) writeonly uniform image2D nv12;

layout(location = 0) uniform int uv_offset_y;
layout(location = 1) uniform bool flip_vertical;

vec4 sample_texture(ivec2 tex_coords) {
  return rgb2yuv(imageLoad(rgba_output, tex_coords));
}

void main() {
  // we process 4 pixels at once (a 2x2 region)
  ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
  const vec4 white = vec4(1.0);

  ivec2 y_pos = pos * 2;
  vec4 c00 = sample_texture(y_pos + ivec2(0, 0));
  vec4 c01 = sample_texture(y_pos + ivec2(0, 1));
  vec4 c10 = sample_texture(y_pos + ivec2(1, 0));
  vec4 c11 = sample_texture(y_pos + ivec2(1, 1));

  ivec2 dir = ivec2(1, 1);
  if(flip_vertical) {
    int height = imageSize(rgba_output).y;
    y_pos.y = height - 1 - y_pos.y;
    pos.y = height / 2 - 1 - pos.y;
    dir.y = -dir.y;
  }

  // 4 stores to the luma image
  imageStore(nv12, y_pos + ivec2(0, 0) * dir, c00.rrrr);
  imageStore(nv12, y_pos + ivec2(0, 1) * dir, c01.rrrr);
  imageStore(nv12, y_pos + ivec2(1, 0) * dir, c10.rrrr);
  imageStore(nv12, y_pos + ivec2(1, 1) * dir, c11.rrrr);
  
  // 1 store to the chroma image
  imageStore(nv12, pos * ivec2(2, 1) + ivec2(0, uv_offset_y), ((c00 + c01 + c10 + c11) * 0.25).bbbb);
  imageStore(nv12, pos * ivec2(2, 1) + ivec2(1, uv_offset_y), ((c00 + c01 + c10 + c11) * 0.25).gggg);
}

