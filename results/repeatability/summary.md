# FOC run summary

- Run: `repeatability`
- Echo: PASS
- Validation: PASS — 20510 DSP steps, 0 exact mismatches, 13.275 LSB max FP32 error
- PI clamp test: PASS — 30 cases, 0 exact mismatches, 4.114 LSB max FP32 error
- PI stress: PASS — saturation d/q=True/True, recovery d/q=True/True, reset=True/True/True, long 100000 steps, 0 exact mismatches, 26230.912 LSB max FP32 error
- HVX: PASS, PASS, PASS — scalar median 468.227 cycles/iteration, HVX median 7.010 cycles/iteration, speedup 66.798x

| Metric | Min | Median | Mean | Max |
|---|---:|---:|---:|---:|
| DSP process-median cycles/step | 376.999 | 377.006 | 377.004 | 377.007 |
| DSP all 15 cycles/step | 376.965 | 377.005 | 377.008 | 377.057 |
| DSP process-median ns/step | 322.919 | 326.742 | 325.537 | 326.951 |
| DSP all 15 ns/step | 322.912 | 326.742 | 325.541 | 326.995 |
| DSP ns/step, no preflight subtraction | 325.225 | 328.639 | 328.498 | 331.629 |
| DSP RPC wall (us) | 26643.001 | 26922.356 | 26910.840 | 27167.162 |
| DSP effective clock (MHz) | 1153.097 | 1153.838 | 1158.135 | 1167.470 |
| DSP clock no correction (MHz) | 1136.832 | 1147.174 | 1147.733 | 1159.193 |
| Preflight share of wall (%) | 0.514 | 0.709 | 0.899 | 1.474 |
| CPU ns/step | 60.307 | 60.546 | 60.609 | 60.972 |

DSP/CPU time ratio: **5.40x**

Checksums: 2827389792
