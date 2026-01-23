#ifndef PS_IMG_RESAMPLE_H
#define PS_IMG_RESAMPLE_H

/*
 * Minimal, standalone resampling API extracted and simplified from GIMP.
 * Assumes tightly packed RGBA8 pixels (4 bytes per pixel).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PsImgResampleInterpolation
{
  PS_IMG_RESAMPLE_INTERP_NONE = 0,
  PS_IMG_RESAMPLE_INTERP_LINEAR,
  PS_IMG_RESAMPLE_INTERP_CUBIC,
  PS_IMG_RESAMPLE_INTERP_NOHALO,
  PS_IMG_RESAMPLE_INTERP_LOHALO
} PsImgResampleInterpolation;

/*
 * Returns a newly allocated RGBA8 buffer of size dst_width * dst_height * 4.
 * Caller must free() the returned pointer.
 */
uint8_t *ps_img_resample_rgba8 (const uint8_t             *src,
                            int                        src_width,
                            int                        src_height,
                            int                        dst_width,
                            int                        dst_height,
                            PsImgResampleInterpolation interpolation);

#ifdef __cplusplus
}
#endif

#endif /* PS_IMG_RESAMPLE_H */
