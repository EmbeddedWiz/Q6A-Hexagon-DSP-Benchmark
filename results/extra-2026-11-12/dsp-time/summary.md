# Direct DSP time vs indirect effective clock — 2026-11-12

## Purpose

P1.2: determine whether a direct DSP time source is available and compare it
with the indirect effective-clock calculation used by the FOC benchmark.

## Method

A standalone CDSP probe (`dsp/probes/time_probe.c`) runs 1,000,000 iterations
of a fixed 32-bit LCG loop between:

- `HAP_perf_get_time_us()` — the 19.2 MHz qtimer; and
- `qurt_get_core_pcycles()` — the core cycle counter.

`run_main_on_hexagon` returns only `main()`'s exit code, so the probe encodes
the measured direct clock in 10 MHz units. A return value of 117 means
`117 × 10 MHz = 1.17 GHz`; 255 means no nonzero interval was observed.

The indirect side is the normal FOC `bench` mode on the same board and session:
cycles divided by RPC-overhead-corrected host wall time. Three independent
`bench` processes are used, each with 5 repetitions of 16,384 steps.

## Conditions

- Board: Radxa Dragon Q6A / QCS6490, Hexagon v68 CDSP.
- Commit: `29188bb` (`P1.1_dsp_perf_ab_results`); FOC code at `a5a2b5e`.
- Thermal during the probe: 48.5 °C.
- Raw logs: `time-probe.log`, `bench-indirect.log` in this directory.

## Results

Direct probe, five runs: **117, 117, 117, 117, 117** → **1.170 GHz**
(±0.005 GHz quantization).

Indirect FOC runs:

| Process | Median cycles/step | Median ns/step | Effective clock (GHz) |
|---|---:|---:|---:|
| 1 | 376.995 | 323.325 | 1.1660 |
| 2 | 377.647 | 342.201 | 1.1036 |
| 3 | 376.983 | 325.782 | 1.1572 |
| **median** | **376.995** | **325.782** | **1.1572** |

Direct vs indirect median: **+1.11%** (1.170 vs 1.1572 GHz). Against the three
individual indirect values, the direct clock is +0.34%, +6.02%, and +1.11%
higher. All checksums are `2,827,389,792`; all statuses are 0.

## Interpretation

- A direct DSP time source is available in the unsigned PD: both the qtimer and
  the core cycle counter advance and produce a stable 1.17 GHz reading.
- The indirect effective-clock estimate agrees at the median within about 1%.
  Its run-to-run spread (1.104–1.166 GHz) is larger than the direct-vs-indirect
  bias, so the indirect estimate is adequate for the report but not a precise
  clock measurement.
- The direct probe is stable across processes; the indirect estimate is not.
  This supports keeping cycles as the primary metric and treating ns/step as a
  conditions-dependent value.

## Important limitation: instrumentation perturbs the hot loop

Adding a `time_probe` method and `HAP_perf_get_time_us()` calls to the FOC
skeleton changed the measured hot-loop cost from about 377.0 to 383.6
cycles/step (+1.8%), even when the probe was not called. The cause is code
layout/alignment/register-allocation churn in the same shared object, not the
timer itself. The time probe was therefore removed from the FOC skeleton and
kept as a separate standalone binary. The canonical FOC results remain
unaffected.

## Reproduce

Build the probe on the SDK host (the extra `incs/stddef` include is required by
`HAP_perf.h`):

```sh
hexagon-clang -mv68 -G0 -O2 -fpic \
  -I$SDK/incs -I$SDK/incs/stddef \
  -I$SDK/rtos/qurt/computev68/include/qurt \
  -shared dsp/probes/time_probe.c -o time_probe.so
```

Copy it to the board runner directory and run five times:

```sh
./run_main_on_hexagon 3 time_probe.so stack_size=0x50000
```

Then run the normal FOC benchmark and compare with its `effective_clock_hz`.
