# 1. Kalman filter theory

This chapter explains what the filter in [`src/kalman.c`](../src/kalman.c)
does and why it works. It starts with intuition, then derives the scalar
equations used in the firmware, solves for the steady state, and finally
shows how the same steps look in the general matrix filter.

You don't need any background beyond mean and variance.

---

## 1.1 The problem

Every 100 ms the ADC gives us a reading *z* of the die temperature. The
readings are noisy. On the development board they jump by ±1 ADC count,
which is about ±0.3 °C, even though the true temperature barely moves:

```
adc  meas_c
938  31.493
939  31.790
938  31.493
940  32.087   <- the chip didn't really warm by 0.6 C in 100 ms
939  31.790
```

We want a better estimate of the true temperature than any single reading
gives. The two obvious approaches each have a flaw:

- **Average the last N readings.** More readings means less noise, but the
  average also lags behind a real change by about N/2 samples, and it is not
  obvious how to choose N.
- **Exponential moving average** `x += α (z − x)`. This is cheaper, but it has
  the same trade-off, now hidden in α.

The Kalman filter turns this trade-off into two physical questions:

- **R**: how noisy is a single measurement? This can be measured; the
  firmware does so at start-up.
- **Q**: how much does the true value change from one sample to the next?
  This is a statement about the physics.

From R and Q it computes the best α for each step. That α is called the
*Kalman gain*, K. As a bonus, the filter tracks **how uncertain its own
estimate is** (P).

## 1.2 Uncertainty as variance

The filter describes each quantity as a *best guess* plus a *variance*, the
square of the standard deviation:

| Symbol | Meaning | Units |
|---|---|---|
| *x* | best estimate of the true temperature | °C |
| *P* | variance of that estimate: "I think it's *x* ± √P" | °C² |
| *z* | a measurement | °C |
| *R* | variance of the measurement noise | °C² |
| *Q* | variance of the true value's change per step | °C² |

Variances are used because they **add** when independent uncertainties
combine. Standard deviations don't. If you are ±0.1 °C sure now, and the
temperature can drift ±0.03 °C before the next sample, then you are
±√(0.1² + 0.03²) ≈ ±0.104 °C sure about the next value.

## 1.3 The model

The filter assumes the world behaves like this:

$$
\begin{aligned}
x_k &= x_{k-1} + w_k, & w_k &\sim \mathcal{N}(0, Q) \quad\text{(process)}\\
z_k &= x_k + v_k,     & v_k &\sim \mathcal{N}(0, R) \quad\text{(measurement)}
\end{aligned}
$$

In words: the true temperature stays where it was, plus a random nudge of
variance Q each step. Each reading is the true value plus independent noise
of variance R. This is called a **random-walk** or **local-level** model.

The model is *wrong* in a precise sense: real temperature doesn't wander
randomly, it follows heat flow. That's fine. Q is the tool that admits the
model is imperfect. A larger Q tells the filter "don't trust your prediction
too much; reality may have moved".

## 1.4 Deriving the two steps

Each step has two halves, **predict** and **update**.

### Predict: what do we expect before looking?

According to the model the temperature hasn't changed, so the best guess
stays the same. But the true value may have drifted by a variance of Q, so
the uncertainty grows:

$$
\hat{x}^-_k = \hat{x}_{k-1}, \qquad P^-_k = P_{k-1} + Q
$$

The minus superscript marks the *prior*: the estimate before the new
measurement is used.

### Update: blending prediction and measurement

We now have two independent estimates of the same quantity:

- the prediction, $\hat{x}^-$, with variance $P^-$
- the measurement, *z*, with variance *R*

Any blend $\hat{x} = (1-K)\hat{x}^- + K z$ is unbiased. Its variance is

$$
P(K) = (1-K)^2 P^- + K^2 R
$$

Setting $dP/dK = 0$ gives the K with the smallest variance:

$$
-2(1-K)P^- + 2KR = 0 \;\Rightarrow\; \boxed{K = \frac{P^-}{P^- + R}}
$$

This is the **Kalman gain**. It is the fraction of the way we move toward the
measurement:

| Situation | K | Behaviour |
|---|---|---|
| Very confident prediction (P⁻ ≪ R) | ≈ 0 | Mostly ignore the reading |
| Very noisy measurement (R ≫ P⁻) | ≈ 0 | Same |
| No idea yet (P⁻ ≫ R) | ≈ 1 | Take the reading at face value |
| Equal confidence | 0.5 | Split the difference |

Substituting K back gives the new variance:

$$
P = (1-K)^2 P^- + K^2 R = \frac{P^- R}{P^- + R} = (1-K)\,P^-
$$

Note that $P^- R / (P^- + R)$ is smaller than both $P^-$ and $R$. Combining
two independent estimates always beats either one alone. (It's the same
formula as two resistors in parallel.)

Written in the usual order, with the innovation $y$ made explicit:

$$
\begin{aligned}
y &= z - \hat{x}^-           && \text{innovation (the surprise)}\\
S &= P^- + R                 && \text{expected variance of } y\\
K &= P^- / S                 && \text{gain}\\
\hat{x} &= \hat{x}^- + K y   && \text{correct the estimate}\\
P &= (1-K) P^-               && \text{reduce the uncertainty}
\end{aligned}
$$

These are exactly the lines in `kalman1d_update()`.

### Why "optimal"?

If the noises really are Gaussian, independent and of variance Q and R, this
estimate is the exact conditional mean of the true state given all the
measurements so far, and P is its exact variance. No other estimator does
better. If the noise is not Gaussian, the Kalman filter is still the best
*linear* estimator. The host tests check this property:
`test_consistency` confirms that the actual mean-squared error matches
the filter's own P to within a few percent.

## 1.5 Steady state

The gain does **not** depend on the measurements, only on P, Q and R. With
constant Q and R, P converges to a fixed value. After that the filter is just
an exponential moving average with a fixed α = K.

At steady state, one predict-and-update cycle must map P to itself:

$$
P = \frac{(P+Q)\,R}{P+Q+R}
\quad\Longrightarrow\quad
P^2 + QP - QR = 0
$$

This is the scalar **discrete algebraic Riccati equation**. Its positive root
is

$$
P_\infty = \frac{-Q + \sqrt{Q^2 + 4QR}}{2}, \qquad
K_\infty = \frac{P_\infty + Q}{P_\infty + Q + R}
$$

`kalman1d_steady_state_gain()` computes this, and the firmware prints it
whenever you change Q or R. Some useful consequences:

- **Only the ratio Q/R matters** for K. Doubling both changes P but not the
  behaviour.
- For Q ≪ R: $K_\infty \approx \sqrt{Q/R}$. Making the filter respond twice
  as fast takes **4×** the Q.
- **Time constant** ≈ T / K, where T is the sample period. With the
  defaults on the development board (Q = 1e-3, R ≈ 0.065), K ≈ 0.12, so
  the time constant is about 0.9 s.
- **Lag on a ramp.** If the true value rises steadily by *m* per step, the
  estimate trails it by $m(1-K)/K$ (from EMA theory). `test_q_tradeoff`
  checks this formula.
- **Noise passed through.** For white measurement noise, the estimate's
  variance is $R \cdot K/(2-K)$.

So a Kalman filter with constant Q and R is, after a few samples, the same as
a well-chosen EMA. What it adds over a hand-tuned EMA:

1. **Principled tuning**: K comes from physical noise levels rather than
   trial and error.
2. **Fast start**: with a large initial P, the first samples get K ≈ 1, so
   there's no slow climb from zero. Press `z` on the board to see this.
3. **Self-knowledge**: P tells you how good the estimate is, and the
   innovation test (|y| > 3√S lights the red LED) tells you when the model
   no longer fits reality.
4. **Generality**: the same machinery handles multiple states, sensors that
   measure combinations of states, varying sample times and missing
   measurements. Those cases are covered next.

## 1.6 The general (matrix) Kalman filter

For a state *vector* **x** with *n* entries and a measurement vector **z**
with *m* entries:

$$
\begin{aligned}
\mathbf{x}_k &= F\,\mathbf{x}_{k-1} + B\,\mathbf{u}_k + \mathbf{w}_k, & \mathbf{w}&\sim\mathcal{N}(0,Q)\\
\mathbf{z}_k &= H\,\mathbf{x}_k + \mathbf{v}_k, & \mathbf{v}&\sim\mathcal{N}(0,R)
\end{aligned}
$$

| Step | Matrix form | Our scalar case |
|---|---|---|
| Predict state | $\hat{\mathbf{x}}^- = F\hat{\mathbf{x}} + B\mathbf{u}$ | $F=1$, no input |
| Predict covariance | $P^- = F P F^\top + Q$ | $P + Q$ |
| Innovation | $\mathbf{y} = \mathbf{z} - H\hat{\mathbf{x}}^-$ | $H = 1$ |
| Innovation covariance | $S = H P^- H^\top + R$ | $P^- + R$ |
| Gain | $K = P^- H^\top S^{-1}$ | $P^-/S$ |
| Update state | $\hat{\mathbf{x}} = \hat{\mathbf{x}}^- + K\mathbf{y}$ | same |
| Update covariance | $P = (I - KH)P^-$ | $(1-K)P^-$ |

In matrix filters the covariance update is usually written in the **Joseph
form**, $P = (I-KH)P^-(I-KH)^\top + KRK^\top$. It stays symmetric and
positive-definite under rounding errors. In the scalar case both forms are
mathematically identical (see the comment in `kalman1d_update`), and single
precision is plenty.

### Extending this demo

Some natural next steps, each a small change to the model:

- **Temperature + rate** (2 states): $\mathbf{x} = [T, \dot T]^\top$,
  $F = \begin{bmatrix}1 & \Delta t\\ 0 & 1\end{bmatrix}$, $H = [1\;0]$.
  The estimate no longer lags on a steady ramp, because the filter
  learns the slope.
- **Two sensors** (e.g. the internal sensor and an external thermistor):
  stack them in **z** with $H = [1\;1]^\top$, and give each its own
  noise in a diagonal R.
- **Variable sample time**: scale Q by Δt, as in $Q_k = q_c\,\Delta t$,
  where $q_c$ is a continuous-time noise density.
- **Missing or rejected samples**: skip the update and only predict. P
  grows, and the next good reading gets a larger K. Combined with the 3σ
  innovation gate this gives basic outlier rejection.

## 1.7 Further reading

- G. Welch & G. Bishop, *An Introduction to the Kalman Filter*, UNC TR 95-041.
  A short classic.
- R. Labbe, *Kalman and Bayesian Filters in Python*. A free interactive book
  on GitHub.
- D. Simon, *Optimal State Estimation*, Wiley 2006. Thorough, including
  extended and unscented filters.
- R. E. Kalman, "A New Approach to Linear Filtering and Prediction Problems",
  *Trans. ASME J. Basic Eng.*, 1960. The original paper.
