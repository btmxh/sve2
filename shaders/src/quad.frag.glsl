#include "common.glsl"

vec4 sample_texture(vec2 tex_coords);

in vec2 tc;
out vec4 color;

void main() {
  color = sample_texture(tc);
}

