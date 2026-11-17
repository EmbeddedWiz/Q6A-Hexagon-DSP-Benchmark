# FOC run summary

- Run: `post-final-layout-check`
- Echo: PASS
- Validation: PASS — 20510 DSP steps, 0 exact mismatches, 13.275 LSB max FP32 error
- PI clamp test: PASS — 30 cases, 0 exact mismatches, 4.114 LSB max FP32 error
- PI stress: PASS — saturation d/q=True/True, recovery d/q=True/True, reset=True/True/True, long 100000 steps, 0 exact mismatches, 26230.912 LSB max FP32 error
- HVX: PASS, PASS, PASS — scalar median 457.317 cycles/iteration, HVX median 7.011 cycles/iteration, speedup 65.232x; 4-accumulator HVX median 2.013 cycles/iteration, speedup 227.204x

| Metric | Min | Median | Mean | Max |
|---|---:|---:|---:|---:|
| DSP process-median cycles/step | 383.597 | 383.600 | 383.600 | 383.602 |
| DSP all 15 cycles/step | 383.564 | 383.598 | 383.598 | 383.640 |
| DSP process-median ns/step | 328.843 | 330.918 | 332.075 | 336.463 |
| DSP all 15 ns/step | 328.819 | 330.918 | 332.073 | 336.485 |
| DSP ns/step, no preflight subtraction | 334.693 | 334.723 | 336.379 | 339.719 |
| DSP RPC wall (us) | 27417.787 | 27420.442 | 27556.031 | 27829.865 |
| DSP effective clock (MHz) | 1140.087 | 1159.207 | 1155.269 | 1166.514 |
| DSP clock no correction (MHz) | 1129.158 | 1146.021 | 1140.437 | 1146.130 |
| Preflight share of wall (%) | 0.959 | 1.128 | 1.281 | 1.757 |
| CPU ns/step | 59.733 | 60.057 | 59.960 | 60.089 |

DSP/CPU time ratio: **5.51x** (process-median envelope 5.47–5.63x)

Checksums: 2827389792
