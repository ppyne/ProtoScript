#ifndef PS_IMG_RESAMPLE_H
#define PS_IMG_RESAMPLE_H

/*
 * Minimal, standalone resampling API.
 * Assumes tightly packed RGBA8 pixels (4 bytes per pixel).
 *
 * Origins and references (per src/img_resample.c):
 * - LBB (Locally Bounded Bicubic): Nicolas Robidoux & Chantal Racette;
 *   based on Brodlie/Mashwama/Butt, Computer & Graphics 19(4), 1995.
 * - Nohalo / LBB-Nohalo: Robidoux & Racette.
 * - ClampUpAxes (EWA clamp): ImageMagick resample.c; Robidoux & Racette,
 *   with suggestions from Anthony Thyssen.
 * - EWA ellipse clamping: Andreas Gustaffson, "Interactive Image Warping",
 *   1993; SVD clamping per Craig DeForest (PDL::Transform).
 * - Mitchell-Netravali weights: method of N. Robidoux.
 * - Robidoux cubic: Keys cubic variant (see src/img_resample.c).
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
