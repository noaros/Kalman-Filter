# 4. Experiments and tuning

Hands-on things to try with the running demo. Each one shows a different
part of the [theory](01-kalman-theory.md). Run the live plot so you can
see what happens:

```sh
python3 tools/kf_plot.py --log run.csv
```

Its key presses go to the board: `+ - r R z n h`.

---

## 4.1 Baseline: watch it settle

Reset the board (black RESET button) and leave it alone.

- The blue LED stays on for 1 s while the noise is measured. The banner
  reports `std` and the resulting `R`.
- In the first CSV lines `k` starts high (about 0.5) and decays toward the
  steady-state value printed on the `# Q=...` line. **P** decays with it.
  This is the filter becoming more sure of itself as readings come in.
- `meas_c` jumps between neighbouring ADC codes, 0.3 °C apart. `est_c`
  moves smoothly *between* them. The filter resolves changes smaller than
  one ADC step.

## 4.2 Heat the chip

Put a fingertip on the STM32 (the big square chip) for 10–20 s, then remove
it. The die temperature rises by a degree or two, then falls slowly.

- With the default Q = 1e-3 the estimate follows within about a second.
- Watch the **innovation** column. While the temperature is rising, it stays
  mostly positive: the readings keep landing above the prediction. A
  well-tuned filter on a steady signal has innovations that are random in
  sign and average zero.

A hair dryer on low or a can of freeze spray gives larger, faster changes.
Be careful with ESD and condensation.

## 4.3 Q too small: confident and wrong

Press `-` until `Q=0.000001` (K is about 0.004, a time constant of about
25 s), then heat the chip again.

- The estimate **lags far behind** the readings.
- The **red LED lights up**: the innovation exceeds 3√S. The filter is
  "surprised", because its model says the temperature can't move that fast.
- Look at `p`. It is tiny, so the filter *believes* it is very accurate
  while being off by a degree. A filter with the wrong model is
  **overconfident**, and the innovation test is how you catch it.

## 4.4 Q too large: no filtering

Press `+` until `Q=1.000000` (K is about 0.94).

- The estimate follows every noisy reading, with almost no smoothing.
- `p` is close to R: the filter admits it isn't doing better than a single
  reading.

## 4.5 Only the ratio matters

Start from the defaults and note the steady-state K on the `# Q=` line
(about 0.12 on the development board).

- Press `+` once. Q goes up **10×**, but K only rises about **3×** (to about
  0.32), because K∞ ≈ √(Q/R) when Q ≪ R.
- Now press `R` three times (R × 8). Q/R is back to nearly where it started
  (×10/8), and K returns to about 0.13.

Behaviour depends only on **Q/R**: the absolute values change P, not K.

## 4.6 Reset and convergence

Press `z`. The filter starts again at the current reading with P = 100 °C²
(σ = 10 °C, which means "no idea"). This capture was taken with Q = 1e-2:

```
# filter reset
12900,937,31.197,31.197,0.064815,0.9994,-0.593   <- K ≈ 1: take the reading
13000,939,31.790,31.515,0.034741,0.5356,0.593    <- K ≈ 1/2: average of 2
13100,939,31.790,31.627,0.026477,0.4082,0.275    <- K ≈ 1/3 ...
13200,938,31.493,31.579,0.023347,0.3600,-0.134
```

For the first few samples K ≈ 1/n, so **the filter is computing a running
average** of the readings since the reset. That is the optimal estimate of
a constant. As P falls toward its steady state, Q starts to matter and K
levels off. A plain EMA started at zero takes many time constants to get
going.

## 4.7 Get R wrong on purpose

- Press `r` a few times (R ÷ 8). The filter now believes the readings are
  much cleaner than they are. K rises and the estimate gets noisier, and
  `p` *understates* the real scatter of `est_c`.
- Press `n` to measure R again and get back to a consistent tuning.

## 4.8 Analyse a capture offline

Everything from `--log` is plain CSV. Some ideas:

```python
import pandas as pd
d = pd.read_csv("run.csv")
print(d.meas_c.std(), d.est_c.std())     # scatter before and after filtering

# Innovation variance S isn't logged, but it follows from the logged
# posterior p and gain k:  p = (1-K) P-  and  K = P-/S  =>  S = p / ((1-k) k)
S = d.p / ((1 - d.k) * d.k)
nis = d.innov / S**0.5                   # normalised innovation
print(nis.std(), nis.autocorr())         # ~1 and ~0 when Q and R are right
```

The **normalised innovation** y/√S should have a standard deviation of
about 1 and no autocorrelation when Q and R are right. That's the standard
check for a Kalman filter on real data.

Replay a capture in the plotter:

```sh
python3 tools/kf_plot.py --replay run.csv
```

---

## Choosing Q and R in practice

1. **R: measure it.** Hold the true value constant and take the variance of
   the readings. The firmware does this at boot. If the true value can't be
   held still, read R from the sensor's datasheet noise figure.
2. **Q: decide how fast the truth moves.** √Q is roughly how much the true
   value changes in one sample period. A die temperature might move
   0.1 °C/s at most under normal conditions. That is 0.01 °C per 100 ms,
   giving Q ≈ 1e-4. Allow for faster events, like a fingertip, by going up
   to about 1e-3.
3. **Check the innovations.** If they have long runs of the same sign, or
   frequently exceed 3σ, Q is too small. If the estimate looks as noisy as
   the input, Q/R is too large.
4. **Remember Q depends on the sample period.** If you change
   `SAMPLE_PERIOD_MS`, scale Q in proportion to keep the same behaviour in
   real time. For a random walk, the variance grows linearly with time.
