#!/usr/bin/env bash
#
# Thermal ramp and degradation check for the two tested objects:
#   * the Hexagon CDSP running the FOC `bench` RPC;
#   * CPU 7 (Cortex-A78 prime) running the pinned fixed-point FOC loop.
#
# The script raises package temperature with the real FOC workload on CPU 4
# and probes the CDSP with its normal short bench call at intervals. It probes
# DSP and CPU 7 repeatedly during the ramp, so the same objects are measured
# cold, warm and hot. A watchdog stops the load at --target-temp and aborts at
# --abort-temp. Long DSP calls are avoided because they trip the CDSP watchdog.
#
# Board thermal ceiling for this project is 79 C. The default soak targets
# 68 C and aborts at 79 C. One core plateaus near 69 C, so the ceiling is not
# approached; the ramp is intentionally bounded.
#
# Usage:
#   scripts/thermal_soak.sh [--out DIR] [--target-temp C] [--abort-temp C]
#                           [--probe-every S] [--allow-dirty]
#
set -Eeuo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT=""
TARGET_TEMP_C=68
ABORT_TEMP_C=79
PROBE_EVERY=2
HEAT_STEPS=1800000000
LOAD_CPUS="4"
SAMPLE_INTERVAL=1
MAX_RUN_S=300
ALLOW_DIRTY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out)
            [[ $# -ge 2 ]] || { echo "--out needs a directory" >&2; exit 2; }
            OUT="$2"; shift 2 ;;
        --target-temp)
            [[ $# -ge 2 ]] || { echo "--target-temp needs a value" >&2; exit 2; }
            TARGET_TEMP_C="$2"; shift 2 ;;
        --abort-temp)
            [[ $# -ge 2 ]] || { echo "--abort-temp needs a value" >&2; exit 2; }
            ABORT_TEMP_C="$2"; shift 2 ;;
        --probe-every)
            [[ $# -ge 2 ]] || { echo "--probe-every needs a value" >&2; exit 2; }
            PROBE_EVERY="$2"; shift 2 ;;
        --load-cores)
            [[ $# -ge 2 ]] || { echo "--load-cores needs a value" >&2; exit 2; }
            LOAD_CPUS="${2//,/ }"; shift 2 ;;
        --allow-dirty)
            ALLOW_DIRTY=1; shift ;;
        *)
            echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$OUT" ]]; then
    OUT="$PROJECT/results/thermal/$(date -u +%Y%m%dT%H%M%SZ)"
fi
mkdir -p "$OUT"

if [[ $ALLOW_DIRTY -eq 0 && -n "$(git -C "$PROJECT" status --porcelain)" ]]; then
    echo "Working tree is not clean. Commit or stash changes, or pass --allow-dirty." >&2
    git -C "$PROJECT" status --short >&2
    exit 1
fi

git -C "$PROJECT" rev-parse HEAD > "$OUT/git-commit.txt"
git -C "$PROJECT" status --porcelain > "$OUT/git-status.txt"
date -Is > "$OUT/started-at.txt"

RUNNER="$PROJECT/build/foc/foc_runner"
test -x "$RUNNER"
export DSP_LIBRARY_PATH="${Q6A_RUNNER_DIR:-$HOME/Projects/hexagon-v68-test}"
export ADSP_LIBRARY_PATH="$DSP_LIBRARY_PATH:/usr/lib/dsp/cdsp:/usr/lib/rfsa/adsp"
test -r /sys/class/remoteproc/remoteproc1/state

temp_mc_by_type() {
    local want="$1" z
    for z in /sys/class/thermal/thermal_zone*; do
        if [[ -r "$z/temp" && "$(cat "$z/type" 2>/dev/null)" == "$want" ]]; then
            cat "$z/temp"
            return
        fi
    done
    echo 0
}

# Print "millidegrees|zone-type" for the hottest readable zone.
max_zone() {
    local z t max=0 name=unknown best=unknown
    for z in /sys/class/thermal/thermal_zone*; do
        [[ -r "$z/temp" ]] || continue
        t="$(cat "$z/temp")"
        if (( t > max )); then
            max="$t"
            best="$(basename "$z")"
            name="$(cat "$z/type" 2>/dev/null || echo unknown)"
        fi
    done
    printf '%s|%s %s\n' "$max" "$best" "$name"
}

max_temp_mc() { max_zone | cut -d'|' -f1; }
max_zone_name() { max_zone | cut -d'|' -f2; }

nsp_temp_mc() {
    local a b
    a="$(temp_mc_by_type nspss0-thermal)"
    b="$(temp_mc_by_type nspss1-thermal)"
    (( a > b )) && echo "$a" || echo "$b"
}

cpu7_temp_mc() { temp_mc_by_type cpu7-thermal; }
cpu7_freq() { cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq 2>/dev/null || echo 0; }
c_to_mc() { awk -v c="$1" 'BEGIN { printf "%d", c * 1000 }'; }

json_field() {
    sed -n 's/.*"'"$2"'":\([^,}]*\).*/\1/p' "$1" | head -n 1
}

STOP_SAMPLER="$OUT/.stop-sampler"
STOP_LOAD="$OUT/.stop-load"
rm -f "$STOP_SAMPLER" "$STOP_LOAD"
TRACE="$OUT/thermal-trace.csv"
printf 'timestamp,max_temp_c,max_zone,cpu7_temp_c,nsp_temp_c,cpu7_freq_khz,load_active\n' > "$TRACE"

sampler() {
    while [[ ! -f "$STOP_SAMPLER" ]]; do
        local mz load
        mz="$(max_zone)"
        [[ -f "$STOP_LOAD" ]] && load=0 || load=1
        printf '%s,%.1f,%s,%.1f,%.1f,%s,%s\n' \
            "$(date -Is)" \
            "$(awk -v v="${mz%%|*}" 'BEGIN{print v/1000}')" \
            "${mz#*|}" \
            "$(awk -v v="$(cpu7_temp_mc)" 'BEGIN{print v/1000}')" \
            "$(awk -v v="$(nsp_temp_mc)" 'BEGIN{print v/1000}')" \
            "$(cpu7_freq)" "$load" >> "$TRACE"
        sleep "$SAMPLE_INTERVAL"
    done
}
sampler &
SAMPLER_PID=$!

cleanup() {
    touch "$STOP_SAMPLER" "$STOP_LOAD"
    wait "$SAMPLER_PID" 2>/dev/null || true
    rm -f "$STOP_SAMPLER" "$STOP_LOAD"
}
trap cleanup EXIT

PROBES="$OUT/probes.csv"
printf 'timestamp,phase,elapsed_s,max_temp_c,max_zone,cpu7_temp_c,nsp_temp_c,cpu7_freq_khz,cpu7_freq_max_khz,cpu7_freq_med_khz,dsp_temp_before_c,dsp_temp_after_c,cpu_temp_before_c,cpu_temp_after_c,dsp_cycles_per_step,dsp_ns_per_step,dsp_effective_mhz,dsp_rpc_wall_us,cpu_ns_per_step\n' > "$PROBES"

probe() {
    local phase="$1" elapsed="$2"
    local dsp_before dsp_after cpu_before cpu_after
    local tmp_dsp tmp_cpu tmp_freq cpu_pid freq_max freq_med mz
    tmp_dsp="$(mktemp)"
    tmp_cpu="$(mktemp)"
    tmp_freq="$(mktemp)"

    dsp_before="$(max_temp_mc)"
    "$RUNNER" bench > "$tmp_dsp"
    dsp_after="$(max_temp_mc)"

    cpu_before="$(max_temp_mc)"
    : > "$tmp_freq"
    taskset -c 7 "$RUNNER" cpu --cpu-warmup 20000000 > "$tmp_cpu" &
    cpu_pid=$!
    while kill -0 "$cpu_pid" 2>/dev/null; do
        cpu7_freq >> "$tmp_freq"
        sleep 0.05
    done
    wait "$cpu_pid"
    cpu_after="$(max_temp_mc)"
    freq_max="$(sort -n "$tmp_freq" | tail -n 1)"
    freq_med="$(sort -n "$tmp_freq" | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}')"
    mz="$(max_zone)"

    printf '%s,%s,%s,%.1f,"%s",%.1f,%.1f,%s,%s,%s,%.1f,%.1f,%.1f,%.1f,%s,%s,%s,%s,%s\n' \
        "$(date -Is)" "$phase" "$elapsed" \
        "$(awk -v v="${mz%%|*}" 'BEGIN{print v/1000}')" \
        "${mz#*|}" \
        "$(awk -v v="$(cpu7_temp_mc)" 'BEGIN{print v/1000}')" \
        "$(awk -v v="$(nsp_temp_mc)" 'BEGIN{print v/1000}')" \
        "$(cpu7_freq)" "$freq_max" "$freq_med" \
        "$(awk -v v="$dsp_before" 'BEGIN{print v/1000}')" \
        "$(awk -v v="$dsp_after" 'BEGIN{print v/1000}')" \
        "$(awk -v v="$cpu_before" 'BEGIN{print v/1000}')" \
        "$(awk -v v="$cpu_after" 'BEGIN{print v/1000}')" \
        "$(json_field "$tmp_dsp" median_cycles_per_step)" \
        "$(json_field "$tmp_dsp" median_ns_per_step)" \
        "$(json_field "$tmp_dsp" effective_clock_hz | awk '{printf "%.3f", $1 / 1e6}')" \
        "$(json_field "$tmp_dsp" rpc_wall_us)" \
        "$(json_field "$tmp_cpu" median_ns_per_step)" \
        >> "$PROBES"

    cp "$tmp_dsp" "$OUT/probe-${phase}-${elapsed}-dsp.json"
    cp "$tmp_cpu" "$OUT/probe-${phase}-${elapsed}-cpu.json"
    rm -f "$tmp_dsp" "$tmp_cpu" "$tmp_freq"
}

CPU_PIDS=()
stop_load() {
    touch "$STOP_LOAD"
    local p
    for p in "${CPU_PIDS[@]:-}"; do
        kill "$p" 2>/dev/null || true
    done
    for p in "${CPU_PIDS[@]:-}"; do
        wait "$p" 2>/dev/null || true
    done
}

{
    echo "=== Q6A thermal ramp ==="
    date -Is
    echo "project:      $PROJECT"
    echo "output:       $OUT"
    echo "git commit:   $(cat "$OUT/git-commit.txt")"
    echo "load cores:   $LOAD_CPUS (Cortex-A78, $HEAT_STEPS ramp steps each)"
    echo "dsp probe:    one 5 x 16384-step FOC bench per probe"
    echo "target:       $TARGET_TEMP_C C; abort $ABORT_TEMP_C C"
} | tee "$OUT/thermal-soak.log"

# Cold reference before any sustained load.
probe cold 0

# Package heat from the selected CPU cores (default CPU 4 and 5). The tested
# CPU 7 is left free. One core plateaus near 69 C, which is below the 79 C
# ceiling. The CDSP is only touched by the short probe call; no long DSP loop
# and no second session.
for core in $LOAD_CPUS; do
    taskset -c "$core" "$RUNNER" cpu --cpu-warmup "$HEAT_STEPS" \
        --runs 1 --iterations 1 > "$OUT/load-cpu-${core}.json" &
    CPU_PIDS+=("$!")
done

START_S="$(date +%s)"
LAST_PROBE=0
while true; do
    NOW_S="$(date +%s)"
    ELAPSED=$((NOW_S - START_S))
    if (( ELAPSED >= MAX_RUN_S )); then
        echo "TIMEOUT at ${ELAPSED}s before target" | tee -a "$OUT/thermal-soak.log"
        stop_load
        probe abort "$ELAPSED"
        exit 4
    fi
    MC="$(max_temp_mc)"
    if (( MC >= $(c_to_mc "$ABORT_TEMP_C") )); then
        echo "ABORT at ${ELAPSED}s: $(awk -v v="$MC" 'BEGIN{print v/1000}') C" \
            | tee -a "$OUT/thermal-soak.log"
        stop_load
        probe abort "$ELAPSED"
        exit 3
    fi
    if (( NOW_S - LAST_PROBE >= PROBE_EVERY )); then
        probe soak "$ELAPSED"
        LAST_PROBE="$NOW_S"
        printf 'probe %3ds  max %.1f C (%s)  cpu7 %.1f C  nsp %.1f C\n' \
            "$ELAPSED" \
            "$(awk -v v="$(max_temp_mc)" 'BEGIN{print v/1000}')" \
            "$(max_zone_name)" \
            "$(awk -v v="$(cpu7_temp_mc)" 'BEGIN{print v/1000}')" \
            "$(awk -v v="$(nsp_temp_mc)" 'BEGIN{print v/1000}')" \
            | tee -a "$OUT/thermal-soak.log"
        # Re-read after the probe: the probe itself adds heat. Never let the
        # target decision use a stale pre-probe temperature.
        MC="$(max_temp_mc)"
        if (( MC >= $(c_to_mc "$ABORT_TEMP_C") )); then
            echo "ABORT after probe at ${ELAPSED}s: $(awk -v v="$MC" 'BEGIN{print v/1000}') C" \
                | tee -a "$OUT/thermal-soak.log"
            stop_load
            probe abort "$ELAPSED"
            exit 3
        fi
    fi
    if (( MC >= $(c_to_mc "$TARGET_TEMP_C") )); then
        echo "target reached at ${ELAPSED}s: $(awk -v v="$MC" 'BEGIN{print v/1000}') C" \
            | tee -a "$OUT/thermal-soak.log"
        # One core plateaus below the 79 C ceiling, so the hot point can be
        # measured while the load is active. Re-read after the probe anyway.
        probe hot "$ELAPSED"
        MC="$(max_temp_mc)"
        if (( MC >= $(c_to_mc "$ABORT_TEMP_C") )); then
            echo "ABORT after hot probe at ${ELAPSED}s: $(awk -v v="$MC" 'BEGIN{print v/1000}') C" \
                | tee -a "$OUT/thermal-soak.log"
            stop_load
            exit 3
        fi
        stop_load
        break
    fi
    sleep 0.5
done

echo "cooldown 60 s" | tee -a "$OUT/thermal-soak.log"
sleep 60
probe cooldown "$(( $(date +%s) - START_S ))"

gzip -9 -f "$OUT"/load-*.json 2>/dev/null || true

"$PROJECT/scripts/summarize_thermal.sh" "$OUT"

(
    cd "$OUT"
    sha256sum ./*.log ./*.csv ./*.json ./*.md ./*.txt 2>/dev/null || true
) > "$OUT/SHA256SUMS"

echo "=== THERMAL SOAK PASS ==="
echo "results: $OUT"
