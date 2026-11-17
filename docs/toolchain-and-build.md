# Toolchain and build

## Verified versions

| Component | Version / path |
|---|---|
| Board | Radxa Dragon Q6A, Qualcomm QCS6490 |
| Board OS | Ubuntu 24.04.4 LTS, kernel `6.18.2-3-qcom` (AArch64) |
| CDSP | Hexagon v68, domain 3, unsigned User PD |
| Firmware package | `radxa-firmware-qcs6490 0.2.42` |
| FastRPC user space | `fastrpc 1.0.7-3`, `libcdsprpc1 1.0.7-3` |
| Hexagon SDK | `/home/builder/hexagon-sdk/6.4.0.2` |
| Hexagon Tools | `19.0.04` |
| DSP compiler | `hexagon-clang` / `hexagon-clang++` 19.0.04 |
| IDL compiler | `qaic` (SDK FastRPC tools) |
| Build host | x86_64 VMware Linux, reached by SSH |
| Host compiler | `c++` (Ubuntu 13.3.0) for AArch64 |
| Legacy runner | `~/Projects/hexagon-v68-test/run_main_on_hexagon` |

The CDSP firmware string recorded during bring-up was
`CDSP.HT.2.5.c4-00004-KODIAK-1`. `sysfs` reports `firmware: unknown` for the
remoteproc node, so the string is historical evidence, not a live reading.

## Build-host layout

The scripts do not assume the board's SSH alias. They use an explicit IP/key by
default and allow overrides:

```bash
Q6A_BUILD_HOST=builder@build-host
Q6A_BUILD_KEY=$HOME/.ssh/id_ed25519_build_host
Q6A_BUILD_DIR=/home/builder/hexagon-v68-test/q6a-foc
Q6A_SDK=/home/builder/hexagon-sdk/6.4.0.2
Q6A_RUNNER_DIR=$HOME/Projects/hexagon-v68-test
```

`scripts/build_foc_bench.sh` copies the IDL, DSP implementation and shared FOC
sources to the build host, runs QAIC, compiles the skeleton, pulls the generated
header/stub/skel back to the board, compiles the host stub and runner, and
deploys the skel next to the runner sandbox.

## QAIC generation

```bash
qaic -m dll -I "$SDK/incs" -o generated foc_bench.idl
```

Generated files:

- `foc_bench.h` — AP-side declarations and QAIC copy macros.
- `foc_bench_stub.c` — AP-side marshalling code.
- `foc_bench_skel.c` — DSP-side dispatch skeleton.

The generated header is authoritative. In this SDK, IDL `long` maps to plain
`int` in the C prototypes, and `uint64_t` stays `uint64_t`. The DSP
implementation must use exactly those spellings. Using `int32_t` produced
`conflicting types` errors because the Hexagon headers do not make `int32_t`
the same typedef token as `int` in that context. This is a spelling/ABI
requirement, not a size difference (`int` is 32-bit here).

## DSP compile and link

The proven command set is:

```bash
SDK=/home/builder/hexagon-sdk/6.4.0.2
TC=$SDK/tools/HEXAGON_Tools/19.0.04/Tools/bin
QURT=$SDK/rtos/qurt/computev68/include/qurt

CFLAGS=(-mv68 -G0 -O2 -Wall -Wextra -fpic
        -Igenerated -I"$SDK/incs" -I"$SDK/incs/stddef" -I"$QURT")

"$TC/hexagon-clang"   "${CFLAGS[@]}" -c generated/foc_bench_skel.c      -o skel.o
"$TC/hexagon-clang++" "${CFLAGS[@]}" -c dsp/fastrpc/foc_bench_skel_impl.cpp -o impl.o
"$TC/hexagon-clang++" "${CFLAGS[@]}" -c workloads/motor_control/foc_fixed.cpp -o foc_fixed.o
"$TC/hexagon-clang++" "${CFLAGS[@]}" -c workloads/motor_control/motor_control_fixed.cpp -o motor_control_fixed.o

"$TC/hexagon-clang++" -mv68 -O2 -shared -fpic \
    skel.o impl.o foc_fixed.o motor_control_fixed.o \
    -o libfoc_bench_skel.so
```

### Critical pitfall: `-fpic`, not `-fPIC`

The Hexagon G0 `libc.a` shipped with SDK 6.4.0.2 is not built with `-fPIC`.
Compiling objects with `-fPIC` and linking `-shared` produces:

```text
Fatal: Current link configuration does not support relocation type
`R_HEX_32_6_X' for symbol `__eh_nodes' ... recompile with -fPIC
```

The error text is misleading: recompiling with `-fPIC` is exactly what fails.
The working recipe is lowercase `-fpic` on every object and on the link, with
no explicit `-lc`:

```bash
hexagon-clang++ -mv68 -O2 -shared -fpic *.o -o libfoc_bench_skel.so
```

The earlier scalar smoke module used the same lowercase flag. This is recorded
in `docs/decisions-log.md` and `docs/engineering-findings.md`.

## Host stub and runner

On the board:

```bash
cc -std=c11 -O2 -Wall -Wextra -Wno-type-limits -Wno-strict-aliasing \
   -I/usr/include/fastrpc -Ibuild/foc/generated \
   -c build/foc/generated/foc_bench_stub.c -o build/foc/foc_bench_stub.o

c++ -std=c++20 -O2 -fno-tree-vectorize -Wall -Wextra \
    -Wconversion -Wsign-conversion \
    -I/usr/include/fastrpc -Ibuild/foc/generated \
    -Iworkloads/motor_control -Ivalidation/foc \
    host/foc_runner.cpp \
    validation/foc/foc_reference.cpp validation/foc/foc_vectors.cpp \
    workloads/motor_control/foc_fixed.cpp \
    workloads/motor_control/motor_control_fixed.cpp \
    build/foc/foc_bench_stub.o -lcdsprpc \
    -o build/foc/foc_runner
```

`-fno-tree-vectorize` keeps the CPU fixed-point baseline scalar, matching the
DSP build. The QAIC stub's strict-aliasing warnings are suppressed because they
come from the generated copy macros, not from project code.

## Deployment and runtime environment

```bash
export DSP_LIBRARY_PATH=$HOME/Projects/hexagon-v68-test
export ADSP_LIBRARY_PATH=$DSP_LIBRARY_PATH:/usr/lib/dsp/cdsp:/usr/lib/rfsa/adsp
cp libfoc_bench_skel.so "$DSP_LIBRARY_PATH/"
./build/foc/foc_runner echo
```

The host runner opens the module with the URI produced by
`std::string(foc_bench_URI) + CDSP_DOMAIN`:

```text
file:///libfoc_bench_skel.so?foc_bench_skel_handle_invoke&_modver=1.0&_dom=cdsp
```

Before opening, it enables the unsigned module domain:

```cpp
remote_rpc_control_unsigned_module unsigned_module{};
unsigned_module.domain = CDSP_DOMAIN_ID;  // 3
unsigned_module.enable = 1;
remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE,
                       &unsigned_module, sizeof(unsigned_module));
```

The stock `run_main_on_hexagon` and its skeleton are not overwritten or used by
the FOC suite. They remain available for the legacy scalar/HVX smoke scripts.

## Build logs

The build writes a timestamped log in the working tree; transient build logs
are not part of the published result tree. The final run's build-artifact
hashes are in `results/final/artifacts.sha256`:

```text
bc6d5f1fc23edc670b5a58c96a4ad2189966f5748862d12a79ef17141ebf1114  libfoc_bench_skel.so
7e39710191899cbef48d692ab7c7058339b332be3a8c19a92603bd5a7a21979d  foc_runner
d4eaf048f5352aca5c3b09a53580b4234a12c70b5ff10d56884bf6063dc6f28d  foc_bench.h
```

## Reproducibility checklist

1. Confirm the board kernel and package versions in
   `results/final/environment.txt`.
2. Confirm the build host SDK path and compiler version.
3. Run `./scripts/build_foc_bench.sh`; it must end with `=== BUILD PASS ===`.
4. Run `./scripts/run_foc_suite.sh --out results/final`.
5. Require `echo` PASS, validation PASS, every DSP `status == 0`, and a single
   checksum across DSP and CPU.
6. Compare the new artifact hashes; a different compiler or code layout can
   change cycles by a few percent, so treat the numbers as build-specific.
