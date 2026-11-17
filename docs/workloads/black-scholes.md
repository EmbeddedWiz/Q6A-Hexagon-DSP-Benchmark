# Black-Scholes — reference design, not implemented

## Status

**Not implemented in this repository.** There is no option-pricing source,
IDL method, validation file or timing result. This page records the exact
theory and reference implementation contract that a future workload must
follow. It is not a result report.

```bash
git log --all --name-only -- '*black*'
# only docs/workloads/black-scholes.md
```

## Theory (limited)

For a European call/put under the Black-Scholes assumptions:

```text
d1 = (ln(S/K) + (r + sigma^2/2) * T) / (sigma * sqrt(T))
d2 = d1 - sigma * sqrt(T)

C = S * N(d1) - K * exp(-r*T) * N(d2)
P = K * exp(-r*T) * N(-d2) - S * N(-d1)
```

where `N(x) = 0.5 * erfc(-x / sqrt(2))`. The put-call parity identity
`C - P = S - K*exp(-r*T)` is a useful independent check.

The numerical difficulty is not the algebra; it is the normal CDF and
cancellation. For deep out-of-the-money options, `S*N(d1)` and
`K*exp(-rT)*N(d2)` may be almost equal, so a low-precision CDF or a naive
fixed-point polynomial can lose most of the result's significant bits.

## Reference implementation

The reference is deliberately CPU-only and double precision. It is not a DSP
kernel and it is not wired into `foc_bench.idl`.

### Data

```cpp
struct OptionInput {
    double spot;       // S > 0
    double strike;     // K > 0
    double rate;       // r, per year
    double volatility; // sigma > 0
    double maturity;   // T > 0
};
```

### Per-option path

1. Reject `S <= 0`, `K <= 0`, `sigma <= 0`, `T <= 0`; return a documented
   error instead of a NaN.
2. Compute `sqrt_T = sqrt(T)`, `sigma_sqrt_T = sigma * sqrt_T`.
3. Compute `d1`, `d2` in double precision.
4. Compute `N(d1)`, `N(d2)` with `erfc` (not a low-order polynomial).
5. Compute `exp(-r*T)` once per batch if `r` and `T` are shared.
6. Evaluate call and put, then check `C - P` against parity within tolerance.
7. Return `C`, `P` and a status code. Greeks are out of scope for the first
   version.

### Batch contract for a future IDL

An IDL extension must not reuse the FOC `bench` semantics. A separate method
should take a scalar batch descriptor and a supported buffer handle:

```text
batch_size, option_parameter_offset, output_offset, flags
```

The current scalar unsigned-PD path avoids pointer-based transports, so the
buffer mechanism must be designed and measured before any DSP number is
claimed.

### Validation

- FP64 reference vs an independent high-precision calculation on a fixed
  grid;
- relative error by moneyness `S/K` and maturity;
- put-call parity within a stated tolerance;
- monotonicity in `S` and `sigma`;
- no NaN/Inf on the declared domain;
- checksum over the output grid.

## Limitations

- **Not implemented.** No source, IDL, vectors, logs or timings exist.
- The model assumes European exercise, no dividends, constant rate and
  volatility, and continuous hedging. It is not a trading or risk system.
- FP32/fixed-point conversion is not specified because no kernel exists. A
  Q15 version would need a separate CDF approximation and error study.
- No Greeks, American options, stochastic volatility or local volatility.
- No DSP/HVX claim. A vectorised batch would be a new result, not an
  extension of the FOC measurement.
- RPC transport is not free: an option batch is only useful if the buffer
  path is measured with the kernel.

## Acceptance criteria

- [ ] source + data layout;
- [ ] separate IDL methods and generated stubs;
- [ ] FP64 reference and deterministic grid;
- [ ] error/parity table;
- [ ] CPU and DSP timings with the same inputs;
- [ ] raw evidence and a checksum.
