# Motor control workload — fixed-point FOC

## Purpose

This workload is the scalar control path used for the CPU/DSP qualification
comparison. It is deliberately a realistic fixed-point FOC chain, not a
microbenchmark: Clarke transform, CORDIC sine/cosine, Park transform, two PI
current controllers, inverse Park, three-phase reconstruction and centered
SVPWM duty generation.

The controller is serial and stateful. Each step depends on the previous PI
integrator state, so the loop cannot be vectorized wholesale.

## Files

| File | Role |
|---|---|
| `workloads/motor_control/foc_fixed.h/.cpp` | FOC controller, Q15 arithmetic, `configure()`/`reset()`/`step()` |
| `workloads/motor_control/motor_control_fixed.h/.cpp` | CORDIC `sin_q15`, `cos_q15`, fused `sincos_q15`, three-phase helper |
| `dsp/fastrpc/foc_bench_skel_impl.cpp` | DSP RPC methods and benchmark loop |
| `host/foc_runner.cpp` | host validation, benchmark and CPU comparison |
| `validation/foc/` | independent FP32 model and deterministic vectors |

## Inputs and outputs

Inputs per step:

```text
ia_q15, ib_q15          phase currents, Q15
id_ref_q15, iq_ref_q15  current references, Q15
electrical_angle        14-bit turn, 0..16383
```

Outputs per step:

```text
id_q15, iq_q15          Park currents, Q15
vd_q15, vq_q15          PI voltages, Q15
duty_a_q15              unsigned Q15, 0..32767
duty_b_q15              unsigned Q15, 0..32767
duty_c_q15              unsigned Q15, 0..32767
```

The default gains are supplied by the caller. The benchmark uses:

```text
kp_d = 2500, ki_d = 120, kp_q = 2500, ki_q = 120
```

These are test gains for numerical qualification, not a tuned plant
compensation.

## Algorithm summary

1. Clarke: `alpha = ia`, `beta = (ia + 2*ib)/sqrt(3)`.
2. CORDIC: one pass for `sin` and `cos` at the electrical angle.
3. Park: `id = alpha*cos + beta*sin`, `iq = -alpha*sin + beta*cos`.
4. PI: `v = sat(prop + integral)`, with integrator clamping.
5. Inverse Park: `v_alpha = vd*cos - vq*sin`,
   `v_beta = vd*sin + vq*cos`.
6. Three-phase: `va = v_alpha`,
   `vb = -va/2 + v_beta*sqrt(3)/2`,
   `vc = -va/2 - v_beta*sqrt(3)/2`.
7. Centered SVPWM: subtract `(vmax + vmin)/2` and clamp duty to `[0, 32767]`.

Exact fixed-point constants and rounding rules are in `docs/validation.md`.

## Benchmark inputs

The timed loop uses a deterministic sequence:

```text
ia     = 12000
ib     = -7000
id_ref = 3000
iq_ref = -2000
angle  = i & 0x3FFF
```

for `i = 0 .. 16383`. The controller is reset at the start of each repetition.
This gives a repeatable checksum and exercises all angle quadrants.

## Measurement

- DSP: 256 warm-up steps + five timed repetitions of 16,384 steps inside one
  `bench` RPC. Raw `qurt_get_core_pcycles()` counts are returned.
- CPU: 256 warm-up steps + 2,000,000 untimed clock-ramp steps, then five timed
  repetitions of 16,384 steps, pinned to CPU 7.

## Results

| Variant | Cycles/step | ns/step | Checksum |
|---|---:|---:|---:|
| DSP baseline, two CORDIC passes | 593.84 | 524.13 (median) | 2,827,389,792 |
| DSP optimized, fused `sincos_q15` | **376.996** | **320.96 (median)** | 2,827,389,792 |
| CPU fixed-point | — | **60.73** | 2,827,389,792 |

The optimization removes one 16-iteration CORDIC pass per step: 36.52% fewer
cycles, 1.575× cycle speed-up. Main validation is unchanged (0 exact
mismatches, max 13.275 LSB vs FP32); the separate high-ki PI clamp test also
passes with 0 mismatches and a 4.114 LSB maximum.

The DSP is ≈5.28× slower in wall time for this serial workload (5.40× on the
same-source repeat run). See `docs/engineering-findings.md` for why this is expected and
why the historical HVX vector-add result is not applicable.

## What this workload does not prove

- No physical motor, inverter, current sensor or PWM capture.
- No closed-loop plant stability or tuning claim.
- No HVX suitability claim.
- No hard-real-time deadline claim; the CPU baseline is a steady-state
  throughput measurement, not a WCET analysis.
