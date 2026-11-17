# Validation

## What is being validated

The workload is a fixed-point FOC controller followed by centered SVPWM duty
generation. Validation has two independent oracles:

1. **Bit-exact CPU oracle** — the same `Controller` source compiled for AArch64
   with vectorization disabled. Any difference from the DSP is a defect in the
   transport, the DSP build, or the state machine, not a quantization choice.
2. **Independent FP32 model** — a separate implementation in
   `validation/foc/foc_reference.cpp` using `double`, `std::sin`, `std::cos`,
   floating-point Clarke/Park/inverse-Park and floating-point PI. It measures
   how far the Q15 implementation is from the ideal algorithm; it is not
   expected to match bit-for-bit.

## Fixed-point formats

| Signal | Format | Range / note |
|---|---|---|
| `ia_q15`, `ib_q15` | Q15 | −32767..32767 |
| `id_ref_q15`, `iq_ref_q15` | Q15 | −32767..32767 |
| `electrical_angle` | 14-bit turn | 0..16383; 16384 = 360° |
| `id_q15`, `iq_q15` | Q15 | saturated to ±32767 |
| `vd_q15`, `vq_q15` | Q15 | saturated to ±32767 |
| duties | unsigned Q15 | 0..32767 |
| gains `kp_*`, `ki_*` | Q15 | supplied by caller |

Arithmetic uses 64-bit intermediates for products and saturates to the
asymmetric range ±32767 (not −32768). Q15 products are shifted right by 15
**individually** before they are summed; this is not the same as shifting the
sum once. Right shifts are arithmetic (round toward negative infinity), while
the centered-SVPWM `/2` divisions truncate toward zero. These conventions are
intentional and are identical in the CPU oracle and the DSP build. The FP32
model does not reproduce integer truncation; that difference is part of the
measured quantization error.

## Algorithm

### Clarke

```text
alpha = ia
beta  = (ia + 2*ib) * 18919 >> 15        // 18919 ≈ 32767/sqrt(3)
```

### CORDIC sine/cosine

- External angle: 14 bits per turn.
- Internal angle: 18 bits per turn (4 extra fractional bits).
- Quadrant reduction to `[0, π/2]`.
- 16 iterations with the `atan(2^-i)` table in 18-bit phase units.
- Initial vector `x = 19898` (Q15 `1/K`, K ≈ 1.646760258).
- Outputs saturated to ±32767.
- `sincos_q15()` performs one CORDIC pass and returns both `sin` and `cos`.
  `sin_q15()` and `cos_q15()` are retained as wrappers for the historical
  two-pass comparison.

### Park

```text
id = mul_q15(alpha, cos) + mul_q15(beta, sin)
iq = -mul_q15(alpha, sin) + mul_q15(beta, cos)
mul_q15(a, b) = (int64_t(a) * b) >> 15
```

Both results are saturated to ±32767. Each product is truncated separately
before the sum.

### PI

```text
proportional = (error * kp) >> 15
increment    = (error * ki) >> 15
integral    += increment
integral     = clamp(integral, -32767, 32767)
voltage      = clamp(proportional + integral, -32767, 32767)
```

The integrator is clamped **before** adding the proportional term, so wind-up
recovery is bounded but the output can still saturate.

### Inverse Park

```text
v_alpha = mul_q15(vd, cos) - mul_q15(vq, sin)
v_beta  = mul_q15(vd, sin) + mul_q15(vq, cos)
```

Both are saturated to ±32767. As in Park, each product is truncated before the
sum.

### Three-phase voltages and centered SVPWM

```text
va = v_alpha
vb = -v_alpha/2 + (v_beta * 28378 >> 15)   // 28378 ≈ 32767*sqrt(3)/2
vc = -v_alpha/2 - (v_beta * 28378 >> 15)

vmax = max(va, vb, vc)
vmin = min(va, vb, vc)
common = -(vmax + vmin) / 2

duty_x = clamp(16384 + (v_x + common)/2, 0, 32767)
```

The `common` term is the standard centered space-vector common-mode injection.
Duties are unsigned and clamped; no hardware PWM is driven.

## Test vectors

`validation/foc/foc_vectors.cpp` builds 20,510 cases:

| Group | Count | Coverage |
|---|---:|---|
| Edge cases | 30 | zero; five axis angles; positive/negative saturation at 0° and 90°; d-only; q-only; mixed signs; angle 16383; 8-step large-error hold; 8-step large-error reverse |
| Deterministic random | 4,096 | LCG seed, full Q15 current/reference range, random 14-bit angle |
| Angle sweep | 16,384 | every angle code, fixed currents and references |

The random generator is:

```text
state = state * 1664525 + 1013904223
```

with 32-bit wrap, currents taken as `(int16_t)(state >> 16)` and angle as
`(state >> 18) & 0x3FFF`.

Each case is sent as one `step` RPC. The first case of a group resets the
integrators; subsequent cases in the large-error groups intentionally do **not**
reset, so state retention across calls is exercised.

The main run is P-only (`kp=8192, ki=0`), matching the benchmark configuration.
A dedicated PI clamp run reuses the 30 edge cases with `ki=32767`, which drives
the integrator into its clamp and then reverses the error. That run is separate
so the 20,510-step FP32 statistics remain P-only and comparable to the timed
workload.

## Results

Final run (`results/final/validation.log`, CSV in
`results/final/validation.csv.gz`). This is the **P-only** main suite:

| Metric | Value |
|---|---:|
| Cases / DSP steps | 20,510 |
| DSP RPC failures | 0 |
| Exact CPU↔DSP mismatches | **0** |
| Values compared against FP32 | 143,570 (7 per step) |
| FP32 maximum absolute error | **13.2753 LSB** |
| FP32 mean absolute error | **0.71681 LSB** |
| ≤ 1 LSB | 108,695 (75.71%) |
| ≤ 2 LSB | 137,037 (95.45%) |
| ≤ 5 LSB | 143,318 (99.82%) |
| ≤ 16 LSB | 143,570 (100%) |
| ≤ 32 LSB | 143,570 (100%) |
| > 32 LSB | 0 |
| Checksum | 2,827,389,792 |

Dedicated PI clamp run (`results/final/validation-pi.log`, CSV in
`results/final/validation-pi.csv.gz`), `kp=8192, ki=32767`:

| Metric | Value |
|---|---:|
| Cases / DSP steps | 30 |
| DSP RPC failures | 0 |
| Exact CPU↔DSP mismatches | **0** |
| Values compared against FP32 | 210 (7 per step) |
| FP32 maximum absolute error | **4.1145 LSB** |
| FP32 mean absolute error | **0.87747 LSB** |
| ≤ 1 LSB | 142 (67.62%) |
| ≤ 2 LSB | 178 (84.76%) |
| ≤ 5 LSB | 210 (100%) |
| > 32 LSB | 0 |

The baseline (two CORDIC passes) and optimized (`sincos_q15`) builds have
**identical** main validation results: zero exact mismatches and the same FP32
statistics. The optimization changed the number of CORDIC passes, not the
arithmetic result.

A full 20,510-step run with `ki=120` was also measured and retained in
`results/pi120-2026-11-08/`: CPU and DSP stayed bit-exact (0
mismatches), but the FP32 error rose to 2,143 LSB maximum and 197.8 LSB mean.
The cause is the per-step floor truncation of the integral, which accumulates
over tens of thousands of steps. This is a quantization property, not a parity
failure, and is why the main table is P-only.

### Interpreting the 13.275 LSB maximum

A 13.3 LSB error on a Q15 value is about 0.04% of full scale. It is the
accumulated effect of:

- CORDIC angle quantization (14-bit input, 16 iterations);
- truncating right shifts in Clarke/Park/inverse Park;
- Q15 gain quantization (`18919`, `28378`, `19898`);
- PI proportional path (the main suite has `ki=0`; integrator behavior is
  covered by the dedicated high-ki test);
- final duty clamping.

It is not evidence of a transport or state bug: the CPU fixed-point oracle
matches the DSP exactly on all 20,510 steps, including the worst FP32 case.

## CORDIC history

An earlier exhaustive CORDIC-only test compared all 16,384 angle codes and
reported a maximum error of 5 LSB. That test covered the sine/cosine kernel in
isolation. The current 13.275 LSB figure covers the whole FOC chain, including
the P-only PI path and SVPWM, so the two numbers are not directly comparable.
The earlier kernel-only result is historical context, not a published artifact.

## Limitations

- Only final controller outputs are compared. Per-stage intermediate values are
  not dumped over RPC; the independent FP32 model reconstructs the expected
  chain mathematically.
- No physical motor, inverter, current sensor or PWM capture is used. The
  duties are numerical.
- No temperature sweep or long-duration numerical stability test was run.
- The CPU oracle is compiled from the same source, so it detects transport and
  DSP-build divergence, not a shared algorithmic mistake. The FP32 model is the
  independent check for algorithm-level error.
- The checksum is a 16-bit-per-field sum and is not a proof of equality; the
  per-case CSV is.
- The PI clamp test is a targeted 30-case run, not a long-duration stability
  test. A long `ki>0` run shows accumulated integral truncation bias against
  FP32, while CPU/DSP parity remains exact.

## Reproduce

```bash
cd ~/Projects/Q6A-Hexagon-DSP-Benchmark
./scripts/run_foc_suite.sh --no-build --out /tmp/foc-check
zcat /tmp/foc-check/validation.csv.gz | head
zcat /tmp/foc-check/validation-pi.csv.gz | head
```

A passing run prints `"result":"PASS"` and exits 0. Any nonzero RPC result,
nonzero status bit or exact mismatch fails the suite.
