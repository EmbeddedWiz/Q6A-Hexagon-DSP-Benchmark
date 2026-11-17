# CDSP performance mode A/B — 2026-11-12

## Purpose

Check whether the CDSP DCVS/performance state was pinned in the original
measurements, and measure the FOC benchmark in the default and maximum
performance modes. This does not replace `results/final/`.

## Conditions

- Board: Radxa Dragon Q6A / QCS6490, Hexagon v68 CDSP.
- Commit: `a5a2b5e` (`P1.1_dsp_perf_mode`).
- Build host: `builder@build-host`, SDK 6.4.0.2.
- Workload: P-only FOC, warm-up 256 steps, 3 independent processes per mode,
  5 timed repetitions of 16,384 steps each.
- Mode: default DCVS versus `HAP_power_set(HAP_power_set_DCVS_v2,
  HAP_DCVS_V2_PERFORMANCE_MODE)` with target/max corner TURBO.
- Thermal: 49.6 C before and after both modes; CPU 7 at 2,707,200 kHz before
  and after.
- Raw log: `results/extra-2026-11-12/dsp-perf/bench-default-vs-max.log`.

## Results

| Mode | Cycles/step, process medians | Median cycles | Median ns/step | Effective clock (Gcycles/s) |
|---|---:|---:|---:|---:|
| default | 377.021, 377.025, 378.776 | **377.025** | **325.276** | 1.108–1.162, median **1.159** |
| max perf | 376.992, 376.984, 376.982 | **376.984** | **280.644** | 1.323–1.363, median **1.343** |

All checksums are `2,827,389,792`, all statuses are 0, all runs PASS.

## Interpretation

- Maximum performance mode raises the effective rate by **15.9%**
  (1.159 → 1.343 Gcycles/s) and lowers ns/step by **13.7%**
  (325.276 → 280.644).
- Instructions do not change: cycle medians differ by **0.011%**
  (377.025 → 376.984). The change is clock state, not code.
- Max mode was more stable in this A/B: 376.982–376.992 cycles/step and
  1.323–1.363 Gcycles/s. Default mode had one 378.776-cycle process and a
  1.108 Gcycles/s low point.
- `HAP_power_set` returned success in the unsigned PD; the runner now has an
  explicit `--dsp-perf` flag and restores the default up/down policy afterward.

## Limitations

- The canonical `results/final/` numbers were measured in default mode and are
  not replaced. This A/B is additional evidence.
- The CPU side was not run in the same A/B, so the DSP/CPU ratio under max mode
  is not claimed. A paired CPU run is required before changing the headline.
- The default-mode low point shows that unpinned DCVS is a real source of
  run-to-run ns variance; cycles remain the primary metric.
