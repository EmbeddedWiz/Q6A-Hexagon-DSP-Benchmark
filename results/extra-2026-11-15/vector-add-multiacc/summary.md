# Vector-add: one versus four HVX accumulators — 2026-11-16

## Purpose

Test whether the 0.0936 ns/lane-add HVX result was limited by a single
accumulator dependency chain. The existing `mode=1` kernel is unchanged; a new
`mode=2` kernel keeps the same data, iteration count, checksum and 128-byte HVX
width, but uses four independent accumulators and combines them modulo 2^16.

## Conditions

- Board: Radxa Dragon Q6A / QCS6490, Hexagon v68 CDSP; CPU 7 is an A78.
- Commit: `0850348`.
- 3 independent processes per size; 5 timed repetitions per process, one
  untimed warm-up; 5,000 iterations; 64 and 4,096 lanes.
- CPU 7 pinned with `taskset -c 7`; CPU 7 governor `schedutil`.
- DSP times are cycle-derived at the direct **1.170 GHz** qtimer measurement;
  CPU times are host wall-clock.
- Thermal: 37.9 °C before, 38.7 °C after.
- Raw log: `vector-add-multiacc.log`.
- Checksums: **1,575,000** (64 lanes) and **102,375,000** (4,096 lanes); all
  five implementations match in every process and run.

## Results (median of 3 process medians, ns per lane-add)

| Lanes | CPU scalar | CPU NEON | DSP scalar | DSP HVX (1 acc.) | DSP HVX (4 acc.) | DSP scalar / 1 acc. | DSP scalar / 4 acc. |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 0.866 | 0.103 | 6.107 | 0.0936 | **0.0269** | 65.23× | **227.20×** |
| 4,096 | 0.915 | 0.0614 | 6.014 | 0.0936 | **0.0268** | 64.28× | **224.62×** |

DSP cycles per element: scalar 7.146 / 7.037, one accumulator 0.1095 / 0.1095,
four accumulators **0.03145 / 0.03133** (64 / 4,096 lanes). The four-accumulator
kernel is **3.48× / 3.49×** faster than the one-accumulator kernel at the two
sizes.

## Interpretation

- The one-accumulator kernel was dependency-limited at about 7 cycles per
  64-lane vector. Four independent chains reduce that to about 2.0 cycles per
  iteration (64 lanes) and 128.3 cycles per iteration (4,096 lanes = 64
  vectors).
- Against CPU NEON, the four-accumulator HVX kernel is **3.83× faster** at 64
  lanes and **2.29× faster** at 4,096 lanes. The earlier 1.61× NEON advantage at
  4,096 lanes was specific to the unoptimized single-accumulator kernel and is
  superseded by this result.
- Against DSP scalar, the four-accumulator speedup is **227× / 225×**, versus
  65× / 64× for one accumulator. The historical 66.8× remains an intra-DSP
  one-accumulator result, not a DSP-versus-CPU result.

## Limitations

- This is an unrolled four-accumulator C kernel, not hand-written assembly or a
  general HVX optimization study. Other unroll factors were not measured.
- CPU wall time with `schedutil` includes frequency variation; CPU medians are
  stable across the three processes but are not a hard real-time bound.
- DSP ns values are cycle-derived at 1.170 GHz, not direct wall-clock
  measurements. Cycles are the primary DSP metric.
- This is a vector-add microbenchmark, not FOC and not a general DSP-vs-CPU
  claim.
