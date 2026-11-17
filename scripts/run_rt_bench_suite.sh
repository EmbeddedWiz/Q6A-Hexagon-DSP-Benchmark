#!/usr/bin/env bash
# Bench-RPC raw collection in 10.5k-sample chunks.
# Chunking keeps the CDSP PD stable; each chunk is a separate host process.
set -Eeuo pipefail
PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$PROJECT/results/rt-2026-11-14}"
RUNNER="$PROJECT/build/foc/foc_runner"
STRESSOR="${RT_CPU_STRESSOR:-$HOME/Projects/qcs6490-resnet50-inference-benchmark/stressor/cpu-stressor-affinity}"
MEMORY_LOAD="$PROJECT/build/rt/memory_load"
mkdir -p "$OUT/raw" "$OUT/logs"
export DSP_LIBRARY_PATH="${Q6A_RUNNER_DIR:-$HOME/Projects/hexagon-v68-test}"
export ADSP_LIBRARY_PATH="$DSP_LIBRARY_PATH:/usr/lib/dsp/cdsp:/usr/lib/rfsa/adsp"

stop_load() {
    if [[ -n "${LOAD_PID:-}" ]]; then
        kill "$LOAD_PID" 2>/dev/null || true
        wait "$LOAD_PID" 2>/dev/null || true
        LOAD_PID=""
    fi
    pkill -f cpu-stressor-affinity 2>/dev/null || true
    pkill -f "$MEMORY_LOAD" 2>/dev/null || true
}
trap 'stop_load' EXIT INT TERM

for condition in 1 2 3; do
    case "$condition" in
        1) : ;;
        2) ( while :; do "$STRESSOR" >/dev/null 2>&1 || break; done ) & LOAD_PID=$!; sleep 2 ;;
        3) "$MEMORY_LOAD" >/dev/null 2>&1 & LOAD_PID=$!; sleep 2 ;;
    esac
    for chunk in $(seq 1 10); do
        csv="$OUT/raw/bench-cond${condition}-chunk${chunk}.csv"
        echo "bench condition=$condition chunk=$chunk" | tee -a "$OUT/logs/progress-bench.log"
        taskset -c 7 "$RUNNER" rt-bench --rt-samples 10500 --rt-batch 16 \
            --rt-warmup 100 --rt-condition "$condition" --rt-round "$chunk" \
            --rt-config dsp-rpc-bench --rt-csv "$csv" >/dev/null
        state=$(cat /sys/class/remoteproc/remoteproc1/state)
        if [[ "$state" != "attached" ]]; then
            echo "CDSP state=$state after condition=$condition chunk=$chunk" >&2
            exit 4
        fi
    done
    stop_load
done

# DSP timer-read cost as 10 x 100k calls (1M total), avoiding one long watchdog window.
: > "$OUT/timer-overhead-dsp.jsonl"
for i in $(seq 1 10); do
    taskset -c 7 "$RUNNER" rt-timer-dsp --rt-samples 100000 >> "$OUT/timer-overhead-dsp.jsonl"
done
echo "BENCH SUITE DONE: $OUT"
