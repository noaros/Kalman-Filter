/**
 * @file    test_kalman.c
 * @brief   Host-side tests for src/kalman.c.  Run with `make test`.
 *
 * Each test generates synthetic data with *known* truth and noise, runs
 * the filter, and checks a property that the theory predicts. Because the
 * noise generator is a fixed-seed PRNG, results are reproducible.
 *
 * What is checked:
 *   1. Constant truth: the estimate error is much smaller than the raw
 *      measurement noise (that's the whole point of filtering).
 *   2. The gain converges to the analytic steady-state value from the
 *      Riccati equation, regardless of the initial P.
 *   3. Consistency: the filter's own variance P matches the actual
 *      squared error it makes (it is neither over- nor under-confident).
 *   4. Q trade-off: a larger Q follows a ramp with less lag but passes
 *      more noise through.
 *   5. Reset: with a huge initial P the first update jumps (almost) all
 *      the way to the measurement.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kalman.h"

/* M_PI is POSIX, not ISO C, so -std=c11 hides it. */
#define PI 3.14159265358979323846

static int g_failures;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) {                                                   \
            printf("  ok    ");                                       \
        } else {                                                      \
            printf("  FAIL  ");                                       \
            g_failures++;                                             \
        }                                                             \
        printf(__VA_ARGS__);                                          \
        printf("\n");                                                 \
    } while (0)

/* ---- Deterministic Gaussian noise ------------------------------------ */

static uint64_t g_rng = 0x2545F4914F6CDD1DULL;

/* xorshift64*: small, fast, good enough for test noise. */
static double uniform01(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return (double)((g_rng * 0x2545F4914F6CDD1DULL) >> 11) * (1.0 / 9007199254740992.0);
}

/* Box-Muller transform: two uniforms -> one standard normal sample. */
static float gaussian(float sigma)
{
    double u1 = uniform01();
    double u2 = uniform01();
    if (u1 < 1e-300) {
        u1 = 1e-300;
    }
    return (float)(sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2)) * sigma;
}

/* ---- Tests ----------------------------------------------------------- */

static void test_constant_truth(void)
{
    printf("constant truth, noisy measurements\n");
    const float truth = 35.0f, sigma_r = 0.5f;
    kalman1d_t kf;
    kalman1d_init(&kf, 30.0f, 100.0f, 1e-4f, sigma_r * sigma_r);

    double meas_se = 0.0, est_se = 0.0;
    int n = 0;
    for (int i = 0; i < 2000; i++) {
        const float z = truth + gaussian(sigma_r);
        kalman1d_step(&kf, z);
        if (i >= 200) {  /* skip the initial convergence */
            meas_se += (double)((z - truth) * (z - truth));
            est_se  += (double)((kf.x - truth) * (kf.x - truth));
            n++;
        }
    }
    const double meas_rms = sqrt(meas_se / n), est_rms = sqrt(est_se / n);
    CHECK(est_rms < 0.25 * meas_rms,
          "rms error: measurement %.3f C, estimate %.3f C (%.1fx better)",
          meas_rms, est_rms, meas_rms / est_rms);
}

static void test_steady_state_gain(void)
{
    printf("convergence to analytic steady-state gain\n");
    const float q = 1e-3f, r = 0.25f;
    const float k_ss = kalman1d_steady_state_gain(q, r);
    const float p0s[] = { 0.0f, 0.01f, 1.0f, 1e4f };

    for (unsigned i = 0; i < sizeof p0s / sizeof p0s[0]; i++) {
        kalman1d_t kf;
        kalman1d_init(&kf, 0.0f, p0s[i], q, r);
        for (int k = 0; k < 500; k++) {
            kalman1d_step(&kf, 0.0f);  /* gain doesn't depend on data */
        }
        CHECK(fabsf(kf.k - k_ss) < 1e-5f,
              "P0=%-7g -> K=%.6f, analytic %.6f", (double)p0s[i],
              (double)kf.k, (double)k_ss);
    }
    /* And the steady-state P must satisfy the Riccati equation. */
    const float p = kalman1d_steady_state_variance(q, r);
    const float residual = p * p + q * p - q * r;
    CHECK(fabsf(residual) < 1e-7f, "Riccati residual P^2+QP-QR = %.2e",
          (double)residual);
}

static void test_consistency(void)
{
    printf("filter variance P matches actual squared error\n");
    /* The model must match reality for P to be "honest": generate a true
     * random walk with exactly variance Q per step. */
    const float q = 1e-3f, r = 0.25f;
    kalman1d_t kf;
    float truth = 35.0f;
    kalman1d_init(&kf, truth, r, q, r);

    double se = 0.0, p_sum = 0.0;
    int n = 0;
    for (int i = 0; i < 200000; i++) {
        truth += gaussian(sqrtf(q));
        kalman1d_step(&kf, truth + gaussian(sqrtf(r)));
        if (i >= 1000) {
            se += (double)((kf.x - truth) * (kf.x - truth));
            p_sum += (double)kf.p;
            n++;
        }
    }
    const double ratio = (se / n) / (p_sum / n);
    CHECK(ratio > 0.9 && ratio < 1.1,
          "mean squared error / mean P = %.3f (ideal 1.0)", ratio);
}

/* Run a ramp through the filter; report lag and noise of the estimate. */
static void run_ramp(float q, double *lag_out, double *noise_out)
{
    const float slope = 0.01f;   /* C per step (0.1 C/s at 10 Hz) */
    const float r = 0.25f;
    kalman1d_t kf;
    kalman1d_init(&kf, 0.0f, r, q, r);
    double err_sum = 0.0, err_sq = 0.0;
    int n = 0;
    for (int i = 0; i < 20000; i++) {
        const float truth = slope * (float)i;
        kalman1d_step(&kf, truth + gaussian(0.5f));
        if (i >= 2000) {
            const double e = (double)(truth - kf.x);
            err_sum += e;
            err_sq += e * e;
            n++;
        }
    }
    *lag_out = err_sum / n;                                    /* bias */
    *noise_out = sqrt(err_sq / n - (*lag_out) * (*lag_out));   /* std  */
}

static void test_q_tradeoff(void)
{
    printf("Q trade-off on a ramp: lag vs noise\n");
    double lag_lo, noise_lo, lag_hi, noise_hi;
    run_ramp(1e-4f, &lag_lo, &noise_lo);
    run_ramp(1e-2f, &lag_hi, &noise_hi);
    printf("        Q=1e-4: lag %.3f C, noise %.3f C\n", lag_lo, noise_lo);
    printf("        Q=1e-2: lag %.3f C, noise %.3f C\n", lag_hi, noise_hi);
    CHECK(lag_hi < lag_lo, "larger Q lags less");
    CHECK(noise_hi > noise_lo, "larger Q is noisier");

    /* Theory: in steady state the filter is an EMA with factor K, whose
     * lag on a ramp of slope m is m * (1 - K) / K. */
    const float k = kalman1d_steady_state_gain(1e-4f, 0.25f);
    const double lag_theory = 0.01 * (1.0 - (double)k) / (double)k;
    CHECK(fabs(lag_lo - lag_theory) < 0.05 * lag_theory,
          "ramp lag %.3f C matches EMA theory %.3f C", lag_lo, lag_theory);
}

static void test_reset_snaps(void)
{
    printf("large initial P trusts the first measurement\n");
    kalman1d_t kf;
    kalman1d_init(&kf, 0.0f, 1e6f, 1e-3f, 0.25f);
    kalman1d_step(&kf, 42.0f);
    CHECK(fabsf(kf.x - 42.0f) < 1e-3f && kf.k > 0.9999f,
          "after one step x=%.4f, K=%.6f", (double)kf.x, (double)kf.k);
}

int main(void)
{
    test_constant_truth();
    test_steady_state_gain();
    test_consistency();
    test_q_tradeoff();
    test_reset_snaps();

    if (g_failures != 0) {
        printf("\n%d check(s) FAILED\n", g_failures);
        return EXIT_FAILURE;
    }
    printf("\nall checks passed\n");
    return EXIT_SUCCESS;
}
