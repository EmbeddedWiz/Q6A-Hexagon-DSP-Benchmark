# RPC latency and step scaling — 2026-11-13

## Purpose

P2.1/P2.2: measure the fixed FastRPC cost, its tail behavior, and the cost of
one RPC carrying N FOC steps. Conditions: Radxa Dragon Q6A, Hexagon v68 CDSP,
commit `3c354c9`, runner and CPU 7 pinned with `taskset`, 10,000 calls per
latency series, 30 repetitions per scaling size. Raw logs and `rpc-scaling.svg`
are in this directory.

The idle run started at 53.6 °C and ended at 58.3 °C. The light loaded run
started at 59.0 °C and ended at 62.2 °C. The runner was pinned to CPU 7; the
loaded case used two `yes` loops, one on CPU 0 and one on CPU 7.

## P2.1 — empty echo and single-step RPC

All values are microseconds over 10,000 calls.

| Series | min | median | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|
| echo, idle | 63.901 | **74.034** | 105.870 | 150.851 | 1,710.140 |
| step, idle | 64.256 | **73.697** | 103.701 | 157.570 | 5,780.170 |
| echo, loaded | 64.270 | **72.603** | 79.581 | 89.164 | 4,035.300 |
| step, loaded | 64.008 | **72.810** | 75.727 | 87.550 | 5,333.810 |

The loaded medians are slightly lower and the loaded p99 is much lower than
idle. This is a CPU-frequency effect: the competing loops keep CPU 7 out of
low-power states, so the RPC path runs at a higher clock. It is not evidence
that load makes RPC faster in general. Rare multi-millisecond outliers remain
in both cases.

## P2.2 — one RPC carrying N steps

Median wall time in microseconds; `min` in parentheses. The same run's CPU
reference is **60.807 ns/step**.

| N | idle median (min) | loaded median (min) | DSP ns/step, loaded | CPU compute |
|---:|---:|---:|---:|---:|
| 1 | 88.201 (85.657) | 87.810 (73.696) | 87,810 | 0.061 µs |
| 16 | 93.656 (81.040) | 93.096 (78.800) | 5,818.5 | 0.973 µs |
| 256 | 356.658 (163.917) | 158.745 (154.527) | 620.1 | 15.567 µs |
| 4,096 | 1,605.58 (1,403.34) | 1,255.07 (1,251.21) | 306.4 | 249.066 µs |
| 16,384 | 5,426.29 (4,829.56) | 4,603.49 (4,359.98) | 281.0 | 996.262 µs |

The idle N=256 median is an outlier caused by intermittent slow calls; its min
is 163.9 µs and the loaded run gives 158.7 µs. The idle scaling series is
therefore shown with both median and min; the loaded series is stable.

### Break-even

A least-squares fit over N = 256, 4096, 16384 gives:

- idle: **313.5 ns/step**, intercept 295.9 µs;
- loaded: **274.8 ns/step**, intercept 106.1 µs;
- CPU reference: **60.807 ns/step**.

Both DSP slopes exceed the CPU per-step cost, and the RPC also adds a fixed
88–94 µs at N = 1. Therefore there is **no break-even N** for this FOC workload:
the DSP is slower per step and pays additional RPC overhead at every N. At
N = 16,384 loaded, one RPC takes 4.603 ms versus 0.996 ms of CPU compute
(4.62×). At N = 1 the ratio is about 1,444×. The fixed overhead falls below 2%
of the RPC wall only at the largest size (about 1.9% at N = 16,384 loaded).

## Reliability caveats

- After a repeated 10,000+10,000-call sequence plus scaling, a follow-up run
  failed with FastRPC open error 114 and the unsigned PD became unavailable.
  A board reboot restored it (`pd-wedge-114.log`). This is a stress-reliability
  observation, not part of the benchmark result; the reported logs are from
  successful sessions.
- An earlier loaded attempt with eight busy loops exceeded the thermal abort
  threshold (79.4 → 84.5 °C) within seconds. It was aborted and is retained only
  as `loaded-thermal-abort-84C.log`; it is not used in the tables.
- Idle scaling has a bimodal tail; a repeat is retained as
  `idle-repeat-bimodal.log`. Report medians together with minima/p95, not as a
  single precise constant.
