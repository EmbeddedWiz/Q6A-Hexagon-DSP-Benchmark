# Architecture

## Execution path

```text
┌──────────────────────────────┐        SSH/SCP        ┌─────────────────────────────┐
│ x86_64 VMware build host     │ ────────────────────> │ Radxa Dragon Q6A / QCS6490  │
│                              │                       │ Ubuntu 24.04.4, AArch64     │
│ hexagon-clang 19.0.04        │                       │                             │
│ qaic (SDK 6.4.0.2)           │                       │  host/foc_runner            │
│ generated stub + skel        │                       │    │                        │
└──────────────────────────────┘                       │    │ libcdsprpc1            │
                                                       │    v                        │
                                                       │  /dev/fastrpc-cdsp          │
                                                       │    │                        │
                                                       │    v                        │
                                                       │  CDSP domain 3 (User PD)    │
                                                       │  libfoc_bench_skel.so       │
                                                       │    Controller + CORDIC      │
                                                       └─────────────────────────────┘
```

The project deliberately separates two build products:

| Product | Built on | Runs on | Purpose |
|---|---|---|---|
| `libfoc_bench_skel.so` | x86 VMware, `hexagon-clang++ -mv68` | CDSP domain 3 | QAIC skeleton + FOC + CORDIC |
| `foc_runner` | Dragon Q6A, `c++` (AArch64) | Cortex-A78/A55 | host CLI and FastRPC stub |

The generated QAIC header and stub bridge the two. The skeleton and the host
runner are produced by `scripts/build_foc_bench.sh`.

## Why a project-owned FastRPC contract

The earlier path used the SDK's `run_main_on_hexagon` helper. That helper
executes a `main()`-style DSP module and treats a nonzero return as an error.
Returning a cycle count as the process status is therefore both ambiguous and
unusable as a result channel; `printf`/FARF output was not visible in the
unsigned-PD runner output.

The project instead defines `dsp/idl/foc_bench.idl`, generates an AP stub and
DSP skeleton with `qaic`, and returns all measurements through scalar `rout`
parameters. The RPC return value is reserved for transport/argument errors.
This makes "the RPC succeeded" and "the workload is valid" independent
conditions.

## Host runner

`host/foc_runner.cpp` has four modes:

| Mode | What it does |
|---|---|
| `echo` | Sends five 64-bit patterns and requires `~input`. Verifies both 64-bit halves. |
| `validate` | Drives 20,510 single-step RPCs; compares against a CPU fixed-point oracle and an independent FP32 model; optionally writes a per-case CSV. |
| `bench` | One RPC runs 256 warm-up steps plus five timed 16,384-step repetitions on the DSP and returns the raw cycle counts. Three independent processes are used by the suite. |
| `cpu` | Runs the same `Controller` source on the AArch64 CPU with the same inputs, reset policy, runs and checksum, pinned to CPU 7 with a long clock-ramp warm-up. |

`all` runs echo, validation, bench and CPU in one session. `validate` accepts
gain overrides; the suite uses the default P-only configuration for the main
20,510-step run and a `ki=32767` edge-case run for PI clamp/recovery.

## DSP skeleton

`dsp/fastrpc/foc_bench_skel_impl.cpp` implements the IDL methods and owns one
global `Controller` instance. The controller is configured and reset through
explicit RPC methods:

- `configure(gains)` replaces gains and resets the PI integrators.
- `reset()` clears only the integrators.
- `step(..., reset_state)` optionally resets before one step.
- `bench(...)` configures, warms up, then runs the timed loop.

A global controller is used because the unsigned User PD has no heap/STL
guarantees and the benchmark needs one persistent state across calls. The
generated `remote_handle64` is treated as an opaque non-null token; the
implementation does not dereference it or use it as a pointer.

## Memory and threading boundary

- No pointers, STL containers, or variable-length arrays cross the RPC boundary.
- Only scalar `in` and `rout` parameters are used (`int` and `uint64_t` in the
  generated ABI). This was verified in the generated header, not assumed.
- No RPCMEM/DMA-BUF is needed for this scalar workload.
- The unsigned PD lacks `qurt_thread_*`; the implementation is single-threaded.
- The benchmark loop is one RPC, so the 16,384 steps are not individually
  serialized. Validation deliberately uses one RPC per step for simplicity and
  deterministic host-driven reset control; it is not a timing path.

## Build tree

`scripts/build_foc_bench.sh` writes:

```text
build/foc/
├── generated/
│   ├── foc_bench.h          QAIC AP declarations
│   ├── foc_bench_stub.c     QAIC AP stub
│   ├── foc_bench_skel.c     QAIC DSP skeleton
│   └── libfoc_bench_skel.so deployed DSP module
├── foc_bench_stub.o         AArch64 stub object
└── foc_runner               AArch64 host CLI
```

Generated files and binaries live under `build/`, which is not committed.
The canonical result logs and compressed validation CSVs are committed.

## Failure model

| Layer | Success signal | Failure signal |
|---|---|---|
| FastRPC open/call | return value 0 | nonzero (`104`, `114`, `AEE_*`) |
| Workload | `status == 0` bitmask | any status bit set |
| Correctness | exact CPU parity and bounded FP32 error | mismatch count > 0 |
| Transport integrity | echo complement, checksum agreement | wrong echo/checksum |

`104` can mean `ENORPCMEMORY` or `ECONNRESET` depending on context; `114` is
`ECONNREFUSED`. A `114` on open in this project has meant the CDSP was already
dead, not that the IDL or skel was wrong. See
`docs/engineering-findings.md` for the crash/reboot evidence.
