#pragma once

#include <optional>

namespace thermal {

struct Status {
    double temperature_c;
    bool valid;
};

std::optional<Status> read_cpu_temperature();

bool check_safe_temperature(
    double temperature_c,
    double abort_limit_c = 84.0,
    double pause_limit_c = 82.0
);

} // namespace thermal
