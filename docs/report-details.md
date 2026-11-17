# Validation, history, and exact values

Detail moved out of the main README to keep the entry short. The main report is
[`README.md`](../README.md).

## Validation detail

Validation has two independent oracles:

1. **Bit-exact CPU oracle:** the same `Controller` source compiled for AArch64 with vectorization disabled. Any difference from the DSP is a transport, build or state-machine defect.
2. **Independent FP32 model:** a separate implementation in `validation/foc/foc_reference.cpp` using `double`, `std::sin`, `std::cos`, floating-point Clarke/Park/inverse-Park and floating-point PI. It measures distance from the ideal algorithm; it is not expected to match bit-for-bit.

### 8.1 Main P-only suite

Final run files: `results/final/validation.log`, `results/final/validation.csv.gz`.

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

The vector groups are:

- **30 edge cases:** zero, five axis angles, positive/negative saturation at 0° and 90°, d-only, q-only, mixed signs, angle 16383, 8-step large-error hold and 8-step large-error reverse.
- **4,096 deterministic random steps:** LCG seed, full Q15 current/reference range, random 14-bit angle.
- **16,384 angle codes:** every angle, fixed currents and references.

The random generator is `state = state * 1664525 + 1013904223`, with 32-bit wrap; currents are `(int16_t)(state >> 16)` and angle is `(state >> 18) & 0x3FFF`.

### 8.2 Dedicated PI clamp test

The main suite is P-only (`ki=0`), so it does **not** exercise integrator wind-up. A separate 30-case run uses `kp=8192, ki=32767`:

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

Files: `results/final/validation-pi.log`, `results/final/validation-pi.csv.gz`.

### 8.3 Long `ki=120` experiment

A full 20,510-step run with `ki=120` is retained in `results/pi120-2026-11-08/`. CPU and DSP still match exactly (0 mismatches), but the FP32 error grows to **2,143.09 LSB maximum** and **197.80 LSB mean** (53,665 values >32 LSB). The cause is per-step floor truncation of the integral accumulating over tens of thousands of steps. This is a real quantization effect, not a parity failure; it is why the main FP32 table is deliberately P-only.

### 8.4 Interpretation of the 13.275 LSB maximum

A 13.3 LSB error on a Q15 value is about 0.04% of full scale. It comes from:

- CORDIC angle quantization (14-bit input, 16 iterations);
- truncating right shifts in Clarke/Park/inverse Park;
- Q15 gain quantization (`18919`, `28378`, `19898`);
- the P-only proportional path;
- final duty clamping.

It is not evidence of a transport or state bug: the CPU fixed-point oracle matches the DSP exactly on all 20,510 steps, including the worst FP32 case.

## Clock and baseline notes

The runner fallback is 1.16 GHz. The measured effective rate is
**1.15–1.19 Gcycles/s**, so the fallback was close. The rate is derived from all
repetitions in an RPC and is labelled `effective_clock_hz`; it is not a PLL
measurement. Cycle counts are the primary metric.

### 12.5 Integral truncation bias

A long `ki>0` run accumulates floor-truncation bias in FP32 comparison: `ki=120` produced 2,143 LSB max / 197.8 LSB mean over 20,510 steps, even though CPU and DSP remained bit-exact. The main validation is therefore P-only; the PI clamp test is short and separately reported.

## Thermal and environment detail

The retained thermal run covered **49.6 °C idle → 68.0 °C hot**, then a 60 s
cooldown to 50.4 °C; CPU 7 stayed at 2,707,200 kHz in every probe. The 69–79 °C
band was not covered. Later sessions measured 48.5 °C (direct-time probe),
49.6 °C (DSP performance A/B), 53.6 → 58.3 °C (idle RPC), and 59.0 → 62.2 °C
(light CPU-loaded RPC). An eight-loop load test reached 84.5 °C within seconds
and was aborted; it is retained only as an invalid stress artifact.

## Exact final-run values

**Run:** suite start 2026-11-10 23:04:55 UTC, kernel `6.18.2-3-qcom`.

**DSP optimized benchmark:**

| Process | Raw cycles/step (5 repetitions) | Median cycles/step | Median ns/step | RPC wall (µs) | Effective rate (Mcycles/s) |
|---|---|---:|---:|---:|---:|
| 1 | 376.9785767, 377.0208740, 376.9939575, 377.0083618, 376.9963989 | 376.9964 | 319.332 | 26,651.882 | 1,180.580 |
| 2 | 377.1176147, 376.9769897, 376.9947510, 376.9876709, 377.0330200 | 376.9948 | 327.207 | 26,963.954 | 1,152.160 |
| 3 | 376.9552002, 377.0085449, 377.0112915, 377.0023193, 377.0270386 | 377.0085 | 320.960 | 26,686.517 | 1,174.630 |

**CPU fixed benchmark:**

| Process | Median ns/step |
|---|---:|
| 1 | 60.534 |
| 2 | 60.734 |
| 3 | 60.788 |

**Checksum:** `2,827,389,792` for all DSP runs and the CPU run.

**Thermal:** 49.6 °C idle, 68.0 °C hot, 50.4 °C after 60 s cooldown. Maximum 68.0 °C, below the 79 °C ceiling. DSP cycles/step slope −0.0013 cycles/°C; CPU 7 at 2,707,200 kHz in every probe.

## Artifact hashes

Final build artifacts (not committed; from `results/final/artifacts.sha256`):

```text
bc6d5f1fc23edc670b5a58c96a4ad2189966f5748862d12a79ef17141ebf1114  build/foc/generated/libfoc_bench_skel.so
103d09155cbdde05617360307466639bb26a567d8e3a98bbe82f3923b75d4a8b  build/foc/foc_runner
d4eaf048f5352aca5c3b09a53580b4234a12c70b5ff10d56884bf6063dc6f28d  build/foc/generated/foc_bench.h
```

The exact result-file hashes are in `results/final/SHA256SUMS`; the repeat run has its own `SHA256SUMS` under `results/repeatability/`.

---

This report is based on the repository's retained evidence. For design choices, read `docs/decisions-log.md`; for engineering findings and limitations, read `docs/engineering-findings.md`.
