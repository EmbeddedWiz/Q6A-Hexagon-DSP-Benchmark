#include <stdint.h>
#include <HAP_perf.h>
#include <qurt_cycles.h>

/*
 * Standalone CDSP time-source probe.
 *
 * Runs a fixed integer loop between two direct time readings:
 *   - HAP_perf_get_time_us() reads the 19.2 MHz qtimer;
 *   - qurt_get_core_pcycles() reads the core cycle counter.
 *
 * The host runner only returns main()'s exit code, so encode the measured
 * direct clock in 10 MHz units: 120 means ~1.20 GHz. 255 means the probe
 * could not obtain a nonzero interval.
 */
int main(void)
{
    const int iterations = 1000000;
    volatile uint32_t x = 0x12345678u;

    const uint64_t t0 = HAP_perf_get_time_us();
    const uint64_t c0 = qurt_get_core_pcycles();
    for (int i = 0; i < iterations; ++i) {
        x = x * 1664525u + 1013904223u;
    }
    const uint64_t c1 = qurt_get_core_pcycles();
    const uint64_t t1 = HAP_perf_get_time_us();

    if (x == 0U || t1 <= t0 || c1 <= c0) {
        return 255;
    }

    const uint64_t cycles = c1 - c0;
    const uint64_t us = t1 - t0;
    const uint64_t mhz = cycles / us;
    const uint64_t code = mhz / 10U;
    return (int)(code > 250U ? 250U : code);
}
