#!/usr/bin/env bash
#
# Summarize the thermal ramp probes and continuous temperature trace using awk.
# Kept in shell/awk so the measurement path does not add a Python dependency.
#
# Usage: summarize_thermal.sh RESULTS_DIR
#
set -Eeuo pipefail

OUT="${1:?usage: summarize_thermal.sh RESULTS_DIR}"
PROBES="$OUT/probes.csv"
TRACE="$OUT/thermal-trace.csv"
MD="$OUT/thermal-summary.md"
JSON="$OUT/thermal-summary.json"

[[ -r "$PROBES" && -r "$TRACE" ]] || { echo "missing $PROBES or $TRACE" >&2; exit 2; }

TRACE_STATS="$(awk -F, '
    NR == 1 { for (i=1;i<=NF;i++) col[$i]=i; next }
    {
        t = $col["max_temp_c"] + 0
        f = $col["cpu7_freq_khz"] + 0
        load = $col["load_active"] + 0
        if (n == 0 || t < tmin) tmin = t
        if (n == 0 || t > tmax) tmax = t
        if (f > 0) { if (!fn || f < fmin) fmin = f; if (f > fmax) fmax = f; fn++ }
        if (load) { lx[nl]=t; ly[nl]=f; nl++ }
        n++
    }
    END {
        r="nan"; s="nan"
        if (nl >= 3) {
            for (i=0;i<nl;i++){sx+=lx[i];sy+=ly[i];sxx+=lx[i]*lx[i];syy+=ly[i]*ly[i];sxy+=lx[i]*ly[i]}
            d=(nl*sxx-sx*sx)*(nl*syy-sy*sy)
            if (d>0) r=(nl*sxy-sx*sy)/sqrt(d)
            den=nl*sxx-sx*sx; if (den!=0) s=(nl*sxy-sx*sy)/den
        }
        printf "%.1f %.1f %.0f %.0f %d %.6f %.6f\n", tmin, tmax, fmin, fmax, n, r, s
    }' "$TRACE")"
read -r TMIN TMAX FMIN FMAX NSAMP R_FREQ S_FREQ <<< "$TRACE_STATS"

# Probe rows: cold=first, hot=last phase hot, cooldown=last phase cooldown.
awk -F, -v tmin="$TMIN" -v tmax="$TMAX" -v fmin="$FMIN" -v fmax="$FMAX" \
    -v nsamp="$NSAMP" -v rfreq="$R_FREQ" -v sfreq="$S_FREQ" '
    NR == 1 { for (i=1;i<=NF;i++) col[$i]=i; next }
    {
        phase=$col["phase"]
        n++
        if (phase=="cold") { cold=n }
        if (phase=="hot") hot=n
        if (phase=="cooldown") cool=n
        if (phase=="cold" || phase=="soak" || phase=="hot") {
            soak[ns]=n; ns++
            dt=($col["dsp_temp_before_c"]+$col["dsp_temp_after_c"])/2
            ct=($col["cpu_temp_before_c"]+$col["cpu_temp_after_c"])/2
            dcy[ns-1]=$col["dsp_cycles_per_step"]+0
            dns[ns-1]=$col["dsp_ns_per_step"]+0
            dmh[ns-1]=$col["dsp_effective_mhz"]+0
            cns[ns-1]=$col["cpu_ns_per_step"]+0
            cmx[ns-1]=$col["cpu7_freq_max_khz"]+0
            dtx[ns-1]=dt; ctx[ns-1]=ct
        }
        rows[n]=$0
    }
    function get(idx,key){ split(rows[idx],a,","); return a[col[key]]+0 }
    function pearson(x,y,n,  i,sx,sy,sxx,syy,sxy,d){ if(n<3)return -999; for(i=0;i<n;i++){sx+=x[i];sy+=y[i];sxx+=x[i]*x[i];syy+=y[i]*y[i];sxy+=x[i]*y[i]} d=(n*sxx-sx*sx)*(n*syy-sy*sy); if(d<=0)return -999; return (n*sxy-sx*sy)/sqrt(d) }
    function slope(x,y,n,  i,sx,sy,sxx,sxy,den){ if(n<3)return -999; for(i=0;i<n;i++){sx+=x[i];sy+=y[i];sxx+=x[i]*x[i];sxy+=x[i]*y[i]} den=n*sxx-sx*sx; if(den==0)return -999; return (n*sxy-sx*sy)/den }
    function fmt(v){ return v==-999 ? "–" : sprintf("%+.3f",v) }
    function fmts(v){ return v==-999 ? "–" : sprintf("%+.4f",v) }
    function row(label,idx){
        if (!idx) return "| " label " | – | – | – | – | – | – | – |"
        return sprintf("| %s | %.1f | %.1f | %.1f | %.3f | %.1f | %.2f | %.0f |",
            label,get(idx,"max_temp_c"),get(idx,"cpu7_temp_c"),get(idx,"nsp_temp_c"),
            get(idx,"dsp_cycles_per_step"),get(idx,"dsp_ns_per_step"),
            get(idx,"cpu_ns_per_step"),get(idx,"cpu7_freq_max_khz"))
    }
    END {
        printf "# Thermal soak summary\n\n"
        printf "Probes: **%d** (%d used for correlation); continuous samples: **%d**.\n\n", n, ns, nsamp
        printf "Observed temperature range: **%.1f–%.1f °C**.\n", tmin, tmax
        printf "CPU 7 frequency range in the trace: **%.0f–%.0f kHz**.\n\n", fmin, fmax
        print "| Point | Max °C | CPU7 °C | NSP °C | DSP cycles/step | DSP ns/step | CPU ns/step | CPU7 max MHz |"
        print "|---|---:|---:|---:|---:|---:|---:|---:|"
        print row("Cold (idle)", cold)
        print row("Hot (under load)", hot)
        print row("After 60 s cooldown", cool)
        print "\n## Temperature correlation over cold, ramp and hot points\n"
        print "| Metric | Pearson r | Slope per °C |"
        print "|---|---:|---:|"
        printf "| DSP cycles/step | %s | %s |\n", fmt(pearson(dtx,dcy,ns)), fmts(slope(dtx,dcy,ns))
        printf "| DSP ns/step | %s | %s |\n", fmt(pearson(dtx,dns,ns)), fmts(slope(dtx,dns,ns))
        printf "| DSP effective MHz | %s | %s |\n", fmt(pearson(dtx,dmh,ns)), fmts(slope(dtx,dmh,ns))
        printf "| CPU ns/step | %s | %s |\n", fmt(pearson(ctx,cns,ns)), fmts(slope(ctx,cns,ns))
        printf "| CPU 7 max frequency during probe (kHz) | %s | %s |\n", fmt(pearson(ctx,cmx,ns)), fmts(slope(ctx,cmx,ns))
        printf "\nThe during-load trace CPU 7 frequency/temperature correlation is r = %s, slope %s kHz/°C.\n\n", fmt(rfreq), fmts(sfreq)
        print "Interpretation rule used in the report: a cycle-count slope near zero means the DSP performed the same number of core cycles; a positive CPU ns/step slope together with a falling CPU 7 frequency indicates DVFS/thermal throttling. A DSP ns/step change without a cycles/step change is a wall-clock/effective-rate effect, not extra DSP work."
        printf "\n"
    }' "$PROBES" > "$MD"

# Machine-readable copy of the same numbers.
awk -F, -v tmin="$TMIN" -v tmax="$TMAX" -v fmin="$FMIN" -v fmax="$FMAX" \
    -v nsamp="$NSAMP" -v rfreq="$R_FREQ" -v sfreq="$S_FREQ" '
    NR == 1 { for (i=1;i<=NF;i++) col[$i]=i; next }
    {
        phase=$col["phase"]; n++
        if (phase=="cold") cold=n
        if (phase=="hot") hot=n
        if (phase=="cooldown") cool=n
        if (phase=="cold" || phase=="soak" || phase=="hot") {
            dt=($col["dsp_temp_before_c"]+$col["dsp_temp_after_c"])/2
            ct=($col["cpu_temp_before_c"]+$col["cpu_temp_after_c"])/2
            dcy[ns]=$col["dsp_cycles_per_step"]+0
            dns[ns]=$col["dsp_ns_per_step"]+0
            dmh[ns]=$col["dsp_effective_mhz"]+0
            cns[ns]=$col["cpu_ns_per_step"]+0
            cmx[ns]=$col["cpu7_freq_max_khz"]+0
            dtx[ns]=dt; ctx[ns]=ct; ns++
        }
        rows[n]=$0
    }
    function get(idx,key){ split(rows[idx],a,","); return a[col[key]]+0 }
    function pearson(x,y,n,  i,sx,sy,sxx,syy,sxy,d){ if(n<3)return "null"; for(i=0;i<n;i++){sx+=x[i];sy+=y[i];sxx+=x[i]*x[i];syy+=y[i]*y[i];sxy+=x[i]*y[i]} d=(n*sxx-sx*sx)*(n*syy-sy*sy); if(d<=0)return "null"; return sprintf("%.6f",(n*sxy-sx*sy)/sqrt(d)) }
    function slope(x,y,n,  i,sx,sy,sxx,sxy,den){ if(n<3)return "null"; for(i=0;i<n;i++){sx+=x[i];sy+=y[i];sxx+=x[i]*x[i];sxy+=x[i]*y[i]} den=n*sxx-sx*sx; if(den==0)return "null"; return sprintf("%.6f",(n*sxy-sx*sy)/den) }
    function obj(label,idx){ return sprintf("\"%s\":{\"max_temp_c\":%.1f,\"dsp_cycles_per_step\":%.3f,\"dsp_ns_per_step\":%.1f,\"dsp_effective_mhz\":%.3f,\"cpu_ns_per_step\":%.2f,\"cpu7_freq_max_khz\":%.0f}", label,get(idx,"max_temp_c"),get(idx,"dsp_cycles_per_step"),get(idx,"dsp_ns_per_step"),get(idx,"dsp_effective_mhz"),get(idx,"cpu_ns_per_step"),get(idx,"cpu7_freq_max_khz")) }
    END {
        printf "{\n  \"probes\": %d,\n  \"soak_probes\": %d,\n  \"continuous_samples\": %d,\n", n, ns, nsamp
        printf "  \"temperature_c\": {\"min\": %.1f, \"max\": %.1f},\n", tmin, tmax
        printf "  \"cpu7_freq_khz\": {\"min\": %.0f, \"max\": %.0f},\n", fmin, fmax
        printf "  \"cold\": %s,\n  \"hot\": %s,\n  \"cooldown\": %s,\n", obj("cold",cold), obj("hot",hot), obj("cooldown",cool)
        printf "  \"correlation\": {\n"
        printf "    \"dsp_cycles_per_step\": {\"pearson_r\": %s, \"slope_per_c\": %s},\n", pearson(dtx,dcy,ns), slope(dtx,dcy,ns)
        printf "    \"dsp_ns_per_step\": {\"pearson_r\": %s, \"slope_per_c\": %s},\n", pearson(dtx,dns,ns), slope(dtx,dns,ns)
        printf "    \"dsp_effective_mhz\": {\"pearson_r\": %s, \"slope_per_c\": %s},\n", pearson(dtx,dmh,ns), slope(dtx,dmh,ns)
        printf "    \"cpu_ns_per_step\": {\"pearson_r\": %s, \"slope_per_c\": %s},\n", pearson(ctx,cns,ns), slope(ctx,cns,ns)
        printf "    \"cpu7_freq_max_khz\": {\"pearson_r\": %s, \"slope_per_c\": %s}\n", pearson(ctx,cmx,ns), slope(ctx,cmx,ns)
        printf "  },\n  \"trace_freq_vs_temp\": {\"pearson_r\": %s, \"slope_khz_per_c\": %s}\n}\n", rfreq, sfreq
    }' "$PROBES" > "$JSON"

cat "$MD"
