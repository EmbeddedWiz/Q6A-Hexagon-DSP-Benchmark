# Engineering findings and limitations

This page records the findings that shaped the final design and the limits of
the delivered evidence. Each claim is tied to a retained artifact. Superseded
development logs and intermediate failures are not part of the published tree.

## Positive findings

### F1 — A project-owned FastRPC contract works end to end

The IDL in `dsp/idl/foc_bench.idl` is generated with QAIC, the skeleton is
built with `hexagon-clang++ -mv68`, and the host runner opens it in the CDSP
unsigned User PD. Five 64-bit echo vectors returned the bitwise complement of
the input (`results/final/echo.log`), proving that both halves of a 64-bit
scalar survive the boundary.

### F2 — CPU↔DSP bit-exact parity holds over 20,510 steps

The fixed-point CPU oracle and the DSP build produced zero mismatches across
edge cases, 4,096 random steps and all 16,384 angle codes. The independent FP32
model measured a maximum of 13.275 LSB and a mean of 0.717 LSB, with all
143,570 values within 16 LSB (`results/final/validation.log`). The main run is
P-only; a separate 30-case `ki=32767` run adds zero-mismatch PI clamp/recovery
coverage with a 4.114 LSB maximum FP32 error (`validation-pi.log`).

### F3 — Fusing the CORDIC pass is a real, correctness-preserving optimization

Baseline (two passes): 593.85 cycles/step. Optimized (`sincos_q15`, one pass):
376.996 cycles/step. Reduction: 36.52%; cycle speed-up: 1.575×. The checksum
and validation statistics are identical. The retained baseline is in
`results/baseline/bench-baseline-3x-corrected.log`.

### F4 — CPU measurements become stable with affinity and a clock ramp

Without pinning, the same CPU workload varied substantially. Pinned to CPU 7
with a 2,000,000-step untimed ramp, three process medians were 60.534, 60.734
and 60.788 ns/step, with a spread below 0.5%. The governor is `schedutil` with
`qcom-cpufreq-hw`; the project records this rather than changing it.

### F5 — The unsigned User PD is sufficient for this scalar workload

No RPCMEM, DMA-BUF, secure PD, threads or heap allocation are required. A
single global controller and scalar RPC arguments are enough. This keeps the
trust boundary simple and avoids unproven pointer/layout assumptions.

### F6 — Raw evidence is reproducible in one command

`scripts/run_foc_suite.sh` rebuilds, checks transport, validates 20,510 steps,
runs the dedicated PI clamp test, runs three DSP processes and three pinned CPU
processes, captures thermal and environment data, compresses both CSVs, writes
`summary.json`/`summary.md` and hashes every result file.

### F7 — PI clamp and recovery are validated separately and cheaply

A 30-case run with `ki=32767` keeps the integrator in the clamp for several
steps and then reverses the error. CPU and DSP match exactly, and the FP32
maximum is 4.114 LSB. This gives explicit integrator coverage without mixing
the high-`ki` transient into the 20,510-step P-only statistics.

## Limitations

### L1 — The DSP is about 5.28× slower for this scalar FOC

The dependency-chained scalar loop does not expose data-level parallelism. The
DSP is about 5.28× slower than the pinned A78 core by median ns (5.40× on the
same-source repeat run). This is a property of the workload, not a platform
failure, and it is why the HVX vector-add result is reported separately.

### L2 — No RTOS tick timer; a separate qtimer probe is possible

`qurt_timer_get_ticks()` returns 0, so FOC uses host wall time and an
RPC-overhead correction and labels the result an **effective** rate
(1.15–1.19 Gcycles/s). A standalone `HAP_perf_get_time_us()` probe measured
**1.170 GHz ± 0.005 GHz**, but it is not linked into the FOC skeleton because it
perturbed the hot loop by +1.8%. Direct minus effective is +1.11% in the same
session. FOC uses the effective rate; HVX ns uses the direct value. Cycles remain
the primary metric; the report does not claim the physical CDSP PLL frequency.
Source: `results/extra-2026-11-12/dsp-time/`.

### L3 — Long `ki` runs accumulate integral truncation bias

A full 20,510-step run with `ki=120` kept CPU↔DSP parity exact but raised the
FP32 error to 2,143 LSB maximum and 197.8 LSB mean. Each integral increment is
floored by `>> 15`, and the small negative bias accumulates over tens of
thousands of steps. This is a quantization property, not a transport defect;
the main FP32 table is deliberately P-only, and the high-`ki` clamp test stays
short. Evidence: `results/pi120-2026-11-08/validation.log`.

### L4 — The CPU oracle shares the source with the DSP build

The CPU↔DSP comparison proves transport and build parity, not algorithmic
correctness: a shared mistake would cancel. The independent FP32 model is the
second oracle for algorithm-level error. It is a numerical model, not a plant
model.

### L5 — Scope is numerical, not physical

Only final controller outputs are compared; per-stage intermediates are not
dumped. There is no motor, inverter, current sensor, PWM capture or HIL loop.
There is no long-duration stability test beyond the bounded thermal ramp. The
checksum is a 16-bit-per-field sum; the per-case CSV is the equality proof.

## Evidence map

| Finding | Artifact |
|---|---|
| F1 echo | `results/final/echo.log` |
| F2 validation | `results/final/validation.log`, `validation.csv.gz` |
| F3 optimization | `results/final/bench-dsp.log`, `results/baseline/bench-baseline-3x-corrected.log` |
| F4 CPU | `results/final/bench-cpu.log`, `results/final/environment.txt` |
| F6 suite | `results/final/suite.log`, `results/final/SHA256SUMS` |
| F7 PI clamp | `results/final/validation-pi.log`, `validation-pi.csv.gz` |
| L1 repeat | `results/repeatability/summary.md` |
| L2 timer | `results/extra-2026-11-12/dsp-time/summary.md` |
| L3 integral bias | `results/pi120-2026-11-08/validation.log` |
| L4 numerics | `docs/validation.md` |
| L5 scope | `docs/scope-review.md` |
