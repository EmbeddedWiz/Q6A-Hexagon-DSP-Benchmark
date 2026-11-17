# CPU A55 versus A78 for the scalar FOC loop — 2026-11-13

## Purpose

P5.2: check whether the negative DSP result is an artifact of comparing only
against the fast A78 prime core. The same controller source was run on a
Cortex-A55 core and on the A78 prime core.

## Conditions

- Board: Radxa Dragon Q6A; CPUs 0–3 are Cortex-A55 (max 1.9584 GHz), CPUs 4–6
  are Cortex-A78 (2.4 GHz), CPU 7 is the A78 prime core (2.7072 GHz).
- Commit `5be3852`; same `build/foc/foc_runner cpu` binary and inputs.
- 3 independent processes per core, each with 5 timed repetitions of 16,384
  steps and the standard 2,000,000-step clock ramp.
- CPU 0 pinned for A55, CPU 7 pinned for A78; no DSP call in this comparison.
- Thermal: 39.1 °C before, 44.6 °C after.

## Results

| Core | Process medians (ns/step) | Median |
|---|---|---:|
| Cortex-A55, CPU 0 | 173.992, 173.874, 173.505 | **173.87** |
| Cortex-A78 prime, CPU 7 | 59.575, 59.463, 59.349 | **59.46** |

A78 prime / A55 speed ratio: **2.92×**. Checksums matched.

## Interpretation

The A55 is slower than the A78 prime but still much faster than the CDSP for
this scalar, dependency-chained FOC loop: the final DSP value is 320.96 ns/step,
about **1.85× slower than the A55** and about 5.4× slower than the A78 in this
session. The main 5.28× headline uses the final-suite A78 value; the 5.4× here
is a same-session cross-check. The negative conclusion is therefore not caused
by choosing only the fastest CPU core.

Raw log: `a55-vs-a78.log`.
