# Vector-add: CPU scalar, CPU NEON, DSP scalar, DSP HVX — 2026-11-13

## Purpose

P3.1: compare four implementations on one workload and two data sizes.
P3.2: keep the historical 66.8× result correctly labelled as **DSP scalar vs
DSP HVX**, not DSP vs CPU.

Workload: 16-bit vector accumulation, `acc[lane] += sum[lane]`, 5,000
iterations. Sum data is `(lane % 7) + (lane % 5)`. Implementations:

- CPU scalar: A78 scalar loop, compiled with auto-vectorization disabled.
- CPU NEON: 8×uint16 adds, AArch64.
- DSP scalar: Hexagon scalar loop.
- DSP HVX: 64×uint16 vector adds, 128-byte HVX.

## Conditions

- Board: Radxa Dragon Q6A, Hexagon v68 CDSP + A78 CPU 7.
- Commit: b28915d.
- 3 independent processes per size, 5 timed repetitions per process; CPU 7
  pinned with `taskset`.
- DSP times are derived from cycle counts at the measured **1.17 GHz** direct
  clock (`dsp/probes/time_probe.c`); CPU times are host wall-clock.
- Raw log: `vector-add.log`; plot: `vector-add.svg`.
- Checksums match across all four implementations and all runs: **1,575,000**
  (64 lanes) and **102,375,000** (4096 lanes).

## Results (median of 3 process medians, ns per lane-add)

| Lanes | CPU scalar | CPU NEON | DSP scalar | DSP HVX | DSP scalar / DSP HVX |
|---:|---:|---:|---:|---:|---:|
| 64 | 0.855 | 0.103 | 6.093 | **0.0936** | **65.08×** |
| 4,096 | 0.927 | 0.0581 | 6.014 | **0.0936** | **64.28×** |

DSP cycle counts per element are 7.129 / 0.1095 (64 lanes) and 7.036 / 0.1095
(4096 lanes) for scalar / HVX.

## Interpretation

- **The historical 66.8× is an intra-DSP result:** DSP scalar versus DSP HVX.
  It is not a DSP-versus-CPU result. The generic 64/4096-lane rebuild gives
  65.08× / 64.28×, close to the historical 66.8×; the difference is code-layout
  and loop-structure change, not a different workload.
- DSP scalar is **7.1×** slower than CPU scalar at 64 lanes and **6.5×** slower
  at 4096 lanes.
- DSP HVX versus CPU NEON is size-dependent: DSP is **1.10× faster** at 64
  lanes, but **1.61× slower** at 4096 lanes. The 64-lane case has only eight
  NEON vectors per iteration, so loop/setup costs weigh more; at 4096 lanes
  CPU NEON reaches 0.058 ns per lane-add.
- DSP HVX is 9.1–9.9× faster than CPU **scalar**, but not faster than CPU NEON
  at the larger size. Both vector implementations are far ahead of scalar.

## Limitations

- CPU wall time includes frequency-governor variation; the scalar medians are
  stable across the three processes (0.76–0.89 ns at 64 lanes, 0.90–0.93 ns at
  4096), but absolute values are not a hard real-time bound.
- DSP ns values are cycle-derived at 1.17 GHz, not direct wall-clock
  measurements. The cycle counts are the primary DSP metric.
- This is a microbenchmark, not a FOC result and not a general DSP-vs-CPU
  claim.
