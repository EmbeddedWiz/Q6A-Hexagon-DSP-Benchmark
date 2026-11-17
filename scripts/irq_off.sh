moved=0
failed=0
for f in /proc/irq/[0-9]*/smp_affinity_list; do
    v=$(cat "$f" 2>/dev/null || true)
    if echo "$v" | grep -q '7'; then
        if echo 0-6 > "$f" 2>/dev/null; then
            moved=$((moved + 1))
        else
            failed=$((failed + 1))
        fi
    fi
done
echo 7f > /proc/irq/default_smp_affinity 2>/dev/null || true
echo "moved=$moved failed=$failed"
cat /proc/irq/default_smp_affinity
printf 'governor='; cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor
printf 'freq='; cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq
