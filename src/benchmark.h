#pragma once

#include <string>

namespace benchmark {

struct Config {
    double thermal_pause_c = 78.0;
    double thermal_limit_c = 82.0;
    double thermal_abort_c = 84.0;
    unsigned cooldown_ms = 2500;
};

void print_system_info();
void run_smoke_test(const Config& config);

} // namespace benchmark
