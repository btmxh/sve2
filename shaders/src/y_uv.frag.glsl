#include "common.glsl"
#include "yuv.glsl"

layout(binding = 0) uniform sampler2D y_plane;
layout(binding = 1) uniform sampler2D uv_plane;

vec4 sample_texture(vec2 tex_coords) {
    return yuv2rgb(vec4(texture(y_plane, tex_coords).r,
                        texture(uv_plane, tex_coords).rg, 1.0));
}

#include "quad.frag.glsl"
