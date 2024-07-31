#include "common.glsl"

in vec2 tc;
out vec4 color;

void main() {
  color = sample_texture(tc);
}

