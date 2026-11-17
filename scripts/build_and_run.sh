#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Build host: address the VM by IP; the SSH key is bound to this explicit path.
REMOTE="${Q6A_BUILD_HOST:-builder@build-host}"
SSH_KEY="${Q6A_BUILD_KEY:-$HOME/.ssh/id_ed25519_build_host}"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10 -i "$SSH_KEY")
REMOTE_DIR="${Q6A_BUILD_DIR:-/home/builder/hexagon-v68-test}"
SRC="$PROJECT/dsp/fastrpc/hello_dsp_smoke.c"
LOCAL_BIN="$PROJECT/build/hexagon/hello_dsp_smoke.so"
DEPLOY_DIR="${Q6A_RUNNER_DIR:-$HOME/Projects/hexagon-v68-test}"
LOG_DIR="$PROJECT/build/logs"
LOG="$LOG_DIR/$(date +%Y%m%d-%H%M%S)-hexagon-smoke.log"

mkdir -p "$LOG_DIR" "$(dirname "$LOCAL_BIN")"
exec > >(tee -a "$LOG") 2>&1

echo "=== Q6A Hexagon build-and-run ==="
date -Is
echo "Project: $PROJECT"
echo "Build host: $REMOTE"
echo "DSP target: Dragon Q6A / CDSP domain 3"

# Do not build or run if the SSH connection is unavailable.
# Do not build or run if the SSH connection is unavailable.
ssh "${SSH_OPTS[@]}" "$REMOTE" 'hostname'

# Copy source to the VMware build host.
scp "${SSH_OPTS[@]}" "$SRC" "$REMOTE:$REMOTE_DIR/hello_dsp_smoke.c"

# Compile using the already verified V68 compiler.
ssh "${SSH_OPTS[@]}" "$REMOTE" '
    set -eu
    COMPILER=/home/builder/hexagon-sdk/6.4.0.2/tools/HEXAGON_Tools/19.0.04/Tools/bin/hexagon-clang
    cd /home/builder/hexagon-v68-test
    test -x "$COMPILER"
    "$COMPILER" -mv68 -O0 -shared -fpic \
        hello_dsp_smoke.c -o hello_dsp_smoke.so
    file hello_dsp_smoke.so
'

# Pull the freshly built binary; never run an old binary after build failure.
scp "${SSH_OPTS[@]}" "$REMOTE:$REMOTE_DIR/hello_dsp_smoke.so" "$LOCAL_BIN"

# Reuse the previously verified AArch64 runner and V68 skeleton.
test -x "$DEPLOY_DIR/run_main_on_hexagon"
test -f "$DEPLOY_DIR/librun_main_on_hexagon_skel.so"

cp "$LOCAL_BIN" "$DEPLOY_DIR/hello_dsp_smoke.so"

echo "=== Execute on real CDSP ==="
cd "$DEPLOY_DIR"
export DSP_LIBRARY_PATH="$DEPLOY_DIR"
export ADSP_LIBRARY_PATH="$DEPLOY_DIR:/usr/lib/dsp/cdsp:/usr/lib/rfsa/adsp"

./run_main_on_hexagon 3 hello_dsp_smoke.so stack_size=0x50000

echo "=== PASS: DSP returned zero ==="
echo "Binary: $LOCAL_BIN"
echo "Log: $LOG"
