# Post-final source layout check — 2026-11-16

This run used the tree at then-commit `488f2d1`, which contains the later
real-time sampler code (157 added lines in `dsp/fastrpc/foc_bench_skel_impl.cpp`
and 31 in `dsp/idl/foc_bench.idl`) that is not present in the final build
`245dc6a`. It produced **383.600 cycles/step** and a **5.51×** DSP/CPU ratio,
with the same checksum and validation as the final run.

It is **not** the repeatability run and does not replace `results/final/`.
The same-source repeat is `results/repeatability/`. This directory documents
that later non-algorithm code can shift the hot loop by +1.75% cycles through
code layout; it is not caused by a change to the FOC algorithm.
