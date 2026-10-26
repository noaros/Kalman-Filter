/**
 * @file    kalman.c
 * @brief   Scalar Kalman filter implementation. See kalman.h for the model.
 *
 * Everything is single-precision float: the Cortex-M4F FPU does float
 * add/mul/div in hardware (1, 1 and 14 cycles), whereas double would be
 * emulated in software. The build uses -Wdouble-promotion to catch any
 * accidental double arithmetic (e.g. a literal written 0.5 instead of 0.5f).
 */
#include "kalman.h"

void kalman1d_init(kalman1d_t *kf, float x0, float p0, float q, float r)
{
    kf->x = x0;
    kf->p = p0;
    kf->q = q;
    kf->r = r;
    kf->k = 0.0f;
    kf->y = 0.0f;
    kf->s = p0 + r;
}

void kalman1d_predict(kalman1d_t *kf)
{
    /* State transition F = 1: the model predicts no change in x.
     * Uncertainty grows by the process noise: P- = F P F' + Q = P + Q. */
    kf->p += kf->q;
}

void kalman1d_update(kalman1d_t *kf, float z)
{
    /* Measurement matrix H = 1: we measure the state directly. */
    kf->y = z - kf->x;           /* innovation         y = z - H x-      */
    kf->s = kf->p + kf->r;       /* innovation var.    S = H P- H' + R   */
    kf->k = kf->p / kf->s;       /* gain               K = P- H' / S     */
    kf->x += kf->k * kf->y;      /* corrected state    x = x- + K y      */

    /* Covariance update. The textbook form is P = (1 - K H) P-. For a
     * scalar filter it's numerically fine; in matrix filters the "Joseph
     * form" P = (I-KH) P- (I-KH)' + K R K' is preferred because it stays
     * symmetric and positive under rounding. Here the two are equal:
     *   (1-K)^2 P- + K^2 R = (1-K) P-  when K = P-/(P-+R).              */
    kf->p = (1.0f - kf->k) * kf->p;
}

float kalman1d_step(kalman1d_t *kf, float z)
{
    kalman1d_predict(kf);
    kalman1d_update(kf, z);
    return kf->x;
}

/*
 * Steady state.
 *
 * Write P for the posterior variance after an update. In steady state,
 * one full predict+update cycle must map P to itself:
 *
 *     P = (P + Q) * R / (P + Q + R)
 *
 * Multiplying out gives the (scalar) discrete algebraic Riccati equation
 *
 *     P^2 + Q P - Q R = 0
 *
 * whose positive root is P = (-Q + sqrt(Q^2 + 4 Q R)) / 2.
 * The gain is then K = (P + Q) / (P + Q + R).
 *
 * __builtin_sqrtf compiles to the single VSQRT.F32 instruction on the M4F
 * (thanks to -fno-math-errno), so no libm is required.
 */
float kalman1d_steady_state_variance(float q, float r)
{
    return 0.5f * (-q + __builtin_sqrtf(q * q + 4.0f * q * r));
}

float kalman1d_steady_state_gain(float q, float r)
{
    const float p_prior = kalman1d_steady_state_variance(q, r) + q;
    return p_prior / (p_prior + r);
}
