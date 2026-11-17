#include "motor_control.h"

#include <cmath>

namespace motor_control {

namespace {

constexpr uint16_t kAngleMask = 0x3FFF;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

constexpr float kPhaseB = kTwoPi / 3.0f;
constexpr float kPhaseC = 2.0f * kTwoPi / 3.0f;

constexpr float kQ15Scale = 32767.0f;

int16_t to_q15(float value)
{
    const float scaled = value * kQ15Scale;

    if (scaled > kQ15Scale)
        return 32767;

    if (scaled < -32767.0f)
        return -32767;

    return static_cast<int16_t>(std::lround(scaled));
}

float angle_to_radians(uint16_t angle)
{
    const uint16_t normalized = angle & kAngleMask;

    return static_cast<float>(normalized) * kTwoPi / 16384.0f;
}

} // namespace

void compute(const Input& input, Output& output)
{
    const float theta = angle_to_radians(input.angle);

    output.sin_a = to_q15(std::sin(theta));
    output.sin_b = to_q15(std::sin(theta - kPhaseB));
    output.sin_c = to_q15(std::sin(theta - kPhaseC));

    output.cos_a = to_q15(std::cos(theta));
    output.cos_b = to_q15(std::cos(theta - kPhaseB));
    output.cos_c = to_q15(std::cos(theta - kPhaseC));
}

void compute_batch(
    const uint16_t* angles,
    Output* outputs,
    std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        compute(Input{angles[i]}, outputs[i]);
    }
}

} // namespace motor_control
