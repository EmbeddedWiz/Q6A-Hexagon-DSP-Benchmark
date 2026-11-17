#include "foc_fixed.h"

#include <cstdint>
#include <cstdio>
#include <qurt_cycles.h>

#if __has_include(<hexagon_protos.h>)
#include <hexagon_protos.h>
#endif

extern "C" int main()
{
    using namespace motor_control::foc;

    constexpr uint32_t N = 16384;
    const Config cfg{8192, 0, 8192, 0};
    Controller controller(cfg);

    uint64_t checksum = 0;
    Output out{};

    // Warm-up
    for (uint32_t i = 0; i < 256; ++i) {
        const Input in{
            static_cast<int16_t>((i * 7919u) & 0xffffu),
            static_cast<int16_t>((i * 3571u) & 0xffffu),
            0, 10000, static_cast<uint16_t>(i & 0x3fffu)
        };
        controller.step(in, out);
    }

    uint64_t best_cycles = UINT64_MAX;

    for (int run = 0; run < 5; ++run) {
        controller.reset();
        checksum = 0;

#if defined(__hexagon__)
        const uint64_t start = qurt_get_core_pcycles();
#else
        const uint64_t start = 0;
#endif

        for (uint32_t i = 0; i < N; ++i) {
            const Input in{
                static_cast<int16_t>((i * 7919u) & 0xffffu),
                static_cast<int16_t>((i * 3571u) & 0xffffu),
                0, 10000, static_cast<uint16_t>(i & 0x3fffu)
            };
            controller.step(in, out);

            checksum += static_cast<uint16_t>(out.id_q15);
            checksum += static_cast<uint16_t>(out.iq_q15);
            checksum += static_cast<uint16_t>(out.vd_q15);
            checksum += static_cast<uint16_t>(out.vq_q15);
            checksum += out.duty_a_q15;
            checksum += out.duty_b_q15;
            checksum += out.duty_c_q15;
        }

#if defined(__hexagon__)
        const uint64_t elapsed = qurt_get_core_pcycles() - start;
        if (elapsed < best_cycles) best_cycles = elapsed;
#endif
    }

#if defined(__hexagon__)
    std::printf("FOC DSP benchmark\n");
    std::printf("Iterations: %lu\n", static_cast<unsigned long>(N));
    std::printf("Best cycles total: %llu\n",
                static_cast<unsigned long long>(best_cycles));
    std::printf("Cycles per FOC step: %llu\n",
                static_cast<unsigned long long>(best_cycles / N));
    std::printf("Checksum: %llu\n",
                static_cast<unsigned long long>(checksum));
    if (best_cycles == UINT64_MAX || checksum == 0) return 22;
      const uint64_t cycles_per_step = best_cycles / N;
      if (cycles_per_step == 0 || cycles_per_step > 1000000) return 23;
      return static_cast<int>(cycles_per_step);
#else
    std::printf("This benchmark must run on Hexagon DSP.\n");
    return 1;
#endif
}
