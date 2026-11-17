#include "foc_vectors.h"

#include <array>

namespace validation::foc {
namespace {

motor_control::foc::Input make_input(int ia, int ib, int id_ref, int iq_ref,
                                     int angle)
{
    return motor_control::foc::Input{
        static_cast<std::int16_t>(ia),
        static_cast<std::int16_t>(ib),
        static_cast<std::int16_t>(id_ref),
        static_cast<std::int16_t>(iq_ref),
        static_cast<std::uint16_t>(angle)
    };
}

void add(std::vector<Case>& cases, const char* name,
         int ia, int ib, int id_ref, int iq_ref, int angle, bool reset)
{
    cases.push_back(Case{name, make_input(ia, ib, id_ref, iq_ref, angle), reset});
}

} // namespace

std::vector<Case> edge_cases()
{
    std::vector<Case> cases;
    cases.reserve(24);

    add(cases, "zero", 0, 0, 0, 0, 0, true);

    constexpr std::array<int, 5> angles{0, 4096, 8192, 12288, 16383};
    for (const int angle : angles) {
        add(cases, "axis", 10000, -5000, 0, 0, angle, true);
    }

    add(cases, "positive_saturation", 32767, 32767, 32767, 32767, 0, true);
    add(cases, "negative_saturation", -32767, -32767, -32767, -32767, 0, true);
    add(cases, "positive_saturation_quadrant", 32767, 32767, 32767, 32767,
        8192, true);
    add(cases, "negative_saturation_quadrant", -32767, -32767, -32767, -32767,
        8192, true);

    add(cases, "d_axis_only", 20000, 0, 8000, 0, 0, true);
    add(cases, "q_axis_only", 0, 20000, 0, 8000, 4096, true);
    add(cases, "mixed_sign", -20000, 15000, 5000, -5000, 12000, true);
    add(cases, "angle_last", 12345, -6789, 4000, -3000, 16383, true);

    // Large error held for several samples. The main validation run uses
    // ki = 0 (state retention); the dedicated high-ki run in
    // scripts/run_foc_suite.sh drives the integrator into its clamp.
    for (int step = 0; step < 8; ++step) {
        add(cases, "large_error_hold", 30000, -30000, 32767, -32767,
            step * 1024, step == 0);
    }

    // Reverse the error without resetting, so the integrator must recover.
    for (int step = 0; step < 8; ++step) {
        add(cases, "large_error_reverse", -30000, 30000, -32767, 32767,
            step * 1024, false);
    }

    return cases;
}

std::vector<Case> random_cases(std::size_t count, std::uint32_t seed)
{
    std::vector<Case> cases;
    cases.reserve(count);

    std::uint32_t state = seed;
    const auto next = [&state]() {
        state = state * 1664525U + 1013904223U;
        return state;
    };
    const auto next_i16 = [&next]() {
        return static_cast<int>(static_cast<std::int16_t>(next() >> 16));
    };
    const auto next_angle = [&next]() {
        return static_cast<int>((next() >> 18) & 0x3FFFU);
    };

    for (std::size_t index = 0; index < count; ++index) {
        cases.push_back(Case{
            "random",
            make_input(next_i16(), next_i16(), next_i16(), next_i16(),
                       next_angle()),
            index == 0
        });
    }
    return cases;
}

std::vector<Case> angle_sweep()
{
    std::vector<Case> cases;
    cases.reserve(16384);
    for (int angle = 0; angle < 16384; ++angle) {
        add(cases, "angle_sweep", 12000, -7000, 3000, -2000, angle,
            angle == 0);
    }
    return cases;
}

} // namespace validation::foc
