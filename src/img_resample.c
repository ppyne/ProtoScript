#include "ps_config.h"

#if PS_ENABLE_MODULE_IMG

#include "ps_img_resample.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CLAMP_INT(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define CLAMP_FLOAT(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

#define RESIZE_CONTEXT_MAX 64

#define NOHALO_OFFSET_0 (13)
#define NOHALO_SIZE_0 (1 + 2 * NOHALO_OFFSET_0)

#define LOHALO_OFFSET_0 (13)
#define LOHALO_SIZE_0 (1 + 2 * LOHALO_OFFSET_0)
#define LOHALO_CONTRAST (3.38589)

#define NOHALO_MINMOD(_a_, _b_, _a_times_a_, _a_times_b_)                      \
  ((_a_times_b_) >= (gfloat)0.                                                 \
       ? ((_a_times_a_) <= (_a_times_b_) ? (_a_) : (_b_))                      \
       : (gfloat)0.)

#define NOHALO_MIN(_x_, _y_) ((_x_) <= (_y_) ? (_x_) : (_y_))
#define NOHALO_MAX(_x_, _y_) ((_x_) >= (_y_) ? (_x_) : (_y_))
#define NOHALO_ABS(_x_) ((_x_) >= (gfloat)0. ? (_x_) : -(_x_))
#define NOHALO_SIGN(_x_) ((_x_) >= (gfloat)0. ? (gfloat)1. : (gfloat) - 1.)

#define LOHALO_MIN(_x_, _y_) ((_x_) <= (_y_) ? (_x_) : (_y_))
#define LOHALO_MAX(_x_, _y_) ((_x_) >= (_y_) ? (_x_) : (_y_))

typedef float gfloat;
typedef double gdouble;
typedef int gint;
typedef unsigned int guint;

typedef enum ResizeAbyssPolicy { RESIZE_ABYSS_CLAMP = 0 } ResizeAbyssPolicy;

typedef struct ResizeScaleMatrix {
  double coeff[2][2];
} ResizeScaleMatrix;

typedef struct ResizeSampler {
  const uint8_t *src;
  int src_w;
  int src_h;
  int interpolate_components;
  float context[RESIZE_CONTEXT_MAX * RESIZE_CONTEXT_MAX * 4];
} ResizeSampler;

static inline gint int_floorf(const gfloat x) { return (gint)floorf(x); }

static inline gint int_ceilf(const gfloat x) { return (gint)ceilf(x); }

static inline float u8_to_float(uint8_t v) { return v * (1.0f / 255.0f); }

static inline uint8_t float_to_u8(float v) {
  v = CLAMP_FLOAT(v, 0.0f, 1.0f);
  return (uint8_t)(v * 255.0f + 0.5f);
}

static float *resize_sampler_get_ptr(ResizeSampler *self, int ix, int iy,
                                     int offset,
                                     ResizeAbyssPolicy repeat_mode) {
  const int size = offset * 2 + 1;
  (void)repeat_mode;

  for (int dy = 0; dy < size; ++dy) {
    int sy = CLAMP_INT(iy + dy - offset, 0, self->src_h - 1);
    for (int dx = 0; dx < size; ++dx) {
      int sx = CLAMP_INT(ix + dx - offset, 0, self->src_w - 1);
      const uint8_t *sp = self->src + (sy * self->src_w + sx) * 4;
      float *dp = self->context + ((dy * RESIZE_CONTEXT_MAX) + dx) * 4;
      dp[0] = u8_to_float(sp[0]);
      dp[1] = u8_to_float(sp[1]);
      dp[2] = u8_to_float(sp[2]);
      dp[3] = u8_to_float(sp[3]);
    }
  }

  return self->context + ((offset * RESIZE_CONTEXT_MAX) + offset) * 4;
}

static inline float int_fabsf(const float x) {
  union {
    float f;
    unsigned int i;
  } u = {x};
  u.i &= 0x7fffffff;
  return u.f;
}

static inline float cubicKernel(const float x, const float b, const float c) {
  const float ax = int_fabsf(x);
  const float x2 = ax * ax;
  const float x3 = x2 * ax;

  if (ax > 2.f)
    return 0.f;

  if (ax < 1.f)
    return ((12.f - 9.f * b - 6.f * c) * x3 +
            (-18.f + 12.f * b + 6.f * c) * x2 + (6.f - 2.f * b)) *
           (1.f / 6.f);
  return ((-b - 6.f * c) * x3 + (6.f * b + 30.f * c) * x2 +
          (-12.f * b - 48.f * c) * ax + (8.f * b + 24.f * c)) *
         (1.f / 6.f);
}

static inline void nohalo_subdivision(
    const gfloat uno_two, const gfloat uno_thr, const gfloat uno_fou,
    const gfloat dos_one, const gfloat dos_two, const gfloat dos_thr,
    const gfloat dos_fou, const gfloat dos_fiv, const gfloat tre_one,
    const gfloat tre_two, const gfloat tre_thr, const gfloat tre_fou,
    const gfloat tre_fiv, const gfloat qua_one, const gfloat qua_two,
    const gfloat qua_thr, const gfloat qua_fou, const gfloat qua_fiv,
    const gfloat cin_two, const gfloat cin_thr, const gfloat cin_fou,
    gfloat *restrict uno_one_1, gfloat *restrict uno_two_1,
    gfloat *restrict uno_thr_1, gfloat *restrict uno_fou_1,
    gfloat *restrict dos_one_1, gfloat *restrict dos_two_1,
    gfloat *restrict dos_thr_1, gfloat *restrict dos_fou_1,
    gfloat *restrict tre_one_1, gfloat *restrict tre_two_1,
    gfloat *restrict tre_thr_1, gfloat *restrict tre_fou_1,
    gfloat *restrict qua_one_1, gfloat *restrict qua_two_1,
    gfloat *restrict qua_thr_1, gfloat *restrict qua_fou_1) {

  /*
   * Build the 4x4 half-step stencil used by LBB around the sample.
   * We draw from a 5x5 neighborhood around (ix,iy), reflect so the
   * sample lies in the lower-right of tre_thr, then compute the
   * 16 half-pixel values (uno/dos/tre/qua *_1).
   * Computation of the nonlinear slopes: If two consecutive pixel
   * value differences have the same sign, the smallest one (in
   * absolute value) is taken to be the corresponding slope; if the
   * two consecutive pixel value differences don't have the same sign,
   * the corresponding slope is set to 0.
   *
   * In other words: Apply minmod to consecutive differences.
   */

  /*
   * Two vertical simple differences:
   */
  const gfloat d_unodos_two = dos_two - uno_two;
  const gfloat d_dostre_two = tre_two - dos_two;
  const gfloat d_trequa_two = qua_two - tre_two;
  const gfloat d_quacin_two = cin_two - qua_two;
  /*
   * Thr(ee) vertical differences:
   */
  const gfloat d_unodos_thr = dos_thr - uno_thr;
  const gfloat d_dostre_thr = tre_thr - dos_thr;
  const gfloat d_trequa_thr = qua_thr - tre_thr;
  const gfloat d_quacin_thr = cin_thr - qua_thr;
  /*
   * Fou(r) vertical differences:
   */
  const gfloat d_unodos_fou = dos_fou - uno_fou;
  const gfloat d_dostre_fou = tre_fou - dos_fou;
  const gfloat d_trequa_fou = qua_fou - tre_fou;
  const gfloat d_quacin_fou = cin_fou - qua_fou;
  /*
   * Dos horizontal differences:
   */
  const gfloat d_dos_onetwo = dos_two - dos_one;
  const gfloat d_dos_twothr = dos_thr - dos_two;
  const gfloat d_dos_thrfou = dos_fou - dos_thr;
  const gfloat d_dos_foufiv = dos_fiv - dos_fou;
  /*
   * Tre(s) horizontal differences:
   */
  const gfloat d_tre_onetwo = tre_two - tre_one;
  const gfloat d_tre_twothr = tre_thr - tre_two;
  const gfloat d_tre_thrfou = tre_fou - tre_thr;
  const gfloat d_tre_foufiv = tre_fiv - tre_fou;
  /*
   * Qua(ttro) horizontal differences:
   */
  const gfloat d_qua_onetwo = qua_two - qua_one;
  const gfloat d_qua_twothr = qua_thr - qua_two;
  const gfloat d_qua_thrfou = qua_fou - qua_thr;
  const gfloat d_qua_foufiv = qua_fiv - qua_fou;

  /*
   * Recyclable vertical products and squares:
   */
  const gfloat d_unodos_times_dostre_two = d_unodos_two * d_dostre_two;
  const gfloat d_dostre_two_sq = d_dostre_two * d_dostre_two;
  const gfloat d_dostre_times_trequa_two = d_dostre_two * d_trequa_two;
  const gfloat d_trequa_times_quacin_two = d_quacin_two * d_trequa_two;
  const gfloat d_quacin_two_sq = d_quacin_two * d_quacin_two;

  const gfloat d_unodos_times_dostre_thr = d_unodos_thr * d_dostre_thr;
  const gfloat d_dostre_thr_sq = d_dostre_thr * d_dostre_thr;
  const gfloat d_dostre_times_trequa_thr = d_trequa_thr * d_dostre_thr;
  const gfloat d_trequa_times_quacin_thr = d_trequa_thr * d_quacin_thr;
  const gfloat d_quacin_thr_sq = d_quacin_thr * d_quacin_thr;

  const gfloat d_unodos_times_dostre_fou = d_unodos_fou * d_dostre_fou;
  const gfloat d_dostre_fou_sq = d_dostre_fou * d_dostre_fou;
  const gfloat d_dostre_times_trequa_fou = d_trequa_fou * d_dostre_fou;
  const gfloat d_trequa_times_quacin_fou = d_trequa_fou * d_quacin_fou;
  const gfloat d_quacin_fou_sq = d_quacin_fou * d_quacin_fou;
  /*
   * Recyclable horizontal products and squares:
   */
  const gfloat d_dos_onetwo_times_twothr = d_dos_onetwo * d_dos_twothr;
  const gfloat d_dos_twothr_sq = d_dos_twothr * d_dos_twothr;
  const gfloat d_dos_twothr_times_thrfou = d_dos_twothr * d_dos_thrfou;
  const gfloat d_dos_thrfou_times_foufiv = d_dos_thrfou * d_dos_foufiv;
  const gfloat d_dos_foufiv_sq = d_dos_foufiv * d_dos_foufiv;

  const gfloat d_tre_onetwo_times_twothr = d_tre_onetwo * d_tre_twothr;
  const gfloat d_tre_twothr_sq = d_tre_twothr * d_tre_twothr;
  const gfloat d_tre_twothr_times_thrfou = d_tre_thrfou * d_tre_twothr;
  const gfloat d_tre_thrfou_times_foufiv = d_tre_thrfou * d_tre_foufiv;
  const gfloat d_tre_foufiv_sq = d_tre_foufiv * d_tre_foufiv;

  const gfloat d_qua_onetwo_times_twothr = d_qua_onetwo * d_qua_twothr;
  const gfloat d_qua_twothr_sq = d_qua_twothr * d_qua_twothr;
  const gfloat d_qua_twothr_times_thrfou = d_qua_thrfou * d_qua_twothr;
  const gfloat d_qua_thrfou_times_foufiv = d_qua_thrfou * d_qua_foufiv;
  const gfloat d_qua_foufiv_sq = d_qua_foufiv * d_qua_foufiv;

  /*
   * Minmod slopes and first level pixel values:
   */
  const gfloat dos_thr_y = NOHALO_MINMOD(
      d_dostre_thr, d_unodos_thr, d_dostre_thr_sq, d_unodos_times_dostre_thr);
  const gfloat tre_thr_y = NOHALO_MINMOD(
      d_dostre_thr, d_trequa_thr, d_dostre_thr_sq, d_dostre_times_trequa_thr);

  const gfloat newval_uno_two =
      (gfloat)0.5 * (dos_thr + tre_thr + (gfloat)0.5 * (dos_thr_y - tre_thr_y));

  const gfloat qua_thr_y = NOHALO_MINMOD(
      d_quacin_thr, d_trequa_thr, d_quacin_thr_sq, d_trequa_times_quacin_thr);

  const gfloat newval_tre_two =
      (gfloat)0.5 * (tre_thr + qua_thr + (gfloat)0.5 * (tre_thr_y - qua_thr_y));

  const gfloat tre_fou_y = NOHALO_MINMOD(
      d_dostre_fou, d_trequa_fou, d_dostre_fou_sq, d_dostre_times_trequa_fou);
  const gfloat qua_fou_y = NOHALO_MINMOD(
      d_quacin_fou, d_trequa_fou, d_quacin_fou_sq, d_trequa_times_quacin_fou);

  const gfloat newval_tre_fou =
      (gfloat)0.5 * (tre_fou + qua_fou + (gfloat)0.5 * (tre_fou_y - qua_fou_y));

  const gfloat dos_fou_y = NOHALO_MINMOD(
      d_dostre_fou, d_unodos_fou, d_dostre_fou_sq, d_unodos_times_dostre_fou);

  const gfloat newval_uno_fou =
      (gfloat)0.5 * (dos_fou + tre_fou + (gfloat)0.5 * (dos_fou_y - tre_fou_y));

  const gfloat tre_two_x = NOHALO_MINMOD(
      d_tre_twothr, d_tre_onetwo, d_tre_twothr_sq, d_tre_onetwo_times_twothr);
  const gfloat tre_thr_x = NOHALO_MINMOD(
      d_tre_twothr, d_tre_thrfou, d_tre_twothr_sq, d_tre_twothr_times_thrfou);

  const gfloat newval_dos_one =
      (gfloat)0.5 * (tre_two + tre_thr + (gfloat)0.5 * (tre_two_x - tre_thr_x));

  const gfloat tre_fou_x = NOHALO_MINMOD(
      d_tre_foufiv, d_tre_thrfou, d_tre_foufiv_sq, d_tre_thrfou_times_foufiv);

  const gfloat tre_thr_x_minus_tre_fou_x = tre_thr_x - tre_fou_x;

  const gfloat newval_dos_thr =
      (gfloat)0.5 *
      (tre_thr + tre_fou + (gfloat)0.5 * tre_thr_x_minus_tre_fou_x);

  const gfloat qua_thr_x = NOHALO_MINMOD(
      d_qua_twothr, d_qua_thrfou, d_qua_twothr_sq, d_qua_twothr_times_thrfou);
  const gfloat qua_fou_x = NOHALO_MINMOD(
      d_qua_foufiv, d_qua_thrfou, d_qua_foufiv_sq, d_qua_thrfou_times_foufiv);

  const gfloat qua_thr_x_minus_qua_fou_x = qua_thr_x - qua_fou_x;

  const gfloat newval_qua_thr =
      (gfloat)0.5 *
      (qua_thr + qua_fou + (gfloat)0.5 * qua_thr_x_minus_qua_fou_x);

  const gfloat qua_two_x = NOHALO_MINMOD(
      d_qua_twothr, d_qua_onetwo, d_qua_twothr_sq, d_qua_onetwo_times_twothr);

  const gfloat newval_qua_one =
      (gfloat)0.5 * (qua_two + qua_thr + (gfloat)0.5 * (qua_two_x - qua_thr_x));

  const gfloat newval_tre_thr =
      (gfloat)0.5 *
      (newval_tre_two + newval_tre_fou +
       (gfloat)0.25 * (tre_thr_x_minus_tre_fou_x + qua_thr_x_minus_qua_fou_x));

  const gfloat dos_thr_x = NOHALO_MINMOD(
      d_dos_twothr, d_dos_thrfou, d_dos_twothr_sq, d_dos_twothr_times_thrfou);
  const gfloat dos_fou_x = NOHALO_MINMOD(
      d_dos_foufiv, d_dos_thrfou, d_dos_foufiv_sq, d_dos_thrfou_times_foufiv);

  const gfloat newval_uno_thr =
      (gfloat)0.5 * (newval_uno_two + newval_dos_thr +
                     (gfloat)0.5 * (dos_fou - tre_thr +
                                    (gfloat)0.5 * (dos_fou_y - tre_fou_y +
                                                   dos_thr_x - dos_fou_x)));

  const gfloat tre_two_y = NOHALO_MINMOD(
      d_dostre_two, d_trequa_two, d_dostre_two_sq, d_dostre_times_trequa_two);
  const gfloat qua_two_y = NOHALO_MINMOD(
      d_quacin_two, d_trequa_two, d_quacin_two_sq, d_trequa_times_quacin_two);

  const gfloat newval_tre_one =
      (gfloat)0.5 * (newval_dos_one + newval_tre_two +
                     (gfloat)0.5 * (qua_two - tre_thr +
                                    (gfloat)0.5 * (qua_two_x - qua_thr_x +
                                                   tre_two_y - qua_two_y)));

  const gfloat dos_two_x = NOHALO_MINMOD(
      d_dos_twothr, d_dos_onetwo, d_dos_twothr_sq, d_dos_onetwo_times_twothr);
  const gfloat dos_two_y = NOHALO_MINMOD(
      d_dostre_two, d_unodos_two, d_dostre_two_sq, d_unodos_times_dostre_two);

  const gfloat newval_uno_one =
      (gfloat)0.25 *
      (dos_two + dos_thr + tre_two + tre_thr +
       (gfloat)0.5 * (dos_two_x - dos_thr_x + tre_two_x - tre_thr_x +
                      dos_two_y + dos_thr_y - tre_two_y - tre_thr_y));

  /*
   * Return the sixteen LBB stencil values:
   */
  *uno_one_1 = newval_uno_one;
  *uno_two_1 = newval_uno_two;
  *uno_thr_1 = newval_uno_thr;
  *uno_fou_1 = newval_uno_fou;
  *dos_one_1 = newval_dos_one;
  *dos_two_1 = tre_thr;
  *dos_thr_1 = newval_dos_thr;
  *dos_fou_1 = tre_fou;
  *tre_one_1 = newval_tre_one;
  *tre_two_1 = newval_tre_two;
  *tre_thr_1 = newval_tre_thr;
  *tre_fou_1 = newval_tre_fou;
  *qua_one_1 = newval_qua_one;
  *qua_two_1 = qua_thr;
  *qua_thr_1 = newval_qua_thr;
  *qua_fou_1 = qua_fou;
}

static inline gfloat
lbb(const gfloat c00, const gfloat c10, const gfloat c01, const gfloat c11,
    const gfloat c00dx, const gfloat c10dx, const gfloat c01dx,
    const gfloat c11dx, const gfloat c00dy, const gfloat c10dy,
    const gfloat c01dy, const gfloat c11dy, const gfloat c00dxdy,
    const gfloat c10dxdy, const gfloat c01dxdy, const gfloat c11dxdy,
    const gfloat uno_one, const gfloat uno_two, const gfloat uno_thr,
    const gfloat uno_fou, const gfloat dos_one, const gfloat dos_two,
    const gfloat dos_thr, const gfloat dos_fou, const gfloat tre_one,
    const gfloat tre_two, const gfloat tre_thr, const gfloat tre_fou,
    const gfloat qua_one, const gfloat qua_two, const gfloat qua_thr,
    const gfloat qua_fou) {
  /*
   * LBB (Locally Bounded Bicubic) is a nonlinear Catmull-Rom variant.
   * It uses a Hermite bicubic over a 4x4 stencil with derivative
   * limiting so results stay within local min/max (no output clamp).
   * This implementation uses the "soft" limiter used by Nohalo.
   * 
   * LBB uses the standard 4x4 Hermite bicubic stencil (ix/iy are the
   * floor of the sample position).
   * 
   * Compute 3x3 sub-block mins/maxes over the 4x4 stencil. The Nohalo
   * monotonicity constraints let us order comparisons and drop a
   * redundant flag (see the commented lines below).
   */
  const gfloat m1 = (dos_two <= dos_thr) ? dos_two : dos_thr;
  const gfloat M1 = (dos_two <= dos_thr) ? dos_thr : dos_two;
  const gfloat m2 = (tre_two <= tre_thr) ? tre_two : tre_thr;
  const gfloat M2 = (tre_two <= tre_thr) ? tre_thr : tre_two;
  const gfloat m4 = (qua_two <= qua_thr) ? qua_two : qua_thr;
  const gfloat M4 = (qua_two <= qua_thr) ? qua_thr : qua_two;
  const gfloat m3 = (uno_two <= uno_thr) ? uno_two : uno_thr;
  const gfloat M3 = (uno_two <= uno_thr) ? uno_thr : uno_two;
  const gfloat m5 = NOHALO_MIN(m1, m2);
  const gfloat M5 = NOHALO_MAX(M1, M2);
  const gfloat m6 = (dos_one <= tre_one) ? dos_one : tre_one;
  const gfloat M6 = (dos_one <= tre_one) ? tre_one : dos_one;
  const gfloat m7 = (dos_fou <= tre_fou) ? dos_fou : tre_fou;
  const gfloat M7 = (dos_fou <= tre_fou) ? tre_fou : dos_fou;
  const gfloat m13 = (dos_fou <= qua_fou) ? dos_fou : qua_fou;
  const gfloat M13 = (dos_fou <= qua_fou) ? qua_fou : dos_fou;
  /*
   * Nohalo constraints make these equivalent to the NOHALO_MIN/MAX
   * pair below; we keep this form to improve scheduling.
   */
  const gfloat m9 = NOHALO_MIN(m5, m4);
  const gfloat M9 = NOHALO_MAX(M5, M4);
  const gfloat m11 = NOHALO_MIN(m6, qua_one);
  const gfloat M11 = NOHALO_MAX(M6, qua_one);
  const gfloat m10 = NOHALO_MIN(m6, uno_one);
  const gfloat M10 = NOHALO_MAX(M6, uno_one);
  const gfloat m8 = NOHALO_MIN(m5, m3);
  const gfloat M8 = NOHALO_MAX(M5, M3);
  const gfloat m12 = NOHALO_MIN(m7, uno_fou);
  const gfloat M12 = NOHALO_MAX(M7, uno_fou);
  const gfloat min11 = NOHALO_MIN(m9, m13);
  const gfloat max11 = NOHALO_MAX(M9, M13);
  const gfloat min01 = NOHALO_MIN(m9, m11);
  const gfloat max01 = NOHALO_MAX(M9, M11);
  const gfloat min00 = NOHALO_MIN(m8, m10);
  const gfloat max00 = NOHALO_MAX(M8, M10);
  const gfloat min10 = NOHALO_MIN(m8, m12);
  const gfloat max10 = NOHALO_MAX(M8, M12);

  /*
   * Per-channel limiting uses a fixed sequence of min/max and abs
   * operations to keep derivatives bounded.
   */

  /*
   * Distances to the local min and max:
   */
  const gfloat u11 = tre_thr - min11;
  const gfloat v11 = max11 - tre_thr;
  const gfloat u01 = tre_two - min01;
  const gfloat v01 = max01 - tre_two;
  const gfloat u00 = dos_two - min00;
  const gfloat v00 = max00 - dos_two;
  const gfloat u10 = dos_thr - min10;
  const gfloat v10 = max10 - dos_thr;

  /*
   * Initial values of the derivatives computed with centered
   * differences. Factors of 1/2 are left out because they are folded
   * in later:
   */
  const gfloat dble_dzdx00i = dos_thr - dos_one;
  const gfloat dble_dzdy11i = qua_thr - dos_thr;
  const gfloat dble_dzdx10i = dos_fou - dos_two;
  const gfloat dble_dzdy01i = qua_two - dos_two;
  const gfloat dble_dzdx01i = tre_thr - tre_one;
  const gfloat dble_dzdy10i = tre_thr - uno_thr;
  const gfloat dble_dzdx11i = tre_fou - tre_two;
  const gfloat dble_dzdy00i = tre_two - uno_two;

  /*
   * Signs of the derivatives. The upcoming clamping does not change
   * them (except if the clamping sends a negative derivative to 0, in
   * which case the sign does not matter anyway).
   */
  const gfloat sign_dzdx00 = NOHALO_SIGN(dble_dzdx00i);
  const gfloat sign_dzdx10 = NOHALO_SIGN(dble_dzdx10i);
  const gfloat sign_dzdx01 = NOHALO_SIGN(dble_dzdx01i);
  const gfloat sign_dzdx11 = NOHALO_SIGN(dble_dzdx11i);

  const gfloat sign_dzdy00 = NOHALO_SIGN(dble_dzdy00i);
  const gfloat sign_dzdy10 = NOHALO_SIGN(dble_dzdy10i);
  const gfloat sign_dzdy01 = NOHALO_SIGN(dble_dzdy01i);
  const gfloat sign_dzdy11 = NOHALO_SIGN(dble_dzdy11i);

  /*
   * Initial values of the cross-derivatives. Factors of 1/4 are left
   * out because folded in later:
   */
  const gfloat quad_d2zdxdy00i = uno_one - uno_thr + dble_dzdx01i;
  const gfloat quad_d2zdxdy10i = uno_two - uno_fou + dble_dzdx11i;
  const gfloat quad_d2zdxdy01i = qua_thr - qua_one - dble_dzdx00i;
  const gfloat quad_d2zdxdy11i = qua_fou - qua_two - dble_dzdx10i;

  /*
   * Slope limiters. The key multiplier is 3 but we fold a factor of
   * 2, hence 6:
   */
  const gfloat dble_slopelimit_00 = (gfloat)6.0 * NOHALO_MIN(u00, v00);
  const gfloat dble_slopelimit_10 = (gfloat)6.0 * NOHALO_MIN(u10, v10);
  const gfloat dble_slopelimit_01 = (gfloat)6.0 * NOHALO_MIN(u01, v01);
  const gfloat dble_slopelimit_11 = (gfloat)6.0 * NOHALO_MIN(u11, v11);

  /*
   * Clamped first derivatives:
   */
  const gfloat dble_dzdx00 = (sign_dzdx00 * dble_dzdx00i <= dble_slopelimit_00)
                                 ? dble_dzdx00i
                                 : sign_dzdx00 * dble_slopelimit_00;
  const gfloat dble_dzdy00 = (sign_dzdy00 * dble_dzdy00i <= dble_slopelimit_00)
                                 ? dble_dzdy00i
                                 : sign_dzdy00 * dble_slopelimit_00;
  const gfloat dble_dzdx10 = (sign_dzdx10 * dble_dzdx10i <= dble_slopelimit_10)
                                 ? dble_dzdx10i
                                 : sign_dzdx10 * dble_slopelimit_10;
  const gfloat dble_dzdy10 = (sign_dzdy10 * dble_dzdy10i <= dble_slopelimit_10)
                                 ? dble_dzdy10i
                                 : sign_dzdy10 * dble_slopelimit_10;
  const gfloat dble_dzdx01 = (sign_dzdx01 * dble_dzdx01i <= dble_slopelimit_01)
                                 ? dble_dzdx01i
                                 : sign_dzdx01 * dble_slopelimit_01;
  const gfloat dble_dzdy01 = (sign_dzdy01 * dble_dzdy01i <= dble_slopelimit_01)
                                 ? dble_dzdy01i
                                 : sign_dzdy01 * dble_slopelimit_01;
  const gfloat dble_dzdx11 = (sign_dzdx11 * dble_dzdx11i <= dble_slopelimit_11)
                                 ? dble_dzdx11i
                                 : sign_dzdx11 * dble_slopelimit_11;
  const gfloat dble_dzdy11 = (sign_dzdy11 * dble_dzdy11i <= dble_slopelimit_11)
                                 ? dble_dzdy11i
                                 : sign_dzdy11 * dble_slopelimit_11;

  /*
   * Sums and differences of first derivatives:
   */
  const gfloat twelve_sum00 = (gfloat)6.0 * (dble_dzdx00 + dble_dzdy00);
  const gfloat twelve_dif00 = (gfloat)6.0 * (dble_dzdx00 - dble_dzdy00);
  const gfloat twelve_sum10 = (gfloat)6.0 * (dble_dzdx10 + dble_dzdy10);
  const gfloat twelve_dif10 = (gfloat)6.0 * (dble_dzdx10 - dble_dzdy10);
  const gfloat twelve_sum01 = (gfloat)6.0 * (dble_dzdx01 + dble_dzdy01);
  const gfloat twelve_dif01 = (gfloat)6.0 * (dble_dzdx01 - dble_dzdy01);
  const gfloat twelve_sum11 = (gfloat)6.0 * (dble_dzdx11 + dble_dzdy11);
  const gfloat twelve_dif11 = (gfloat)6.0 * (dble_dzdx11 - dble_dzdy11);

  /*
   * Absolute values of the sums:
   */
  const gfloat twelve_abs_sum00 = NOHALO_ABS(twelve_sum00);
  const gfloat twelve_abs_sum10 = NOHALO_ABS(twelve_sum10);
  const gfloat twelve_abs_sum01 = NOHALO_ABS(twelve_sum01);
  const gfloat twelve_abs_sum11 = NOHALO_ABS(twelve_sum11);

  /*
   * Scaled distances to the min:
   */
  const gfloat u00_times_36 = (gfloat)36.0 * u00;
  const gfloat u10_times_36 = (gfloat)36.0 * u10;
  const gfloat u01_times_36 = (gfloat)36.0 * u01;
  const gfloat u11_times_36 = (gfloat)36.0 * u11;

  /*
   * First cross-derivative limiter:
   */
  const gfloat first_limit00 = twelve_abs_sum00 - u00_times_36;
  const gfloat first_limit10 = twelve_abs_sum10 - u10_times_36;
  const gfloat first_limit01 = twelve_abs_sum01 - u01_times_36;
  const gfloat first_limit11 = twelve_abs_sum11 - u11_times_36;

  const gfloat quad_d2zdxdy00ii = NOHALO_MAX(quad_d2zdxdy00i, first_limit00);
  const gfloat quad_d2zdxdy10ii = NOHALO_MAX(quad_d2zdxdy10i, first_limit10);
  const gfloat quad_d2zdxdy01ii = NOHALO_MAX(quad_d2zdxdy01i, first_limit01);
  const gfloat quad_d2zdxdy11ii = NOHALO_MAX(quad_d2zdxdy11i, first_limit11);

  /*
   * Scaled distances to the max:
   */
  const gfloat v00_times_36 = (gfloat)36.0 * v00;
  const gfloat v10_times_36 = (gfloat)36.0 * v10;
  const gfloat v01_times_36 = (gfloat)36.0 * v01;
  const gfloat v11_times_36 = (gfloat)36.0 * v11;

  /*
   * Second cross-derivative limiter:
   */
  const gfloat second_limit00 = v00_times_36 - twelve_abs_sum00;
  const gfloat second_limit10 = v10_times_36 - twelve_abs_sum10;
  const gfloat second_limit01 = v01_times_36 - twelve_abs_sum01;
  const gfloat second_limit11 = v11_times_36 - twelve_abs_sum11;

  const gfloat quad_d2zdxdy00iii = NOHALO_MIN(quad_d2zdxdy00ii, second_limit00);
  const gfloat quad_d2zdxdy10iii = NOHALO_MIN(quad_d2zdxdy10ii, second_limit10);
  const gfloat quad_d2zdxdy01iii = NOHALO_MIN(quad_d2zdxdy01ii, second_limit01);
  const gfloat quad_d2zdxdy11iii = NOHALO_MIN(quad_d2zdxdy11ii, second_limit11);

  /*
   * Absolute values of the differences:
   */
  const gfloat twelve_abs_dif00 = NOHALO_ABS(twelve_dif00);
  const gfloat twelve_abs_dif10 = NOHALO_ABS(twelve_dif10);
  const gfloat twelve_abs_dif01 = NOHALO_ABS(twelve_dif01);
  const gfloat twelve_abs_dif11 = NOHALO_ABS(twelve_dif11);

  /*
   * Third cross-derivative limiter:
   */
  const gfloat third_limit00 = twelve_abs_dif00 - v00_times_36;
  const gfloat third_limit10 = twelve_abs_dif10 - v10_times_36;
  const gfloat third_limit01 = twelve_abs_dif01 - v01_times_36;
  const gfloat third_limit11 = twelve_abs_dif11 - v11_times_36;

  const gfloat quad_d2zdxdy00iiii =
      NOHALO_MAX(quad_d2zdxdy00iii, third_limit00);
  const gfloat quad_d2zdxdy10iiii =
      NOHALO_MAX(quad_d2zdxdy10iii, third_limit10);
  const gfloat quad_d2zdxdy01iiii =
      NOHALO_MAX(quad_d2zdxdy01iii, third_limit01);
  const gfloat quad_d2zdxdy11iiii =
      NOHALO_MAX(quad_d2zdxdy11iii, third_limit11);

  /*
   * Fourth cross-derivative limiter:
   */
  const gfloat fourth_limit00 = u00_times_36 - twelve_abs_dif00;
  const gfloat fourth_limit10 = u10_times_36 - twelve_abs_dif10;
  const gfloat fourth_limit01 = u01_times_36 - twelve_abs_dif01;
  const gfloat fourth_limit11 = u11_times_36 - twelve_abs_dif11;

  const gfloat quad_d2zdxdy00 = NOHALO_MIN(quad_d2zdxdy00iiii, fourth_limit00);
  const gfloat quad_d2zdxdy10 = NOHALO_MIN(quad_d2zdxdy10iiii, fourth_limit10);
  const gfloat quad_d2zdxdy01 = NOHALO_MIN(quad_d2zdxdy01iiii, fourth_limit01);
  const gfloat quad_d2zdxdy11 = NOHALO_MIN(quad_d2zdxdy11iiii, fourth_limit11);

  /*
   * Part of the result that does not need derivatives:
   */
  const gfloat newval1 =
      c00 * dos_two + c10 * dos_thr + c01 * tre_two + c11 * tre_thr;

  /*
   * Twice the part of the result that only needs first derivatives.
   */
  const gfloat newval2 = c00dx * dble_dzdx00 + c10dx * dble_dzdx10 +
                         c01dx * dble_dzdx01 + c11dx * dble_dzdx11 +
                         c00dy * dble_dzdy00 + c10dy * dble_dzdy10 +
                         c01dy * dble_dzdy01 + c11dy * dble_dzdy11;

  /*
   * Four times the part of the result that only uses
   * cross-derivatives:
   */
  const gfloat newval3 = c00dxdy * quad_d2zdxdy00 + c10dxdy * quad_d2zdxdy10 +
                         c01dxdy * quad_d2zdxdy01 + c11dxdy * quad_d2zdxdy11;

  const gfloat newval =
      newval1 + (gfloat)0.5 * (newval2 + (gfloat)0.5 * newval3);

  return newval;
}

static inline gfloat teepee(const gfloat c_major_x, const gfloat c_major_y,
                            const gfloat c_minor_x, const gfloat c_minor_y,
                            const gfloat s, const gfloat t) {
  const gfloat q1 = s * c_major_x + t * c_major_y;
  const gfloat q2 = s * c_minor_x + t * c_minor_y;

  const float r2 = q1 * q1 + q2 * q2;

  const gfloat weight =
      r2 < (float)1. ? (gfloat)((float)1. - sqrtf(r2)) : (gfloat)0.;

  return weight;
}

static inline void ewa_update(const gint j, const gint i,
                              const gfloat c_major_x, const gfloat c_major_y,
                              const gfloat c_minor_x, const gfloat c_minor_y,
                              const gfloat x_0, const gfloat y_0,
                              const gint channels, const gint row_skip,
                              const gfloat *restrict input_ptr,
                              gdouble *restrict total_weight,
                              gfloat *restrict ewa_newval) {
  const gint skip = j * channels + i * row_skip;

  const gfloat weight = teepee(c_major_x, c_major_y, c_minor_x, c_minor_y,
                               x_0 - (gfloat)j, y_0 - (gfloat)i);
  gint c;

  *total_weight += weight;
  for (c = 0; c < channels; c++)
    ewa_newval[c] += weight * input_ptr[skip + c];
}

static void resize_nohalo_get(ResizeSampler *restrict self,
                              const gdouble absolute_x,
                              const gdouble absolute_y,
                              ResizeScaleMatrix *scale, void *restrict output,
                              ResizeAbyssPolicy repeat_mode) {
  /*
   * Needed constants related to the input pixel value pointer
   * provided by resize_sampler_get_ptr (self, ix, iy). pixels_per_row
   * corresponds to fetch_rectangle.width in resize_sampler_get_ptr.
   */
  const gint channels = self->interpolate_components;
  const gint pixels_per_row = RESIZE_CONTEXT_MAX;
  const gint row_skip = channels * pixels_per_row;

  /*
   * Pick the nearest pixel center in corner-based coordinates.
   */
  const gint ix_0 = floor((double)absolute_x);
  const gint iy_0 = floor((double)absolute_y);

  /*
   * This is the pointer we use to pull pixel from "base" mipmap level
   * (level "0"), the one with scale=1.0.
   */
  const gfloat *restrict input_ptr = (gfloat *)resize_sampler_get_ptr(
      self, ix_0, iy_0, NOHALO_OFFSET_0, repeat_mode);

  /*
   * Convert from corner-based to center-based coordinates (-0.5).
   */
  const gdouble iabsolute_x = absolute_x - (gdouble)0.5;
  const gdouble iabsolute_y = absolute_y - (gdouble)0.5;

  /*
   * (x_0,y_0) is the relative position of the sampling location
   * w.r.t. the anchor pixel.
   */
  const gfloat x_0 = iabsolute_x - ix_0;
  const gfloat y_0 = iabsolute_y - iy_0;

  const gint sign_of_x_0 = 2 * (x_0 >= (gfloat)0.) - 1;
  const gint sign_of_y_0 = 2 * (y_0 >= (gfloat)0.) - 1;

  const gint shift_forw_1_pix = sign_of_x_0 * channels;
  const gint shift_forw_1_row = sign_of_y_0 * row_skip;

  const gint shift_back_1_pix = -shift_forw_1_pix;
  const gint shift_back_1_row = -shift_forw_1_row;

  const gint shift_back_2_pix = 2 * shift_back_1_pix;
  const gint shift_back_2_row = 2 * shift_back_1_row;
  const gint shift_forw_2_pix = 2 * shift_forw_1_pix;
  const gint shift_forw_2_row = 2 * shift_forw_1_row;

  const gint uno_two_shift = shift_back_1_pix + shift_back_2_row;
  const gint uno_thr_shift = shift_back_2_row;
  const gint uno_fou_shift = shift_forw_1_pix + shift_back_2_row;

  const gint dos_one_shift = shift_back_2_pix + shift_back_1_row;
  const gint dos_two_shift = shift_back_1_pix + shift_back_1_row;
  const gint dos_thr_shift = shift_back_1_row;
  const gint dos_fou_shift = shift_forw_1_pix + shift_back_1_row;
  const gint dos_fiv_shift = shift_forw_2_pix + shift_back_1_row;

  const gint tre_one_shift = shift_back_2_pix;
  const gint tre_two_shift = shift_back_1_pix;
  const gint tre_thr_shift = 0;
  const gint tre_fou_shift = shift_forw_1_pix;
  const gint tre_fiv_shift = shift_forw_2_pix;

  const gint qua_one_shift = shift_back_2_pix + shift_forw_1_row;
  const gint qua_two_shift = shift_back_1_pix + shift_forw_1_row;
  const gint qua_thr_shift = shift_forw_1_row;
  const gint qua_fou_shift = shift_forw_1_pix + shift_forw_1_row;
  const gint qua_fiv_shift = shift_forw_2_pix + shift_forw_1_row;

  const gint cin_two_shift = shift_back_1_pix + shift_forw_2_row;
  const gint cin_thr_shift = shift_forw_2_row;
  const gint cin_fou_shift = shift_forw_1_pix + shift_forw_2_row;

  /*
   * The newval array will contain one computed resampled value per
   * channel:
   */
  gfloat newval[channels];

  {
    /*
     * Computation of the needed weights (coefficients).
     */
    const gfloat xp1over2 = (2 * sign_of_x_0) * x_0;
    const gfloat xm1over2 = xp1over2 + (gfloat)(-1.0);
    const gfloat onepx = (gfloat)0.5 + xp1over2;
    const gfloat onemx = (gfloat)1.5 - xp1over2;
    const gfloat xp1over2sq = xp1over2 * xp1over2;

    const gfloat yp1over2 = (2 * sign_of_y_0) * y_0;
    const gfloat ym1over2 = yp1over2 + (gfloat)(-1.0);
    const gfloat onepy = (gfloat)0.5 + yp1over2;
    const gfloat onemy = (gfloat)1.5 - yp1over2;
    const gfloat yp1over2sq = yp1over2 * yp1over2;

    const gfloat xm1over2sq = xm1over2 * xm1over2;
    const gfloat ym1over2sq = ym1over2 * ym1over2;

    const gfloat twice1px = onepx + onepx;
    const gfloat twice1py = onepy + onepy;
    const gfloat twice1mx = onemx + onemx;
    const gfloat twice1my = onemy + onemy;

    const gfloat xm1over2sq_times_ym1over2sq = xm1over2sq * ym1over2sq;
    const gfloat xp1over2sq_times_ym1over2sq = xp1over2sq * ym1over2sq;
    const gfloat xp1over2sq_times_yp1over2sq = xp1over2sq * yp1over2sq;
    const gfloat xm1over2sq_times_yp1over2sq = xm1over2sq * yp1over2sq;

    const gfloat four_times_1px_times_1py = twice1px * twice1py;
    const gfloat four_times_1mx_times_1py = twice1mx * twice1py;
    const gfloat twice_xp1over2_times_1py = xp1over2 * twice1py;
    const gfloat twice_xm1over2_times_1py = xm1over2 * twice1py;

    const gfloat twice_xm1over2_times_1my = xm1over2 * twice1my;
    const gfloat twice_xp1over2_times_1my = xp1over2 * twice1my;
    const gfloat four_times_1mx_times_1my = twice1mx * twice1my;
    const gfloat four_times_1px_times_1my = twice1px * twice1my;

    const gfloat twice_1px_times_ym1over2 = twice1px * ym1over2;
    const gfloat twice_1mx_times_ym1over2 = twice1mx * ym1over2;
    const gfloat xp1over2_times_ym1over2 = xp1over2 * ym1over2;
    const gfloat xm1over2_times_ym1over2 = xm1over2 * ym1over2;

    const gfloat xm1over2_times_yp1over2 = xm1over2 * yp1over2;
    const gfloat xp1over2_times_yp1over2 = xp1over2 * yp1over2;
    const gfloat twice_1mx_times_yp1over2 = twice1mx * yp1over2;
    const gfloat twice_1px_times_yp1over2 = twice1px * yp1over2;

    const gfloat c00 = four_times_1px_times_1py * xm1over2sq_times_ym1over2sq;
    const gfloat c00dx = twice_xp1over2_times_1py * xm1over2sq_times_ym1over2sq;
    const gfloat c00dy = twice_1px_times_yp1over2 * xm1over2sq_times_ym1over2sq;
    const gfloat c00dxdy =
        xp1over2_times_yp1over2 * xm1over2sq_times_ym1over2sq;

    const gfloat c10 = four_times_1mx_times_1py * xp1over2sq_times_ym1over2sq;
    const gfloat c10dx = twice_xm1over2_times_1py * xp1over2sq_times_ym1over2sq;
    const gfloat c10dy = twice_1mx_times_yp1over2 * xp1over2sq_times_ym1over2sq;
    const gfloat c10dxdy =
        xm1over2_times_yp1over2 * xp1over2sq_times_ym1over2sq;

    const gfloat c01 = four_times_1px_times_1my * xm1over2sq_times_yp1over2sq;
    const gfloat c01dx = twice_xp1over2_times_1my * xm1over2sq_times_yp1over2sq;
    const gfloat c01dy = twice_1px_times_ym1over2 * xm1over2sq_times_yp1over2sq;
    const gfloat c01dxdy =
        xp1over2_times_ym1over2 * xm1over2sq_times_yp1over2sq;

    const gfloat c11 = four_times_1mx_times_1my * xp1over2sq_times_yp1over2sq;
    const gfloat c11dx = twice_xm1over2_times_1my * xp1over2sq_times_yp1over2sq;
    const gfloat c11dy = twice_1mx_times_ym1over2 * xp1over2sq_times_yp1over2sq;
    const gfloat c11dxdy =
        xm1over2_times_ym1over2 * xp1over2sq_times_yp1over2sq;

    for (gint c = 0; c < channels; c++) {
      /*
       * Channel by channel computation of the new pixel values:
       */
      gfloat uno_one, uno_two, uno_thr, uno_fou;
      gfloat dos_one, dos_two, dos_thr, dos_fou;
      gfloat tre_one, tre_two, tre_thr, tre_fou;
      gfloat qua_one, qua_two, qua_thr, qua_fou;
      nohalo_subdivision(
          input_ptr[uno_two_shift + c], input_ptr[uno_thr_shift + c],
          input_ptr[uno_fou_shift + c], input_ptr[dos_one_shift + c],
          input_ptr[dos_two_shift + c], input_ptr[dos_thr_shift + c],
          input_ptr[dos_fou_shift + c], input_ptr[dos_fiv_shift + c],
          input_ptr[tre_one_shift + c], input_ptr[tre_two_shift + c],
          input_ptr[tre_thr_shift + c], input_ptr[tre_fou_shift + c],
          input_ptr[tre_fiv_shift + c], input_ptr[qua_one_shift + c],
          input_ptr[qua_two_shift + c], input_ptr[qua_thr_shift + c],
          input_ptr[qua_fou_shift + c], input_ptr[qua_fiv_shift + c],
          input_ptr[cin_two_shift + c], input_ptr[cin_thr_shift + c],
          input_ptr[cin_fou_shift + c], &uno_one, &uno_two, &uno_thr, &uno_fou,
          &dos_one, &dos_two, &dos_thr, &dos_fou, &tre_one, &tre_two, &tre_thr,
          &tre_fou, &qua_one, &qua_two, &qua_thr, &qua_fou);

      newval[c] = lbb(c00, c10, c01, c11, c00dx, c10dx, c01dx, c11dx, c00dy,
                      c10dy, c01dy, c11dy, c00dxdy, c10dxdy, c01dxdy, c11dxdy,
                      uno_one, uno_two, uno_thr, uno_fou, dos_one, dos_two,
                      dos_thr, dos_fou, tre_one, tre_two, tre_thr, tre_fou,
                      qua_one, qua_two, qua_thr, qua_fou);
    }

    {
      /*
       * If downsampling, blend LBB-Nohalo with clamped EWA (tent
       * filter) using ellipse parameters derived from Jinv.
       * 
       * Clamp the inverse Jacobian so the pullback ellipse contains
       * the unit disk: compute SVD of Jinv, clamp singular values to
       * >= 1, and keep the left singular vectors (V is irrelevant to
       * the ellipse).
       * 
       * Derive major/minor axis lengths and unit directions used for
       * ellipse distance and weighting.
       */
      const gdouble a = scale ? scale->coeff[0][0] : 1;
      const gdouble b = scale ? scale->coeff[0][1] : 0;
      const gdouble c = scale ? scale->coeff[1][0] : 0;
      const gdouble d = scale ? scale->coeff[1][1] : 1;

      /*
       * Computations are done in double precision because "direct"
       * SVD computations are prone to round off error. (Computing in
       * single precision most likely would be fine.)
       * 
       * n is the matrix Jinv * transpose(Jinv). Eigenvalues of n are
       * the squares of the singular values of Jinv.
       */
      const gdouble aa = a * a;
      const gdouble bb = b * b;
      const gdouble cc = c * c;
      const gdouble dd = d * d;
      /*
       * Eigenvectors of n are left singular vectors of Jinv.
       */
      const gdouble n11 = aa + bb;
      const gdouble n12 = a * c + b * d;
      const gdouble n21 = n12;
      const gdouble n22 = cc + dd;
      const double det = a * d - b * c;
      const double twice_det = det + det;
      const double frobenius_squared = n11 + n22;
      const double discriminant =
          (frobenius_squared + twice_det) * (frobenius_squared - twice_det);
      /*
       * Clamp negative discriminant to zero to avoid FP artifacts.
       */
      const double sqrt_discriminant =
          discriminant > 0. ? sqrt(discriminant) : 0.;

      /*
       * Initially, we only compute the squares of the singular
       * values.
       * 
       * s1 is the largest singular value of the inverse Jacobian
       * matrix. In other words, its reciprocal is the smallest
       * singular value of the Jacobian matrix itself.  If s1 = 0,
       * both singular values are 0, and any orthogonal pair of left
       * and right factors produces a singular decomposition of Jinv.
       */
      const gdouble twice_s1s1 = frobenius_squared + sqrt_discriminant;
      /*
       * If s1 <= 1, the forward transformation is not downsampling in
       * any direction, and consequently we do not need the
       * downsampling scheme at all.
       */

      if (twice_s1s1 > (gdouble)2.) {
        /*
         * Downsampling path: include the teepee (EWA) component.
         */
        const gdouble s1s1 = (gdouble)0.5 * twice_s1s1;
        /*
         * s2 the smallest singular value of the inverse Jacobian
         * matrix. Its reciprocal is the largest singular value of
         * the Jacobian matrix itself.
         */
        const gdouble s2s2 = 0.5 * (frobenius_squared - sqrt_discriminant);

        const gdouble s1s1minusn11 = s1s1 - n11;
        const gdouble s1s1minusn22 = s1s1 - n22;
        /*
         * u1, the first column of the U factor of a singular
         * decomposition of Jinv, is a (non-normalized) left
         * singular vector corresponding to s1. It has entries u11
         * and u21. We compute u1 from the fact that it is an
         * eigenvector of n corresponding to the eigenvalue s1^2.
         */
        const gdouble s1s1minusn11_squared = s1s1minusn11 * s1s1minusn11;
        const gdouble s1s1minusn22_squared = s1s1minusn22 * s1s1minusn22;
        /*
         * Build the eigenvector from the largest row of (n - s1^2 I).
         * If the norm is zero, fall back to [1,0].
         */
        const gdouble temp_u11 =
            s1s1minusn11_squared >= s1s1minusn22_squared ? n12 : s1s1minusn22;
        const gdouble temp_u21 =
            s1s1minusn11_squared >= s1s1minusn22_squared ? s1s1minusn11 : n21;
        const gdouble norm = sqrt(temp_u11 * temp_u11 + temp_u21 * temp_u21);
        /*
         * Finalize the entries of first left singular vector
         * (associated with the largest singular value).
         */
        const gdouble u11 =
            norm > (gdouble)0.0 ? temp_u11 / norm : (gdouble)1.0;
        const gdouble u21 =
            norm > (gdouble)0.0 ? temp_u21 / norm : (gdouble)0.0;
        /*
         * Clamp the singular values up to 1:
         */
        const gdouble major_mag =
            s1s1 <= (gdouble)1.0 ? (gdouble)1.0 : sqrt(s1s1);
        const gdouble minor_mag =
            s2s2 <= (gdouble)1.0 ? (gdouble)1.0 : sqrt(s2s2);
        /*
         * Unit major and minor axis direction vectors:
         */
        const gdouble major_unit_x = u11;
        const gdouble major_unit_y = u21;
        const gdouble minor_unit_x = -u21;
        const gdouble minor_unit_y = u11;

        /*
         * Project (s,t) onto ellipse axes to get normalized distance.
         */
        const gfloat c_major_x = major_unit_x / major_mag;
        const gfloat c_major_y = major_unit_y / major_mag;
        const gfloat c_minor_x = minor_unit_x / minor_mag;
        const gfloat c_minor_y = minor_unit_y / minor_mag;

        /*
         * Remainder of the ellipse geometry computation:
         */
        /*
         * Major and minor axis direction vectors:
         */
        const gdouble major_x = major_mag * major_unit_x;
        const gdouble major_y = major_mag * major_unit_y;
        const gdouble minor_x = minor_mag * minor_unit_x;
        const gdouble minor_y = minor_mag * minor_unit_y;

        /*
         * Ellipse coefficients:
         */
        const gdouble ellipse_a = major_y * major_y + minor_y * minor_y;
        const gdouble folded_ellipse_b = major_x * major_y + minor_x * minor_y;
        const gdouble ellipse_c = major_x * major_x + minor_x * minor_x;
        const gdouble ellipse_f = major_mag * minor_mag;

        /*
         * Bounding box of the ellipse:
         */
        const gdouble bounding_box_factor =
            ellipse_f * ellipse_f /
            (ellipse_c * ellipse_a - folded_ellipse_b * folded_ellipse_b);
        const gfloat bounding_box_half_width =
            sqrtf((gfloat)(ellipse_c * bounding_box_factor));
        const gfloat bounding_box_half_height =
            sqrtf((gfloat)(ellipse_a * bounding_box_factor));

        /*
         * Relative weight of the contribution of LBB-Nohalo:
         */
        const gfloat theta = (gfloat)((gdouble)1. / ellipse_f);

        /*
         * Grab the pixel values located within the level 0
         * context_rect.
         */
        const gint out_left_0 =
            NOHALO_MAX((gint)int_ceilf((float)(x_0 - bounding_box_half_width)),
                       -NOHALO_OFFSET_0);
        const gint out_rite_0 =
            NOHALO_MIN((gint)int_floorf((float)(x_0 + bounding_box_half_width)),
                       NOHALO_OFFSET_0);
        const gint out_top_0 =
            NOHALO_MAX((gint)int_ceilf((float)(y_0 - bounding_box_half_height)),
                       -NOHALO_OFFSET_0);
        const gint out_bot_0 = NOHALO_MIN(
            (gint)int_floorf((float)(y_0 + bounding_box_half_height)),
            NOHALO_OFFSET_0);

        /*
         * Accumulator for the EWA weights:
         */
        gdouble total_weight = (gdouble)0.0;
        /*
         * Storage for the EWA contribution:
         */
        gfloat ewa_newval[channels];
        for (gint c = 0; c < channels; c++)
          ewa_newval[c] = (gfloat)0;

        {
          gint i = out_top_0;
          do {
            gint j = out_left_0;
            do {
              ewa_update(j, i, c_major_x, c_major_y, c_minor_x, c_minor_y, x_0,
                         y_0, channels, row_skip, input_ptr, &total_weight,
                         ewa_newval);
            } while (++j <= out_rite_0);
          } while (++i <= out_bot_0);
        }

        {
          {
            /*
             * Blend the LBB-Nohalo and EWA results:
             */
            const gfloat beta = (gfloat)(((gdouble)1.0 - theta) / total_weight);
            for (gint c = 0; c < channels; c++)
              newval[c] = theta * newval[c] + beta * ewa_newval[c];
          }
        }
      }

      /*
       * Ship out the result:
       */
      {
        gfloat *out = (gfloat *)output;
        for (gint c = 0; c < channels; c++)
          out[c] = newval[c];
      }
      return;
    }
  }
}

static inline double sigmoidal(const double p) {
  /*
   * Only used to compute compile-time constants, so efficiency is
   * irrelevant.
   */
  return tanh(0.5 * LOHALO_CONTRAST * (p - 0.5));
}

static inline float sigmoidalf(const float p) {
  /*
   * Cheaper runtime version.
   */
  return tanhf((gfloat)(0.5 * LOHALO_CONTRAST) * p +
               (gfloat)(-0.25 * LOHALO_CONTRAST));
}

static inline gfloat extended_sigmoidal(const gfloat q) {
  /*
   * This function extends the standard sigmoidal with straight lines
   * at p=0 and p=1, in such a way that there is no value or slope
   * discontinuity.
   */
  const gdouble sig1 = sigmoidal(1.);
  const gdouble slope = (1. / sig1 - sig1) * 0.25 * LOHALO_CONTRAST;

  const gfloat slope_times_q = (gfloat)slope * q;

  if (q <= (gfloat)0.)
    return slope_times_q;

  if (q >= (gfloat)1.)
    return slope_times_q + (gfloat)(1. - slope);

  {
    const gfloat p = (float)(0.5 / sig1) * sigmoidalf((float)q) + (float)0.5;
    return p;
  }
}

static inline gfloat inverse_sigmoidal(const gfloat p) {
  /*
   * This function is the inverse of extended_sigmoidal above.
   */
  const gdouble sig1 = sigmoidal(1.);
  const gdouble sig0 = -sig1;
  const gdouble slope = (1. / sig1 + sig0) * 0.25 * LOHALO_CONTRAST;
  const gdouble one_over_slope = 1. / slope;

  const gfloat p_over_slope = p * (gfloat)one_over_slope;

  if (p <= (gfloat)0.)
    return p_over_slope;

  if (p >= (gfloat)1.)
    return p_over_slope + (gfloat)(1. - one_over_slope);

  {
    const gfloat ssq = (gfloat)(2. * sig1) * p + (gfloat)sig0;
    const gfloat q = (float)(2. / LOHALO_CONTRAST) * atanhf(ssq) + (float)0.5;
    return q;
  }
}

static inline gfloat robidoux(const gfloat c_major_x, const gfloat c_major_y,
                              const gfloat c_minor_x, const gfloat c_minor_y,
                              const gfloat s, const gfloat t) {
  /*
   * Compute a scaled Robidoux (Keys) cubic weight for EWA. The scale
   * removes one multiply; weights are normalized later.
   */
  const gfloat q1 = s * c_major_x + t * c_major_y;
  const gfloat q2 = s * c_minor_x + t * c_minor_y;

  const gfloat r2 = q1 * q1 + q2 * q2;

  if (r2 >= (gfloat)4.)
    return (gfloat)0.;

  {
    const gfloat r = sqrtf((float)r2);

    const gfloat minus_inner_root =
        (-103. - 36. * sqrt(2.)) / (7. + 72. * sqrt(2.));
    const gfloat minus_outer_root = -2.;

    const gfloat a3 = -3.;
    const gfloat a2 = (45739. + 7164. * sqrt(2.)) / 10319.;
    const gfloat a0 = (-8926. + -14328. * sqrt(2.)) / 10319.;

    const gfloat weight = (r2 >= (float)1.) ? (r + minus_inner_root) *
                                                  (r + minus_outer_root) *
                                                  (r + minus_outer_root)
                                            : r2 * (a3 * r + a2) + a0;

    return weight;
  }
}

static inline void
ewa_update_lohalo(const gint j, const gint i, const gfloat c_major_x,
                  const gfloat c_major_y, const gfloat c_minor_x,
                  const gfloat c_minor_y, const gfloat x_0, const gfloat y_0,
                  const gint channels, const gint row_skip,
                  const gfloat *restrict input_ptr,
                  gdouble *restrict total_weight, gfloat *restrict ewa_newval) {
  const gint skip = j * channels + i * row_skip;
  gint c;
  const gfloat weight = robidoux(c_major_x, c_major_y, c_minor_x, c_minor_y,
                                 x_0 - (gfloat)j, y_0 - (gfloat)i);

  *total_weight += weight;
  for (c = 0; c < channels; c++)
    ewa_newval[c] += weight * input_ptr[skip + c];
}

static void resize_lohalo_get(ResizeSampler *restrict self,
                              const gdouble absolute_x,
                              const gdouble absolute_y,
                              ResizeScaleMatrix *scale, void *restrict output,
                              ResizeAbyssPolicy repeat_mode) {
  /*
   * Needed constants related to the input pixel value pointer
   * provided by resize_sampler_get_ptr (self, ix, iy). pixels_per_row
   * corresponds to fetch_rectangle.width in resize_sampler_get_ptr.
   */
  const gint channels = self->interpolate_components;
  const gint pixels_per_row = RESIZE_CONTEXT_MAX;
  const gint row_skip = channels * pixels_per_row;

  /*
   * Pick the nearest pixel center in corner-based coordinates.
   */
  const gint ix_0 = floor((double)absolute_x);
  const gint iy_0 = floor((double)absolute_y);

  /*
   * Nearest-anchor choice is not optimal for tensor bicubic but keeps
   * the stencil symmetric and simple.
   */

  /*
   * This is the pointer we use to pull pixel from "base" mipmap level
   * (level "0"), the one with scale=1.0.
   */
  const gfloat *restrict input_ptr = (gfloat *)resize_sampler_get_ptr(
      self, ix_0, iy_0, LOHALO_OFFSET_0, repeat_mode);

  /*
   * Convert from corner-based to center-based coordinates (-0.5).
   */
  const gdouble iabsolute_x = absolute_x - (gdouble)0.5;
  const gdouble iabsolute_y = absolute_y - (gdouble)0.5;

  /*
   * (x_0,y_0) is the relative position of the sampling location
   * w.r.t. the anchor pixel.
   */
  const gfloat x_0 = iabsolute_x - ix_0;
  const gfloat y_0 = iabsolute_y - iy_0;

  const gint sign_of_x_0 = 2 * (x_0 >= (gfloat)0.) - 1;
  const gint sign_of_y_0 = 2 * (y_0 >= (gfloat)0.) - 1;

  const gint shift_forw_1_pix = sign_of_x_0 * channels;
  const gint shift_forw_1_row = sign_of_y_0 * row_skip;

  const gint shift_back_1_pix = -shift_forw_1_pix;
  const gint shift_back_1_row = -shift_forw_1_row;

  const gint shift_forw_2_pix = 2 * shift_forw_1_pix;
  const gint shift_forw_2_row = 2 * shift_forw_1_row;

  const gint uno_one_shift = shift_back_1_pix + shift_back_1_row;
  const gint uno_two_shift = shift_back_1_row;
  const gint uno_thr_shift = shift_forw_1_pix + shift_back_1_row;
  const gint uno_fou_shift = shift_forw_2_pix + shift_back_1_row;

  const gint dos_one_shift = shift_back_1_pix;
  const gint dos_two_shift = 0;
  const gint dos_thr_shift = shift_forw_1_pix;
  const gint dos_fou_shift = shift_forw_2_pix;

  const gint tre_one_shift = shift_back_1_pix + shift_forw_1_row;
  const gint tre_two_shift = shift_forw_1_row;
  const gint tre_thr_shift = shift_forw_1_pix + shift_forw_1_row;
  const gint tre_fou_shift = shift_forw_2_pix + shift_forw_1_row;

  const gint qua_one_shift = shift_back_1_pix + shift_forw_2_row;
  const gint qua_two_shift = shift_forw_2_row;
  const gint qua_thr_shift = shift_forw_1_pix + shift_forw_2_row;
  const gint qua_fou_shift = shift_forw_2_pix + shift_forw_2_row;

  /*
   * Flip coordinates so we can assume we are in the interval [0,1].
   */
  const gfloat ax = x_0 >= (gfloat)0. ? x_0 : -x_0;
  const gfloat ay = y_0 >= (gfloat)0. ? y_0 : -y_0;
  /*
   * Compute Mitchell-Netravali weights via a 13-flop method.
   */
  const gfloat xt1 = (gfloat)(7. / 18.) * ax;
  const gfloat yt1 = (gfloat)(7. / 18.) * ay;
  const gfloat xt2 = (gfloat)1. - ax;
  const gfloat yt2 = (gfloat)1. - ay;
  const gfloat fou = (xt1 + (gfloat)(-1. / 3.)) * ax * ax;
  const gfloat qua = (yt1 + (gfloat)(-1. / 3.)) * ay * ay;
  const gfloat one = ((gfloat)(1. / 18.) - xt1) * xt2 * xt2;
  const gfloat uno = ((gfloat)(1. / 18.) - yt1) * yt2 * yt2;
  const gfloat xt3 = fou - one;
  const gfloat yt3 = qua - uno;
  const gfloat thr = ax - fou - xt3;
  const gfloat tre = ay - qua - yt3;
  const gfloat two = xt2 - one + xt3;
  const gfloat dos = yt2 - uno + yt3;
  gint c;
  /*
   * The newval array will contain one computed resampled value per
   * channel:
   */
  gfloat newval[channels];
  for (c = 0; c < channels - 1; c++)
    newval[c] = extended_sigmoidal(
        uno * (one * inverse_sigmoidal(input_ptr[uno_one_shift + c]) +
               two * inverse_sigmoidal(input_ptr[uno_two_shift + c]) +
               thr * inverse_sigmoidal(input_ptr[uno_thr_shift + c]) +
               fou * inverse_sigmoidal(input_ptr[uno_fou_shift + c])) +
        dos * (one * inverse_sigmoidal(input_ptr[dos_one_shift + c]) +
               two * inverse_sigmoidal(input_ptr[dos_two_shift + c]) +
               thr * inverse_sigmoidal(input_ptr[dos_thr_shift + c]) +
               fou * inverse_sigmoidal(input_ptr[dos_fou_shift + c])) +
        tre * (one * inverse_sigmoidal(input_ptr[tre_one_shift + c]) +
               two * inverse_sigmoidal(input_ptr[tre_two_shift + c]) +
               thr * inverse_sigmoidal(input_ptr[tre_thr_shift + c]) +
               fou * inverse_sigmoidal(input_ptr[tre_fou_shift + c])) +
        qua * (one * inverse_sigmoidal(input_ptr[qua_one_shift + c]) +
               two * inverse_sigmoidal(input_ptr[qua_two_shift + c]) +
               thr * inverse_sigmoidal(input_ptr[qua_thr_shift + c]) +
               fou * inverse_sigmoidal(input_ptr[qua_fou_shift + c])));
  /*
   * It appears that it is a bad idea to sigmoidize the transparency
   * channel (in RaGaBaA, at least). So don't.
   */
  newval[channels - 1] = uno * (one * input_ptr[uno_one_shift + channels - 1] +
                                two * input_ptr[uno_two_shift + channels - 1] +
                                thr * input_ptr[uno_thr_shift + channels - 1] +
                                fou * input_ptr[uno_fou_shift + channels - 1]) +
                         dos * (one * input_ptr[dos_one_shift + channels - 1] +
                                two * input_ptr[dos_two_shift + channels - 1] +
                                thr * input_ptr[dos_thr_shift + channels - 1] +
                                fou * input_ptr[dos_fou_shift + channels - 1]) +
                         tre * (one * input_ptr[tre_one_shift + channels - 1] +
                                two * input_ptr[tre_two_shift + channels - 1] +
                                thr * input_ptr[tre_thr_shift + channels - 1] +
                                fou * input_ptr[tre_fou_shift + channels - 1]) +
                         qua * (one * input_ptr[qua_one_shift + channels - 1] +
                                two * input_ptr[qua_two_shift + channels - 1] +
                                thr * input_ptr[qua_thr_shift + channels - 1] +
                                fou * input_ptr[qua_fou_shift + channels - 1]);

  {
    /*
     * If downsampling, blend Mitchell-Netravali with clamped EWA
     * (Robidoux) by deriving the ellipse from Jinv.
     * 
     * Clamp the inverse Jacobian so the pullback ellipse contains
     * the unit disk: compute SVD of Jinv, clamp singular values to
     * >= 1, and keep the left singular vectors (V is irrelevant).
     * 
     * Derive major/minor axis lengths and unit directions used for
     * ellipse distance and weighting.
     * 
     * Use the scale object if defined. Otherwise, assume that the
     * inverse Jacobian matrix is the identity.
     */
    const gdouble a = scale ? scale->coeff[0][0] : 1;
    const gdouble b = scale ? scale->coeff[0][1] : 0;
    const gdouble c = scale ? scale->coeff[1][0] : 0;
    const gdouble d = scale ? scale->coeff[1][1] : 1;

    /*
     * Computations are done in double precision because "direct"
     * SVD computations are prone to round off error. (Computing in
     * single precision most likely would be fine.)
     * 
     * n is the matrix Jinv * transpose(Jinv). Eigenvalues of n are
     * the squares of the singular values of Jinv.
     */
    const gdouble aa = a * a;
    const gdouble bb = b * b;
    const gdouble cc = c * c;
    const gdouble dd = d * d;
    /*
     * Eigenvectors of n are left singular vectors of Jinv.
     */
    const gdouble n11 = aa + bb;
    const gdouble n12 = a * c + b * d;
    const gdouble n21 = n12;
    const gdouble n22 = cc + dd;
    const gdouble det = a * d - b * c;
    const gdouble twice_det = det + det;
    const gdouble frobenius_squared = n11 + n22;
    const gdouble discriminant =
        (frobenius_squared + twice_det) * (frobenius_squared - twice_det);
    /*
     * Clamp negative discriminant to zero to avoid FP artifacts.
     */
    const gdouble sqrt_discriminant =
        sqrt(discriminant > 0. ? discriminant : 0.);

    /*
     * Initially, we only compute the squares of the singular
     * values.
     * 
     * s1 is the largest singular value of the inverse Jacobian
     * matrix. In other words, its reciprocal is the smallest
     * singular value of the Jacobian matrix itself.  If s1 = 0,
     * both singular values are 0, and any orthogonal pair of left
     * and right factors produces a singular decomposition of Jinv.
     */
    const gdouble twice_s1s1 = frobenius_squared + sqrt_discriminant;
    /*
     * If s1 <= 1, the forward transformation is not downsampling in
     * any direction, and consequently we do not need the
     * downsampling scheme at all.
     * 
     * Following now done by arithmetic branching.
     */
    // if (twice_s1s1 > (gdouble) 2.)
    {
      /*
       * Downsampling path: include the EWA component.
       */
      const gdouble s1s1 = (gdouble)0.5 * twice_s1s1;
      /*
       * s2 the smallest singular value of the inverse Jacobian
       * matrix. Its reciprocal is the largest singular value of
       * the Jacobian matrix itself.
       */
      const gdouble s2s2 = 0.5 * (frobenius_squared - sqrt_discriminant);

      const gdouble s1s1minusn11 = s1s1 - n11;
      const gdouble s1s1minusn22 = s1s1 - n22;
      /*
       * u1, the first column of the U factor of a singular
       * decomposition of Jinv, is a (non-normalized) left
       * singular vector corresponding to s1. It has entries u11
       * and u21. We compute u1 from the fact that it is an
       * eigenvector of n corresponding to the eigenvalue s1^2.
       */
      const gdouble s1s1minusn11_squared = s1s1minusn11 * s1s1minusn11;
      const gdouble s1s1minusn22_squared = s1s1minusn22 * s1s1minusn22;
      /*
       * Build the eigenvector from the largest row of (n - s1^2 I).
       * If the norm is zero, fall back to [1,0].
       */
      const gdouble temp_u11 =
          s1s1minusn11_squared >= s1s1minusn22_squared ? n12 : s1s1minusn22;
      const gdouble temp_u21 =
          s1s1minusn11_squared >= s1s1minusn22_squared ? s1s1minusn11 : n21;
      const gdouble norm = sqrt(temp_u11 * temp_u11 + temp_u21 * temp_u21);
      /*
       * Finalize the entries of first left singular vector
       * (associated with the largest singular value).
       */
      const gdouble u11 = norm > (gdouble)0.0 ? temp_u11 / norm : (gdouble)1.0;
      const gdouble u21 = norm > (gdouble)0.0 ? temp_u21 / norm : (gdouble)0.0;
      /*
       * Clamp the singular values up to 1:
       */
      const gdouble major_mag =
          s1s1 <= (gdouble)1.0 ? (gdouble)1.0 : sqrt(s1s1);
      const gdouble minor_mag =
          s2s2 <= (gdouble)1.0 ? (gdouble)1.0 : sqrt(s2s2);
      /*
       * Unit major and minor axis direction vectors:
       */
      const gdouble major_unit_x = u11;
      const gdouble major_unit_y = u21;
      const gdouble minor_unit_x = -u21;
      const gdouble minor_unit_y = u11;

      /*
       * Project (s,t) onto ellipse axes to get normalized distance.
       */
      const gfloat c_major_x = major_unit_x / major_mag;
      const gfloat c_major_y = major_unit_y / major_mag;
      const gfloat c_minor_x = minor_unit_x / minor_mag;
      const gfloat c_minor_y = minor_unit_y / minor_mag;

      /*
       * Remainder of the ellipse geometry computation:
       
       * Major and minor axis direction vectors:
       */
      const gdouble major_x = major_mag * major_unit_x;
      const gdouble major_y = major_mag * major_unit_y;
      const gdouble minor_x = minor_mag * minor_unit_x;
      const gdouble minor_y = minor_mag * minor_unit_y;

      /*
       * Ellipse coefficients:
       */
      const gdouble ellipse_a = major_y * major_y + minor_y * minor_y;
      const gdouble folded_ellipse_b = major_x * major_y + minor_x * minor_y;
      const gdouble ellipse_c = major_x * major_x + minor_x * minor_x;
      const gdouble ellipse_f = major_mag * minor_mag;

      /*
       * ewa_radius is the unscaled radius, which here is 2
       * because we use EWA Robidoux, which is based on a Keys
       * cubic.
       */
      const gfloat ewa_radius = 2.;
      /*
       * Bounding box of the ellipse:
       */
      const gdouble bounding_box_factor =
          ellipse_f * ellipse_f /
          (ellipse_c * ellipse_a - folded_ellipse_b * folded_ellipse_b);
      const gfloat bounding_box_half_width =
          ewa_radius * sqrtf((gfloat)(ellipse_c * bounding_box_factor));
      const gfloat bounding_box_half_height =
          ewa_radius * sqrtf((gfloat)(ellipse_a * bounding_box_factor));

      /*
       * Relative weight of the contribution of
       * Mitchell-Netravali:
       */
      const gfloat theta = (gfloat)((gdouble)1. / ellipse_f);

      /*
       * Grab the pixel values located within the level 0
       * context_rect.
       */
      const gint out_left_0 =
          LOHALO_MAX((gint)int_ceilf((float)(x_0 - bounding_box_half_width)),
                     -LOHALO_OFFSET_0);
      const gint out_rite_0 =
          LOHALO_MIN((gint)int_floorf((float)(x_0 + bounding_box_half_width)),
                     LOHALO_OFFSET_0);
      const gint out_top_0 =
          LOHALO_MAX((gint)int_ceilf((float)(y_0 - bounding_box_half_height)),
                     -LOHALO_OFFSET_0);
      const gint out_bot_0 =
          LOHALO_MIN((gint)int_floorf((float)(y_0 + bounding_box_half_height)),
                     LOHALO_OFFSET_0);

      /*
       * Accumulator for the EWA weights:
       */
      gdouble total_weight = (gdouble)0.0;
      /*
       * Storage for the EWA contribution:
       */
      gfloat ewa_newval[channels];
      ewa_newval[0] = (gfloat)0;
      ewa_newval[1] = (gfloat)0;
      ewa_newval[2] = (gfloat)0;
      ewa_newval[3] = (gfloat)0;

      {
        gint i = out_top_0;
        do {
          gint j = out_left_0;
          do {
            ewa_update_lohalo(j, i, c_major_x, c_major_y, c_minor_x, c_minor_y,
                              x_0, y_0, channels, row_skip, input_ptr,
                              &total_weight, ewa_newval);
          } while (++j <= out_rite_0);
        } while (++i <= out_bot_0);
      }

      {
        /*
         * Blend the sigmoidized Mitchell-Netravali and EWA Robidoux
         * results:
         */
        const gfloat beta =
            twice_s1s1 > (gdouble)2.
                ? (gfloat)(((gdouble)1.0 - theta) / total_weight)
                : (gfloat)0.;
        const gfloat newtheta = twice_s1s1 > (gdouble)2. ? theta : (gfloat)1.;
        gint c;
        for (c = 0; c < channels; c++)
          newval[c] = newtheta * newval[c] + beta * ewa_newval[c];
      }
    }

    /*
     * Ship out the result:
     */
    {
      gfloat *out = (gfloat *)output;
      for (gint c = 0; c < channels; c++)
        out[c] = newval[c];
    }
    return;
  }
}

static void resize_nearest_rgba8(const uint8_t *src, int src_w, int src_h,
                                 uint8_t *dst, int dst_w, int dst_h) {
  float x_ratio = (float)src_w / (float)dst_w;
  float y_ratio = (float)src_h / (float)dst_h;

  for (int y = 0; y < dst_h; ++y) {
    int sy = (int)(y * y_ratio);
    sy = CLAMP_INT(sy, 0, src_h - 1);

    for (int x = 0; x < dst_w; ++x) {
      int sx = (int)(x * x_ratio);
      sx = CLAMP_INT(sx, 0, src_w - 1);

      const uint8_t *sp = src + (sy * src_w + sx) * 4;
      uint8_t *dp = dst + (y * dst_w + x) * 4;

      dp[0] = sp[0];
      dp[1] = sp[1];
      dp[2] = sp[2];
      dp[3] = sp[3];
    }
  }
}

static void resize_bilinear_rgba8(const uint8_t *src, int src_w, int src_h,
                                  uint8_t *dst, int dst_w, int dst_h) {
  float x_ratio = (float)src_w / (float)dst_w;
  float y_ratio = (float)src_h / (float)dst_h;

  for (int y = 0; y < dst_h; ++y) {
    float fy = (y + 0.5f) * y_ratio - 0.5f;
    int y0 = (int)floorf(fy);
    int y1 = y0 + 1;
    float ty = fy - y0;

    y0 = CLAMP_INT(y0, 0, src_h - 1);
    y1 = CLAMP_INT(y1, 0, src_h - 1);

    for (int x = 0; x < dst_w; ++x) {
      float fx = (x + 0.5f) * x_ratio - 0.5f;
      int x0 = (int)floorf(fx);
      int x1 = x0 + 1;
      float tx = fx - x0;

      x0 = CLAMP_INT(x0, 0, src_w - 1);
      x1 = CLAMP_INT(x1, 0, src_w - 1);

      const uint8_t *p00 = src + (y0 * src_w + x0) * 4;
      const uint8_t *p10 = src + (y0 * src_w + x1) * 4;
      const uint8_t *p01 = src + (y1 * src_w + x0) * 4;
      const uint8_t *p11 = src + (y1 * src_w + x1) * 4;
      uint8_t *dp = dst + (y * dst_w + x) * 4;

      for (int c = 0; c < 4; ++c) {
        float a = u8_to_float(p00[c]) +
                  (u8_to_float(p10[c]) - u8_to_float(p00[c])) * tx;
        float b = u8_to_float(p01[c]) +
                  (u8_to_float(p11[c]) - u8_to_float(p01[c])) * tx;
        float v = a + (b - a) * ty;
        dp[c] = float_to_u8(v);
      }
    }
  }
}

static void resize_cubic_rgba8(const uint8_t *src, int src_w, int src_h,
                               uint8_t *dst, int dst_w, int dst_h) {
  const float b = 0.5f;
  const float c = 0.5f * (1.0f - b);
  float x_ratio = (float)src_w / (float)dst_w;
  float y_ratio = (float)src_h / (float)dst_h;

  for (int y = 0; y < dst_h; ++y) {
    double absolute_y = (y + 0.5) * y_ratio;
    double iabsolute_y = absolute_y - 0.5;
    int iy = int_floorf((float)iabsolute_y);
    float fy = (float)(iabsolute_y - iy);

    for (int x = 0; x < dst_w; ++x) {
      double absolute_x = (x + 0.5) * x_ratio;
      double iabsolute_x = absolute_x - 0.5;
      int ix = int_floorf((float)iabsolute_x);
      float fx = (float)(iabsolute_x - ix);

      float out[4] = {0.f, 0.f, 0.f, 0.f};

      for (int j = 0; j < 4; ++j) {
        float wy = cubicKernel(fy - (j - 1), b, c);
        int sy = CLAMP_INT(iy + (j - 1), 0, src_h - 1);
        for (int i = 0; i < 4; ++i) {
          float wx = cubicKernel(fx - (i - 1), b, c);
          int sx = CLAMP_INT(ix + (i - 1), 0, src_w - 1);
          const uint8_t *sp = src + (sy * src_w + sx) * 4;
          float w = wx * wy;
          out[0] += w * u8_to_float(sp[0]);
          out[1] += w * u8_to_float(sp[1]);
          out[2] += w * u8_to_float(sp[2]);
          out[3] += w * u8_to_float(sp[3]);
        }
      }

      uint8_t *dp = dst + (y * dst_w + x) * 4;
      dp[0] = float_to_u8(out[0]);
      dp[1] = float_to_u8(out[1]);
      dp[2] = float_to_u8(out[2]);
      dp[3] = float_to_u8(out[3]);
    }
  }
}

static void resize_nohalo_rgba8(const uint8_t *src, int src_w, int src_h,
                                uint8_t *dst, int dst_w, int dst_h) {
  ResizeSampler sampler = {0};
  ResizeScaleMatrix scale = {0};
  const double x_ratio = (double)src_w / (double)dst_w;
  const double y_ratio = (double)src_h / (double)dst_h;

  sampler.src = src;
  sampler.src_w = src_w;
  sampler.src_h = src_h;
  sampler.interpolate_components = 4;

  scale.coeff[0][0] = x_ratio;
  scale.coeff[0][1] = 0.0;
  scale.coeff[1][0] = 0.0;
  scale.coeff[1][1] = y_ratio;

  for (int y = 0; y < dst_h; ++y) {
    double absolute_y = (y + 0.5) * y_ratio;
    for (int x = 0; x < dst_w; ++x) {
      double absolute_x = (x + 0.5) * x_ratio;
      float out[4] = {0.f, 0.f, 0.f, 0.f};
      resize_nohalo_get(&sampler, absolute_x, absolute_y, &scale, out,
                        RESIZE_ABYSS_CLAMP);
      uint8_t *dp = dst + (y * dst_w + x) * 4;
      dp[0] = float_to_u8(out[0]);
      dp[1] = float_to_u8(out[1]);
      dp[2] = float_to_u8(out[2]);
      dp[3] = float_to_u8(out[3]);
    }
  }
}

static void resize_lohalo_rgba8(const uint8_t *src, int src_w, int src_h,
                                uint8_t *dst, int dst_w, int dst_h) {
  ResizeSampler sampler = {0};
  ResizeScaleMatrix scale = {0};
  const double x_ratio = (double)src_w / (double)dst_w;
  const double y_ratio = (double)src_h / (double)dst_h;

  sampler.src = src;
  sampler.src_w = src_w;
  sampler.src_h = src_h;
  sampler.interpolate_components = 4;

  scale.coeff[0][0] = x_ratio;
  scale.coeff[0][1] = 0.0;
  scale.coeff[1][0] = 0.0;
  scale.coeff[1][1] = y_ratio;

  for (int y = 0; y < dst_h; ++y) {
    double absolute_y = (y + 0.5) * y_ratio;
    for (int x = 0; x < dst_w; ++x) {
      double absolute_x = (x + 0.5) * x_ratio;
      float out[4] = {0.f, 0.f, 0.f, 0.f};
      resize_lohalo_get(&sampler, absolute_x, absolute_y, &scale, out,
                        RESIZE_ABYSS_CLAMP);
      uint8_t *dp = dst + (y * dst_w + x) * 4;
      dp[0] = float_to_u8(out[0]);
      dp[1] = float_to_u8(out[1]);
      dp[2] = float_to_u8(out[2]);
      dp[3] = float_to_u8(out[3]);
    }
  }
}

uint8_t *ps_img_resample_rgba8(const uint8_t *src, int src_width,
                               int src_height, int dst_width, int dst_height,
                               PsImgResampleInterpolation interpolation) {
  if (!src || src_width <= 0 || src_height <= 0 || dst_width <= 0 ||
      dst_height <= 0)
    return NULL;

  size_t out_size = (size_t)dst_width * (size_t)dst_height * 4u;
  uint8_t *dst = (uint8_t *)malloc(out_size);
  if (!dst)
    return NULL;

  if (src_width == dst_width && src_height == dst_height) {
    memcpy(dst, src, out_size);
    return dst;
  }

  switch (interpolation) {
  case PS_IMG_RESAMPLE_INTERP_NONE:
    resize_nearest_rgba8(src, src_width, src_height, dst, dst_width,
                         dst_height);
    break;
  case PS_IMG_RESAMPLE_INTERP_LINEAR:
    resize_bilinear_rgba8(src, src_width, src_height, dst, dst_width,
                          dst_height);
    break;
  case PS_IMG_RESAMPLE_INTERP_CUBIC:
    resize_cubic_rgba8(src, src_width, src_height, dst, dst_width, dst_height);
    break;
  case PS_IMG_RESAMPLE_INTERP_NOHALO:
    resize_nohalo_rgba8(src, src_width, src_height, dst, dst_width, dst_height);
    break;
  case PS_IMG_RESAMPLE_INTERP_LOHALO:
    resize_lohalo_rgba8(src, src_width, src_height, dst, dst_width, dst_height);
    break;
  default:
    resize_bilinear_rgba8(src, src_width, src_height, dst, dst_width,
                          dst_height);
    break;
  }

  return dst;
}

#endif /* PS_ENABLE_MODULE_IMG */
