# Energy and power measurement

## Status

**Not measured — blocked.** No external power meter, calibrated shunt, or
per-rail current sensor was available for this project. The report therefore
contains no energy, power, or joules-per-step number.

## Why the existing data is not a substitute

- DSP **cycles** and host **wall time** measure latency, not energy.
- The board's thermal sensors measure package temperature, not rail power.
- CPU/DCVS frequency changes (P1.1) change both time and power; without a meter
  they cannot be converted into an energy result.
- A lower cycle count is not automatically a lower-energy result, especially
  when the performance mode raises the clock and voltage.

## What a valid measurement would require

1. An external meter or calibrated shunt on the relevant rails (at minimum the
   CDSP rail and the CPU cluster rail), with a known sample rate and accuracy.
2. Synchronized timestamps around the same workload used here (5 × 16,384 FOC
   steps, plus idle baselines).
3. Repeated runs at fixed thermal state and clock mode, reporting median and
   range in joules per step, not only watts.
4. A separate measurement for DSP performance mode, because it changes the
   clock/voltage operating point.

Until those conditions exist, any energy claim would be invented. The report
explicitly does not make one.

## Related evidence

- DSP performance-mode A/B: `results/extra-2026-11-12/dsp-perf/summary.md`
- Direct clock probe: `results/extra-2026-11-12/dsp-time/summary.md`
- RPC overhead and scaling: `results/extra-2026-11-12/rpc-overhead/summary.md`
