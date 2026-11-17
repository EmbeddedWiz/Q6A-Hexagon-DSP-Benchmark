# RPC carrying N=256 FOC steps: 300-sample distribution — 2026-11-16

## Purpose

Resolve the earlier description of the idle N=256 result as “outliers”. The
previous scaling run used 30 samples; this run keeps the same RPC path and
records 300 samples per condition.

## Conditions

- Board: Radxa Dragon Q6A / QCS6490, Hexagon v68 CDSP; runner pinned to CPU 7.
- Commit: `21abbf7`.
- `rpc-scaling --rpc-repeats 300`, all sizes; only N=256 is analysed here.
- Idle: no competing load, CPU 7 `schedutil`, 41.4 → 41.8 °C.
- Loaded: one `yes` on CPU 0 and one on CPU 7, runner also on CPU 7, 50.0 →
  52.0 °C.
- Raw samples: `rpc-idle.csv`, `rpc-loaded.csv`; logs: `rpc-idle.log`,
  `rpc-loaded.log`; histograms: `rpc-idle.svg`, `rpc-loaded.svg`.

## Percentiles (microseconds, 300 samples)

| Condition | n | p50 | p90 | p99 | max | mean |
|---|---:|---:|---:|---:|---:|---:|
| idle | 300 | **228.9** | 628.0 | 1,239.4 | 1,261.1 | 339.6 |
| loaded | 300 | **147.0** | 153.2 | 171.7 | 3,144.2 | 161.7 |

The old 30-sample idle median (356.7 µs) sat between the fast group and the
slow tail; the 300-sample median is 228.9 µs. The loaded median is 147.0 µs.

## Distribution

- **Idle is not unimodal.** 174/300 samples (58.0%) are at or below 250 µs,
  72/300 (24.0%) are 250–500 µs, and 54/300 (18.0%) are above 500 µs. A
  two-means description splits at 633 µs into 272 samples (90.7%) centred at
  272 µs and 28 samples (9.3%) centred at 993 µs; the upper group is broad
  (roughly 590–1,260 µs), so this is a fast group plus a slow tail rather than
  two narrow modes.
- **Loaded is one dominant mode with rare outliers.** 298/300 samples are
  at or below 250 µs (p90 153.2, p99 171.7); two samples are above 500 µs
  (one near 1.0 ms, one at 3.14 ms).

Possible causes of the idle slow tail are host CPU idle-state/scheduler
behaviour and CDSP DCVS state; the loaded run's higher CPU clock is consistent
with that, but this dataset does not prove a mechanism. The two rare loaded
outliers are consistent with host preemption or a transient service delay; they
are not treated as a separate mode.

## Files

- `rpc-idle.percentiles.txt`, `rpc-loaded.percentiles.txt`: tool output and
  two-means description.
- `rpc-idle.hist.csv`, `rpc-loaded.hist.csv`: 48-bin histogram counts.
- `rpc-idle.svg`, `rpc-loaded.svg`: plots; the loaded plot truncates at p99 and
  states the number above it, so the two rare outliers do not compress the
  main mode.
- `SHA256SUMS`: hashes of the retained raw and derived files.
