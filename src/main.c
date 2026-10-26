/**
 * @file    main.c
 * @brief   Kalman-filter demo: smooth the STM32F429's internal temperature
 *          sensor and stream raw + filtered values over the ST-LINK VCP.
 *
 * PROGRAM FLOW
 * ------------
 *   1. Bring up flash caches, the 1 ms tick, LEDs/button, UART and ADC.
 *   2. Print a banner with the factory calibration and measured VDDA.
 *   3. Characterise the measurement noise: take NOISE_SAMPLES readings,
 *      compute their mean and variance. The variance becomes R and the
 *      mean becomes the filter's starting estimate.
 *   4. Every SAMPLE_PERIOD_MS: read the sensor, run one Kalman step and
 *      print one CSV line.
 *   5. In between, poll the user button and the serial port for commands
 *      that change Q/R or reset the filter live.
 *
 * OUTPUT FORMAT (115200 8N1)
 * --------------------------
 * Lines starting with '#' are human-readable information. All other lines
 * are CSV with this header (also printed at start-up):
 *
 *     t_ms,adc,meas_c,est_c,p,k,innov
 *
 *   t_ms    milliseconds since boot
 *   adc     raw 12-bit ADC code from the sensor
 *   meas_c  that code converted to C   (the noisy measurement z)
 *   est_c   Kalman estimate x          (the filtered value)
 *   p       estimate variance P (C^2); +-2*sqrt(p) is a ~95 % band
 *   k       Kalman gain applied this step
 *   innov   innovation z - x_prior (C)
 *
 * tools/kf_plot.py plots this live; any serial terminal shows it as text.
 *
 * LEDS
 * ----
 *   green  heartbeat, toggles every 500 ms   ("firmware alive")
 *   blue   toggles on every sample           ("sampling at 10 Hz")
 *   red    lit while the innovation is beyond 3 sigma: the measurement
 *          disagrees with the filter's prediction by more than the filter
 *          thinks plausible -- a sign that Q is too small for how fast
 *          the temperature is actually changing. Also: solid red with
 *          everything else frozen = fault handler (see startup.c).
 */
#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "kalman.h"
#include "stm32f429.h"
#include "systick.h"
#include "temp_sensor.h"
#include "uart.h"

/* ---- Configuration --------------------------------------------------- */

#define UART_BAUD            115200u
#define SAMPLE_PERIOD_MS     100u    /* 10 Hz                             */
#define HEARTBEAT_PERIOD_MS  500u
#define BUTTON_POLL_MS       10u     /* also acts as the debounce time    */

#define NOISE_SAMPLES        100u    /* start-up noise characterisation   */
#define NOISE_SAMPLE_GAP_MS  10u     /* -> takes 1 s                      */
#define R_MIN                1e-4f   /* floor so R can never be 0         */

#define P_RESET              100.0f  /* "no idea" variance after reset:
                                      * sigma = 10 C                      */
#define INNOV_GATE_SIGMA2    9.0f    /* 3 sigma, squared                  */

/* Process-noise presets (C^2 per step), cycled with the button or +/-.
 * Rule of thumb: sqrt(Q) is how far you expect the *true* temperature to
 * move in one sample period. 1e-3 -> ~0.03 C per 100 ms. */
static const float Q_PRESETS[] = { 1e-6f, 1e-5f, 1e-4f, 1e-3f, 1e-2f, 1e-1f, 1.0f };
#define NUM_Q_PRESETS   (sizeof Q_PRESETS / sizeof Q_PRESETS[0])
#define Q_PRESET_DEFAULT 3u  /* 1e-3 */

/* ---- State ----------------------------------------------------------- */

static kalman1d_t s_kf;
static unsigned   s_q_index = Q_PRESET_DEFAULT;

/* ---- Helpers --------------------------------------------------------- */

static void print_help(void)
{
    uart_puts("# Commands (type in the serial terminal, or press keys in kf_plot.py):\n"
              "#   +  / -   next / previous Q preset   (blue USER button = next, wraps)\n"
              "#   R  / r   double / halve R\n"
              "#   z        reset filter to the current reading with P = 100 C^2\n"
              "#   n        re-measure sensor noise and set R from it\n"
              "#   h  / ?   this help\n");
}

/** Print the current tuning and what it implies for steady state. */
static void print_tuning(void)
{
    const float k_ss = kalman1d_steady_state_gain(s_kf.q, s_kf.r);
    const float p_ss = kalman1d_steady_state_variance(s_kf.q, s_kf.r);

    uart_puts("# Q=");
    uart_put_float(s_kf.q, 6);
    uart_puts(" C^2/step  R=");
    uart_put_float(s_kf.r, 4);
    uart_puts(" C^2  -> steady-state K=");
    uart_put_float(k_ss, 4);
    uart_puts(", sigma_est=");
    uart_put_float(__builtin_sqrtf(p_ss), 3);
    uart_puts(" C, time constant ~");
    /* An EMA with factor K has time constant -T/ln(1-K) ~ T/K for small K. */
    uart_put_float((float)SAMPLE_PERIOD_MS / k_ss / 1000.0f, 1);
    uart_puts(" s\n");
}

static void print_csv_header(void)
{
    uart_puts("t_ms,adc,meas_c,est_c,p,k,innov\n");
}

/**
 * Measure the sensor noise. The die temperature barely moves over one
 * second, so the spread of these readings is (almost) pure measurement
 * noise -- exactly what R is supposed to be.
 *
 * Welford's online algorithm computes mean and variance in a single pass
 * without the catastrophic cancellation of the naive sum(x^2) - n*mean^2
 * (which in float, with mean ~35 and variance ~0.1, would lose most of
 * its significant digits).
 */
static void measure_noise(float *mean_out, float *var_out)
{
    float mean = 0.0f;
    float m2 = 0.0f;
    for (uint32_t n = 1; n <= NOISE_SAMPLES; n++) {
        const float z = temp_sensor_raw_to_celsius(temp_sensor_read_raw());
        const float delta = z - mean;
        mean += delta / (float)n;
        m2 += delta * (z - mean);
        delay_ms(NOISE_SAMPLE_GAP_MS);
    }
    *mean_out = mean;
    *var_out = m2 / (float)(NOISE_SAMPLES - 1u);  /* unbiased sample variance */
}

static void calibrate_and_init_filter(void)
{
    uart_puts("# Measuring sensor noise (");
    uart_put_u32(NOISE_SAMPLES);
    uart_puts(" samples, keep the board still)...\n");
    led_on(LED_BLUE);

    float mean, var;
    measure_noise(&mean, &var);
    led_off(LED_BLUE);

    uart_puts("# mean=");
    uart_put_float(mean, 2);
    uart_puts(" C  std=");
    uart_put_float(__builtin_sqrtf(var), 3);
    uart_puts(" C  -> R=");
    uart_put_float(var, 4);
    uart_puts(" C^2\n");

    if (var < R_MIN) {
        var = R_MIN;
    }
    /* Start at the measured mean with an uncertainty equal to the noise
     * variance -- we averaged 100 readings, so that's even pessimistic. */
    kalman1d_init(&s_kf, mean, var, Q_PRESETS[s_q_index], var);
    print_tuning();
}

static void print_banner(void)
{
    const temp_cal_t *cal = temp_sensor_calibration();

    uart_puts("\n# ==== STM32F429 bare-metal Kalman filter demo ====\n");
    uart_puts("# SYSCLK=");
    uart_put_u32(SYSCLK_HZ / 1000000u);
    uart_puts(" MHz (HSI), sample period=");
    uart_put_u32(SAMPLE_PERIOD_MS);
    uart_puts(" ms\n# TS_CAL1(30C)=");
    uart_put_u32(cal->ts_cal1);
    uart_puts(" TS_CAL2(110C)=");
    uart_put_u32(cal->ts_cal2);
    uart_puts(" VREFINT_CAL=");
    uart_put_u32(cal->vrefint_cal);
    uart_puts("\n# VREFINT now=");
    uart_put_float(cal->vrefint_avg, 1);
    uart_puts(" -> VDDA=");
    uart_put_float(cal->vdda_v, 3);
    uart_puts(" V\n");
    if (!cal->factory_cal) {
        uart_puts("# WARNING: factory calibration invalid, using datasheet "
                  "typicals (absolute value may be off by many C)\n");
    }
    print_help();
}

static void next_q_preset(int dir)
{
    s_q_index = (s_q_index + NUM_Q_PRESETS + (unsigned)dir) % NUM_Q_PRESETS;
    s_kf.q = Q_PRESETS[s_q_index];
    print_tuning();
}

static void handle_command(char c, float last_meas)
{
    switch (c) {
    case '+': next_q_preset(+1); break;
    case '-': next_q_preset(-1); break;
    case 'R': s_kf.r *= 2.0f; print_tuning(); break;
    case 'r':
        s_kf.r *= 0.5f;
        if (s_kf.r < R_MIN) {
            s_kf.r = R_MIN;
        }
        print_tuning();
        break;
    case 'z':
        /* Throw away everything the filter knows. With P huge, the next
         * gain is ~1 and it snaps to the measurements, then K decays
         * toward steady state -- a nice way to watch convergence. */
        kalman1d_init(&s_kf, last_meas, P_RESET, s_kf.q, s_kf.r);
        uart_puts("# filter reset\n");
        break;
    case 'n':
        calibrate_and_init_filter();
        print_csv_header();
        break;
    case 'h':
    case '?':
        print_help();
        print_tuning();
        break;
    default:
        break;  /* ignore newlines etc. */
    }
}

/* ---- Main ------------------------------------------------------------ */

int main(void)
{
    board_flash_init();
    systick_init(SYSCLK_HZ);  /* first: delay_ms() is used by the others */
    board_init();
    uart_init(UART_BAUD);
    temp_sensor_init();

    print_banner();
    calibrate_and_init_filter();
    print_csv_header();

    const uint32_t start = millis();
    uint32_t next_sample = start;
    uint32_t next_heartbeat = start;
    uint32_t next_button = start;
    bool button_was_pressed = false;
    float last_meas = s_kf.x;

    /* Super-loop: no RTOS, no interrupts besides SysTick. Each task runs
     * when its deadline arrives; all of them are short and non-blocking
     * (apart from UART output, which takes a few ms per line). */
    for (;;) {
        const uint32_t now = millis();

        if (time_reached(now, next_sample)) {
            /* Advance by the period rather than setting now + period, so
             * that jitter doesn't accumulate into drift. */
            next_sample += SAMPLE_PERIOD_MS;

            const uint16_t raw = temp_sensor_read_raw();
            const float z = temp_sensor_raw_to_celsius(raw);
            last_meas = z;

            kalman1d_step(&s_kf, z);

            /* Innovation gate: y^2 > 9 S  <=>  |y| > 3 sqrt(S). Squared to
             * avoid a sqrt; S is the variance the filter *expected* y to
             * have, so this flags measurements it considers implausible. */
            led_set(LED_RED, s_kf.y * s_kf.y > INNOV_GATE_SIGMA2 * s_kf.s);
            led_toggle(LED_BLUE);

            uart_put_u32(now - start);
            uart_putc(',');
            uart_put_u32(raw);
            uart_putc(',');
            uart_put_float(z, 3);
            uart_putc(',');
            uart_put_float(s_kf.x, 3);
            uart_putc(',');
            uart_put_float(s_kf.p, 6);
            uart_putc(',');
            uart_put_float(s_kf.k, 4);
            uart_putc(',');
            uart_put_float(s_kf.y, 3);
            uart_puts("\n");
        }

        if (time_reached(now, next_heartbeat)) {
            next_heartbeat += HEARTBEAT_PERIOD_MS;
            led_toggle(LED_GREEN);
        }

        /* Sampling a bouncing contact every 10 ms and acting only on a
         * released->pressed transition is a simple, adequate debounce:
         * bounces are shorter than 10 ms, so we see at most one edge. */
        if (time_reached(now, next_button)) {
            next_button += BUTTON_POLL_MS;
            const bool pressed = button_is_pressed();
            if (pressed && !button_was_pressed) {
                next_q_preset(+1);
            }
            button_was_pressed = pressed;
        }

        char c;
        if (uart_getc_nonblocking(&c)) {
            handle_command(c, last_meas);
            /* 'n' blocks for a second; don't try to catch up afterwards. */
            const uint32_t after = millis();
            if (time_reached(after, next_sample + SAMPLE_PERIOD_MS)) {
                next_sample = after;
                next_heartbeat = after;
                next_button = after;
            }
        }
    }
}
