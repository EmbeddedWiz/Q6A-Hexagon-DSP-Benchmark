# Radar — reference design, not implemented

## Status

**Not implemented in this repository.** There is no radar source, FFT/CFAR
kernel, IDL method, synthetic frame, validation log or timing result. This
page records the theory and the exact reference contract. It is not a result
report.

```bash
git log --all --name-only -- '*radar*'
# only docs/workloads/radar.md
```

## Theory (limited)

A pulsed radar frame is a two-dimensional complex array
`x[chirp][range_sample]`. The reference pipeline is:

```text
range FFT:    X[c][k] = sum_n x[c][n] * exp(-2*pi*i*k*n/Nr)
Doppler FFT:  D[m][k] = sum_c X[c][k] * exp(-2*pi*i*m*c/Nc)
power:        P[m][k] = |D[m][k]|^2
```

A window (`Hann` or `Hamming`) is applied before each FFT to control
sidelobes. `Nr` is the number of range samples, `Nc` the number of chirps.

Cell-averaging CFAR uses a window of `N` reference cells around the cell under
test and a threshold multiplier `alpha`:

```text
noise = (1/N) * sum(reference cells)
alpha = N * (Pfa^(-1/N) - 1)
detect if P[m][k] > alpha * noise
```

`Pfa` is the design false-alarm probability per cell. Guard cells are excluded
from the reference window. The result is a range-Doppler map plus a detection
list.

## Reference implementation

The reference is CPU-only and uses `double` complex arithmetic. It is not a
DSP kernel and it is not connected to `foc_bench.idl`.

### Data

```cpp
struct RadarFrame {
    std::size_t range_bins;   // Nr
    std::size_t chirps;       // Nc
    std::vector<std::complex<double>> samples; // [chirp][range]
};
```

### Per-frame path

1. Validate that `Nr` and `Nc` are powers of two for the reference FFT.
2. Apply the chosen window along range.
3. Run an in-place radix-2 Cooley-Tukey FFT for each chirp.
4. Apply the Doppler window and run the FFT across chirps for each range bin.
5. Compute power `|D|^2` in double.
6. Run CA-CFAR with explicit guard/reference cells and a fixed `Pfa`.
7. Emit detections as `(range_bin, doppler_bin, power, threshold)`.
8. Compute a checksum over the map and detection list.

### FFT details

```text
bit-reversal permutation
for stage s = 1..log2(N):
    m = 2^s
    wm = exp(-2*pi*i/m)
    for k = 0..N step m:
        w = 1
        for j = 0..m/2-1:
            t = w * a[k+j+m/2]
            u = a[k+j]
            a[k+j] = u + t
            a[k+j+m/2] = u - t
            w = w * wm
```

Twiddle factors are generated once per size and reused. A fixed-point
version must document per-stage scaling and saturation; the double reference
is the correctness oracle.

### Validation

- FFT vs a double-precision DFT on small deterministic frames;
- Parseval/energy check and inverse-FFT round trip;
- a known target at a chosen range/velocity must land in the expected bin;
- CFAR false-alarm count on pure noise vs the design `Pfa`;
- detection probability on a fixed SNR grid;
- exact CPU↔DSP parity for a fixed-point pipeline;
- checksum over the range-Doppler map and detections.

### Batch contract for a future IDL

```text
frame_count, range_bins, chirps, input buffer, output buffer, flags
```

Radar frames are much larger than FOC inputs. A supported buffer/DMA path is a
prerequisite; measuring only the kernel would understate end-to-end latency.

## Limitations

- **Not implemented.** No source, frames, logs or timings exist.
- The reference is a synthetic, memory-backed pipeline. It has no ADC, RF
  front-end, calibration, phase noise or antenna array.
- No STAP, beamforming, clutter model, track association or tracker.
- No hard-real-time deadline or WCET claim. Frame latency and throughput must
  be measured separately from the FFT kernel.
- The double reference is not a DSP implementation; fixed-point scaling and
  saturation are a new error source.
- HVX benefit depends on frame size and memory bandwidth, not only arithmetic.
- The CFAR design `Pfa` is a per-cell target; measured false alarms include
  guard/window effects and must be reported, not assumed.

## Acceptance criteria

- [ ] named pipeline and frame geometry;
- [ ] deterministic synthetic input and seed;
- [ ] fixed-point/FP32 kernels with documented scaling;
- [ ] separate IDL methods and generated stubs;
- [ ] independent FFT/CFAR reference;
- [ ] detection and false-alarm metrics;
- [ ] raw per-frame timings and a checksum;
- [ ] a result section that does not borrow FOC numbers.
