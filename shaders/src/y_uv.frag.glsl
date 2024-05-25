#include "common.glsl"
#include "yuv.glsl"

in vec2 tc;
out vec4 color;

layout(binding = 0) uniform sampler2D y_plane;
layout(binding = 1) uniform sampler2D uv_plane;

void main() {
  vec4 yuva = vec4(texture(y_plane, tc).r, texture(uv_plane, tc).rg, 1.0);
  color = yuv2rgb(yuva);
}

