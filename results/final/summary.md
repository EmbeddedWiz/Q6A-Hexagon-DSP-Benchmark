# FOC run summary

- Run: `20261110T230455Z`
- Echo: PASS
- Validation: PASS — 20510 DSP steps, 0 exact mismatches, 13.275 LSB max FP32 error
- PI clamp test: PASS — 30 cases, 0 exact mismatches, 4.114 LSB max FP32 error
- PI stress: PASS — saturation d/q=True/True, recovery d/q=True/True, reset=True/True/True, long 100000 steps, 0 exact mismatches, 26230.912 LSB max FP32 error
- HVX: PASS, PASS, PASS — scalar median 468.239 cycles/iteration, HVX median 7.010 cycles/iteration, speedup 66.800x

| Metric | Min | Median | Mean | Max |
|---|---:|---:|---:|---:|
| DSP process-median cycles/step | 376.995 | 376.996 | 377.000 | 377.009 |
| DSP all 15 cycles/step | 376.955 | 377.002 | 377.008 | 377.118 |
| DSP process-median ns/step | 319.332 | 320.960 | 322.499 | 327.207 |
| DSP all 15 ns/step | 319.317 | 320.960 | 322.506 | 327.313 |
| DSP ns/step, no preflight subtraction | 325.338 | 325.770 | 326.744 | 329.126 |
| DSP RPC wall (us) | 26651.882 | 26686.517 | 26767.451 | 26963.954 |
| DSP effective clock (MHz) | 1152.160 | 1174.630 | 1169.123 | 1180.580 |
| DSP clock no correction (MHz) | 1145.442 | 1157.285 | 1153.837 | 1158.785 |
| Preflight share of wall (%) | 0.583 | 1.477 | 1.302 | 1.846 |
| CPU ns/step | 60.534 | 60.734 | 60.685 | 60.788 |

DSP/CPU time ratio: **5.28x**

Checksums: 2827389792
