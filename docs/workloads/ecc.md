# ECC — reference design, not implemented

## Status

**Not implemented in this repository.** There is no curve source, field
arithmetic, IDL method, vector set or timing result. This page records the
theory and the exact reference contract. It is not a security or performance
claim.

```bash
git log --all --name-only -- '*ecc*'
# only docs/workloads/ecc.md
```

## Theory (limited)

The reference curve is secp256r1 (P-256). Its short-Weierstrass equation is:

```text
y^2 = x^3 + a*x + b  (mod p),  a = -3
p = 2^256 - 2^224 + 2^192 + 2^96 - 1
```

A point is `(x, y)` with `0 <= x,y < p`; the point at infinity is the identity.
Point addition and doubling use the chord/tangent rules. For performance,
implementations use Jacobian coordinates `(X:Y:Z)` with `x = X/Z^2`,
`y = Y/Z^3`, so one inversion is deferred until the end.

For a scalar `k` and point `P`, scalar multiplication computes `kP` with a
double-and-add or Montgomery-ladder sequence. The result is affine
`(x, y)` or infinity.

## Reference implementation

The reference is CPU-only, fixed-width and not a production crypto library.

### Field layer

```text
p256_fe: 4 x uint64_t limbs, little-endian
add/sub: carry/borrow chains, conditional p reduction
mul:     schoolbook or CIOS Montgomery multiplication
inverse: Fermat inversion a^(p-2) mod p
```

Montgomery multiplication needs `R = 2^256`, `n0 = -p^-1 mod 2^64`, and a
final conditional subtraction. All intermediate products use `unsigned
__int128`; no floating point is allowed.

### Point layer

- Jacobian doubling and mixed addition;
- explicit handling of `Z == 0` (infinity);
- equality and on-curve checks;
- scalar multiplication with a fixed window or a Montgomery ladder;
- convert to affine with one field inversion at the end.

### Validation

- known-answer vectors for `kG` (including `k = 1`, `n-1`, `n`);
- point-on-curve checks for every generated point;
- edge cases: infinity, equal points, inverse points, scalar zero;
- exact CPU↔DSP parity for a fixed-width implementation;
- checksum over a deterministic scalar/point batch.

### Batch contract for a future IDL

A separate method would take:

```text
curve_id, operation, batch_size, scalar/point buffers, flags
```

Single scalar multiplication is mostly serial. Batch verification or
multi-scalar multiplication is the realistic HVX target.

## Limitations

- **Not implemented.** No source, vectors, logs or timings exist.
- This is not a hardened cryptographic implementation. Branch and memory
  access patterns may depend on the scalar; constant-time behaviour is a
  separate requirement and is not claimed.
- No side-channel, fault-injection or key-recovery analysis.
- No TLS/signature-protocol integration; no key generation or storage.
- A toy curve would not justify P-256 performance claims. The reference must
  use the named curve and public vectors.
- HVX suitability is unproven for a single scalar multiplication; it must be
  measured on a defined batch.

## Acceptance criteria

- [ ] named curve and field representation;
- [ ] field/point/scalar source;
- [ ] public known-answer vectors and independent reference;
- [ ] exact parity or a declared error bound;
- [ ] raw single and batch timings;
- [ ] constant-time/side-channel disclaimer;
- [ ] a result section that does not borrow FOC numbers.
