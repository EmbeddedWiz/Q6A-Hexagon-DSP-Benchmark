#include "motor_control_benchmark.h"

#include "motor_control.h"
#include "motor_control_fixed.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace motor_control::benchmark {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kBatchSize = 4096;
constexpr std::size_t kBatchIterations = 256;
constexpr std::size_t kScalarIterations = 200000;

constexpr uint16_t kAngleMask = 0x3FFF;

struct Result {
    double total_ms;
    double ns_per_sample;
    double samples_per_second;
    int64_t checksum;
};

std::vector<uint16_t> make_angles(std::size_t count)
{
    std::vector<uint16_t> angles(count);

    constexpr uint16_t kStep = 37;

    uint16_t angle = 0;

    for (std::size_t i = 0; i < count; ++i) {
        angles[i] = angle;

        angle = static_cast<uint16_t>(
            (static_cast<uint32_t>(angle) + kStep) & kAngleMask);
    }

    return angles;
}

int64_t checksum_outputs(
    const std::vector<Output>& outputs)
{
    int64_t checksum = 0;

    for (const Output& output : outputs) {
        checksum += output.sin_a;
        checksum += output.sin_b;
        checksum += output.sin_c;
        checksum += output.cos_a;
        checksum += output.cos_b;
        checksum += output.cos_c;
    }

    return checksum;
}

template <typename ComputeBatch>
Result run_batch_benchmark(
    const std::vector<uint16_t>& angles,
    std::vector<Output>& outputs,
    std::size_t iterations,
    ComputeBatch&& compute_batch)
{
    for (int i = 0; i < 4; ++i) {
        compute_batch(
            angles.data(),
            outputs.data(),
            angles.size());
    }

    const auto start = Clock::now();

    for (std::size_t iteration = 0;
         iteration < iterations;
         ++iteration) {
        compute_batch(
            angles.data(),
            outputs.data(),
            angles.size());
    }

    const auto end = Clock::now();

    const double total_seconds =
        std::chrono::duration<double>(end - start).count();

    const std::size_t total_samples =
        angles.size() * iterations;

    Result result{};

    result.total_ms = total_seconds * 1000.0;

    result.ns_per_sample =
        total_seconds * 1'000'000'000.0 /
        static_cast<double>(total_samples);

    result.samples_per_second =
        static_cast<double>(total_samples) /
        total_seconds;

    result.checksum = checksum_outputs(outputs);

    return result;
}

Result run_scalar_fp32(
    const std::vector<uint16_t>& angles,
    std::vector<Output>& outputs)
{
    for (int i = 0; i < 4; ++i) {
        compute(
            Input{angles[0]},
            outputs[0]);
    }

    const auto start = Clock::now();

    for (std::size_t iteration = 0;
         iteration < kScalarIterations;
         ++iteration) {
        compute(
            Input{angles[iteration % angles.size()]},
            outputs[0]);
    }

    const auto end = Clock::now();

    const double total_seconds =
        std::chrono::duration<double>(end - start).count();

    Result result{};

    result.total_ms = total_seconds * 1000.0;

    result.ns_per_sample =
        total_seconds * 1'000'000'000.0 /
        static_cast<double>(kScalarIterations);

    result.samples_per_second =
        static_cast<double>(kScalarIterations) /
        total_seconds;

    result.checksum = checksum_outputs(outputs);

    return result;
}

Result run_scalar_fixed(
    const std::vector<uint16_t>& angles,
    std::vector<Output>& outputs)
{
    for (int i = 0; i < 4; ++i) {
        fixed::compute(
            Input{angles[0]},
            outputs[0]);
    }

    const auto start = Clock::now();

    for (std::size_t iteration = 0;
         iteration < kScalarIterations;
         ++iteration) {
        fixed::compute(
            Input{angles[iteration % angles.size()]},
            outputs[0]);
    }

    const auto end = Clock::now();

    const double total_seconds =
        std::chrono::duration<double>(end - start).count();

    Result result{};

    result.total_ms = total_seconds * 1000.0;

    result.ns_per_sample =
        total_seconds * 1'000'000'000.0 /
        static_cast<double>(kScalarIterations);

    result.samples_per_second =
        static_cast<double>(kScalarIterations) /
        total_seconds;

    result.checksum = checksum_outputs(outputs);

    return result;
}

void print_result(
    const char* name,
    const Result& result)
{
    std::cout << std::left
              << std::setw(16) << name
              << " total = "
              << std::fixed << std::setprecision(3)
              << result.total_ms
              << " ms, ns/sample = "
              << std::setprecision(2)
              << result.ns_per_sample
              << ", samples/s = "
              << std::setprecision(0)
              << result.samples_per_second
              << ", checksum = "
              << result.checksum
              << '\n';
}

} // namespace

void run()
{
    const auto angles = make_angles(kBatchSize);

    std::vector<Output> fp32_outputs(kBatchSize);
    std::vector<Output> fixed_outputs(kBatchSize);

    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "Motor-control CPU benchmark\n";
    std::cout << "========================================\n";

    std::cout << "Angle resolution : 14-bit\n";
    std::cout << "Batch size       : " << kBatchSize << '\n';
    std::cout << "Batch iterations : " << kBatchIterations << '\n';
    std::cout << "Scalar iterations: " << kScalarIterations << '\n';

    std::cout << "\nScalar latency\n";

    const Result scalar_fp32 =
        run_scalar_fp32(angles, fp32_outputs);

    const Result scalar_fixed =
        run_scalar_fixed(angles, fixed_outputs);

    print_result("CPU FP32", scalar_fp32);
    print_result("CPU fixed", scalar_fixed);

    std::cout << "\nBatch throughput\n";

    const Result batch_fp32 =
        run_batch_benchmark(
            angles,
            fp32_outputs,
            kBatchIterations,
            [](const uint16_t* input,
               Output* output,
               std::size_t count) {
                compute_batch(input, output, count);
            });

    const Result batch_fixed =
        run_batch_benchmark(
            angles,
            fixed_outputs,
            kBatchIterations,
            [](const uint16_t* input,
               Output* output,
               std::size_t count) {
                fixed::compute_batch(input, output, count);
            });

    print_result("CPU FP32", batch_fp32);
    print_result("CPU fixed", batch_fixed);

    std::cout << "\nSpeedup\n";

    std::cout << "Fixed vs FP32, scalar : "
              << std::fixed << std::setprecision(2)
              << scalar_fp32.ns_per_sample /
                     scalar_fixed.ns_per_sample
              << "x\n";

    std::cout << "Fixed vs FP32, batch  : "
              << std::fixed << std::setprecision(2)
              << batch_fp32.ns_per_sample /
                     batch_fixed.ns_per_sample
              << "x\n";

    std::cout << "\n";
}

} // namespace motor_control::benchmark
