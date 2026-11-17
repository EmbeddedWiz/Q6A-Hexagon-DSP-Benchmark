#include <cstdint>
#include <climits>
#include <qurt_cycles.h>
#include "motor_control_fixed.h"

namespace {
volatile uint32_t checksum = 0;
}

int main()
{
    constexpr uint32_t N = 16384;
    motor_control::Output out{};

    uint32_t warmup = 0;
    for (uint32_t i = 0; i < 256; ++i) {
        motor_control::fixed::compute(
            motor_control::Input{static_cast<uint16_t>(i)}, out);
        warmup += static_cast<uint16_t>(out.sin_a);
    }
    checksum = warmup;

    unsigned long long best = ULLONG_MAX;
    unsigned long long total = 0;

    for (int run = 0; run < 5; ++run) {
        uint32_t local = static_cast<uint32_t>(run + 1);
        const unsigned long long start = qurt_get_core_pcycles();

        for (uint32_t angle = 0; angle < N; ++angle) {
            motor_control::fixed::compute(
                motor_control::Input{static_cast<uint16_t>(angle)}, out);
            local += static_cast<uint16_t>(out.sin_a);
            local += static_cast<uint16_t>(out.cos_a);
            local += static_cast<uint16_t>(out.sin_b);
            local += static_cast<uint16_t>(out.cos_b);
            local += static_cast<uint16_t>(out.sin_c);
            local += static_cast<uint16_t>(out.cos_c);
        }

        const unsigned long long end = qurt_get_core_pcycles();
        if (end <= start)
            return -34;

        const unsigned long long delta = end - start;
        if (delta > INT_MAX)
            return -35;

        checksum = checksum ^ local;
        if (delta < best)
            best = delta;
        total += delta;
    }

    const unsigned long long average = total / 5;
    // Runner reports main()'s return value as the cycle count.
    // Return best-run cycles divided by N, in units of 10 cycles.
    const unsigned long long per_angle_x10 = (best * 10ULL) / N;
    if (per_angle_x10 == 0 || per_angle_x10 > INT_MAX)
        return -36;

    return static_cast<int>(per_angle_x10);
}
