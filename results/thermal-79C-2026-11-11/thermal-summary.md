# Thermal soak summary

Probes: **5** (4 used for correlation); continuous samples: **59**.

Observed temperature range: **46.2–68.4 °C**.
CPU 7 frequency range in the trace: **806400–2707200 kHz**.

| Point | Max °C | CPU7 °C | NSP °C | DSP cycles/step | DSP ns/step | CPU ns/step | CPU7 max MHz |
|---|---:|---:|---:|---:|---:|---:|---:|
| Cold (idle) | 49.6 | 48.1 | 45.3 | 377.032 | 323.8 | 60.33 | 2707200 |
| Hot (under load) | 68.0 | 57.5 | 49.6 | 377.011 | 325.9 | 60.57 | 2707200 |
| After 60 s cooldown | 50.4 | 48.9 | 46.1 | 377.004 | 325.4 | 60.36 | 2707200 |

## Temperature correlation over cold, ramp and hot points

| Metric | Pearson r | Slope per °C |
|---|---:|---:|
| DSP cycles/step | -0.904 | -0.0013 |
| DSP ns/step | +0.491 | +0.0562 |
| DSP effective MHz | -0.499 | -0.2045 |
| CPU ns/step | +0.984 | +0.0139 |
| CPU 7 max frequency during probe (kHz) | – | -0.0000 |

The during-load trace CPU 7 frequency/temperature correlation is r = +0.646, slope +29243.5105 kHz/°C.

Interpretation rule used in the report: a cycle-count slope near zero means the DSP performed the same number of core cycles; a positive CPU ns/step slope together with a falling CPU 7 frequency indicates DVFS/thermal throttling. A DSP ns/step change without a cycles/step change is a wall-clock/effective-rate effect, not extra DSP work.

