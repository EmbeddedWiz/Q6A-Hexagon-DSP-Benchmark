#pragma once

#include <cstddef>
#include <cstdint>

namespace motor_control {

struct Input {
    uint16_t angle;
};

struct Output {
    int16_t sin_a;
    int16_t sin_b;
    int16_t sin_c;

    int16_t cos_a;
    int16_t cos_b;
    int16_t cos_c;
};

void compute(const Input& input, Output& output);

void compute_batch(
    const uint16_t* angles,
    Output* outputs,
    std::size_t count);

} // namespace motor_control
