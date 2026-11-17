# Q6A Hexagon DSP Benchmark

**Qualification report for a fixed-point Field-Oriented Control (FOC) workload on the Qualcomm Hexagon v68 CDSP inside a Radxa Dragon Q6A (QCS6490), measured through a project-owned FastRPC contract and compared against the same code on the AArch64 CPU.**

This is a qualification project, not a marketing benchmark. Every reported number was produced on the physical board and is retained in the `results/` tree. The project reports both the optimization result (a real, correctness-preserving DSP change) and the measured boundary: for this scalar, serial FOC loop the DSP is about 5.28× slower than a pinned Cortex-A78 core.

- **Final run:** `results/final/`, 2026-11-10 23:04 UTC, commit `245dc6a`
- **Repeat:** `results/repeatability/`, 2026-11-16 22:35 UTC, same source `245dc6a`
- **DSP mode:** default DCVS, no `--dsp-perf` request; max-performance is a separate A/B
- **Evidence state:** clean working tree; `results/final/` is authoritative, and the repeat uses the same source
- **Primary metric:** DSP core cycles per FOC step
- **Secondary metric:** host-measured end-to-end wall time
- **Optimization:** fused `sincos_q15`, one CORDIC pass instead of two
- **Key result:** 593.85 → 376.996 cycles/step, **−36.52%**, **1.575×**, checksum unchanged
- **Validation:** 20,510 P-only steps and 30 high-`ki` PI-clamp cases, **0 CPU↔DSP mismatches**
- **Erratum:** the earlier 2026-11-09 report's ≈28× DSP/CPU ratio was a 5× clock-formula error; see [docs/CHANGELOG.md](docs/CHANGELOG.md)

### At a glance

| Question | Answer | Where |
|---|---|---|
| Does fusing the two CORDIC calls help? | Yes: 593.844 → 376.996 cycles/step (−36.52%, 1.575×), checksum unchanged | §9.1 |
| Is the DSP faster for this FOC loop? | No: 5.28× slower in default DCVS (process-median envelope 5.25–5.41×); 4.62× slower with max-performance DCVS | §7.2, §9.4–9.5 |
| What does one empty RPC cost? | 74.0 µs median echo, 73.7 µs step; p99 151 / 158 µs idle | §9.6 |
| Is there an RPC break-even? | No for scalar FOC: DSP 275–314 ns/step versus CPU 60.8 ns/step | §9.6 |
| How fast is HVX? | One accumulator: 65.1× / 64.3× fewer cycles than DSP scalar; four accumulators: 227.2× / 224.6×, and 3.83× / 2.29× faster than CPU NEON. Vector-add only, not FOC | §9.7 |
| Was DSP performance mode tested? | Yes: max mode raises the effective clock 15.9%, lowers DSP/CPU from 5.28× to 4.62×; cycles are unchanged | §7.2, §9 |
| Is DSP+RPC suitable for real-time control? | No at 100/50/20 µs with one RPC per step; p99.9 is 1.05 ms idle. CPU fits 20 µs; DSP compute-only fits but is 5.81× slower than CPU | [`docs/rt-report.md`](docs/rt-report.md) |
| What is not covered? | Physical motor/HIL, energy, the 69–79 °C thermal band, HVX FOC | §2.4–2.5 |

Method: FOC uses 5 repetitions × 3 processes and reports cycles as the primary metric; all raw data is retained under `results/`. Start with §9 for numbers, §12 for limitations, and `docs/report-details.md` for validation/history detail.

**Real-time laboratory report:** distributions, percentile tables, bootstrap intervals, deadline suitability and raw per-sample data are in [`docs/rt-report.md`](docs/rt-report.md) and `results/rt-2026-11-14/`. The key result is a deadline boundary: one FastRPC `step` per control step does not meet 100 µs, while CPU A78 meets 20 µs by p99.9 in the tested conditions.


---

## Table of contents

1. [Executive summary](#1-executive-summary)
2. [Mission and scope](#2-mission-and-scope)
3. [Platform and toolchain](#3-platform-and-toolchain)
4. [What was built](#4-what-was-built)
5. [FOC workload and fixed-point numerics](#5-foc-workload-and-fixed-point-numerics)
6. [Execution architecture](#6-execution-architecture)
7. [Measurement methodology](#7-measurement-methodology)
8. [Validation](#8-validation)
9. [Benchmark results](#9-benchmark-results)
10. [Optimization](#10-optimization)
11. [Positive findings](#11-positive-findings)
12. [Limitations](#12-limitations)
13. [Reproducibility](#13-reproducibility)
14. [Evidence map](#14-evidence-map)
15. [Decisions and deviations](#15-decisions-and-deviations)
16. [Deferred work and future work](#16-deferred-work-and-future-work)
17. [Conclusion](#17-conclusion)
18. [License](#18-license)
19. [Appendix — exact values and hashes](#appendix--exact-values-and-hashes)

---

## 1. Executive summary

The project answers a narrow question: **is the Hexagon CDSP a good execution target for a fixed-point FOC controller, and what does it take to measure that honestly?**

The answer for this workload is:

- The CDSP executes the complete FOC chain correctly: Clarke transform, CORDIC sine/cosine, Park transform, PI control, inverse Park and centered SVPWM.
- The fixed-point DSP implementation is **bit-exact** with a CPU build of the same source over 20,510 test steps.
- An independent FP32 model shows a maximum quantization error of **13.275 LSB** (mean **0.717 LSB**) for the P-only configuration.
- Fusing the two CORDIC calls into one pass is a real optimization: **593.85 → 376.996 cycles/step**, **−36.52%**, with an identical checksum and identical validation.
- The DSP is nevertheless **≈5.28× slower** than a pinned 2.7 GHz Cortex-A78 prime core for this scalar, stateful, dependency-chained loop. This is reported as a first-class engineering result.
- The main validation is deliberately P-only (`ki=0`). A separate high-`ki` test exercises the PI clamp, and a long `ki=120` experiment documents the accumulated integral-truncation bias.

Final headline table:

| Measurement | Result |
|---|---:|
| Transport check | 5/5 64-bit echo vectors exact (`~input`) |
| Numerical validation (P-only, `ki=0`) | 20,510 DSP steps, **0** CPU↔DSP mismatches |
| Fixed-point vs independent FP32 | max **13.275 LSB**, mean **0.717 LSB**, all values ≤16 LSB |
| PI clamp test (`ki=32767`) | 30 cases, **0** mismatches, max **4.114 LSB**, mean **0.877 LSB** |
| DSP baseline (two CORDIC calls) | **593.85 cycles/step** |
| DSP optimized (`sincos_q15`) | **376.996 cycles/step** |
| Optimization effect | **−36.52% cycles**, **1.575×** faster, checksum unchanged |
| DSP compute (effective clock) | **319–327 ns/step**; **26.65–26.96 ms** per 5-repetition RPC |
| CPU fixed-point baseline | **60.73 ns/step** (Cortex-A78 prime, pinned, warmed) |
| DSP vs CPU for this scalar FOC | **≈5.28× slower on the DSP** (≈5.40× on the same-source repeat run) |
| Checksum | `2827389792`, identical for baseline, optimized and CPU |
| Thermal ramp | **49.6 °C idle → 68.0 °C hot**, below the 79 °C ceiling; no throttling |

A second complete suite run reproduced the result within **0.01%** of the cycle medians, with the same checksum and the same validation results. That repeat run is retained in `results/repeatability/`.

---

## 2. Mission and scope

### 2.1 Mission

Build a reproducible, evidence-backed qualification report for a non-trivial DSP workload on the Radxa Dragon Q6A, including:

- a project-owned FastRPC interface rather than a `main()`/`printf` workaround;
- a real fixed-point workload with a numeric specification;
- an independent numerical reference;
- raw measurements, not only summary numbers;
- a retained baseline and one measured optimization;
- a concise record of engineering limitations and superseded work.

### 2.2 In scope (completed)

- Platform bring-up and CDSP liveness.
- Hexagon SDK 6.4.0.2 / Tools 19.0.04 cross-build.
- Project-owned QAIC IDL, stub/skeleton and host runner.
- 64-bit scalar result transport through FastRPC in the unsigned User PD.
- Fixed-point FOC implementation on the DSP and the CPU.
- Independent FP32 reference and deterministic vector suite.
- Bit-exact CPU↔DSP parity and FP32 error study.
- DSP cycle measurement with raw repetitions.
- Pinned CPU comparison with a clock-ramp warm-up.
- One measured optimization with a full correctness re-run.
- Reproducible suite, summary generator, thermal capture and SHA-256 evidence.
- Public repository with logs, CSVs and documentation.

### 2.3 Out of scope (explicitly deferred or excluded)

- HVX FOC implementation. The vector-add microbenchmark was re-run in P3.1; it is not a FOC implementation.
- Aggressive DSP optimization beyond the one measured change (assembly hand-tuning, LTO, cache layout, etc.).
- Physical motor, inverter, current sensor, PWM capture or HIL testing.
- Secure PD, multithreading, RPCMEM/DMA-BUF and other pointer-based transports.
- Per-stage intermediate dumps over RPC.
- Long-duration thermal/stress testing.

### 2.4 Applicability of the measured results

| Workload | Measured result | Valid claim | Not a valid claim |
|---|---|---|---|
| Scalar serial FOC, fixed point | DSP 5.28× slower than a pinned A78 core | This build and this dependency chain | General DSP-vs-CPU performance |
| Vector add, 64 lanes | DSP HVX 0.0269 ns/lane-add with 4 accumulators (0.0936 with 1); CPU NEON 0.103 ns | 4-accumulator HVX is 3.83× faster than NEON; 1 accumulator is comparable | Any FOC or real-time claim |
| Vector add, 4,096 lanes | DSP HVX 0.0268 ns with 4 accumulators (0.0936 with 1); CPU NEON 0.0614 ns | 4-accumulator HVX is 2.29× faster; 1 accumulator was 1.61× slower | A size-independent winner |
| Empty FastRPC call | 74.0 µs median echo RPC | Fixed host↔CDSP call cost on this board | General FastRPC/network latency |
| Fused CORDIC | 36.52% fewer DSP cycles | This FOC build with the same checksum | CORDIC or DSP optimization in general |

### 2.5 What this report does not claim

- It does not claim that the DSP is slower than the CPU in general; the 5.28× is for one scalar, dependency-chained FOC loop.
- The historical **66.8×** is **DSP scalar versus DSP HVX**, not DSP versus CPU.
- It does not report energy or power: no external meter was available. See [`docs/energy-and-power.md`](docs/energy-and-power.md) for the blocking conditions.
- It does not include a physical motor, inverter, current sensor, PWM capture or HIL test.
- It does not cover the 69–79 °C thermal band; the measured range is 48.5–68.4 °C.
- It does not provide a hard real-time bound, a secure-PD result, or a DMA/RPCMEM result.
- It does not claim that DCVS max-performance mode is the default or is permitted on every product.
- It does not generalize across Hexagon versions, toolchains or other FOC implementations.


---

## 3. Platform and toolchain

| Component | Value |
|---|---|
| Board | Radxa Dragon Q6A |
| SoC | Qualcomm QCS6490 |
| DSP | Hexagon v68 CDSP, domain 3 (unsigned User PD) |
| Kernel | `6.18.2-3-qcom` |
| CPU used for comparison | Cortex-A78 prime, CPU 7, max 2707.2 MHz |
| CPU governor / driver | `schedutil` / `qcom-cpufreq-hw` |
| CPU frequency during capture | `scaling_cur_freq = 2707200` |
| Hexagon SDK | 6.4.0.2 |
| Hexagon Tools | 19.0.04 (`hexagon-clang` 19.0.04) |
| QAIC | v01.00.49 |
| Host build OS | x86 Linux VM, Ubuntu |
| FastRPC packages | `fastrpc 1.0.7-3`, `radxa-firmware-qcs6490 0.2.42` |

### 3.1 Why an unsigned User PD

The workload needs only scalar arguments and a global controller. It does not need RPCMEM, DMA-BUF, secure memory, threads or dynamic allocation. The unsigned PD is the simplest trust boundary that still exercises the real FastRPC path. The trade-off is that some services are unavailable:

- `qurt_timer_get_ticks()` returns 0 in this PD; a separate `HAP_perf_get_time_us()` probe is available (see §7.2).
- `qurt_thread_*` is not part of the intended execution model.
- The PD can be killed/recovered by the CDSP firmware; a real RPC is the only reliable liveness check.

### 3.2 Build pitfalls that were proven

- Hexagon shared objects must compile with lowercase `-fpic` and link `-shared -fpic`. Uppercase `-fPIC` conflicts with the SDK's non-PIC G0 `libc.a`.
- Do not add an explicit `-lc`; the G0 libc is linked by the SDK flow.
- QAIC maps IDL `long` to plain `int`. The implementation must use exactly `int`, not `int32_t`, or the build fails with `conflicting types`.
- The generated remote handle is an opaque non-null token. The implementation must not dereference it.
- A DSP crash can leave `/sys/class/remoteproc/remoteproc1/state` misleading. Only a real `echo` RPC proves liveness.

Details: `docs/toolchain-and-build.md`.

---

## 4. What was built

### 4.1 Repository layout

```text
.
├── README.md                     this report
├── LICENSE                       MIT license
├── docs/
│   ├── architecture.md           host/DSP execution and RPC boundary
│   ├── toolchain-and-build.md    SDK, flags, QAIC, deployment pitfalls
│   ├── fastrpc-result-contract.md IDL, ABI and status semantics
│   ├── benchmark-methodology.md  metrics, clock derivation, statistics
│   ├── validation.md             fixed-point formats and test vectors
│   ├── rt-report.md              real-time distributions and deadlines
│   ├── rt-report.ru.md           detailed Russian measurement record
│   ├── engineering-findings.md   engineering findings and limitations
│   ├── decisions-log.md          ADR-style decision record
│   ├── scope-review.md           delivered vs planned, future work
│   ├── energy-and-power.md       power-measurement boundary
│   ├── design-decisions.md       index into the decisions log
│   ├── results/README.md         how to read the results tree
│   └── workloads/                workload and scope notes
├── CMakeLists.txt                legacy host-only benchmark build
├── config.example                documented runner settings used in the report
├── src/                          legacy host benchmark driver
├── dsp/
│   ├── idl/foc_bench.idl         project-owned FOC FastRPC contract
│   ├── idl/hvx_bench.idl         HVX vector-add FastRPC contract
│   ├── fastrpc/                  DSP skeleton implementations and kernels
│   └── probes/time_probe.c       standalone qtimer/cycle-clock probe
├── host/foc_runner.cpp           host CLI: echo / validate / bench / cpu /
│                                 hvx / pi / rpc / rpc-scaling / rt-* / all
├── tools/
│   ├── rt_analyze.cpp            RT percentiles, bootstrap and SVG plots
│   └── rpc_hist.cpp              RPC raw-sample percentiles and histograms
├── validation/foc/               independent FP32 model and vector generator
├── validation/motor_control/     legacy CORDIC benchmark
├── workloads/motor_control/      fixed-point FOC and CORDIC
├── scripts/
│   ├── build_foc_bench.sh        reproducible cross-build and host build
│   ├── run_foc_suite.sh          end-to-end suite
│   ├── run_rt_suite.sh           real-time distribution suite
│   ├── run_rt_bench_suite.sh     real-time bench chunks
│   ├── thermal_soak.sh           continuous thermal trace
│   ├── irq_off.sh                IRQ-isolation helper
│   ├── summarize_thermal.sh      thermal summary
│   ├── memory_load.c             memory-load helper
│   ├── collect_environment.sh    runtime environment capture
│   ├── summarize_foc.py          raw JSON → summary.json / summary.md
│   ├── check_repo.py             structure and Markdown check
│   ├── build_and_run.sh          earlier scalar smoke path
│   └── build_and_run_hvx.sh      earlier HVX smoke path
└── results/
    ├── final/                    canonical final run
    ├── baseline/                 frozen source and corrected baseline
    ├── repeatability/            same-source independent second full run
    ├── pi120-2026-11-08/         long-ki integral-bias evidence
    ├── extra-2026-11-12/         performance mode, time probe, RPC overhead
    ├── extra-2026-11-13/         vector add and A55 comparison
    ├── extra-2026-11-15/         HVX accumulators, RPC N=256, repeat archive
    ├── thermal-79C-2026-11-11/   bounded thermal ramp
    └── rt-2026-11-14/            real-time distributions and raw samples
```

`src/`, `CMakeLists.txt`, `validation/motor_control/` and `workloads/motor_control/motor_control.cpp` are the original host-only benchmark. They are retained as historical context; the qualification results use `host/foc_runner.cpp` and the FOC sources.

### 4.2 Project-owned FastRPC contract

The IDL is in `dsp/idl/foc_bench.idl`. It exposes:

| Method | Purpose |
|---|---|
| `echo` | 64-bit transport check; returns `~input` |
| `configure` | Set `kp_d`, `ki_d`, `kp_q`, `ki_q` and reset state |
| `reset` | Clear PI integrators without changing gains |
| `step` | Run one FOC step and return seven Q15 outputs |
| `bench` | Run 256 warm-up steps plus up to five timed repetitions of 16,384 steps and return raw cycle counts |

Result-transfer rules:

1. Success is the RPC return value and a per-call `status` field; it is separate from the measured workload values.
2. Every result is returned through a scalar `rout` field, never through `main()`, a process exit code, `printf` or FARF.
3. `step` is deliberately per-vector, so the host can drive a deterministic suite through one DSP session without a serialization format.
4. `bench` returns raw cycle samples, not only an average.

The generated header is not committed; its SHA-256 is recorded in `results/final/artifacts.sha256`. The DSP binary is `libfoc_bench_skel.so`. The HVX microbenchmark has a separate contract, `dsp/idl/hvx_bench.idl`, compiled to `libhvx_bench_skel.so`.

### 4.3 Host runner

`host/foc_runner.cpp` modes:

| Mode | What it does |
|---|---|
| `echo` | Sends five 64-bit patterns and requires `~input`. |
| `validate` | Drives single-step RPCs, compares against the fixed-point CPU oracle and the independent FP32 model, optionally writes a CSV. |
| `bench` | Runs one RPC with 256 warm-up steps and five timed 16,384-step repetitions; returns raw cycles. |
| `cpu` | Runs the same controller source on AArch64 with the same inputs/reset policy, pinned to CPU 7 with a 2,000,000-step clock ramp. |
| `hvx` | Vector-add microbenchmark: DSP scalar/HVX (1 and 4 accumulators) and CPU scalar/NEON; `--hvx-lanes` sets 64..4096. |
| `pi` | PI clamp and long `ki=120` experiments. |
| `rpc` | 10,000 empty `echo` and single-`step` RPC latencies; idle or externally loaded. |
| `rpc-scaling` | One RPC carrying N = 1, 16, 256, 4096, 16384 steps; `--rpc-csv` writes every sample. |
| `rt-dsp`, `rt-step`, `rt-bench`, `rt-cpu` | Real-time sample series; require `--rt-csv`; method and results in [`docs/rt-report.md`](docs/rt-report.md). |
| `rt-timer-cpu`, `rt-timer-dsp` | Timer-read overhead series for the RT report. |
| `all` | Runs echo, validation, bench, CPU, HVX and PI in one session. |

Settings
used in this report are recorded in [`config.example`](config.example).
`validate` accepts gain overrides:
the main suite uses P-only, the PI clamp suite uses `ki=32767`.

---

## 5. FOC workload and fixed-point numerics

### 5.1 Signal chain

For every step the controller performs:

1. Clarke transform: `alpha`, `beta` from `ia`, `ib`.
2. CORDIC sine/cosine for the electrical angle.
3. Park transform: `id`, `iq`.
4. PI control on `id`/`iq` errors.
5. Inverse Park: `v_alpha`, `v_beta`.
6. Three-phase voltages and centered SVPWM duty calculation.

All signals are Q15 except the angle, which is a 14-bit turn (0..16383, where 16384 = 360°). Products use 64-bit intermediates. Outputs saturate to the asymmetric range ±32767.

### 5.2 Fixed-point details

| Signal | Format / range |
|---|---|
| `ia_q15`, `ib_q15` | Q15, −32767..32767 |
| `id_ref_q15`, `iq_ref_q15` | Q15, −32767..32767 |
| `electrical_angle` | 14-bit turn, 0..16383 |
| `id_q15`, `iq_q15` | Q15, saturated |
| `vd_q15`, `vq_q15` | Q15, saturated |
| duties | unsigned Q15, 0..32767 |
| gains `kp_*`, `ki_*` | Q15, supplied by caller |

Important truncation rules:

- Q15 products are shifted right by 15 **individually** before sums.
- Right shifts are arithmetic (floor toward negative infinity).
- The centered-SVPWM `/2` divisions truncate toward zero.
- The CPU and DSP builds use the same C++ expressions. The FP32 model does not emulate integer truncation; that difference is part of the measured quantization error.

### 5.3 CORDIC

- External angle: 14 bits per turn.
- Internal angle: 18 bits per turn (4 extra fractional bits).
- Quadrant reduction to `[0, π/2]`.
- 16 iterations with an `atan(2^-i)` table in 18-bit phase units.
- Initial vector `x = 19898` (Q15 `1/K`, K ≈ 1.646760258).
- Outputs saturated to ±32767.
- `sincos_q15()` performs one pass and returns both sine and cosine.
- `sin_q15()` and `cos_q15()` remain as wrappers for the historical two-pass comparison.

### 5.4 PI and integral behavior

```text
proportional = (error * kp) >> 15
increment    = (error * ki) >> 15
integral    += increment
integral     = clamp(integral, -32767, 32767)
voltage      = clamp(proportional + integral, -32767, 32767)
```

The integrator is clamped before adding the proportional term. The benchmark uses P-only gains (`kp=8192, ki=0`); the dedicated clamp test uses `ki=32767`. A long `ki=120` run is retained specifically to show the accumulation of floor-truncation bias.

---

## 6. Execution architecture

```text
AArch64 host process
  └── foc_runner
        ├── remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE)  // domain 3
        ├── fastrpc_handle_open(foc_bench_URI + CDSP_DOMAIN)
        ├── validate: step RPCs, per-step CSV
        ├── bench:    one bench RPC, raw cycles
        ├── cpu:      same controller source, pinned to CPU 7
        ├── hvx:      DSP scalar/HVX and CPU scalar/NEON vector add
        ├── rpc:      empty-call latency; rpc-scaling: N-step RPCs
        └── rt-*:     real-time sample series, written to CSV
                          |
                          v
                    FastRPC / CDSP
                          |
                    libfoc_bench_skel.so
                          |
                    global fixed-point Controller
```

The DSP side is intentionally simple: a single global controller, scalar arguments, no heap, no threads, no pointer serialization. This keeps the trust boundary small and makes the measured cost attributable to the workload rather than to transport machinery.

The host sets `DSP_LIBRARY_PATH` and `ADSP_LIBRARY_PATH` to the deployment directory plus the standard CDSP paths before opening the handle.

---

## 7. Measurement methodology

### 7.1 Two separate metrics

**DSP compute:** `qurt_get_core_pcycles()` inside the timed loop, reported as cycles per step.

**End-to-end:** host wall time around the `bench` RPC, which includes dispatch and return.

The DSP result is the primary optimization metric because the host-derived rate also includes clock-state and stall effects.

### 7.2 Effective cycle rate

`qurt_timer_get_ticks()` returns 0 in this unsigned PD, so there is no RTOS tick time base. The FOC hot loop therefore uses host time: it measures `rpc_wall` and a one-step `preflight_wall`, then derives:

```text
rpc_compute     = rpc_wall - preflight_wall   # one RPC = 5 repetitions
total_cycles    = sum(cycles[0..4])           # all five repetitions
effective_rate  = total_cycles / rpc_compute
cycles_per_step = total_cycles / (5 * 16384)
ns_per_step     = cycles_per_step / effective_rate
```

The final run measured **1,180.6, 1,152.2 and 1,174.6 Mcycles/s**; the repeat run measured about **1.15–1.19 Gcycles/s**. This is an **effective** rate, not the physical CDSP PLL frequency. It is consistent with the 1.16 GHz fallback in the runner.

The canonical final run used **default DCVS**. The 2026-11-12 A/B re-ran the same 3-process × 5 × 16,384-step benchmark in both modes; the CPU reference is the final run's 60.734 ns/step:

| DSP DCVS policy | DSP ns/step | CPU ns/step | DSP/CPU |
|---|---:|---:|---:|
| default, canonical final | 320.960 | 60.734 | **5.28×** |
| default, A/B session | 325.276 | 60.734 | 5.36× |
| max performance, A/B session | 280.644 | 60.734 | **4.62×** |

Max performance lowers DSP ns/step by 13.7% but changes cycles/step by only 0.011%: it is a clock-state effect, not an instruction-count improvement. The 5.28× headline is default DCVS; 4.62× is the max-performance counterpart. The small default-mode difference between the final and A/B sessions is the effective-clock spread, not a code change. Source: `results/extra-2026-11-12/dsp-perf/`.

**Direct time-probe check.** A separate CDSP binary (`dsp/probes/time_probe.c`) timed 1,000,000 LCG iterations between `HAP_perf_get_time_us()` (the 19.2 MHz qtimer) and `qurt_get_core_pcycles()`. Five runs returned **117** in 10 MHz units, i.e. **1.170 GHz ± 0.005 GHz**, with the core-cycle counter advancing. The probe was kept out of the FOC skeleton because adding it changed the measured hot loop from about 377.0 to 383.6 cycles/step (+1.8%) through code-layout/register effects, even when the probe was not called. The indirect FOC median in the same session was **1.1572 GHz**; direct minus indirect is **+1.11%**. Thus FOC ns/step uses the indirect effective rate, while the HVX microbenchmark’s ns values use the direct **1.170 GHz** probe. Source: `results/extra-2026-11-12/dsp-time/`.

### 7.3 CPU baseline controls

The CPU mode uses the same `Controller` source, same input sequence, same 256-step workload warm-up, same five repetitions of 16,384 steps and same checksum. Two additional controls are required:

1. **Pin to CPU 7** (Cortex-A78 prime). Without affinity the process migrates between A55/A78 cores and can vary from about 68 to 181 ns/step.
2. **Ramp the clock** with 2,000,000 untimed steps after the 256-step warm-up. The controller is reset before each timed repetition. Without the ramp, a 16,384-step loop measures the idle clock.

### 7.4 Thermal policy

- Board ceiling for this project: **79 °C**. The ramp targets 68 °C and aborts at 79 °C.
- One CPU 4 core heats the package; CPU 7 remains free and is the CPU under test.
- The CDSP is probed only with the normal short 5 × 16,384-step RPC. Long DSP calls are forbidden.
- Coverage: **49.6 °C idle → 68.0 °C hot**; the 69–79 °C band is not covered. Later sessions at 48.5–62.2 °C and the aborted 84.5 °C stress run are documented in [`docs/report-details.md#thermal-and-environment-detail`](docs/report-details.md#thermal-and-environment-detail).
- The 84.5 °C value is from a separate, invalid **eight-loop** stress attempt (`loaded-thermal-abort-84C.log`) that started at 79.4 °C; the 79 °C abort check runs between probes, so it overshot to 84.5 °C before the stop. It is not a second ceiling and is excluded from all results; conclusions use only the bounded 68 °C ramp.

---

## 8. Validation

Two independent oracles were used: a bit-exact CPU fixed-point twin and an independent FP32 model. The CPU twin must match the DSP exactly; the FP32 model measures distance from the ideal algorithm.

| Gate | Cases | Result |
|---|---:|---|
| Main P-only suite | 20,510 | **0** exact CPU↔DSP mismatches; FP32 max **13.2753 LSB**, mean **0.71681 LSB** |
| PI clamp (`kp=8192`, `ki=32767`) | 30 | **0** mismatches; FP32 max **4.1145 LSB** |
| Long `ki=120` | 20,510 | **0** mismatches; FP32 max **2,143.09 LSB** (quantization accumulation) |
| Checksum | — | **2,827,389,792** |
| Vector groups | — | 30 edge cases + 4,096 deterministic random + 16,384 angle codes |

Full percentile tables, vector construction, and the interpretation of the 13.275 LSB maximum are in [`docs/report-details.md#validation-detail`](docs/report-details.md#validation-detail).

## 9. Benchmark results

### 9.1 Baseline vs optimized

The baseline is the original two-CORDIC code (`sin_q15` + `cos_q15`) running through the new RPC interface. The optimized build uses `sincos_q15` and one CORDIC pass.

| Quantity | Baseline (two passes) | Optimized (fused) |
|---|---:|---:|
| Cycles/step, three process medians | 593.834, 593.844, 593.853 | 376.996, 376.995, 377.009 |
| Median cycles/step | **593.844** | **376.996** |
| Median wall, 5 × 16,384-step RPC | 43.21 ms | 26.69 ms |
| Median ns/step | 524.13 | 320.96 |
| Checksum | 2,827,389,792 | 2,827,389,792 |
| Validation | PASS, 0 mismatches | PASS, 0 mismatches |

Derived:

```text
cycle reduction = (593.844 - 376.996) / 593.844 = 36.52%
cycle speed-up  = 593.844 / 376.996              = 1.575x
wall ratio      = 43.211 ms / 26.687 ms          = 1.619x
```

The wall ratio is higher than the cycle speed-up because this baseline rebuild measured a lower effective rate (1.13–1.15 versus 1.15–1.18 Gcycles/s in the optimized run). Cycles remain the defensible optimization metric.

### 9.2 Final optimized process values

| Process | Cycles/step | ns/step | RPC wall (ms, 5 reps) | Effective rate (Mcycles/s) |
|---|---:|---:|---:|---:|
| 1 | 376.9964 | 319.332 | 26.652 | 1,180.6 |
| 2 | 376.9948 | 327.207 | 26.964 | 1,152.2 |
| 3 | 377.0085 | 320.960 | 26.687 | 1,174.6 |

Cross-process median: **376.996 cycles/step**, **320.96 ns/step**, **26.687 ms per 5-repetition RPC**, **1,174.6 Mcycles/s**.

The ns spread is explained by the effective-rate range; the cycle spread is 0.004%.

### 9.3 Repeatability

A second complete suite run on 2026-11-16 22:35 UTC used the same final source
(`245dc6a`) from a detached worktree, so the FOC code and IDL are identical to
the final build. Both runs use the same effective-clock definition:

| Metric | Final (2026-11-10) | Repeat (2026-11-16) |
|---|---:|---:|
| DSP cycles/step medians | 376.996, 376.995, 377.009 | 377.006, 376.999, 377.007 |
| DSP ns/step medians | 319.332, 327.207, 320.960 | 322.919, 326.951, 326.742 |
| CPU ns/step medians | 60.534, 60.734, 60.788 | 60.546, 60.307, 60.972 |
| DSP/CPU | 5.28× | 5.40× |
| Checksum | 2,827,389,792 | 2,827,389,792 |
| Validation | identical | identical |

Cycle-median spread between runs: **0.004%**. The ns and ratio spread is larger
because the measured effective clock and CPU frequency differ between sessions;
cycles are the primary metric. The repeat run is in `results/repeatability/`.

The current tree also contains later real-time sampler code that was not in the
final build. A separate post-final source check at `488f2d1` measures 383.600
cycles/step (+1.75%) with the same checksum; that is a code-layout effect, not
an FOC algorithm change. It is archived in
`results/extra-2026-11-15/post-final-layout-check/` and is not the repeatability
evidence.

### 9.4 CPU comparison

Final CPU process medians: **60.534, 60.734, 60.788 ns/step**; overall median **60.734 ns/step**.

```text
DSP / CPU by median ns      = 320.96 / 60.734 = 5.28x (envelope 5.25-5.41x)
DSP / CPU by RPC wall/step  = 325.8 / 60.734  = 5.36x
```

The repeat run measured **5.40×** by the same cross-process medians. Report the range as **≈5.3–5.4× slower on the DSP** for this scalar FOC workload. This is not a general DSP-vs-CPU claim.

### 9.5 Observed intervals (final run)

Intervals are observed min–max over all 15 timed repetitions (5 per process),
not confidence intervals. The headline medians use the cross-process median,
so they can differ slightly from the median of all repetitions.

| Metric | Min | Median | Max |
|---|---:|---:|---:|
| DSP cycles/step | 376.955 | 377.002 | 377.118 |
| DSP ns/step | 319.317 | 320.960 | 327.313 |
| CPU ns/step | 60.260 | 60.604 | 61.376 |
| DSP/CPU, process-median envelope | 5.25 | 5.28 | 5.41 |

The DSP/CPU interval pairs each DSP process median with each CPU process median
(9 combinations); the headline 5.28× is the median-of-medians. The wider
all-repetition envelope is 5.20–5.43× and mixes within-process variation.
CPU values are 60.534, 60.734, and 60.788 ns/step; DSP ns values are 319.332,
327.207, and 320.960 ns/step. Raw values remain in `results/final/summary.json`.


### 9.6 RPC overhead and step scaling

Ten thousand empty `echo` and single-`step` RPCs were timed on CPU 7. Idle
medians are **74.0 µs** (echo) and **73.7 µs** (step); p99 is 150.9 and
157.6 µs, and rare maxima reach 1.71 and 5.78 ms. With two competing busy loops
(one on CPU 7), the medians are 72.6 and 72.8 µs and p99 is 89.2 and 87.6 µs.
The loaded tails are lower because the CPU stays out of low-power states, not
because load reduces RPC cost.

One RPC carrying N FOC steps (median of 30; idle / loaded):

| N | Idle median | Loaded median | CPU compute |
|---:|---:|---:|---:|
| 1 | 88.2 µs | 87.8 µs | 0.061 µs |
| 16 | 93.7 µs | 93.1 µs | 0.973 µs |
| 256 | 356.7 µs | 158.7 µs | 15.6 µs |
| 4,096 | 1,605.6 µs | 1,255.1 µs | 249.1 µs |
| 16,384 | 5,426.3 µs | 4,603.5 µs | 996.3 µs |

The idle N=256 median above came from only 30 samples and sat between a fast
group and a slow tail. A 300-sample repeat resolves the distribution:

| N=256 condition | n | p50 | p90 | p99 | max |
|---|---:|---:|---:|---:|---:|
| idle | 300 | **228.9 µs** | 628.0 µs | 1,239.4 µs | 1,261.1 µs |
| loaded | 300 | **147.0 µs** | 153.2 µs | 171.7 µs | 3,144.2 µs |

Idle is **not unimodal**: 58.0% of samples are ≤250 µs, 24.0% are 250–500 µs
and 18.0% are >500 µs. A two-means split at 633 µs gives 90.7% around 272 µs
and 9.3% around 993 µs; the upper group is broad (about 590–1,260 µs), so this
is a fast group plus a slow tail, not two narrow modes. Loaded is one dominant
mode: 298/300 samples are ≤250 µs, with two >500 µs. Possible causes of the
idle tail are host CPU idle-state/scheduler behaviour and CDSP DCVS state; the
dataset does not prove a mechanism. Raw CSV, logs and histograms:
`results/extra-2026-11-15/rpc-n256/`.

A fit over the three largest sizes gives **313.5 ns/step** idle and
**274.8 ns/step** loaded, versus **60.81 ns/step** on CPU 7. Both DSP slopes are
above the CPU cost and the RPC adds 88–94 µs fixed, so there is **no break-even
N** for this scalar FOC workload. At N=16,384 loaded the DSP takes 4.60 ms
versus 0.996 ms of CPU compute (4.62×). Details, raw logs, and the plot are in
`results/extra-2026-11-12/rpc-overhead/`.


### 9.7 Vector-add across CPU and DSP

Same 16-bit accumulation workload, 5,000 iterations, 3 processes per size,
CPU 7 pinned. Values are ns per lane-add; DSP values are cycle-derived at the
measured **1.170 GHz** direct qtimer clock, not the FOC effective rate above.
Checksums match in all five implementations.

2026-11-13 one-accumulator run:

| Lanes | CPU scalar | CPU NEON | DSP scalar | DSP HVX (1 acc.) | DSP scalar / DSP HVX |
|---:|---:|---:|---:|---:|---:|
| 64 | 0.855 | 0.103 | 6.093 | 0.0936 | 65.08× |
| 4,096 | 0.927 | 0.0581 | 6.014 | 0.0936 | 64.28× |

2026-11-16 same-session comparison including the four-accumulator variant:

| Lanes | CPU scalar | CPU NEON | DSP scalar | DSP HVX (1 acc.) | DSP HVX (4 acc.) | DSP scalar / 4 acc. |
|---:|---:|---:|---:|---:|---:|---:|
| 64 | 0.866 | 0.103 | 6.107 | 0.0936 | **0.0269** | **227.20×** |
| 4,096 | 0.915 | 0.0614 | 6.014 | 0.0936 | **0.0268** | **224.62×** |

The historical **66.8×** is **DSP scalar versus DSP HVX**, not DSP versus CPU.
The one-accumulator build reproduces it at 65.08× (64 lanes) and 64.28×
(4,096 lanes). That kernel was dependency-limited at about 7 cycles per vector;
four independent accumulators cut it to about 2 cycles per 64-lane iteration and
give **227.2× / 224.6×** versus DSP scalar. Against CPU NEON, the
four-accumulator kernel is **3.83× faster at 64 lanes** and **2.29× faster at
4,096 lanes**. The earlier 1.61× NEON advantage at 4,096 lanes was specific to
the unoptimized one-accumulator kernel and is superseded. DSP scalar remains
6.5–7.1× slower than CPU scalar. Raw logs and conditions:
`results/extra-2026-11-13/vector-add/` and
`results/extra-2026-11-15/vector-add-multiacc/`.

### 9.8 A55 versus A78 check

The same scalar FOC source was also run on a Cortex-A55 core (CPU 0) and on the
A78 prime core (CPU 7), 3 processes each, 5 × 16,384 steps:

| Core | Median ns/step | Relative |
|---|---:|---:|
| Cortex-A78 prime | **59.46** | 1.00× |
| Cortex-A55 | **173.87** | 2.92× slower |
| CDSP (final suite, cross-session) | 320.96 | 1.85× slower than A55; 5.40× slower than A78 here |

This boundary is not an artifact of using only the fastest CPU core: even
the A55 is about **1.85× faster** than the CDSP for this dependency-chained
scalar loop. The headline remains **5.28×** from the final suite. Raw log:
`results/extra-2026-11-13/a55/`.


---

## 10. Optimization

### 10.1 What changed

The baseline called CORDIC twice per FOC step for the same angle. The optimization:

- added `motor_control::fixed::sincos_q15(angle, sin, cos)`, one CORDIC pass returning both values;
- kept `sin_q15` and `cos_q15` as wrappers for the baseline comparison;
- changed `Controller::step` to use the fused function;
- added `Controller::configure()` so the RPC layer can set gains and reset state without changing the measured hot loop.

The measured effect is **593.85 → 376.996 cycles/step**. The checksum and all validation statistics are identical to the baseline, so the improvement is not a semantic change.

### 10.2 Why not optimize further

The remaining cost is dominated by the serial dependency chain: each FOC step depends on the previous integrator state, and CORDIC itself is a 16-iteration chain. This is the wrong shape for HVX. Further gains would require a different algorithmic structure (for example, a pipelined multi-axis controller) or hand-written assembly with a new retained baseline. That is outside the agreed scope.

### 10.3 Code size

`hexagon-size` reported **13,186 bytes text**, **770 bytes data**, **16 bytes bss**
for the optimized skeleton. The object file is not committed; the artifact
SHA-256 is in `results/final/artifacts.sha256`.

---

## 11. Positive findings

1. **A project-owned FastRPC contract works end to end.** QAIC generation, skeleton build, deployment and 64-bit echo all pass.
2. **CPU↔DSP bit-exact parity is achievable.** Zero mismatches over 20,510 steps, plus a separate zero-mismatch PI clamp run.
3. **Fusing the CORDIC pass is a real optimization.** −36.52% cycles, 1.575×, with identical output.
4. **CPU measurements become stable with affinity and a clock ramp.** The spread across three process medians is well below 0.5%.
5. **The unsigned User PD is sufficient** for this scalar workload: no RPCMEM, DMA-BUF, secure PD or threads.
6. **Raw evidence is reproducible in one command.** The suite captures logs, CSVs, thermal data, summary and hashes.
7. **PI clamp/recovery can be validated separately and cheaply** with a 30-case high-`ki` run.

---

## 12. Limitations

### 12.1 The DSP is not faster for this workload

For this scalar, serial FOC loop the DSP is **≈5.28× slower** than the pinned A78 core by median ns (≈5.40× on the same-source repeat). This is the expected outcome of a dependency-chained scalar workload, not a platform failure. The HVX vector-add result is a different, data-parallel workload and must not be generalized to FOC.

### 12.2 Clock: one direct probe, one effective rate

`qurt_timer_get_ticks()` returns 0 in this unsigned PD, so the RTOS tick timer is unavailable. A separate standalone binary, `dsp/probes/time_probe.c`, does read the 19.2 MHz qtimer through `HAP_perf_get_time_us()` and `qurt_get_core_pcycles()`; five runs gave **1.170 GHz ± 0.005 GHz** (117 in 10 MHz units). That direct value is used for the HVX ns conversion. The FOC skeleton does not contain the probe because adding it perturbed the hot loop by +1.8%; FOC ns/step therefore uses the host-derived **effective** rate (1.15–1.19 Gcycles/s), and cycles remain the primary metric. The same-session difference is **+1.11%** (1.170 vs 1.1572 GHz). Neither value is a claim about the physical CDSP PLL frequency. Sources: `results/extra-2026-11-12/dsp-time/` and `results/final/bench-dsp.log`.

### 12.3 Other limitations

- Only final controller outputs are compared; per-stage intermediate values are not dumped.
- No physical motor, inverter, current sensor or PWM capture is used.
- No long-duration stability test was run beyond the bounded thermal ramp.
- The CPU oracle is the same source, so it cannot detect a shared algorithmic mistake; the FP32 model is the independent check.
- The checksum is a 16-bit-per-field sum; the per-case CSV is the equality proof.
- The PI clamp test is targeted, not a long-duration stability test.
- The long `ki=120` FP32 error growth is integral truncation, not a parity failure; it is why the main table is P-only.

---

## 13. Reproducibility

### 13.1 Prerequisites

- Dragon Q6A with CDSP attached (verify with a real RPC, not only `remoteproc` state).
- Hexagon SDK 6.4.0.2 on an x86 Linux build host reachable by SSH.
- Runner sandbox at `~/Projects/hexagon-v68-test` with `run_main_on_hexagon` for the legacy smoke path.
- FastRPC user-space packages and `libcdsprpc1`.

### 13.2 Build and run

From the board:

```bash
cd ~/Projects/Q6A-Hexagon-DSP-Benchmark
./scripts/build_foc_bench.sh
./scripts/run_foc_suite.sh --out results/final
```

The suite runs echo, the 20,510-step P-only validation, the dedicated PI clamp test, three DSP benchmarks and three pinned CPU benchmarks, captures thermal/environment data, compresses both CSVs, writes `summary.json`/`summary.md`, and produces `SHA256SUMS` and `artifacts.sha256`.

### 13.3 Verify a result

```bash
cd results/final && sha256sum -c SHA256SUMS
cd ../.. && sha256sum -c results/final/artifacts.sha256
```

The build artifacts are not committed; their hashes identify the exact binaries that produced the run. `validation.log` records the CSV name before compression; the committed files are `validation.csv.gz` and `validation-pi.csv.gz`.

### 13.4 Build-host overrides

```bash
Q6A_BUILD_HOST=builder@build-host
Q6A_BUILD_KEY=$HOME/.ssh/id_ed25519_build_host
Q6A_BUILD_DIR=/home/builder/hexagon-v68-test/q6a-foc
Q6A_SDK=/home/builder/hexagon-sdk/6.4.0.2
Q6A_RUNNER_DIR=$HOME/Projects/hexagon-v68-test
```

The DSP skeleton is deployed as `Q6A_RUNNER_DIR/libfoc_bench_skel.so`. The host runner is `build/foc/foc_runner`.

### 13.5 Expected variation

Compiler version, code layout, host load and CDSP state change ns values and sometimes cycle counts slightly. The exact validation pass/fail, checksum and CPU↔DSP parity must hold. Do not select the fastest sample as the result; report the median and range.

---

## 14. Evidence map

| Finding / claim | Artifact |
|---|---|
| Final run, all headline values | `results/final/summary.md`, `summary.json` |
| 64-bit transport | `results/final/echo.log` |
| Main P-only validation | `results/final/validation.log`, `validation.csv.gz` |
| PI clamp validation | `results/final/validation-pi.log`, `validation-pi.csv.gz` |
| Long `ki=120` bias | `results/final/pi.log`, `pi-long.csv.gz` |
| Optimized benchmark | `results/final/bench-dsp.log` |
| Baseline benchmark | `results/baseline/bench-baseline-3x-corrected.log` and `summary-corrected.md` |
| DSP performance mode A/B | `results/extra-2026-11-12/dsp-perf/` |
| Direct DSP time probe | `results/extra-2026-11-12/dsp-time/` |
| RPC latency and scaling | `results/extra-2026-11-12/rpc-overhead/` |
| RPC N=256 distribution (300 samples) | `results/extra-2026-11-15/rpc-n256/` |
| Vector add: CPU scalar/NEON, DSP scalar/HVX | `results/extra-2026-11-13/vector-add/` |
| Vector add: one versus four HVX accumulators | `results/extra-2026-11-15/vector-add-multiacc/` |
| A55 versus A78 scalar FOC | `results/extra-2026-11-13/a55/` |
| CPU baseline | `results/final/bench-cpu.log` |
| Environment | `results/final/environment.txt` |
| Thermal ramp | `results/thermal-79C-2026-11-11/thermal-summary.md`, `probes.csv` |
| HVX vector-add | `results/final/hvx.log` |
| Repeatability | `results/repeatability/` |
| Build artifacts | `results/final/artifacts.sha256` |
| Decisions | `docs/decisions-log.md` |

Engineering findings and limitations: `docs/engineering-findings.md`.
Methodology: `docs/benchmark-methodology.md`.
Numerics and vectors: `docs/validation.md`.

---

## 15. Decisions and deviations

1. **Clock:** the total-cycles/wall measurement is 1.15–1.19 Gcycles/s. Cycles remain primary because the host-derived rate still includes stalls; the value is labelled an effective rate, not a PLL measurement.
2. **HVX benchmark:** re-run through the new FastRPC infrastructure. Vector-add is 65.08× / 64.28× fewer cycles with one accumulator and 227.2× / 224.6× with four (64 / 4,096 lanes); the four-accumulator kernel is also 3.83× / 2.29× faster than CPU NEON. This is a separate data-parallel microbenchmark, not a FOC claim.
3. **Optimization:** exactly one change was made and measured. Further optimization would need a new baseline and a different study.
4. **Validation split:** the main suite is P-only to keep FP32 statistics comparable to the benchmark; PI clamp and long-`ki` bias are separate experiments.

---

## 16. Deferred work and future work

Deferred / out of scope:

- HVX FOC implementation. The vector-add microbenchmark is done (§9.7).
- Energy/power measurement. No external meter was available; see
  [`docs/energy-and-power.md`](docs/energy-and-power.md).
- Aggressive DSP optimization (assembly, LTO, cache/code layout).
- Physical plant / HIL / PWM validation.
- Secure PD, multithreading, RPCMEM/DMA-BUF.
- Per-stage intermediate dumps.
- Long-duration thermal/stress testing.
- Reference-only notes: [Black-Scholes](docs/workloads/black-scholes.md), [ECC](docs/workloads/ecc.md) and [radar](docs/workloads/radar.md); not implemented or measured.

Future work that would strengthen the study:

1. Measure energy with an external meter on the CDSP and CPU rails.
2. Add per-stage validation dumps behind a debug-only IDL method.
3. Build a data-parallel motor-control variant (multi-axis or batch FOC) where HVX can be evaluated fairly.
4. Add host-side CI for the build and static validation vectors; keep real DSP execution on hardware.
5. Add a physical plant/HIL test if the project is extended beyond numerical qualification.

---

## 17. Conclusion

The project demonstrates a complete, reproducible DSP qualification path:

- a real project-owned FastRPC contract;
- a fixed-point FOC workload with documented arithmetic;
- bit-exact CPU↔DSP parity over 20,510 steps plus a dedicated PI clamp test;
- an independent FP32 error study;
- raw cycle samples and a retained baseline;
- one measured, correctness-preserving optimization;
- a second full run that reproduces the result;
- a clear engineering boundary: the DSP is ≈5.28× slower for this serial scalar loop;

The repository is designed to be read as evidence, not as a claim. All numbers in this report are backed by files in `results/`, and all build/measurement decisions are recorded in `docs/`.

---

## 18. License

MIT. See [`LICENSE`](LICENSE).

The license covers the source code and the documentation in this repository.
It does not grant rights to Qualcomm SDK components, board firmware, or
third-party data that are not part of this repository.

---

## Appendix — exact values and hashes

Exact final-run values and SHA-256 artifact hashes were moved to [`docs/report-details.md#exact-final-run-values`](docs/report-details.md#exact-final-run-values) and [`docs/report-details.md#artifact-hashes`](docs/report-details.md#artifact-hashes).

