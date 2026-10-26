/**
 * @file    kalman.h
 * @brief   One-dimensional (scalar) Kalman filter.
 *
 * This module is pure, portable C: it touches no hardware, so it is
 * compiled both into the firmware and into the host-side unit test
 * (test/test_kalman.c, run with `make test`).
 *
 * MODEL
 * -----
 * We estimate one hidden quantity x (the die temperature, in C) from
 * noisy measurements z, assuming
 *
 *     process:      x[k] = x[k-1] + w[k],     w ~ N(0, Q)
 *     measurement:  z[k] = x[k]   + v[k],     v ~ N(0, R)
 *
 * i.e. "the temperature stays the same from one sample to the next, except
 * for a random nudge of variance Q", and "each reading is the truth plus
 * independent noise of variance R". This is called a *random walk* or
 * *local level* model.
 *
 * ALGORITHM (each step)
 * ---------------------
 *     predict:   x- = x                 (best guess: unchanged)
 *                P- = P + Q             (but we're less sure now)
 *
 *     update:    S  = P- + R            (expected spread of the innovation)
 *                K  = P- / S            (Kalman gain, 0..1)
 *                y  = z - x-            (innovation: the "surprise")
 *                x  = x- + K * y        (move toward z by fraction K)
 *                P  = (1 - K) * P-      (and we're more sure again)
 *
 * See docs/01-kalman-theory.md for where these equations come from and how
 * they generalise to the matrix form.
 *
 * UNITS
 * -----
 * x in C; P, Q and R in C^2 (variances). Q is *per step*: with a different
 * sample period you need a different Q for the same behaviour.
 */
#ifndef KALMAN_H
#define KALMAN_H

typedef struct {
    /* --- state --- */
    float x;   /* current estimate                                         */
    float p;   /* variance of that estimate: our uncertainty about x       */

    /* --- tuning --- */
    float q;   /* process noise variance per step (how fast truth drifts)  */
    float r;   /* measurement noise variance (how noisy each reading is)   */

    /* --- diagnostics from the most recent update() --- */
    float k;   /* Kalman gain that was applied                             */
    float y;   /* innovation z - x_prior                                   */
    float s;   /* innovation variance P_prior + R                          */
} kalman1d_t;

/**
 * Initialise the filter.
 * @param x0  initial estimate (e.g. the first measurement)
 * @param p0  its variance. Large = "I have no idea", so the first few
 *            measurements dominate; small = trust x0.
 */
void kalman1d_init(kalman1d_t *kf, float x0, float p0, float q, float r);

/** Time update: propagate the estimate one step forward. */
void kalman1d_predict(kalman1d_t *kf);

/** Measurement update: blend in measurement z. */
void kalman1d_update(kalman1d_t *kf, float z);

/** predict() followed by update(); returns the new estimate. */
float kalman1d_step(kalman1d_t *kf, float z);

/**
 * Gain the filter converges to for constant q and r (independent of the
 * initial P). Useful to predict behaviour before running it:
 * the filter then acts like an exponential moving average with this
 * smoothing factor, i.e. a time constant of about 1/K samples.
 */
float kalman1d_steady_state_gain(float q, float r);

/** Steady-state posterior variance P that goes with the gain above. */
float kalman1d_steady_state_variance(float q, float r);

#endif /* KALMAN_H */
