# Benchmark methodology

## Two different DSP numbers

The suite reports two DSP time figures on purpose:

| Metric | Source | Use |
|---|---|---|
| **Cycles per step** | `qurt_get_core_pcycles()` inside the DSP timed loop | Primary, host-scheduling-independent comparison |
| **Nanoseconds per step** | Host `steady_clock` around the `bench` RPC, minus a measured one-step preflight, divided by 16,384 | End-to-end reality check |

Cycles are primary because the CDSP frequency is not under this project's
control and the board's `schedutil` policy can change host behaviour. The
nanosecond figure is derived from the same cycle counts and the measured
effective cycle rate, so it is a presentation of the same data, not an
independent claim that the DSP runs at a particular PLL frequency.

## DSP measurement

`bench` is one FastRPC call. Inside it:

1. Configure the controller and reset the PI integrators.
2. Run 256 warm-up steps with the deterministic input sequence.
3. For each of five repetitions:
   - reset the controller;
   - read `qurt_get_core_pcycles()`;
   - run 16,384 steps;
   - read the counter again;
   - store the raw elapsed cycle count.
4. Return all five raw counts, the final checksum and the status bitmask.

The host then reports per-step cycles for each repetition and the min, median,
mean and max. The suite runs the whole `bench` RPC in **three independent
processes**, so the final result is three medians, not five samples from one
process. Raw samples stay in `results/final/bench-dsp.log`.

### Effective cycle rate

`qurt_timer_get_ticks()` returns 0 in this unsigned User PD, so the RTOS tick
timer is unavailable. The FOC skeleton therefore uses host time. The host
measures:

- `rpc_wall`: wall time of the `bench` RPC;
- `preflight_wall`: wall time of a one-step RPC used to estimate fixed call
  overhead.

Then:

```text
rpc_compute       = rpc_wall - preflight_wall   # one RPC = 5 repetitions
total_cycles      = sum(cycles[0..4])           # all five repetitions
effective_rate    = total_cycles / rpc_compute
cycles_per_step   = total_cycles / (5 * 16384)
ns_per_step       = cycles_per_step / effective_rate
```

The final run measured 1,180.6, 1,152.2 and 1,174.6 million cycles per second. The
median derived ns/step is 320.96 ns; the per-invocation values are 319.33,
327.21 and 320.96 ns. The second invocation is slowest; report the median and
the range, not a single cherry-picked value.

**Caveat:** a ratio of retired core cycles to wall time is not a hardware clock
measurement: stalls and cache misses still count in wall time. The project labels
it `effective_clock_hz` in the raw JSON. The result, about 1.15–1.19 Gcycles/s,
is consistent with the 1.16 GHz fallback used when calibration is disabled.

### DCVS mode

The canonical final run is **default DCVS**. The 2026-11-12 A/B re-ran the same
3-process × 5 × 16,384-step benchmark in both policies. With the final CPU
baseline (60.734 ns/step):

| DSP policy | DSP ns/step | DSP/CPU |
|---|---:|---:|
| default, canonical final | 320.960 | **5.28×** |
| default, A/B session | 325.276 | 5.36× |
| max performance, A/B session | 280.644 | **4.62×** |

Max performance raises the effective clock by 15.9% and lowers ns/step by 13.7%,
but cycles/step change by only 0.011%. The headline is 5.28× in default mode;
4.62× is the max-performance counterpart. Raw evidence:
`results/extra-2026-11-12/dsp-perf/`.

## CPU measurement

The CPU mode uses the **same `Controller` source**, the same input sequence,
the same 256-step workload warm-up, the same five repetitions of 16,384 steps
and the same checksum function.

Two additional controls are required:

1. **Pin to CPU 7.** CPU 7 is a Cortex-A78 prime core (max 2,707.2 MHz). Without
   pinning, the process migrates between A55 and A78 cores and the measured
   value varies from about 68 to 181 ns/step.
2. **Ramp the clock.** After the 256-step warm-up, the runner executes
   2,000,000 additional steps to let `schedutil` raise the core clock. This
   warm-up is not timed and the controller is reset before each timed
   repetition. Without it, a 16,384-step loop measures the idle clock.

The final run used `taskset -c 7`, governor `schedutil`, driver
`qcom-cpufreq-hw`, and recorded `scaling_cur_freq = 2707200` during environment
capture. The three process medians were 60.534, 60.734 and 60.788 ns/step;
overall median 60.734 ns/step.

### Repeatability

A second full suite run (`results/repeatability/`) on 2026-11-16 used the same
final source (`245dc6a`) from a detached worktree:

```text
DSP process medians: 377.006, 376.999, 377.007 cycles/step
CPU process medians: 60.546, 60.307, 60.972 ns/step
checksum:            2,827,389,792 (all)
validation:          identical main and PI-clamp statistics, 0 mismatches
```

The spread between the two runs' cycle medians is 0.004%. The measured
effective rate varied about 1.153–1.167 Gcycles/s in the second run, which is
why ns varies between runs while cycles stay stable. The second run's DSP/CPU
ratio was 5.40×.

## Validation configurations

The main benchmark and the 20,510-step validation run use `kp=8192, ki=0`
(P-only). This keeps the FP32 quantization statistics comparable to the timed
workload and avoids a long-run integral-truncation bias dominating the result.

The dedicated PI clamp test is a separate 30-case run with `ki=32767`. The
large-error hold drives the integrator into its clamp and the reverse sequence
checks recovery. It is deliberately not mixed into the 20,510-step statistics:
a long run with `ki=120` showed the expected floor-rounding bias (2,143 LSB max,
198 LSB mean) even though CPU and DSP still matched exactly; it is retained in
`results/pi120-2026-11-08/`.

## Input sequence

All timed and validation paths use the same deterministic generator:

```text
ia     = 12000
ib     = -7000
id_ref = 3000
iq_ref = -2000
angle  = i & 0x3FFF
```

for `i = 0 .. 16383`, with the controller reset at the start of each repetition.
The checksum is the unsigned 16-bit sum of all seven outputs per step.

## Statistics and acceptance

- DSP cycles: report every raw repetition, then the per-process median and the
  cross-process median.
- DSP ns: same, and state the effective-rate range.
- CPU: same, and state the affinity and warm-up.
- Acceptance for a benchmark run: all five internal repetitions return
  `status == 0`, all checksums are identical, and the three process medians are
  stable (observed spread < 0.02% for cycles).
- Acceptance for a comparison: the baseline and optimized builds must use the
  same script, timer, inputs, warm-up, iterations and runs, and both must pass
  full validation.

## Baseline vs optimized

| Quantity | Baseline (two CORDIC passes) | Optimized (fused `sincos_q15`) |
|---|---:|---:|
| Cycles/step, three process medians | 593.834, 593.844, 593.853 | 376.996, 376.995, 377.009 |
| Median cycles/step | **593.844** | **376.996** |
| Median wall, 5 × 16,384-step RPC | 43.21 ms | 26.69 ms |
| Median ns/step | 524.13 | 320.96 |
| Checksum | 2,827,389,792 | 2,827,389,792 |
| Validation | PASS, 0 mismatches | PASS, 0 mismatches |

Derived:

```text
cycle reduction   = (593.844 - 376.996) / 593.844 = 36.52%
cycle speed-up    = 593.844 / 376.996              = 1.575x
wall ratio        = 43.211 ms / 26.687 ms          = 1.619x
```

The wall ratio is higher than the cycle speed-up because this baseline rebuild
measured a lower effective rate (1.13–1.15 versus 1.15–1.18 Gcycles/s in the
optimized run). Cycles are the defensible optimization metric.

### CDSP performance mode

The original runs did not pin the CDSP DCVS state; they used the default
policy. The runner now has an explicit `--dsp-perf` mode that calls
`HAP_power_set(HAP_power_set_DCVS_v2, HAP_DCVS_V2_PERFORMANCE_MODE)` before the
bench preflight and restores the default `ADJUST_UP_DOWN` policy afterward. In
the 2026-11-12 A/B, maximum performance raised the effective rate from 1.159 to
1.343 Gcycles/s and lowered ns/step from 325.276 to 280.644, while cycles stayed
at 376.98–377.02. The canonical final numbers remain default-mode; the A/B is
retained in `results/extra-2026-11-12/dsp-perf/`.


### Direct DSP time source

`HAP_perf_get_time_us()` and `qurt_get_core_pcycles()` both work in the unsigned
PD. A standalone 1,000,000-iteration probe returned 117 five times (1.170 GHz),
while the indirect FOC estimate for three processes in the same session was
1.104–1.166 GHz (median 1.157 GHz). The direct reading is therefore about 1.1%
above the indirect median, and the indirect run-to-run spread is larger than
that bias. Adding the probe to the FOC skeleton changed the hot loop from about
377.0 to 383.6 cycles/step (+1.8%) through code-layout effects, so direct timing
is kept in a separate binary (`dsp/probes/time_probe.c`); details and raw logs
are in `results/extra-2026-11-12/dsp-time/`. Rule: FOC ns/step uses the effective
rate; HVX ns uses the direct 1.170 GHz value.


## CPU/DSP comparison

Using the median DSP ns/step and median CPU ns/step:

```text
DSP / CPU = 320.96 ns / 60.734 ns = 5.28x
```

Using the full RPC wall per step:

```text
DSP wall / CPU = (26686.517 us / (5 * 16384)) / 60.734 ns = 5.36x
```

The repeat run measured 5.40× by median ns. Both are reported as
**≈5.28–5.40× slower on the DSP**. This is a property of this scalar, serial
FOC workload, not a general DSP-vs-CPU claim.

## Thermal policy

The board ceiling is 79 °C; the ramp targets 68 °C and aborts at 79 °C. One
CPU 4 core heats the package, CPU 7 is the CPU under test, and the CDSP is
probed only with the short 5 × 16,384-step RPC. The retained ramp recorded
49.6 °C idle, 68.0 °C hot and 50.4 °C after cooldown. CPU 7 stayed at
2,707,200 kHz in every probe and DSP cycles/step were flat. The 69–79 °C band
is not covered. Readings are in `results/thermal-79C-2026-11-11/`.

## What is deliberately excluded

- No per-step RPC inside the timed loop; that would measure transport, not
  compute.
- No `printf`/FARF logging in the timed region.
- No CPU frequency governor change; the project records the policy instead of
  requiring root-side tuning.
- No transfer of the HVX vector-add result onto FOC; it is a separate,
  data-parallel microbenchmark.
