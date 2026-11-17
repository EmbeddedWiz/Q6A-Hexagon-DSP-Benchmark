#pragma once

#include "motor_control.h"

#include <cstddef>
#include <cstdint>

namespace motor_control::fixed {

int16_t sin_q15(uint16_t angle);
int16_t cos_q15(uint16_t angle);

// Compute both components with a single CORDIC pass. This is mathematically
// identical to calling sin_q15() and cos_q15() separately, but avoids running
// the iteration loop twice for the same angle.
void sincos_q15(uint16_t angle, int16_t& sin_value, int16_t& cos_value);

void compute(
    const Input& input,
    Output& output);

void compute_batch(
    const uint16_t* angles,
    Output* outputs,
    std::size_t count);

} // namespace motor_control::fixed
