# Corrected baseline rebuild — 2026-11-12

## Why

The retained baseline log predates the effective-clock fix. Its per-step cycles
and RPC wall time are valid, but its derived `effective_clock_hz` and
`median_ns_per_step` fields are 5x wrong. This rebuild closes that gap without
touching `results/final/`.

## Conditions

- Board: Radxa Dragon Q6A / QCS6490, Hexagon v68 CDSP.
- Source: `27b85ab` plus the one-line baseline restore
  (`sincos_q15` replaced by `sin_q15` + `cos_q15`) in
  `workloads/motor_control/foc_fixed.cpp`.
- Build host: `builder@build-host`, SDK 6.4.0.2.
- Workload: P-only FOC, warm-up 256 steps, 3 independent processes, 5 timed
  repetitions of 16,384 steps each.
- Post-run state: max thermal zone 47.3 C, CPU 7 at 2,707,200 kHz. The CDSP
  performance/DCVS mode was not pinned; this is the same limitation as the main
  run and is tracked separately.
- Raw log: `results/baseline/bench-baseline-3x-corrected.log`.

## Results

| Metric | Process 1 | Process 2 | Process 3 | Median |
|---|---:|---:|---:|---:|
| Cycles/step | 593.8341 | 593.8443 | 593.8531 | **593.8443** |
| ns/step | 524.128 | 515.245 | 524.924 | **524.128** |
| RPC wall, 5 x 16,384 steps (ms) | 43.211 | 42.630 | 43.283 | **43.211** |
| Effective clock (Gcycles/s) | 1.133 | 1.153 | 1.131 | **1.133** |
| Checksum | 2,827,389,792 | 2,827,389,792 | 2,827,389,792 | — |

All checksums match, all statuses are 0, all runs PASS.

## Comparison

The cycle count is stable against the original baseline (593.844 vs 593.849,
within 0.001%). The wall/ns values are higher than the old 41.44 ms / 505.6 ns
because this rebuild measured a lower effective rate: 1.13–1.15 Gcycles/s versus
1.15–1.18 Gcycles/s in the optimized final run. This is exactly the kind of
clock-state variation that the DSP performance-mode probe is meant to bound.
Cycles remain the primary optimization metric.

```text
cycle reduction = (593.844 - 376.996) / 593.844 = 36.52%
cycle speed-up  = 593.844 / 376.996              = 1.575x
```

## Limitations

- The baseline was rebuilt in an isolated runner directory; it does not replace
  `results/final/`.
- CDSP performance mode and direct DSP time were not pinned/measured in this
  rebuild; those remain Priority 1 items.
- Thermal state was recorded only after the run; no thermal soak was performed.
