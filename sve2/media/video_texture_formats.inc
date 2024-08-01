// see X macro: https://en.wikipedia.org/wiki/X_macro
X(AV_PIX_FMT_RGB24, {
                        .internal_format = GL_RGB8,
                        .upload_format = GL_RGB,
                        .upload_elem_type = GL_UNSIGNED_BYTE,
                    })
X(AV_PIX_FMT_BGR24, {
                        .internal_format = GL_RGB8,
                        .upload_format = GL_BGR,
                        .upload_elem_type = GL_UNSIGNED_BYTE,
                    })
X(AV_PIX_FMT_GRAY8, {
                        .internal_format = GL_RED,
                        .upload_format = GL_BGR,
                        .upload_elem_type = GL_UNSIGNED_BYTE,
                        .swizzle = true,
                        .swizzle_mask = {GL_RED, GL_RED, GL_RED, GL_ONE},
                    })
X(AV_PIX_FMT_YA8, {
                      .internal_format = GL_RG8,
                      .upload_format = GL_RG,
                      .upload_elem_type = GL_UNSIGNED_BYTE,
                      .swizzle = true,
                      .swizzle_mask = {GL_RED, GL_RED, GL_RED, GL_GREEN},
                  })
X(AV_PIX_FMT_RGBA, {
                       .internal_format = GL_RGBA8,
                       .upload_format = GL_RGBA,
                       .upload_elem_type = GL_UNSIGNED_BYTE,
                   })
#ifdef __STDC_ENDIAN_LITTLE__
X(AV_PIX_FMT_GRAYF32LE, {
                            .internal_format = GL_R32F,
                            .upload_format = GL_RED,
                            .upload_elem_type = GL_FLOAT,
                            .swizzle_mask = {GL_RED, GL_RED, GL_RED, GL_ONE},
                        })
X(AV_PIX_FMT_RGBF32LE, {
                           .internal_format = GL_RGB32F,
                           .upload_format = GL_RGB,
                           .upload_elem_type = GL_FLOAT,
                       })
X(AV_PIX_FMT_RGBAF32LE, {
                            .internal_format = GL_RGBA32F,
                            .upload_format = GL_RGBA,
                            .upload_elem_type = GL_FLOAT,
                        })
#endif
#ifdef __STDC_ENDIAN_BIG__
X(AV_PIX_FMT_GRAYF32BE, {
                            .internal_format = GL_R32F,
                            .upload_format = GL_RED,
                            .upload_elem_type = GL_FLOAT,
                            .swizzle_mask = {GL_RED, GL_RED, GL_RED, GL_ONE},
                        })
X(AV_PIX_FMT_RGBF32BE, {
                           .internal_format = GL_RGB32F,
                           .upload_format = GL_RGB,
                           .upload_elem_type = GL_FLOAT,
                       })
X(AV_PIX_FMT_RGBAF32BE, {
                            .internal_format = GL_RGBA32F,
                            .upload_format = GL_RGBA,
                            .upload_elem_type = GL_FLOAT,
                        })
#endif
