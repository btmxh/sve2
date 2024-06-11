#pragma once

const mat4 yuv2rgb_mat = mat4(
    vec4(  1.1644,  1.1644,  1.1644,  0.0000 ),
    vec4(  0.0000, -0.2132,  2.1124,  0.0000 ),
    vec4(  1.7927, -0.5329,  0.0000,  0.0000 ),
    vec4( -0.9729,  0.3015, -1.1334,  1.0000 ));

const mat4 rgb2yuv_mat = mat4(
              0.257,  0.439, -0.148, 0.0,
              0.504, -0.368, -0.291, 0.0,
              0.098, -0.071,  0.439, 0.0,
              0.0625, 0.500,  0.500, 1.0);

vec4 yuv2rgb(vec4 yuva) {
  return yuv2rgb_mat * yuva;
}

vec4 rgb2yuv(vec4 rgba) {
  return rgb2yuv_mat * rgba;
}
