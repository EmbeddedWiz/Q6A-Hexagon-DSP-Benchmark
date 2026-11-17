#include "benchmark.h"
#include "motor_control.h"
#include "motor_control_fixed.h"
#include "motor_control_benchmark.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

int absolute_error(int16_t a, int16_t b)
{
    return std::abs(
        static_cast<int>(a) - static_cast<int>(b));
}

int max_output_error(
    const motor_control::Output& reference,
    const motor_control::Output& tested)
{
    int error = 0;

    error = std::max(
        error,
        absolute_error(reference.sin_a, tested.sin_a));

    error = std::max(
        error,
        absolute_error(reference.sin_b, tested.sin_b));

    error = std::max(
        error,
        absolute_error(reference.sin_c, tested.sin_c));

    error = std::max(
        error,
        absolute_error(reference.cos_a, tested.cos_a));

    error = std::max(
        error,
        absolute_error(reference.cos_b, tested.cos_b));

    error = std::max(
        error,
        absolute_error(reference.cos_c, tested.cos_c));

    return error;
}

void print_output(
    const motor_control::Output& output)
{
    std::cout << "sin: "
              << output.sin_a << ", "
              << output.sin_b << ", "
              << output.sin_c << '\n';

    std::cout << "cos: "
              << output.cos_a << ", "
              << output.cos_b << ", "
              << output.cos_c << '\n';
}

} // namespace

int main()
{
    benchmark::Config config;

    benchmark::print_system_info();

    std::cout << '\n';

    benchmark::run_smoke_test(config);

    constexpr uint16_t test_angle = 0;

    motor_control::Input input{test_angle};

    motor_control::Output fp32_output{};
    motor_control::Output fixed_output{};

    motor_control::compute(
        input,
        fp32_output);

    motor_control::fixed::compute(
        input,
        fixed_output);

    std::cout << "\nMotor-control CPU FP32 reference\n";
    std::cout << "angle = " << input.angle << '\n';
    print_output(fp32_output);

    std::cout << "\nMotor-control CPU fixed-point\n";
    std::cout << "angle = " << input.angle << '\n';
    print_output(fixed_output);

    constexpr uint32_t angle_count = 16384;

    int max_error = 0;
    uint16_t worst_angle = 0;

    for (uint32_t angle = 0;
         angle < angle_count;
         ++angle) {

        const auto test_angle_value =
            static_cast<uint16_t>(angle);

        motor_control::Input test_input{
            test_angle_value
        };

        motor_control::Output reference{};
        motor_control::Output fixed{};

        motor_control::compute(
            test_input,
            reference);

        motor_control::fixed::compute(
            test_input,
            fixed);

        const int error =
            max_output_error(reference, fixed);

        if (error > max_error) {
            max_error = error;
            worst_angle = test_angle_value;
        }
    }

    std::cout << "\nFixed-point validation against FP32 reference\n";
    std::cout << "angles tested : " << angle_count << '\n';
    std::cout << "max abs error : " << max_error << " LSB\n";
    std::cout << "worst angle   : " << worst_angle << '\n';

    motor_control::benchmark::run();

    return 0;
}
