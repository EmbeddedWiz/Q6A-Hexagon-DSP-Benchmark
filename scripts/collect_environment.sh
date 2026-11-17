#!/usr/bin/env bash
#
# Capture the runtime environment that produced the FOC results.
# Run on the Dragon Q6A. The build-host environment is captured separately.
set -Eeuo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$PROJECT/results/environment.txt}"
mkdir -p "$(dirname "$OUT")"

{
    echo "=== runtime environment ==="
    echo "date: $(date -Is)"
    echo "host: $(hostname)"
    uname -a
    echo
    echo "--- os-release ---"
    cat /etc/os-release
    echo
    echo "--- cpu topology ---"
    lscpu
    echo
    echo "--- memory and storage ---"
    free -h
    df -h "$PROJECT"
    echo
    echo "--- remoteproc ---"
    for node in /sys/class/remoteproc/remoteproc*; do
        echo "$node"
        echo "  state:     $(cat "$node/state" 2>/dev/null || echo unavailable)"
        echo "  firmware:  $(cat "$node/firmware" 2>/dev/null || echo unavailable)"
    done
    echo
    echo "--- fastrpc devices ---"
    ls -l /dev/fastrpc-* 2>/dev/null || echo "no /dev/fastrpc-* nodes"
    echo
    echo "--- relevant packages ---"
    dpkg-query -W -f='${Package} ${Version}\n' \
        radxa-firmware-qcs6490 2>/dev/null || true
    dpkg-query -W -f='${Package} ${Version}\n' 'fastrpc*' 2>/dev/null || true
    dpkg-query -W -f='${Package} ${Version}\n' 'libcdsprpc*' 2>/dev/null || true
    echo
    echo "--- cpu7 frequency policy ---"
    for file in scaling_governor scaling_driver scaling_cur_freq scaling_min_freq \
                scaling_max_freq; do
        printf '%s: ' "$file"
        cat "/sys/devices/system/cpu/cpu7/cpufreq/$file" 2>/dev/null || echo unavailable
    done
    echo
    echo "--- built artifacts ---"
    for artifact in "$PROJECT/build/foc/foc_runner" \
                    "$PROJECT/build/foc/generated/libfoc_bench_skel.so"; do
        if [[ -f "$artifact" ]]; then
            sha256sum "$artifact"
            file "$artifact"
        fi
    done
} > "$OUT"

echo "wrote $OUT"
