# Scope review

## Delivered

| Workstream | Status | Evidence |
|---|---|---|
| Platform bring-up (firmware, FastRPC test, CDSP liveness) | DONE | `docs/toolchain-and-build.md`; `results/final/environment.txt` |
| Reproducible Hexagon build (SDK 6.4.0.2, Tools 19.0.04) | DONE | `scripts/build_foc_bench.sh`; `results/final/suite.log` |
| Project-owned FastRPC IDL/stub/skeleton | DONE | `dsp/idl/foc_bench.idl`; `dsp/fastrpc/foc_bench_skel_impl.cpp` |
| 64-bit transport check | DONE | `results/final/echo.log` |
| Fixed-point FOC (Clarke, CORDIC, Park, PI, inverse Park, SVPWM) | DONE | `workloads/motor_control/` |
| Independent FP32 reference and vectors | DONE | `validation/foc/` |
| 20,510-step exact parity + FP32 error study | DONE | `results/final/validation.log`, `validation.csv.gz` |
| Dedicated PI clamp/recovery test (`ki=32767`) | DONE | `results/final/validation-pi.log`, `validation-pi.csv.gz` |
| Baseline DSP benchmark with raw samples | DONE | `results/baseline/bench-baseline-3x-corrected.log` |
| One measured optimization with correctness re-run | DONE | `results/final/bench-dsp.log`; 1.575× |
| Pinned CPU baseline | DONE | `results/final/bench-cpu.log` |
| Thermal and environment capture | DONE | `results/final/thermal-*.txt`, `environment.txt` |
| Reproducible suite + summary generator | DONE | `scripts/run_foc_suite.sh`, `summarize_foc.py` |
| Independent second full run | DONE | `results/repeatability/` (same checksum, 0.01% cycle spread) |
| Consolidated documentation | DONE | this `docs/` tree |

## Not delivered (explicitly out of scope)

| Item | Reason |
|---|---|
| HVX FOC implementation or re-run | The FOC loop is serial/stateful; HVX is not a natural fit. The historical vector-add result is archived separately. |
| Aggressive DSP optimization (assembly, LTO, cache tuning) | One measured change was the agreed scope; further work needs a different study and a retained baseline. |
| Physical motor/inverter/PWM validation | No plant or power stage is part of this project; duties are numerical. |
| Secure PD, multithreading, RPCMEM/DMA-BUF | The scalar unsigned-PD path is sufficient and simpler to prove; the unsigned PD lacks `qurt_thread_*`. |
| Per-stage intermediate dumps | Would require a larger IDL and more RPC payload; the FP32 model covers algorithm-level error. |
| Long-duration thermal/stress testing | A bounded ramp to 68.0 °C was run; the 69–79 °C band and multi-hour soak remain future work. |
| Camera/V4L2/GStreamer and ResNet work | Separate projects; deliberately not merged. |
| Black-Scholes, ECC, radar workloads | Planned filenames exist for context, but they were not implemented; see `docs/workloads/`. |

## Deviations from the original plan

1. **Clock:** the plan assumed a 1.16 GHz CDSP clock. The unsigned PD has no
   RTOS tick timer, so the methodology is cycles-primary plus a host-derived
   effective rate of ≈1.15–1.19 Gcycles/s. A standalone qtimer probe measured
   1.170 GHz directly, but it is not linked into the FOC hot loop (+1.8%
   perturbation); FOC and HVX therefore use the effective and direct values
   respectively, differing by +1.11% in the same session.
2. **HVX benchmark:** re-run as a separate vector-add microbenchmark, not as a
   FOC implementation.

## Completion criteria

- [x] Build DSP module and host runner from a clean tree.
- [x] Load the project skeleton in CDSP domain 3 through FastRPC.
- [x] Return 64-bit results through a defined, versioned interface.
- [x] Prove CPU↔DSP exact parity on edge, random and exhaustive angle vectors.
- [x] Quantify fixed-point error against an independent FP32 model.
- [x] Exercise PI integrator clamp and recovery with a dedicated high-ki run.
- [x] Measure DSP cycles with raw repetitions.
- [x] Measure the same workload on the CPU with equivalent work.
- [x] Apply one optimization, re-validate, and measure the effect.
- [x] Preserve the baseline and all raw evidence.
- [x] Document both the optimization and the measured performance boundary.
- [x] Provide a one-command reproduction suite.
- [x] Provide a public, GitHub-ready repository with logs and checksums.
- [x] Re-run the HVX vector-add microbenchmark as a separate workload.

## Future work

1. Re-run the HVX vector-add microbenchmark with the same RPC discipline, if a
   data-parallel workload is added.
2. Add per-stage validation dumps behind a debug-only IDL method.
3. Explore a data-parallel motor-control variant (multi-axis or batch FOC) where
   HVX can be evaluated fairly.
4. Add a hardware timer or a calibrated cycle-to-time path if the platform
   exposes one in the unsigned PD.
5. Add CI that only checks the host-side build and static validation vectors;
   real DSP execution must stay on hardware.
6. Add a physical plant/HIL test if the project is extended beyond numerical
   qualification.
