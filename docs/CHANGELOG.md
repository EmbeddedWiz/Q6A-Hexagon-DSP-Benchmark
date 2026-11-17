# Changelog and errata

## 2026-11-09 final run — DSP/CPU ratio corrected

The 2026-11-09 final report listed DSP/CPU at about **28×** (27.52× process median, 1,670.8 ns/step); the current value is **5.28×** (320.96 ns/step). The old runner divided the cycle count of one 16,384-step repetition by the wall time of the whole five-repetition RPC, so the effective clock was understated by about 5× (222–232 vs 1,152–1,181 Mcycles/s). The fix in commit `245dc6a` sums all five repetitions before dividing; cycles/step, checksum and validation were unaffected. The cause is confirmed by the old source formula; the pre-fix raw log was not retained.
