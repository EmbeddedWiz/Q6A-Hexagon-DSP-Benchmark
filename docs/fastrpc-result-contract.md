# FastRPC result contract

## Source IDL

`dsp/idl/foc_bench.idl` is the single source of truth:

```idl
#include "remote.idl"

interface foc_bench : remote_handle64 {
    long echo(in uint64_t input, rout uint64_t output);

    long configure(in long kp_d_q15, in long ki_d_q15,
                   in long kp_q_q15, in long ki_q_q15);

    long reset();

    long step(in long ia_q15,
              in long ib_q15,
              in long id_ref_q15,
              in long iq_ref_q15,
              in long electrical_angle,
              in long reset_state,
              rout long id_q15,
              rout long iq_q15,
              rout long vd_q15,
              rout long vq_q15,
              rout long duty_a_q15,
              rout long duty_b_q15,
              rout long duty_c_q15);

    long bench(in long warmup,
               in long runs,
               in long iterations,
               in long kp_d_q15,
               in long ki_d_q15,
               in long kp_q_q15,
               in long ki_q_q15,
               rout uint64_t cycles0,
               rout uint64_t cycles1,
               rout uint64_t cycles2,
               rout uint64_t cycles3,
               rout uint64_t cycles4,
               rout uint64_t checksum,
               rout uint64_t status);
};
```

## Design rules

1. **Measurements never travel through `main()` or the process exit code.**
   The RPC return value means transport/argument success only.
2. **Workload validity is a separate bitmask.** `status == 0` means every
   internal check passed; a nonzero status is a workload failure even if the RPC
   returned 0.
3. **Only scalar `rout` values.** No structs, pointers, arrays, STL types or
   RPCMEM sequence buffers cross the boundary. This avoids layout and ownership
   assumptions that are hard to prove on both sides.
4. **The generated header is authoritative.** Do not hand-write the AP or DSP
   prototypes. Regenerate and match the spelling exactly.
5. **Validation and timing are separate paths.** `step` is one RPC per vector
   for deterministic host-driven reset control; `bench` is one RPC containing
   the whole measured loop so per-step RPC overhead is not part of the result.

## Generated ABI (verified)

For this SDK, QAIC maps IDL `long` to plain `int` and `uint64_t` to
`uint64_t`. The generated declarations are of the form:

```c
int foc_bench_echo(remote_handle64 _h, uint64_t input, uint64_t* output);

int foc_bench_step(remote_handle64 _h,
                   int ia_q15, int ib_q15, int id_ref_q15, int iq_ref_q15,
                   int electrical_angle, int reset_state,
                   int* id_q15, int* iq_q15, int* vd_q15, int* vq_q15,
                   int* duty_a_q15, int* duty_b_q15, int* duty_c_q15);

int foc_bench_bench(remote_handle64 _h,
                    int warmup, int runs, int iterations,
                    int kp_d_q15, int ki_d_q15, int kp_q_q15, int ki_q_q15,
                    uint64_t* cycles0, uint64_t* cycles1,
                    uint64_t* cycles2, uint64_t* cycles3,
                    uint64_t* cycles4, uint64_t* checksum, uint64_t* status);
```

The DSP implementation uses these exact types. Using `int32_t` in the definitions
caused `conflicting types` at compile time. The generated header is included in
`results/final/` indirectly via its SHA-256 in `results/final/artifacts.sha256`.

## Method semantics

### `echo`

Returns `~input`. The suite tests `0`, `1`, `0x0123456789ABCDEF`,
`0xFFFFFFFFFFFFFFFF` and `0xDEADBEEFCAFEBABE`. Inverting the value makes both
64-bit halves observable; returning the input unchanged would not catch a
truncated upper word.

### `configure`

Replaces all four Q15 gains and resets both PI integrators. Values must fit in
`int16_t`; otherwise the RPC returns `-2` and does not change state.

### `reset`

Clears `integral_d_` and `integral_q_` without changing gains.

### `step`

Runs exactly one `Controller::step`. `reset_state != 0` calls `reset()` before
the step. All current/reference arguments are Q15 and must fit in `int16_t`.
`electrical_angle` is accepted in `0..65535`; the controller masks it to the
14-bit domain (0..16383). Outputs are written to the seven `rout` pointers.

### `bench`

1. Validate arguments and output pointers.
2. Configure gains and reset the controller.
3. Run `warmup` steps from a deterministic input sequence.
4. For each of `runs` (1..5):
   - reset the controller;
   - read `qurt_get_core_pcycles()`;
   - run `iterations` steps and accumulate the checksum;
   - read the counter again;
   - store the raw elapsed cycles.
5. Return the last checksum and a status bitmask.

The checksum sums the seven output fields as unsigned 16-bit values:

```text
sum += (uint16_t)id_q15 + (uint16_t)iq_q15
     + (uint16_t)vd_q15 + (uint16_t)vq_q15
     + duty_a_q15 + duty_b_q15 + duty_c_q15
```

It is an integrity check, not a proof of full output equality. Equality is
proved by the `validate` mode, which compares every field of every step.

## Status bitmask

| Bit | Meaning |
|---:|---|
| 0 | a timed region produced zero cycles |
| 1 | a checksum is zero |
| 2 | cycles per step is outside `[1, 1_000_000]` |
| 3 | checksums differ between repetitions (non-deterministic) |

`status == 0` is required. The suite treats any nonzero status as a failure.
Bits are additive; the raw value is retained in every log line.

## Clock handling

`qurt_get_core_pcycles()` returns CDSP core cycles. It is the primary metric
because it is independent of host scheduling and of the effective clock rate.

`qurt_timer_get_ticks()` was tested as an independent 19.2 MHz Qtimer time base
and **returns 0 in this unsigned User PD**, so the IDL does not expose a timer
tick field. A separate standalone probe can read the qtimer through
`HAP_perf_get_time_us()`; it measured **1.170 GHz ± 0.005 GHz**, but it is not
linked into the FOC skeleton because its presence perturbed the hot loop by
+1.8%. FOC ns/step therefore uses host time: the host measures the `bench` RPC
with `std::chrono::steady_clock` and subtracts a measured one-step preflight call
to remove the fixed RPC cost:

```text
effective_cycle_rate = sum(cycles[0..4]) / (rpc_wall - preflight_wall)
cycles_per_step      = sum(cycles[0..4]) / (5 * 16384)
ns_per_step          = cycles_per_step / effective_cycle_rate
```

This is a derived *effective* rate (≈1.15–1.19 Gcycles/s across the final and
repeat runs), not a claim about the physical CDSP PLL. The direct probe and the
effective rate differ by **+1.11%** in the same session (1.170 vs 1.1572 GHz);
HVX uses the former, FOC uses the latter. See
`docs/benchmark-methodology.md` and `results/extra-2026-11-12/dsp-time/`.

## Transport and loading

- `remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE, ...)` enables domain 3
  as an unsigned module PD.
- The skeleton is loaded from `DSP_LIBRARY_PATH`/`ADSP_LIBRARY_PATH`; the URI is
  `file:///libfoc_bench_skel.so?...&_dom=cdsp`.
- `foc_bench_open()` stores a non-null token in `remote_handle64`; the DSP
  implementation never dereferences it.
- The host stub is linked with `-lcdsprpc`.

## Error semantics observed on this board

| Symptom | Meaning in this project |
|---|---|
| `foc_bench_open` returns `114` | `ECONNREFUSED`; the CDSP is already dead. Reboot required in the observed case. |
| `104` | Ambiguous: `ENORPCMEMORY` or `ECONNRESET`. Check context and `dmesg`. |
| `AEE_EUNABLETOLOAD (0x80000406)` | The skeleton was not found by the loader. Check `DSP_LIBRARY_PATH`, cwd and the file name. |
| RPC returns 0 but `status != 0` | Transport worked; the workload failed a validity check. |
| RPC returns nonzero | Transport/argument failure; measurements are not valid. |

Do not infer CDSP health from `remoteproc1/state` alone. In the documented crash,
the state was `offline`; after a failed recovery attempt it stayed unusable until
a reboot. The reliable health check is a real RPC such as `echo`.

## What this contract deliberately does not do

- No `printf`/FARF logging as a result channel.
- No `main()` return value as a result channel.
- No `run_main_on_hexagon` invocation for this benchmark.
- No structs or pointer arguments.
- No per-step RPC inside the timed loop.
- No assumption that `long` is 64-bit (it is 32-bit in this ABI).
- No assumption that `qurt_get_core_pcycles()` includes or excludes stalls; the
  host-derived effective rate is labeled as such.
