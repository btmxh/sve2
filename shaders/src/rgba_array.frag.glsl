#include "common.glsl"

layout(binding = 0) uniform sampler2DArray rgba;
layout(location = 0) uniform float frame;

vec4 sample_texture(vec2 tex_coords) {
    return texture(rgba, vec3(tex_coords, frame));
}

#include "quad.frag.glsl"
