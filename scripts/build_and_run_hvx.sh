#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Build host: address the VM by IP; the SSH key is bound to this explicit path.
REMOTE="${Q6A_BUILD_HOST:-builder@build-host}"
SSH_KEY="${Q6A_BUILD_KEY:-$HOME/.ssh/id_ed25519_build_host}"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10 -i "$SSH_KEY")
REMOTE_DIR="${Q6A_BUILD_DIR:-/home/builder/hexagon-v68-test}"
SDK="/home/builder/hexagon-sdk/6.4.0.2"
TOOLS="$SDK/tools/HEXAGON_Tools/19.0.04/Tools/bin"
SRC="$PROJECT/dsp/fastrpc/hvx_vector_add.c"
BIN="$PROJECT/build/hexagon/hvx_vector_add.so"
DEPLOY="${Q6A_RUNNER_DIR:-$HOME/Projects/hexagon-v68-test}"
LOG="$PROJECT/build/logs/$(date +%Y%m%d-%H%M%S)-hvx-vector-add.log"

mkdir -p "$PROJECT/build/logs" "$(dirname "$BIN")"
exec > >(tee -a "$LOG") 2>&1

echo "=== Dragon Q6A: HVX vector-add test ==="
date -Is
echo "Build host: $REMOTE"
echo "Target: Dragon Q6A / CDSP domain 3"
echo "Operation: 64 signed 16-bit lanes, 128-byte HVX vector"

ssh "${SSH_OPTS[@]}" "$REMOTE" 'hostname'
scp "${SSH_OPTS[@]}" "$SRC" "$REMOTE:$REMOTE_DIR/hvx_vector_add.c"

ssh "${SSH_OPTS[@]}" "$REMOTE" 'bash -s' <<'REMOTE_BUILD'
set -eu
SDK=/home/builder/hexagon-sdk/6.4.0.2
TOOLS="$SDK/tools/HEXAGON_Tools/19.0.04/Tools/bin"
cd /home/builder/hexagon-v68-test

"$TOOLS/hexagon-clang" \
    -mv68 -mhvx -mhvx-length=128B \
    -O2 -fno-strict-aliasing -shared -fpic \
    -I"$SDK/rtos/qurt/computev68/include/qurt" \
    hvx_vector_add.c -o hvx_vector_add.so

file hvx_vector_add.so
REMOTE_BUILD

scp "${SSH_OPTS[@]}" "$REMOTE:$REMOTE_DIR/hvx_vector_add.so" "$BIN"
test -x "$DEPLOY/run_main_on_hexagon"
test -f "$DEPLOY/librun_main_on_hexagon_skel.so"
cp "$BIN" "$DEPLOY/hvx_vector_add.so"

cd "$DEPLOY"
export DSP_LIBRARY_PATH="$DEPLOY"
export ADSP_LIBRARY_PATH="$DEPLOY:/usr/lib/dsp/cdsp:/usr/lib/rfsa/adsp"

echo "=== Execute on real CDSP ==="
set +e
./run_main_on_hexagon 3 hvx_vector_add.so stack_size=0x50000
RUN_RC=$?
set -e
echo "Runner exit status: $RUN_RC"

if [ "$RUN_RC" -ne 0 ]; then
    echo "HVX TEST FAILED: inspect the runner output above."
    exit "$RUN_RC"
fi

echo "=== PASS: HVX vector addition returned zero ==="
echo "Log: $LOG"
