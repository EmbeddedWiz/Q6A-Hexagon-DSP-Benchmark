#!/usr/bin/env bash
#
# Run the complete FOC suite: build, transport check, validation, DSP benchmark
# and the pinned CPU baseline. All raw JSON and CSV artifacts are kept.
#
# Usage:
#   scripts/run_foc_suite.sh [--no-build] [--out DIR]
#
# The default output directory is results/runs/<UTC timestamp>.
set -Eeuo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD=1
OUT=""
THERMAL_SOAK=0

# The final suite is defined to start from a clean checkout. Results written by
# the run will of course change the tree; the check is made before any output.
if [[ -n "$(git -C "$PROJECT" status --porcelain)" ]]; then
    echo "Working tree is not clean. Commit or stash changes before the suite." >&2
    git -C "$PROJECT" status --short >&2
    exit 1
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)
            BUILD=0
            shift
            ;;
        --thermal-soak)
            THERMAL_SOAK=1
            shift
            ;;
        --out)
            [[ $# -ge 2 ]] || { echo "--out needs a directory" >&2; exit 2; }
            OUT="$2"
            shift 2
            ;;
        *)
            OUT="$1"
            shift
            ;;
    esac
done

if [[ -z "$OUT" ]]; then
    OUT="$PROJECT/results/runs/$(date -u +%Y%m%dT%H%M%SZ)"
fi
mkdir -p "$OUT"
git -C "$PROJECT" rev-parse HEAD > "$OUT/git-commit.txt"
git -C "$PROJECT" status --porcelain > "$OUT/git-status-before.txt"

RUNNER="$PROJECT/build/foc/foc_runner"
if [[ $BUILD -eq 1 ]]; then
    "$PROJECT/scripts/build_foc_bench.sh"
fi
test -x "$RUNNER"

export DSP_LIBRARY_PATH="${Q6A_RUNNER_DIR:-$HOME/Projects/hexagon-v68-test}"
export ADSP_LIBRARY_PATH="$DSP_LIBRARY_PATH:/usr/lib/dsp/cdsp:/usr/lib/rfsa/adsp"

STATE_FILE=/sys/class/remoteproc/remoteproc1/state
if [[ -r "$STATE_FILE" && "$(cat "$STATE_FILE")" != "attached" ]]; then
    echo "CDSP is not attached (state=$(cat "$STATE_FILE"))." >&2
    echo "Reboot the board and re-run; do not trust a dead CDSP." >&2
    exit 1
fi

"$PROJECT/scripts/collect_environment.sh" "$OUT/environment.txt" >/dev/null
python3 "$PROJECT/scripts/check_repo.py" "$PROJECT" | tee "$OUT/check-repo.log"

{
    echo "=== Q6A FOC suite ==="
    date -Is
    echo "project: $PROJECT"
    echo "output:  $OUT"
    echo "runner:  $RUNNER"
    echo "dsp library path: $DSP_LIBRARY_PATH"
    echo "git commit: $(cat "$OUT/git-commit.txt")"
    echo "git status before run: clean"
} | tee "$OUT/suite.log"

# Thermal state before the run (highest zone reading in millidegrees).
thermal() {
    local label="$1"
    local file="$2"
    {
        echo "# $label"
        date -Is
        for zone in /sys/class/thermal/thermal_zone*; do
            [[ -r "$zone/temp" ]] || continue
            printf '%s %s %s\n' "$(basename "$zone")" \
                "$(cat "$zone/type" 2>/dev/null || echo unknown)" \
                "$(cat "$zone/temp")"
        done
    } > "$file"
}
thermal before "$OUT/thermal-before.txt"

# Continuous thermal trace for the whole suite: every thermal zone plus the
# CPU 7 frequency. This makes a temperature-related slowdown visible instead of
# relying only on the before/after snapshots. The sampler is stopped before the
# results are hashed.
THERMAL_STOP="$OUT/.stop-thermal-trace"
rm -f "$THERMAL_STOP"
thermal_trace() {
    printf 'timestamp,max_temp_c,cpu7_temp_c,nsp_temp_c,cpu7_freq_khz\n' \
        > "$OUT/thermal-trace.csv"
    while [[ ! -f "$THERMAL_STOP" ]]; do
        local max=0 z t nsp=0 cpu7=0 a=0 b=0
        for z in /sys/class/thermal/thermal_zone*; do
            [[ -r "$z/temp" ]] || continue
            t="$(cat "$z/temp")"
            (( t > max )) && max="$t"
            case "$(cat "$z/type" 2>/dev/null)" in
                cpu7-thermal) cpu7="$t" ;;
                nspss0-thermal) a="$t" ;;
                nspss1-thermal) b="$t" ;;
            esac
        done
        (( a > b )) && nsp="$a" || nsp="$b"
        printf '%s,%.1f,%.1f,%.1f,%s\n' \
            "$(date -Is)" \
            "$(awk -v v="$max" 'BEGIN{print v/1000}')" \
            "$(awk -v v="$cpu7" 'BEGIN{print v/1000}')" \
            "$(awk -v v="$nsp" 'BEGIN{print v/1000}')" \
            "$(cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq 2>/dev/null || echo 0)" \
            >> "$OUT/thermal-trace.csv"
        sleep 1
    done
}
thermal_trace &
THERMAL_PID=$!
stop_thermal_trace() {
    touch "$THERMAL_STOP"
    wait "$THERMAL_PID" 2>/dev/null || true
    rm -f "$THERMAL_STOP"
}
trap stop_thermal_trace EXIT

# 1. Transport check: every 64-bit response must be the bitwise complement of
#    its input. This proves routing of both halves of a 64-bit scalar.
"$RUNNER" echo | tee "$OUT/echo.log"

# 2. Numerical validation: 20,510 steps spanning edge cases, a deterministic
#    random sequence and every 14-bit angle. CPU fixed-point is the bit-exact
#    oracle; the independent FP32 model quantifies quantization error. The
#    main validation is P-only, matching the benchmark configuration.
"$RUNNER" validate --csv "$OUT/validation.csv" \
    | tee "$OUT/validation.log"

# 2b. Dedicated PI clamp test: high ki, edge cases only. With ki=32767 the
#     eight-step hold drives the integrator into the clamp and the reverse
#     sequence exercises recovery. This is separate from the 20,510-step
#     transform/quantization suite so the main statistics stay comparable.
"$RUNNER" validate --ki-d 32767 --ki-q 32767 --no-sweep --random 0 \
    --csv "$OUT/validation-pi.csv" | tee "$OUT/validation-pi.log"

# 3. DSP benchmark: three independent processes, five timed repetitions each.
{
    for run in 1 2 3; do
        "$RUNNER" bench
    done
} | tee "$OUT/bench-dsp.log"

# 4. CPU fixed-point baseline, pinned to the 2.7 GHz prime core. The runner
#    performs a long warm-up so schedutil reaches a steady clock.
if command -v taskset >/dev/null 2>&1; then
    {
        for run in 1 2 3; do
            taskset -c 7 "$RUNNER" cpu
        done
    } | tee "$OUT/bench-cpu.log"
else
    {
        for run in 1 2 3; do
            "$RUNNER" cpu
        done
    } | tee "$OUT/bench-cpu.log"
fi

# 5. HVX vector-add microbenchmark through the separate project-owned FastRPC
#    skeleton. Scalar and HVX use the same 64-lane data and 5000 iterations.
#    The result is a microbenchmark only and is not projected onto FOC.
{
    for run in 1 2 3; do
        "$RUNNER" hvx
    done
} | tee "$OUT/hvx.log"

# 6. PI stress: saturation, recovery, all reset paths and a 100,000-step
#    nonzero-ki run. The per-step CSV preserves the raw sequence.
"$RUNNER" pi --pi-csv "$OUT/pi-long.csv" | tee "$OUT/pi.log"

# 7. Optional sustained thermal soak with cold/hot probes and a continuous
#    temperature/frequency trace. Disabled by default because it adds several
#    minutes; enable with --thermal-soak.
if [[ $THERMAL_SOAK -eq 1 ]]; then
    "$PROJECT/scripts/thermal_soak.sh" --out "$OUT/thermal-soak" --allow-dirty
fi

thermal after "$OUT/thermal-after.txt"
stop_thermal_trace

gzip -9 -f "$OUT/validation.csv" "$OUT/validation-pi.csv" "$OUT/pi-long.csv"

if python3 -c 'import sys' >/dev/null 2>&1; then
    python3 "$PROJECT/scripts/summarize_foc.py" "$OUT"
fi

# Result-file hashes are relative to the run directory so that
# `cd "$OUT" && sha256sum -c SHA256SUMS` works. Build artifacts are recorded
# separately with project-relative paths; they are not committed.
(
    cd "$OUT"
    sha256sum ./*.log ./*.txt ./*.gz ./*.json ./*.md ./*.csv 2>/dev/null || true
) > "$OUT/SHA256SUMS"

(
    cd "$PROJECT"
    sha256sum build/foc/generated/libfoc_bench_skel.so \
              build/foc/generated-hvx/libhvx_bench_skel.so \
              build/foc/foc_runner \
              build/foc/generated/foc_bench.h \
              build/foc/generated-hvx/hvx_bench.h
) > "$OUT/artifacts.sha256"

echo "=== SUITE PASS ==="
echo "results: $OUT"
