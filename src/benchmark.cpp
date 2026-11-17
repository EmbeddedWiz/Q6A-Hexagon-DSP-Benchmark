#include "benchmark.h"
#include "thermal.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace benchmark {

void print_system_info()
{
    std::cout
        << "Q6A Hexagon DSP Benchmark\n"
        << "C++ standard: C++20\n"
        << "Thermal policy:\n"
        << "  pause threshold : 82 C\n"
        << "  abort threshold : 84 C\n"
        << "  cooldown target  : < 78 C\n";
}

void run_smoke_test(const Config& config)
{
    std::cout << "\nThermal smoke test\n";

    const auto status = thermal::read_cpu_temperature();

    if (!status) {
        std::cout << "WARNING: CPU/SoC thermal zone not found\n";
        return;
    }

    std::cout << "Current temperature: "
              << status->temperature_c
              << " C\n";

    if (status->temperature_c >= config.thermal_abort_c) {
        std::cout << "ABORT: temperature is above safety limit\n";
        return;
    }

    if (status->temperature_c >= config.thermal_limit_c) {
        std::cout << "COOLDOWN required\n";

        while (true) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config.cooldown_ms));

            const auto current = thermal::read_cpu_temperature();

            if (!current) {
                std::cout << "ERROR: thermal sensor became unavailable\n";
                return;
            }

            std::cout << "Temperature: "
                      << current->temperature_c
                      << " C\n";

            if (current->temperature_c >= config.thermal_abort_c) {
                std::cout << "ABORT: thermal limit reached\n";
                return;
            }

            if (current->temperature_c < config.thermal_pause_c) {
                break;
            }
        }

        std::cout << "Temperature safe, continuing\n";
    }
}

} // namespace benchmark
