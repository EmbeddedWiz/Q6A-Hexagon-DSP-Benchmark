#pragma once

#include "foc_fixed.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace validation::foc {

struct Case {
    const char* name;
    motor_control::foc::Input input;
    bool reset; // reset the controller before this step
};

// Hand-picked boundaries: axes, quadrant boundaries, saturation, and PI
// integrator clamping. Each case starts from a reset controller.
std::vector<Case> edge_cases();

// Deterministic pseudo-random sequence. The controller is reset before the
// first case and then runs continuously, so the PI integrator state matters.
std::vector<Case> random_cases(std::size_t count, std::uint32_t seed);

// Full 14-bit electrical-angle sweep at constant currents. The first case
// resets the controller; the rest run continuously.
std::vector<Case> angle_sweep();

} // namespace validation::foc
