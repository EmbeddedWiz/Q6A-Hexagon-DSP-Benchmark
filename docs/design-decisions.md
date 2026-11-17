# Design decisions

The authoritative, dated record is `docs/decisions-log.md`. This page is a
short index for readers who want the shape of the design without the full ADR
history.

| Area | Decision | Why |
|---|---|---|
| Result transport | Project-owned QAIC IDL, scalar `rout` only | Clear ABI, no `main()`/stdout coupling |
| Status | RPC return = transport; `status` bitmask = workload | Separates "unreachable" from "bad measurement" |
| Correctness | CPU fixed-point is the exact oracle; FP32 is independent | Proves transport/build parity and quantifies quantization separately |
| Timing | DSP cycles primary; host wall secondary | CDSP clock is not controlled or observable |
| Clock | Measured effective rate, not 1.16 GHz | The fallback assumption contradicts wall time |
| CPU baseline | CPU 7, `taskset`, 2M-step ramp | Avoids schedutil/migration variance |
| Optimization | One fused CORDIC pass | Measured 1.575× with identical results |
| History | Do not merge historical sandbox numbers | Different build/checksum; avoids false comparison |
| HVX | Separate vector-add microbenchmark | Serial FOC is not an HVX workload |
| Evidence | Commit curated logs/CSVs, ignore build artifacts | Public reproducibility without binary bloat |
| CDSP health | Real `echo` RPC | `remoteproc` state alone is not a liveness check |
| Documentation | Decision record plus findings | One consistent, evidence-linked record |

## Consequences in one sentence

The project is deliberately conservative: it proves a small scalar contract
exactly, reports both the optimization and the measured boundary, and refuses
to turn a historical microbenchmark or an assumed clock into a general DSP
claim.
