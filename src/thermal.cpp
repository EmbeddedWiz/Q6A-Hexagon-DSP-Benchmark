#include "thermal.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <string>

namespace fs = std::filesystem;

namespace thermal {

std::optional<Status> read_cpu_temperature()
{
    double max_temp = -std::numeric_limits<double>::infinity();
    bool found = false;

    static const std::regex temp_file_regex(R"(thermal_zone[0-9]+)");

    for (const auto& entry : fs::directory_iterator("/sys/class/thermal")) {
        if (!entry.is_directory())
            continue;

        const std::string zone = entry.path().filename().string();

        if (!std::regex_match(zone, temp_file_regex))
            continue;

        std::ifstream type_file(entry.path() / "type");
        std::string type;
        std::getline(type_file, type);

        std::ifstream temp_file(entry.path() / "temp");
        long millidegrees = 0;

        if (!(temp_file >> millidegrees))
            continue;

        const double temperature_c = static_cast<double>(millidegrees) / 1000.0;

        // We deliberately do not classify individual thermal zones.
        // The benchmark safety guard uses the maximum temperature
        // reported by the thermal subsystem.
        (void)type;

        if (temperature_c > max_temp)
            max_temp = temperature_c;

        found = true;
    }

    if (!found)
        return std::nullopt;

    return Status{max_temp, true};
}

bool check_safe_temperature(
    double temperature_c,
    double abort_limit_c,
    double pause_limit_c)
{
    return temperature_c < abort_limit_c &&
           temperature_c < pause_limit_c;
}

} // namespace thermal
