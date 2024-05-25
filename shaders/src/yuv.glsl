#pragma once

const mat4 yuv2rgb_mat = mat4(
    vec4(  1.1644,  1.1644,  1.1644,  0.0000 ),
    vec4(  0.0000, -0.2132,  2.1124,  0.0000 ),
    vec4(  1.7927, -0.5329,  0.0000,  0.0000 ),
    vec4( -0.9729,  0.3015, -1.1334,  1.0000 ));

vec4 yuv2rgb(vec4 yuva) {
  return yuv2rgb_mat * yuva;
}
