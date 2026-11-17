# Decisions log

ADR-style record. Dates are the decision dates; later entries supersede earlier
assumptions explicitly.

## D01 — Use a project-owned QAIC IDL instead of `run_main_on_hexagon`

**Date:** 2026-11-07
**Context:** The legacy helper conflates the DSP `main()` return value with
transport status and makes measurements awkward to return.
**Decision:** Define `dsp/idl/foc_bench.idl`, generate the AP stub and DSP
skeleton with QAIC, and use a dedicated host runner.
**Consequences:** Clear transport/workload separation, typed results, no
reliance on stdout/FARF. Costs an IDL generation step and a deployment path for
the project skeleton.

## D02 — Return only scalar `rout` values

**Date:** 2026-11-07
**Context:** Structs, pointers and sequence buffers require shared layout and
ownership assumptions that are hard to verify on both sides.
**Decision:** All results are scalar `long`/`uint64_t` outputs; no structs,
pointers, STL or RPCMEM.
**Consequences:** Slightly verbose signatures, but a small and provable ABI.

## D03 — Separate transport status from workload status

**Date:** 2026-11-07
**Context:** A successful RPC can still carry invalid or non-deterministic
measurements.
**Decision:** RPC return value = transport/argument status; `status` bitmask =
workload validity (zero cycles, zero checksum, implausible cycles/step,
non-deterministic repetitions).
**Consequences:** The suite can distinguish "DSP unreachable" from "DSP
produced a bad benchmark".

## D04 — Keep the CPU fixed-point implementation as the exact oracle

**Date:** 2026-11-07
**Context:** The CPU and DSP must be compared on identical arithmetic, and a
floating-point reference cannot prove bit-exact transport.
**Decision:** Compile the same `Controller` source for AArch64 with
`-fno-tree-vectorize`; require zero mismatches. Add a separate FP32 model only
for quantization error.
**Consequences:** Strong transport/build verification; the FP32 model remains
independent for algorithm-level error.

## D05 — Use cycles as the primary DSP metric

**Date:** 2026-11-07
**Context:** CDSP frequency is not controlled by this project and the hardware
clock is not observable in the unsigned PD.
**Decision:** Report `qurt_get_core_pcycles()` per step as primary; derive ns
from RPC-corrected wall time as a secondary, explicitly effective metric.
**Consequences:** Stable comparisons across host load; no false claim about the
physical clock.

## D06 — Measure the effective rate instead of assuming 1.16 GHz

**Date:** 2026-11-08
**Context:** The 1.16 GHz fallback produced ns values inconsistent with
observed wall time; `qurt_timer_get_ticks()` returns 0.
**Decision:** Compute `effective_cycle_rate = cycles / (rpc_wall - preflight)`
on the host and label it `effective_clock_hz`. Never claim it is the PLL.
**Consequences:** ns values agree with wall time; the 1.16 GHz assumption is
documented as invalid for this setup. A later standalone `HAP_perf_get_time_us()`
probe measured **1.170 GHz** directly; it is kept out of the FOC skeleton because
it perturbed the hot loop by +1.8%. FOC uses the effective rate, HVX uses the
direct probe (see D16).

## D07 — One optimization only: fused `sincos_q15`

**Date:** 2026-11-08
**Context:** The baseline performs two CORDIC passes per step for sine and
cosine at the same angle.
**Decision:** Add `sincos_q15()` and use it in the FOC step; keep the old
wrappers for comparison. Keep the change only if validation is unchanged and
the benchmark improves.
**Consequences:** 36.52% fewer cycles, identical checksum and validation. No
further DSP optimization is attempted in this scope.

## D08 — Pin the CPU baseline to CPU 7 and ramp the clock

**Date:** 2026-11-08
**Context:** `schedutil` and core migration produced 68–181 ns/step variance.
**Decision:** Use `taskset -c 7` and 2,000,000 untimed ramp steps after the
256-step workload warm-up.
**Consequences:** Stable 60.4–61.3 ns/step. The method is documented; the
governor is not modified.

## D09 — Keep the HVX microbenchmark separate

**Date:** 2026-11-08
**Context:** The delivered scope is the FOC/RPC/validation line; HVX vector add
is a different, data-parallel workload.
**Decision:** Report the HVX vector-add result as a separate microbenchmark and
do not transfer it onto the FOC claim.
**Consequences:** The FOC conclusion is not contaminated by a different
workload.

## D11 — Do not attempt aggressive DSP optimization

**Date:** 2026-11-08
**Context:** The scalar FOC is serial and dependency-bound; HVX is not a
natural fit. Further optimization would need a different algorithm/plant study.
**Decision:** Stop after the one measured CORDIC fusion.
**Consequences:** A defensible optimization with a retained baseline; no
speculative code churn.

## D12 — Verify CDSP health with a real RPC

**Date:** 2026-11-08
**Context:** `remoteproc` state was misleading during the crash.
**Decision:** The suite checks the state file only as a fast pre-check and
treats the `echo` RPC as the authoritative liveness test.
**Consequences:** Faster diagnosis; a dead CDSP is reported as dead instead of
producing confusing validation failures.

## D13 — Commit result logs, not build artifacts

**Date:** 2026-11-08
**Context:** The initial `.gitignore` excluded all `*.log`, hiding the evidence.
**Decision:** Ignore `build/`, objects and shared libraries; commit
`results/**/*.log`, compressed CSVs, summaries and checksums.
**Consequences:** Public reproducibility without binary bloat.

## D14 — Single canonical decision and findings record

**Date:** 2026-11-08
**Context:** The project accumulated session logs and intermediate working notes.
**Decision:** `docs/decisions-log.md` records design choices and
`docs/engineering-findings.md` records evidence-backed findings; superseded
session logs are not retained in the published tree.
**Consequences:** One consistent record, without duplicated or contradictory
history.

## D15 — Report the CPU-faster result as a first-class finding

**Date:** 2026-11-08
**Context:** The project's purpose is qualification, not advocacy.
**Decision:** The README and findings state ≈5.28× DSP slowdown for this
workload (5.40× on the same-source repeat run) and explain why.
**Consequences:** The project remains credible about both the optimization and
the measured performance boundary.

## D16 — Label the clock as an effective rate

**Date:** 2026-11-10
**Context:** The unsigned PD has no RTOS tick timer (`qurt_timer_get_ticks()`
returns 0), so the FOC clock is derived from total cycles and RPC wall time. A
separate qtimer probe is possible and measured 1.170 GHz, but linking it into the
FOC skeleton changed the hot loop by +1.8%.
**Decision:** Compute the rate from all repetitions in an RPC, label it
`effective_clock_hz`, and keep cycles as the primary metric. Use the standalone
direct probe only for HVX ns conversion and as a cross-check.
**Consequences:** The reported DSP/CPU ratio is 5.28× (5.40× on the same-source repeat),
consistent with 1.15–1.19 Gcycles/s. Direct minus effective is +1.11% in the
same session. No physical PLL frequency is claimed.

## D17 — Bound the thermal ramp at 79 °C

**Date:** 2026-11-11
**Context:** Board-specific thermal behavior is a local hardware condition and is
out of scope for this software benchmark. The thermal run only needs a bounded,
repeatable load.
**Decision:** Use one CPU 4 core, target 68 °C, abort at 79 °C, probe only with
the short 5 × 16,384-step RPC, and measure the hot point under load.
**Consequences:** Retained ramp 49.6 → 68.0 °C, CPU 7 at 2,707,200 kHz, DSP
cycles flat. The 69–79 °C band is not covered and is stated as a limitation.
