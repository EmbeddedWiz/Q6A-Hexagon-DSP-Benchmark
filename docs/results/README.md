# Results

All numbers in the report come from this tree. Plain-text logs are committed
for auditability; per-case CSVs are gzip-compressed. The published tree keeps
the canonical final run, the frozen baseline, the repeat run, the real-time
campaign and the focused follow-up experiments. Superseded development logs
are not part of the publication.

## `final/` — canonical final run

Produced by `./scripts/run_foc_suite.sh --out results/final` from a clean build.

| File | Contents |
|---|---|
| `suite.log` | run header and exact paths |
| `echo.log` | 64-bit transport check |
| `validation.log` | 20,510-step P-only parity + FP32 summary |
| `validation.csv.gz` | one row per step: inputs, seven outputs, CPU and FP32 deltas |
| `validation-pi.log` | 30-case high-`ki` PI clamp/recovery test |
| `validation-pi.csv.gz` | per-step data for the PI clamp test |
| `pi-long.csv.gz` | long `ki=120` integration-bias run |
| `bench-dsp.log` | three `bench` RPC lines, five raw cycles each |
| `bench-cpu.log` | three pinned CPU runs, five raw ns each |
| `hvx.log` | HVX vector-add microbenchmark |
| `environment.txt` | kernel, packages, CPU policy, artifact identity |
| `thermal-before.txt`, `thermal-after.txt` | thermal-zone readings |
| `summary.json`, `summary.md` | machine- and human-readable aggregation |
| `SHA256SUMS` | hashes of every result file (check from inside `results/final/`) |
| `artifacts.sha256` | hashes of the non-committed build products |

### Headline values

```text
echo:                  PASS (5/5)
validation (P-only):   20,510 cases, 0 exact mismatches
FP32 error:            max 13.2753 LSB, mean 0.71681 LSB
PI clamp (ki=32767):   30 cases, 0 mismatches, max 4.1145 LSB
DSP cycles/step:       376.996, 376.995, 377.009  (median 376.996)
DSP ns/step:           319.332, 327.207, 320.960  (median 320.960)
DSP RPC wall:          26.652, 26.964, 26.687 ms    (median 26.687 ms, 5 reps)
CPU ns/step:           60.534, 60.734, 60.788       (median 60.734)
checksum:              2,827,389,792 (all runs)
thermal:               49.6 C idle, 68.0 C hot, ceiling 79 C
```

## `baseline/` — frozen source and corrected baseline

`checksums.txt` and `sources-20261108.tar.gz` preserve the known-good FOC
sources before the project-owned RPC and `sincos_q15` changes. The corrected
baseline rebuild is `bench-baseline-3x-corrected.log`; `summary-corrected.md`
records 593.834 / 593.844 / 593.853 cycles/step. The older log's derived clock
fields were superseded by the effective-clock correction; its raw cycles and
wall time remain valid.

## `repeatability/` — independent second full run

A second complete suite run on 2026-11-16 22:35 UTC from the same final source
(`245dc6a`, detached worktree) reproduced the final FOC result: 376.999–377.007
cycles/step, 20,510 cases and 0 mismatches, the same checksum, and a cycle-median
spread of 0.004%. Its cross-process DSP/CPU ratio is 5.40×; the final is 5.28×.
Cycles are the primary metric. The run is evidence of stability, not a
replacement for `final/`.

## `pi120-2026-11-08/` — long `ki=120` validation

CPU and DSP stay bit-exact (0 mismatches), but the FP32 error rises to
**2,143.09 LSB maximum** and **197.80 LSB mean** because per-step integral
truncation accumulates. This is why the main FP32 table is deliberately
P-only; it is a documented numerical limitation, not a parity failure.

## `extra-2026-11-12/` — performance mode, direct time, RPC overhead

- `dsp-perf/`: default versus CDSP max-performance A/B. Max mode raised the
  effective clock by 15.9% (1.159 → 1.343 Gcycles/s); cycles/step stayed
  377.0.
- `dsp-time/`: standalone `HAP_perf` probe returned 1.170 GHz; the same-session
  indirect median was 1.1572 GHz (+1.11%).
- `rpc-overhead/`: 10,000 empty `echo`/`step` RPCs and N-step scaling. Median
  empty RPC was 74.0 µs idle; there is no scalar-FOC break-even because the DSP
  slope exceeds the CPU slope.

## `extra-2026-11-13/` — vector add and A55 comparison

- `vector-add/`: CPU scalar, CPU NEON, DSP scalar and DSP HVX at 64 and 4096
  lanes. DSP scalar/HVX ratios are 65.08× and 64.28×; this is an intra-DSP
  microbenchmark, not a FOC claim.
- `a55/`: Cortex-A55 CPU 0 (173.87 ns/step) versus A78 prime CPU 7
  (59.46 ns/step) for the same scalar FOC loop.

## `extra-2026-11-15/` — focused follow-up experiments

- `vector-add-multiacc/`: one- versus four-accumulator HVX vector add at 64 and
  4,096 lanes, 3 processes, 5 repetitions. Four accumulators give 0.0269 /
  0.0268 ns per lane-add, 3.48× / 3.49× better than one accumulator and
  3.83× / 2.29× faster than CPU NEON. Raw log and conditions in `summary.md`.
- `rpc-n256/`: 300-sample idle and loaded distributions for one RPC carrying
  256 FOC steps. Idle is not unimodal (58% ≤250 µs, 24% 250–500 µs, 18% >500 µs);
  loaded is 298/300 ≤250 µs. Raw CSV, percentile files, histogram CSV/SVG and
  `summary.md`.
- `repeat-2026-11-08-archive/`: pre-fix repeat retained for provenance only.
  Its 26.72× result used the old clock formula and is not evidence.
- `post-final-layout-check/`: current-tree run at `488f2d1` with later sampler
  code; 383.600 cycles/step, same checksum. It documents layout sensitivity and
  is not the repeatability run.

## `thermal-79C-2026-11-11/` — thermal ramp

A bounded thermal run: 46.2–68.4 °C observed, 68.0 °C hot, below the 79 °C
ceiling; no throttling was observed. See `thermal-summary.md` and `probes.csv`.

## `rt-2026-11-14/` — real-time distributions and suitability

- `raw/`: per-sample CSV. Main series: 3 conditions × 3 interleaved rounds ×
  (CPU, DSP compute, DSP+RPC step); `bench` series: 10 chunks of 10.5k calls per
  condition.
- `summary.md`: percentile tables, p99.9/p99.99, bootstrap CIs, deadline
  suitability, condition comparison, drift, timer overhead.
- `figures/`: log-Y histograms and time-series SVGs.
- `timer-overhead-cpu.json`, `timer-overhead-dsp.jsonl`: L6 timer-read cost.
- `environment.txt`: governor, frequency, kernel, sample counts.
- Method and conclusions: [`../../docs/rt-report.md`](../../docs/rt-report.md).

## Regeneration and verification

```bash
./scripts/run_foc_suite.sh --out /tmp/foc-run
diff -u results/final/summary.md /tmp/foc-run/summary.md
```

Cycle and ns values will differ slightly with compiler version, code layout,
host load and CDSP state. Exact validation, checksums and pass/fail conditions
must match. Verify the committed hashes before trusting a copied result:

```bash
cd results/final && sha256sum -c SHA256SUMS
cd ../.. && sha256sum -c results/final/artifacts.sha256
```

The build artifacts listed in `artifacts.sha256` are not committed; their
hashes identify the exact binaries that produced the run.
