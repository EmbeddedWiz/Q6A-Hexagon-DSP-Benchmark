#!/usr/bin/env bash
# Interleaved real-time measurement suite for the Q6A DSP benchmark.
# Raw per-sample CSV files are written under results/rt-<date>/raw/.
set -Eeuo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$PROJECT/results/rt-2026-11-14}"
SAMPLES="${RT_SAMPLES_PER_ROUND:-34000}"
ROUNDS="${RT_ROUNDS:-3}"
BATCH="${RT_BATCH:-16}"
WARMUP="${RT_WARMUP:-100}"
VARIANTS="${RT_VARIANTS:-cpu dsp step}"
RUNNER="$PROJECT/build/foc/foc_runner"
STRESSOR="${RT_CPU_STRESSOR:-$HOME/Projects/qcs6490-resnet50-inference-benchmark/stressor/cpu-stressor-affinity}"
MEMORY_LOAD="$PROJECT/build/rt/memory_load"

mkdir -p "$OUT/raw" "$OUT/logs" "$PROJECT/build/rt"
export DSP_LIBRARY_PATH="${Q6A_RUNNER_DIR:-$HOME/Projects/hexagon-v68-test}"
export ADSP_LIBRARY_PATH="$DSP_LIBRARY_PATH:/usr/lib/dsp/cdsp:/usr/lib/rfsa/adsp"

cc -O2 -pthread "$PROJECT/scripts/memory_load.c" -o "$MEMORY_LOAD"

{
    echo "date=$(date -Is)"
    echo "kernel=$(uname -a)"
    echo "cmdline=$(cat /proc/cmdline)"
    echo "governor=$(cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor)"
    echo "cur_freq_khz=$(cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq)"
    echo "cpu7_affinity=$(taskset -pc $$)"
    echo "nproc=$(nproc)"
    echo "samples_per_round=$SAMPLES"
    echo "rounds=$ROUNDS"
    echo "batch=$BATCH"
    echo "warmup=$WARMUP"
    echo "stressor=$STRESSOR"
} | tee "$OUT/environment.txt"

if [[ ! -x "$RUNNER" ]]; then
    echo "missing runner: $RUNNER" >&2
    exit 1
fi

run_runner() {
    taskset -c 7 "$RUNNER" "$@"
}

run_variant() {
    local condition="$1"
    local round="$2"
    local variant="$3"
    local csv="$OUT/raw/cond${condition}-r${round}-${variant}.csv"
    case "$variant" in
        cpu)
            run_runner rt-cpu --rt-samples "$SAMPLES" --rt-batch "$BATCH" \
                --rt-warmup "$WARMUP" --rt-round "$round" \
                --rt-condition "$condition" --rt-config cpu --rt-csv "$csv"
            ;;
        dsp)
            run_runner rt-dsp --rt-samples "$SAMPLES" --rt-batch "$BATCH" \
                --rt-warmup "$WARMUP" --rt-round "$round" \
                --rt-condition "$condition" --rt-config dsp --rt-csv "$csv"
            ;;
        step)
            run_runner rt-step --rt-samples "$SAMPLES" --rt-batch 1 \
                --rt-warmup "$WARMUP" --rt-round "$round" \
                --rt-condition "$condition" --rt-config dsp-rpc-step --rt-csv "$csv"
            ;;
        bench)
            run_runner rt-bench --rt-samples "$SAMPLES" --rt-batch "$BATCH" \
                --rt-warmup "$WARMUP" --rt-round "$round" \
                --rt-condition "$condition" --rt-config dsp-rpc-bench --rt-csv "$csv"
            ;;
        *)
            echo "unknown variant: $variant" >&2
            exit 2
            ;;
    esac
}

start_cpu_load() {
    if [[ ! -x "$STRESSOR" ]]; then
        echo "missing ResNet-50 stressor: $STRESSOR" >&2
        exit 3
    fi
    (
        while :; do
            "$STRESSOR" >/dev/null 2>&1 || break
        done
    ) &
    LOAD_PID=$!
    sleep 2
}

start_memory_load() {
    "$MEMORY_LOAD" >/dev/null 2>&1 &
    LOAD_PID=$!
    sleep 2
}

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

for round in $(seq 1 "$ROUNDS"); do
    for condition in 1 2 3; do
        case "$condition" in
            1) : ;;
            2) start_cpu_load ;;
            3) start_memory_load ;;
        esac
        for variant in $VARIANTS; do
            echo "round=$round condition=$condition variant=$variant" | tee -a "$OUT/logs/progress.log"
            run_variant "$condition" "$round" "$variant"
        done
        stop_load
    done
done

# DSP-on-CPU interference: CPU measurement while the DSP executes a busy loop.
for round in $(seq 1 "$ROUNDS"); do
    (
        while :; do
            taskset -c 7 "$RUNNER" bench --iterations 16384 --runs 1 --warmup 0 \
                >/dev/null 2>&1 || break
        done
    ) &
    LOAD_PID=$!
    sleep 2
    echo "round=$round condition=4 variant=cpu" | tee -a "$OUT/logs/progress.log"
    run_variant 4 "$round" cpu
    stop_load
done

if [[ "${RT_SKIP_TIMERS:-0}" != "1" ]]; then
    run_runner rt-timer-cpu --rt-samples 1000000 | tee "$OUT/timer-overhead-cpu.json"
    run_runner rt-timer-dsp --rt-samples 1000000 | tee "$OUT/timer-overhead-dsp.json"
fi

echo "RT SUITE DONE: $OUT"
