#!/usr/bin/env bash
#
# Build the project-owned FastRPC FOC contract and its host runner.
#
# The Hexagon skeleton is generated and compiled on the x86 build host; the
# host stub and runner are compiled on the Dragon Q6A itself. Only the resulting
# skel is deployed next to the verified run_main_on_hexagon helper.
set -Eeuo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_HOST="${Q6A_BUILD_HOST:-builder@build-host}"
SSH_KEY="${Q6A_BUILD_KEY:-$HOME/.ssh/id_ed25519_build_host}"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10 -i "$SSH_KEY")
REMOTE_DIR="${Q6A_BUILD_DIR:-/home/builder/hexagon-v68-test/q6a-foc}"
SDK="${Q6A_SDK:-/home/builder/hexagon-sdk/6.4.0.2}"
RUNNER_DIR="${Q6A_RUNNER_DIR:-$HOME/Projects/hexagon-v68-test}"

BUILD_DIR="$PROJECT/build/foc"
GENERATED="$BUILD_DIR/generated"
GENERATED_HVX="$BUILD_DIR/generated-hvx"
LOG_DIR="$PROJECT/build/logs"
LOG="$LOG_DIR/$(date +%Y%m%d-%H%M%S)-foc-build.log"

mkdir -p "$BUILD_DIR" "$GENERATED" "$GENERATED_HVX" "$LOG_DIR"
exec > >(tee -a "$LOG") 2>&1

echo "=== Q6A FOC benchmark build ==="
date -Is
echo "Project:    $PROJECT"
echo "Build host: $BUILD_HOST"
echo "SDK:        $SDK"
echo "Runner dir: $RUNNER_DIR"

ssh "${SSH_OPTS[@]}" "$BUILD_HOST" \
    "mkdir -p '$REMOTE_DIR/generated' '$REMOTE_DIR/generated-hvx'"

scp "${SSH_OPTS[@]}" \
    "$PROJECT/dsp/idl/foc_bench.idl" \
    "$PROJECT/dsp/fastrpc/foc_bench_skel_impl.cpp" \
    "$PROJECT/workloads/motor_control/foc_fixed.h" \
    "$PROJECT/workloads/motor_control/foc_fixed.cpp" \
    "$PROJECT/workloads/motor_control/motor_control.h" \
    "$PROJECT/workloads/motor_control/motor_control_fixed.h" \
    "$PROJECT/workloads/motor_control/motor_control_fixed.cpp" \
    "$BUILD_HOST:$REMOTE_DIR/"

scp "${SSH_OPTS[@]}" \
    "$PROJECT/dsp/idl/hvx_bench.idl" \
    "$PROJECT/dsp/fastrpc/hvx_bench_impl.cpp" \
    "$BUILD_HOST:$REMOTE_DIR/"

ssh "${SSH_OPTS[@]}" "$BUILD_HOST" bash -s -- "$REMOTE_DIR" "$SDK" <<'REMOTE_BUILD'
set -Eeuo pipefail
remote_dir="$1"
sdk="$2"
qaic="$sdk/ipc/fastrpc/qaic/bin/qaic"
tools="$sdk/tools/HEXAGON_Tools/19.0.04/Tools/bin"
qurt="$sdk/rtos/qurt/computev68/include/qurt"
generated="$remote_dir/generated"
generated_hvx="$remote_dir/generated-hvx"

test -x "$qaic"
test -x "$tools/hexagon-clang"

"$qaic" -m dll -I "$sdk/incs" -o "$generated" "$remote_dir/foc_bench.idl"
"$qaic" -m dll -I "$sdk/incs" -o "$generated_hvx" "$remote_dir/hvx_bench.idl"

# Lowercase -fpic is required: the G0 Hexagon libc shipped with SDK 6.4.0.2 is
# not built with -fPIC, and mixing the two fails at link time.
cflags=(-mv68 -G0 -O2 -Wall -Wextra -fpic
        -I"$generated" -I"$sdk/incs" -I"$sdk/incs/stddef")
hvx_cflags=(-mv68 -G0 -O2 -Wall -Wextra -fpic
            -mhvx -mhvx-length=128B -fno-vectorize -fno-slp-vectorize
            -I"$generated_hvx" -I"$sdk/incs" -I"$sdk/incs/stddef")

cd "$remote_dir"
rm -f foc_bench_skel.o foc_bench_skel_impl.o foc_fixed.o motor_control_fixed.o
rm -f hvx_bench_skel.o hvx_bench_impl.o

"$tools/hexagon-clang" "${cflags[@]}" -c "$generated/foc_bench_skel.c" -o foc_bench_skel.o
"$tools/hexagon-clang++" "${cflags[@]}" -I"$qurt" -c foc_bench_skel_impl.cpp -o foc_bench_skel_impl.o
"$tools/hexagon-clang++" "${cflags[@]}" -I"$qurt" -c foc_fixed.cpp -o foc_fixed.o
"$tools/hexagon-clang++" "${cflags[@]}" -I"$qurt" -c motor_control_fixed.cpp -o motor_control_fixed.o
"$tools/hexagon-clang++" -mv68 -O2 -shared -fpic \
    foc_bench_skel.o foc_bench_skel_impl.o foc_fixed.o motor_control_fixed.o \
    -o libfoc_bench_skel.so

"$tools/hexagon-clang" "${cflags[@]}" -c "$generated_hvx/hvx_bench_skel.c" -o hvx_bench_skel.o
"$tools/hexagon-clang++" "${hvx_cflags[@]}" -I"$qurt" -c hvx_bench_impl.cpp -o hvx_bench_impl.o
"$tools/hexagon-clang++" -mv68 -mhvx -mhvx-length=128B -O2 -shared -fpic \
    hvx_bench_skel.o hvx_bench_impl.o -o libhvx_bench_skel.so

file libfoc_bench_skel.so libhvx_bench_skel.so
REMOTE_BUILD

scp "${SSH_OPTS[@]}" \
    "$BUILD_HOST:$REMOTE_DIR/generated/foc_bench.h" \
    "$BUILD_HOST:$REMOTE_DIR/generated/foc_bench_stub.c" \
    "$BUILD_HOST:$REMOTE_DIR/libfoc_bench_skel.so" \
    "$GENERATED/"

scp "${SSH_OPTS[@]}" \
    "$BUILD_HOST:$REMOTE_DIR/generated-hvx/hvx_bench.h" \
    "$BUILD_HOST:$REMOTE_DIR/generated-hvx/hvx_bench_stub.c" \
    "$BUILD_HOST:$REMOTE_DIR/libhvx_bench_skel.so" \
    "$GENERATED_HVX/"

cc -std=c11 -O2 -Wall -Wextra -Wno-type-limits -Wno-strict-aliasing \
    -I/usr/include/fastrpc -I"$GENERATED" \
    -c "$GENERATED/foc_bench_stub.c" -o "$BUILD_DIR/foc_bench_stub.o"

cc -std=c11 -O2 -Wall -Wextra -Wno-type-limits -Wno-strict-aliasing \
    -I/usr/include/fastrpc -I"$GENERATED_HVX" \
    -c "$GENERATED_HVX/hvx_bench_stub.c" -o "$BUILD_DIR/hvx_bench_stub.o"

c++ -std=c++20 -O2 -fno-tree-vectorize -Wall -Wextra -Wconversion -Wsign-conversion \
    -I/usr/include/fastrpc -I"$GENERATED" -I"$GENERATED_HVX" \
    -I"$PROJECT/workloads/motor_control" -I"$PROJECT/validation/foc" \
    "$PROJECT/host/foc_runner.cpp" \
    "$PROJECT/validation/foc/foc_reference.cpp" \
    "$PROJECT/validation/foc/foc_vectors.cpp" \
    "$PROJECT/workloads/motor_control/foc_fixed.cpp" \
    "$PROJECT/workloads/motor_control/motor_control_fixed.cpp" \
    "$BUILD_DIR/foc_bench_stub.o" \
    "$BUILD_DIR/hvx_bench_stub.o" -lcdsprpc \
    -o "$BUILD_DIR/foc_runner"

test -x "$RUNNER_DIR/run_main_on_hexagon"
cp "$GENERATED/libfoc_bench_skel.so" "$RUNNER_DIR/libfoc_bench_skel.so"
cp "$GENERATED_HVX/libhvx_bench_skel.so" "$RUNNER_DIR/libhvx_bench_skel.so"

echo "=== BUILD PASS ==="
echo "Runner: $BUILD_DIR/foc_runner"
echo "FOC skel:   $RUNNER_DIR/libfoc_bench_skel.so"
echo "HVX skel:   $RUNNER_DIR/libhvx_bench_skel.so"
echo "Log:    $LOG"
