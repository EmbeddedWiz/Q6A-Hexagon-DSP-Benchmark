#include "foc_bench.h"
#include "hvx_bench.h"

#include "foc_fixed.h"
#include "foc_reference.h"
#include "foc_vectors.h"

#include <fastrpc/remote.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace {

using motor_control::foc::Config;
using motor_control::foc::Controller;
using motor_control::foc::Input;
using motor_control::foc::Output;

constexpr double kDefaultClockHz = 1.16e9;
constexpr double kDirectClockHz = 1.17e9;
constexpr int kMaxRuns = 5;

struct Options {
    Config config{8192, 0, 8192, 0};
    int warmup = 256;
    int cpu_warmup = 2000000;
    int runs = 5;
    int iterations = 16384;
    std::uint32_t seed = 0x5EED1234U;
    std::size_t random_count = 4096;
    bool sweep = true;
    bool calibrate = true;
    bool dsp_perf = false;
    int rpc_count = 10000;
    int rpc_repeats = 30;
    std::string rpc_csv_path;
    int rt_samples = 100000;
    int rt_batch = 16;
    int rt_round = 1;
    int rt_condition = 0;
    int rt_warmup = 1000;
    std::string rt_csv_path;
    std::string rt_config = "default";
    double clock_hz = kDefaultClockHz;
    std::string csv_path;
    int hvx_lanes = 64;
    int hvx_iterations = 5000;
    int hvx_runs = 5;
    int pi_steps = 100000;
    int pi_ki = 120;
    std::string pi_csv_path;
};

std::string uri()
{
    return std::string(foc_bench_URI) + CDSP_DOMAIN;
}

void enable_unsigned_module()
{
    remote_rpc_control_unsigned_module unsigned_module{};
    unsigned_module.domain = CDSP_DOMAIN_ID;
    unsigned_module.enable = 1;
    const int result = remote_session_control(
        DSPRPC_CONTROL_UNSIGNED_MODULE, &unsigned_module,
        static_cast<std::uint32_t>(sizeof(unsigned_module)));
    if (result != 0) {
        throw std::runtime_error(
            "remote_session_control(unsigned module) failed: " +
            std::to_string(result));
    }
}

class Session {
public:
    Session()
    {
        enable_unsigned_module();
        const std::string domain_uri = uri();
        const int open_result = foc_bench_open(domain_uri.c_str(), &handle_);
        if (open_result != 0) {
            throw std::runtime_error(
                "foc_bench_open failed: " + std::to_string(open_result) +
                " (uri=" + domain_uri + ")");
        }
    }

    ~Session()
    {
        if (handle_ != 0U) {
            foc_bench_close(handle_);
        }
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    remote_handle64 handle() const { return handle_; }

private:
    remote_handle64 handle_ = 0U;
};

class HvxSession {
public:
    HvxSession()
    {
        enable_unsigned_module();
        const std::string domain_uri =
            std::string(hvx_bench_URI) + CDSP_DOMAIN;
        const int open_result = hvx_bench_open(domain_uri.c_str(), &handle_);
        if (open_result != 0) {
            throw std::runtime_error(
                "hvx_bench_open failed: " + std::to_string(open_result) +
                " (uri=" + domain_uri + ")");
        }
    }

    ~HvxSession()
    {
        if (handle_ != 0U) {
            hvx_bench_close(handle_);
        }
    }

    HvxSession(const HvxSession&) = delete;
    HvxSession& operator=(const HvxSession&) = delete;

    remote_handle64 handle() const { return handle_; }

private:
    remote_handle64 handle_ = 0U;
};

int run_echo(remote_handle64 handle)
{
    const std::uint64_t inputs[] = {
        0U, 1U, 0x0123456789ABCDEFULL, 0xFFFFFFFFFFFFFFFFULL,
        0xDEADBEEFCAFEBABEULL
    };

    constexpr std::size_t count = sizeof(inputs) / sizeof(inputs[0]);
    std::uint64_t outputs[count] = {};
    int failures = 0;

    for (std::size_t index = 0; index < count; ++index) {
        const int result = foc_bench_echo(handle, inputs[index], &outputs[index]);
        if (result != 0) {
            std::cerr << "echo RPC failed for input " << index
                      << ": " << result << '\n';
            return 1;
        }
        if (outputs[index] != ~inputs[index]) {
            std::cerr << "echo mismatch for input 0x" << std::hex << inputs[index]
                      << ": got 0x" << outputs[index] << std::dec << '\n';
            ++failures;
        }
    }

    std::cout << "{\"mode\":\"echo\",\"vectors\":" << count
              << ",\"failures\":" << failures
              << ",\"result\":\"" << (failures == 0 ? "PASS" : "FAIL")
              << "\"}\n";
    return failures == 0 ? 0 : 1;
}

void rpc_configure(remote_handle64 handle, const Config& config)
{
    const int result = foc_bench_configure(
        handle, config.kp_d_q15, config.ki_d_q15,
        config.kp_q_q15, config.ki_q_q15);
    if (result != 0) {
        throw std::runtime_error(
            "configure RPC failed: " + std::to_string(result));
    }
}

void rpc_reset(remote_handle64 handle)
{
    const int result = foc_bench_reset(handle);
    if (result != 0) {
        throw std::runtime_error(
            "reset RPC failed: " + std::to_string(result));
    }
}

int rpc_step(remote_handle64 handle, const Input& in, bool reset, Output& out)
{
    int id = 0;
    int iq = 0;
    int vd = 0;
    int vq = 0;
    int duty_a = 0;
    int duty_b = 0;
    int duty_c = 0;
    const int result = foc_bench_step(
        handle, in.ia_q15, in.ib_q15, in.id_ref_q15, in.iq_ref_q15,
        in.electrical_angle, reset ? 1 : 0,
        &id, &iq, &vd, &vq, &duty_a, &duty_b, &duty_c);
    if (result != 0) {
        return result;
    }
    out.id_q15 = static_cast<std::int16_t>(id);
    out.iq_q15 = static_cast<std::int16_t>(iq);
    out.vd_q15 = static_cast<std::int16_t>(vd);
    out.vq_q15 = static_cast<std::int16_t>(vq);
    out.duty_a_q15 = static_cast<std::uint16_t>(duty_a);
    out.duty_b_q15 = static_cast<std::uint16_t>(duty_b);
    out.duty_c_q15 = static_cast<std::uint16_t>(duty_c);
    return 0;
}

double abs_lsb(double a, double b)
{
    return std::fabs(a - b);
}

struct Accuracy {
    std::uint64_t values = 0U;
    std::uint64_t exact_mismatches = 0U;
    std::uint64_t dsp_rpc_failures = 0U;
    double max_abs = 0.0;
    double sum_abs = 0.0;
    std::uint64_t hist_le1 = 0U;
    std::uint64_t hist_le2 = 0U;
    std::uint64_t hist_le5 = 0U;
    std::uint64_t hist_le16 = 0U;
    std::uint64_t hist_le32 = 0U;
    std::uint64_t hist_gt32 = 0U;
};

void account(Accuracy& accuracy, double reference, double candidate)
{
    const double error = abs_lsb(reference, candidate);
    ++accuracy.values;
    accuracy.sum_abs += error;
    accuracy.max_abs = std::max(accuracy.max_abs, error);
    if (error <= 1.0) ++accuracy.hist_le1;
    if (error <= 2.0) ++accuracy.hist_le2;
    if (error <= 5.0) ++accuracy.hist_le5;
    if (error <= 16.0) ++accuracy.hist_le16;
    if (error <= 32.0) ++accuracy.hist_le32;
    if (error > 32.0) ++accuracy.hist_gt32;
}

bool same_output(const Output& a, const Output& b)
{
    return a.id_q15 == b.id_q15 && a.iq_q15 == b.iq_q15 &&
           a.vd_q15 == b.vd_q15 && a.vq_q15 == b.vq_q15 &&
           a.duty_a_q15 == b.duty_a_q15 && a.duty_b_q15 == b.duty_b_q15 &&
           a.duty_c_q15 == b.duty_c_q15;
}

Input make_bench_input(std::uint32_t index)
{
    return Input{
        static_cast<std::int16_t>((index * 7919U) & 0xFFFFU),
        static_cast<std::int16_t>((index * 3571U) & 0xFFFFU),
        0,
        10000,
        static_cast<std::uint16_t>(index & 0x3FFFU)
    };
}

std::uint64_t output_checksum(const Output& out)
{
    return static_cast<std::uint64_t>(static_cast<std::uint16_t>(out.id_q15)) +
           static_cast<std::uint64_t>(static_cast<std::uint16_t>(out.iq_q15)) +
           static_cast<std::uint64_t>(static_cast<std::uint16_t>(out.vd_q15)) +
           static_cast<std::uint64_t>(static_cast<std::uint16_t>(out.vq_q15)) +
           static_cast<std::uint64_t>(out.duty_a_q15) +
           static_cast<std::uint64_t>(out.duty_b_q15) +
           static_cast<std::uint64_t>(out.duty_c_q15);
}

int run_validate(remote_handle64 handle, const Options& options)
{
    std::vector<validation::foc::Case> cases = validation::foc::edge_cases();
    const std::vector<validation::foc::Case> random =
        validation::foc::random_cases(options.random_count, options.seed);
    cases.insert(cases.end(), random.begin(), random.end());
    if (options.sweep) {
        const std::vector<validation::foc::Case> sweep =
            validation::foc::angle_sweep();
        cases.insert(cases.end(), sweep.begin(), sweep.end());
    }

    // The main validation uses the same gains as the benchmark/config (P-only
    // by default). The dedicated high-ki run in scripts/run_foc_suite.sh
    // passes --ki-d/--ki-q to drive the integrator into its clamp. Configure
    // the DSP explicitly so it matches the CPU and FP32 oracles.
    Config validation_config = options.config;

    const int configure_result = foc_bench_configure(
        handle, validation_config.kp_d_q15, validation_config.ki_d_q15,
        validation_config.kp_q_q15, validation_config.ki_q_q15);
    if (configure_result != 0) {
        throw std::runtime_error(
            "configure RPC failed: " + std::to_string(configure_result));
    }

    Controller cpu(validation_config);
    validation::foc::Reference reference(validation_config);
    Accuracy accuracy;
    std::uint64_t dsp_cases = 0U;
    std::uint64_t exact_case_mismatches = 0U;
    std::uint64_t printed_mismatches = 0U;

    std::ofstream csv;
    if (!options.csv_path.empty()) {
        csv.open(options.csv_path);
        if (!csv) {
            throw std::runtime_error("cannot open CSV output: " + options.csv_path);
        }
        csv << "case,index,reset,ia_q15,ib_q15,id_ref_q15,iq_ref_q15,angle,"
               "dsp_id,cpu_id,ref_id,dsp_iq,cpu_iq,ref_iq,"
               "dsp_vd,cpu_vd,ref_vd,dsp_vq,cpu_vq,ref_vq,"
               "dsp_duty_a,cpu_duty_a,ref_duty_a,"
               "dsp_duty_b,cpu_duty_b,ref_duty_b,"
               "dsp_duty_c,cpu_duty_c,ref_duty_c\n";
        csv << std::setprecision(10);
    }

    for (std::size_t index = 0; index < cases.size(); ++index) {
        const validation::foc::Case& item = cases[index];
        if (item.reset) {
            cpu.reset();
            reference.reset();
        }

        Output dsp{};
        const int rpc_result = rpc_step(handle, item.input, item.reset, dsp);
        if (rpc_result != 0) {
            ++accuracy.dsp_rpc_failures;
            if (accuracy.dsp_rpc_failures <= 5U) {
                std::cerr << "step RPC failed at case " << index << " ("
                          << item.name << "): " << rpc_result << '\n';
            }
            continue;
        }
        ++dsp_cases;

        Output cpu_out{};
        cpu.step(item.input, cpu_out);

        validation::foc::ReferenceOutput ref{};
        reference.step(item.input, ref);

        if (!same_output(dsp, cpu_out)) {
            ++exact_case_mismatches;
            if (printed_mismatches < 5U) {
                std::cerr << "exact mismatch at case " << index << " ("
                          << item.name << "): dsp=(" << dsp.id_q15 << ','
                          << dsp.iq_q15 << ',' << dsp.vd_q15 << ',' << dsp.vq_q15
                          << ',' << dsp.duty_a_q15 << ',' << dsp.duty_b_q15 << ','
                          << dsp.duty_c_q15 << ") cpu=(" << cpu_out.id_q15 << ','
                          << cpu_out.iq_q15 << ',' << cpu_out.vd_q15 << ','
                          << cpu_out.vq_q15 << ',' << cpu_out.duty_a_q15 << ','
                          << cpu_out.duty_b_q15 << ',' << cpu_out.duty_c_q15
                          << ")\n";
            }
            ++printed_mismatches;
        }

        account(accuracy, ref.id_q15, cpu_out.id_q15);
        account(accuracy, ref.iq_q15, cpu_out.iq_q15);
        account(accuracy, ref.vd_q15, cpu_out.vd_q15);
        account(accuracy, ref.vq_q15, cpu_out.vq_q15);
        account(accuracy, ref.duty_a_q15, cpu_out.duty_a_q15);
        account(accuracy, ref.duty_b_q15, cpu_out.duty_b_q15);
        account(accuracy, ref.duty_c_q15, cpu_out.duty_c_q15);

        if (csv) {
            csv << item.name << ',' << index << ',' << (item.reset ? 1 : 0)
                << ',' << item.input.ia_q15 << ',' << item.input.ib_q15
                << ',' << item.input.id_ref_q15 << ',' << item.input.iq_ref_q15
                << ',' << item.input.electrical_angle
                << ',' << dsp.id_q15 << ',' << cpu_out.id_q15 << ',' << ref.id_q15
                << ',' << dsp.iq_q15 << ',' << cpu_out.iq_q15 << ',' << ref.iq_q15
                << ',' << dsp.vd_q15 << ',' << cpu_out.vd_q15 << ',' << ref.vd_q15
                << ',' << dsp.vq_q15 << ',' << cpu_out.vq_q15 << ',' << ref.vq_q15
                << ',' << dsp.duty_a_q15 << ',' << cpu_out.duty_a_q15 << ',' << ref.duty_a_q15
                << ',' << dsp.duty_b_q15 << ',' << cpu_out.duty_b_q15 << ',' << ref.duty_b_q15
                << ',' << dsp.duty_c_q15 << ',' << cpu_out.duty_c_q15 << ',' << ref.duty_c_q15
                << '\n';
        }
    }

    const double mean_abs = accuracy.values == 0U
        ? 0.0
        : accuracy.sum_abs / static_cast<double>(accuracy.values);

    std::cout << std::setprecision(10)
              << "{\"mode\":\"validate\""
              << ",\"cases\":" << cases.size()
              << ",\"kp_d\":" << validation_config.kp_d_q15
              << ",\"ki_d\":" << validation_config.ki_d_q15
              << ",\"kp_q\":" << validation_config.kp_q_q15
              << ",\"ki_q\":" << validation_config.ki_q_q15
              << ",\"dsp_cases\":" << dsp_cases
              << ",\"dsp_rpc_failures\":" << accuracy.dsp_rpc_failures
              << ",\"exact_case_mismatches\":" << exact_case_mismatches
              << ",\"fp32_values\":" << accuracy.values
              << ",\"fp32_max_abs_lsb\":" << accuracy.max_abs
              << ",\"fp32_mean_abs_lsb\":" << mean_abs
              << ",\"hist_le1\":" << accuracy.hist_le1
              << ",\"hist_le2\":" << accuracy.hist_le2
              << ",\"hist_le5\":" << accuracy.hist_le5
              << ",\"hist_le16\":" << accuracy.hist_le16
              << ",\"hist_le32\":" << accuracy.hist_le32
              << ",\"hist_gt32\":" << accuracy.hist_gt32
              << ",\"csv\":\"" << options.csv_path << "\""
              << ",\"result\":\""
              << (accuracy.dsp_rpc_failures == 0U && exact_case_mismatches == 0U
                      ? "PASS" : "FAIL")
              << "\"}\n";

    return (accuracy.dsp_rpc_failures == 0U && exact_case_mismatches == 0U)
        ? 0 : 1;
}

struct BenchCall {
    std::uint64_t cycles[kMaxRuns] = {};
    std::uint64_t checksum = 0U;
    std::uint64_t status = 0U;
    double wall_us = 0.0;
};

BenchCall call_bench(remote_handle64 handle, const Options& options, int iterations)
{
    BenchCall call;
    const auto wall_start = std::chrono::steady_clock::now();
    const int result = foc_bench_bench(
        handle, options.warmup, options.runs, iterations,
        options.config.kp_d_q15, options.config.ki_d_q15,
        options.config.kp_q_q15, options.config.ki_q_q15,
        &call.cycles[0], &call.cycles[1], &call.cycles[2],
        &call.cycles[3], &call.cycles[4],
        &call.checksum, &call.status);
    const auto wall_end = std::chrono::steady_clock::now();
    if (result != 0) {
        throw std::runtime_error("bench RPC failed: " + std::to_string(result));
    }
    call.wall_us = std::chrono::duration<double, std::micro>(
        wall_end - wall_start).count();
    return call;
}

std::array<double, 4> stats(const std::vector<double>& values)
{
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double minimum = sorted.front();
    const double maximum = sorted.back();
    const std::size_t middle = sorted.size() / 2U;
    const double med = (sorted.size() % 2U) == 0U
        ? 0.5 * (sorted[middle - 1U] + sorted[middle])
        : sorted[middle];
    double mean = 0.0;
    for (const double value : sorted) {
        mean += value;
    }
    mean /= static_cast<double>(sorted.size());
    return std::array<double, 4>{minimum, med, mean, maximum};
}

int run_cpu(const Options& options)
{
    Controller controller(options.config);
    Output out{};

    for (int index = 0; index < options.warmup; ++index) {
        controller.step(make_bench_input(static_cast<std::uint32_t>(index)), out);
    }

    // Let schedutil raise the CPU clock before the timed region. Without this,
    // a short 16k-step loop measures the idle clock, not the steady-state core.
    for (int index = 0; index < options.cpu_warmup; ++index) {
        controller.step(make_bench_input(static_cast<std::uint32_t>(index) & 0x3FFFU),
                        out);
    }

    std::vector<double> ns_per_step;
    ns_per_step.reserve(static_cast<std::size_t>(options.runs));
    std::vector<double> total_ns;
    total_ns.reserve(static_cast<std::size_t>(options.runs));
    std::uint64_t checksum = 0U;

    for (int run = 0; run < options.runs; ++run) {
        controller.reset();
        std::uint64_t sum = 0U;
        const auto start = std::chrono::steady_clock::now();
        for (int index = 0; index < options.iterations; ++index) {
            controller.step(make_bench_input(static_cast<std::uint32_t>(index)), out);
            sum += output_checksum(out);
        }
        const auto end = std::chrono::steady_clock::now();
        const double elapsed_ns =
            std::chrono::duration<double, std::nano>(end - start).count();
        total_ns.push_back(elapsed_ns);
        ns_per_step.push_back(elapsed_ns / static_cast<double>(options.iterations));
        checksum = sum;
    }

    const auto ns_stats = stats(ns_per_step);
    const auto total_stats = stats(total_ns);

    std::cout << std::setprecision(10)
              << "{\"mode\":\"cpu\""
              << ",\"warmup\":" << options.warmup
              << ",\"runs\":" << options.runs
              << ",\"iterations\":" << options.iterations
              << ",\"ns_per_step\":[";
    for (std::size_t index = 0; index < ns_per_step.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << ns_per_step[index];
    }
    std::cout << "],\"total_ns\":[";
    for (std::size_t index = 0; index < total_ns.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << total_ns[index];
    }
    std::cout << "]"
              << ",\"min_ns_per_step\":" << ns_stats[0]
              << ",\"median_ns_per_step\":" << ns_stats[1]
              << ",\"mean_ns_per_step\":" << ns_stats[2]
              << ",\"max_ns_per_step\":" << ns_stats[3]
              << ",\"min_total_ns\":" << total_stats[0]
              << ",\"median_total_ns\":" << total_stats[1]
              << ",\"checksum\":" << checksum
              << ",\"result\":\"PASS\"}\n";

    return 0;
}

class PerfGuard {
public:
    PerfGuard(remote_handle64 handle, bool enable)
        : handle_(handle), active_(enable)
    {
        if (active_) {
            const int result = foc_bench_set_perf(handle_, 1);
            if (result != 0) {
                throw std::runtime_error(
                    "failed to request CDSP performance mode: " +
                    std::to_string(result));
            }
        }
    }

    ~PerfGuard()
    {
        if (active_) {
            foc_bench_set_perf(handle_, 0);
        }
    }

    PerfGuard(const PerfGuard&) = delete;
    PerfGuard& operator=(const PerfGuard&) = delete;

private:
    remote_handle64 handle_;
    bool active_;
};

std::uint64_t monotonic_raw_ns()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

double read_thermal_c()
{
    double maximum = 0.0;
    for (int zone = 0; zone < 32; ++zone) {
        std::ifstream file(
            "/sys/class/thermal/thermal_zone" + std::to_string(zone) +
            "/temp");
        if (!file) {
            continue;
        }
        double millidegrees = 0.0;
        if (file >> millidegrees && millidegrees > maximum) {
            maximum = millidegrees;
        }
    }
    return maximum / 1000.0;
}

struct LatencySummary {
    std::size_t n = 0U;
    double min = 0.0;
    double median = 0.0;
    double mean = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

LatencySummary latency_stats(const std::vector<double>& values)
{
    if (values.empty()) {
        throw std::invalid_argument("empty latency series");
    }
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&sorted](double fraction) {
        std::size_t index = static_cast<std::size_t>(
            std::ceil(fraction * static_cast<double>(sorted.size())));
        if (index == 0U) {
            index = 1U;
        }
        if (index > sorted.size()) {
            index = sorted.size();
        }
        return sorted[index - 1U];
    };
    LatencySummary summary;
    summary.n = sorted.size();
    summary.min = sorted.front();
    summary.max = sorted.back();
    const std::size_t middle = sorted.size() / 2U;
    summary.median = (sorted.size() % 2U) == 0U
        ? 0.5 * (sorted[middle - 1U] + sorted[middle])
        : sorted[middle];
    summary.p90 = percentile(0.90);
    summary.p95 = percentile(0.95);
    summary.p99 = percentile(0.99);
    double total = 0.0;
    for (const double value : sorted) {
        total += value;
    }
    summary.mean = total / static_cast<double>(sorted.size());
    return summary;
}

void print_latency_json(const char* name, const LatencySummary& s)
{
    std::cout << "\"" << name << "\":{"
              << "\"n\":" << s.n
              << ",\"min_ns\":" << s.min
              << ",\"median_ns\":" << s.median
              << ",\"mean_ns\":" << s.mean
              << ",\"p90_ns\":" << s.p90
              << ",\"p95_ns\":" << s.p95
              << ",\"p99_ns\":" << s.p99
              << ",\"max_ns\":" << s.max
              << "}";
}

int run_rpc_latency(remote_handle64 handle, const Options& options)
{
    if (options.rpc_count <= 0) {
        throw std::invalid_argument("rpc-count must be positive");
    }

    rpc_configure(handle, options.config);
    rpc_reset(handle);
    const Input input = make_bench_input(0U);
    Output out{};
    std::uint64_t echo_output = 0U;

    for (int i = 0; i < 100; ++i) {
        (void)foc_bench_echo(handle, static_cast<std::uint64_t>(i),
                             &echo_output);
        (void)rpc_step(handle, input, false, out);
    }

    std::vector<double> echo_ns;
    std::vector<double> step_ns;
    echo_ns.reserve(static_cast<std::size_t>(options.rpc_count));
    step_ns.reserve(static_cast<std::size_t>(options.rpc_count));

    for (int i = 0; i < options.rpc_count; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const int result = foc_bench_echo(
            handle, static_cast<std::uint64_t>(i), &echo_output);
        const auto end = std::chrono::steady_clock::now();
        if (result != 0) {
            throw std::runtime_error(
                "echo RPC failed: " + std::to_string(result));
        }
        echo_ns.push_back(std::chrono::duration<double, std::nano>(
            end - start).count());
    }

    for (int i = 0; i < options.rpc_count; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const int result = rpc_step(handle, input, false, out);
        const auto end = std::chrono::steady_clock::now();
        if (result != 0) {
            throw std::runtime_error(
                "step RPC failed: " + std::to_string(result));
        }
        step_ns.push_back(std::chrono::duration<double, std::nano>(
            end - start).count());
    }

    const LatencySummary echo = latency_stats(echo_ns);
    const LatencySummary step = latency_stats(step_ns);
    std::cout << "{\"mode\":\"rpc-latency\",\"count\":" << options.rpc_count
              << ",";
    print_latency_json("echo", echo);
    std::cout << ",";
    print_latency_json("step", step);
    std::cout << "}\n";
    return 0;
}

int run_rpc_scaling(remote_handle64 handle, const Options& options)
{
    if (options.rpc_repeats <= 0) {
        throw std::invalid_argument("rpc-repeats must be positive");
    }

    rpc_configure(handle, options.config);

    // Page in the bench entry point before measuring the small sizes.
    {
        std::uint64_t warm_cycles[kMaxRuns] = {};
        std::uint64_t warm_checksum = 0U;
        std::uint64_t warm_status = 0U;
        const int result = foc_bench_bench(
            handle, 256, 1, 16384,
            options.config.kp_d_q15, options.config.ki_d_q15,
            options.config.kp_q_q15, options.config.ki_q_q15,
            &warm_cycles[0], &warm_cycles[1], &warm_cycles[2],
            &warm_cycles[3], &warm_cycles[4],
            &warm_checksum, &warm_status);
        if (result != 0 || warm_status != 0U) {
            throw std::runtime_error("rpc-scaling warm-up failed");
        }
    }

    std::ofstream rpc_csv;
    if (!options.rpc_csv_path.empty()) {
        rpc_csv.open(options.rpc_csv_path,
                     std::ios::out | std::ios::trunc);
        if (!rpc_csv) {
            throw std::runtime_error(
                "cannot open " + options.rpc_csv_path);
        }
        rpc_csv << "steps,repeat,wall_us\n";
    }

    const int sizes[] = {1, 16, 256, 4096, 16384};
    std::cout << "{\"mode\":\"rpc-scaling\",\"repeats\":"
              << options.rpc_repeats << ",\"sizes\":[";
    bool first = true;
    for (const int steps : sizes) {
        std::vector<double> wall_us;
        wall_us.reserve(static_cast<std::size_t>(options.rpc_repeats));
        for (int repeat = 0; repeat < options.rpc_repeats; ++repeat) {
            std::uint64_t cycles[kMaxRuns] = {};
            std::uint64_t checksum = 0U;
            std::uint64_t status = 0U;
            const auto start = std::chrono::steady_clock::now();
            const int result = foc_bench_bench(
                handle, 0, 1, steps,
                options.config.kp_d_q15, options.config.ki_d_q15,
                options.config.kp_q_q15, options.config.ki_q_q15,
                &cycles[0], &cycles[1], &cycles[2],
                &cycles[3], &cycles[4],
                &checksum, &status);
            const auto end = std::chrono::steady_clock::now();
            if (result != 0 || status != 0U) {
                throw std::runtime_error(
                    "rpc-scaling bench failed: " + std::to_string(result) +
                    "/" + std::to_string(status));
            }
            wall_us.push_back(std::chrono::duration<double, std::micro>(
                end - start).count());
            if (rpc_csv) {
                rpc_csv << steps << ',' << repeat << ','
                        << wall_us.back() << '\n';
            }
        }
        const LatencySummary summary = latency_stats(wall_us);
        if (!first) {
            std::cout << ",";
        }
        first = false;
        std::cout << "{\"steps\":" << steps
                  << ",\"median_us\":" << summary.median
                  << ",\"min_us\":" << summary.min
                  << ",\"p90_us\":" << summary.p90
                  << ",\"p95_us\":" << summary.p95
                  << ",\"p99_us\":" << summary.p99
                  << ",\"max_us\":" << summary.max
                  << ",\"median_ns_per_step\":"
                  << (summary.median * 1000.0 / static_cast<double>(steps))
                  << "}";
    }
    std::cout << "]}\n";
    return 0;
}

std::ofstream open_rt_csv(const Options& options)
{
    if (options.rt_csv_path.empty()) {
        throw std::invalid_argument("--rt-csv PATH is required");
    }
    std::ofstream csv(options.rt_csv_path, std::ios::out | std::ios::trunc);
    if (!csv) {
        throw std::runtime_error("cannot open " + options.rt_csv_path);
    }
    csv << "condition,round,config,index,value,unit,steps,temp_c,time_ns,warmup\n";
    return csv;
}

void write_rt_row(std::ofstream& csv, const Options& options, int index,
                  std::uint64_t value, const char* unit, int steps,
                  double temperature, std::uint64_t time_ns, bool warmup)
{
    csv << options.rt_condition << ',' << options.rt_round << ','
        << options.rt_config << ',' << index << ',' << value << ','
        << unit << ',' << steps << ',' << temperature << ',' << time_ns
        << ',' << (warmup ? 1 : 0) << '\n';
}

int run_rt_dsp(remote_handle64 handle, const Options& options)
{
    if (options.rt_samples <= 0 || options.rt_samples > 100000 ||
        options.rt_batch <= 0 || options.rt_batch > 1024) {
        throw std::invalid_argument("invalid rt samples/batch");
    }
    std::ofstream csv = open_rt_csv(options);
    std::uint64_t total_cycles = 0U;
    std::uint64_t checksum = 0U;
    std::uint64_t status = 0U;
    const int result = foc_bench_sampler_run(
        handle, options.rt_batch, options.rt_samples,
        options.config.kp_d_q15, options.config.ki_d_q15,
        options.config.kp_q_q15, options.config.ki_q_q15,
        &total_cycles, &checksum, &status);
    if (result != 0 || status != 0U) {
        throw std::runtime_error(
            "sampler_run failed: " + std::to_string(result) + "/" +
            std::to_string(status));
    }
    int count = 0;
    if (foc_bench_sampler_count(handle, &count) != 0 ||
        count != options.rt_samples) {
        throw std::runtime_error("sampler_count mismatch");
    }

    double temperature = read_thermal_c();
    for (int index = 0; index < count; ++index) {
        std::uint64_t cycles = 0U;
        std::uint64_t steps = 0U;
        if (foc_bench_sampler_get(handle, index, &cycles, &steps) != 0) {
            throw std::runtime_error("sampler_get failed");
        }
        if ((index % 1000) == 0) {
            temperature = read_thermal_c();
        }
        write_rt_row(csv, options, index, cycles, "cycles",
                     static_cast<int>(steps), temperature, monotonic_raw_ns(),
                     index < options.rt_warmup);
    }
    std::cout << "{\"mode\":\"rt-dsp\",\"samples\":" << count
              << ",\"batch\":" << options.rt_batch
              << ",\"total_cycles\":" << total_cycles
              << ",\"checksum\":" << checksum
              << ",\"status\":" << status << "}\n";
    return 0;
}

int run_rt_step(remote_handle64 handle, const Options& options)
{
    if (options.rt_samples <= 0) {
        throw std::invalid_argument("invalid rt samples");
    }
    std::ofstream csv = open_rt_csv(options);
    rpc_configure(handle, options.config);
    rpc_reset(handle);
    const Input input = make_bench_input(0U);
    Output out{};
    double temperature = read_thermal_c();
    for (int index = 0; index < options.rt_samples; ++index) {
        const std::uint64_t start = monotonic_raw_ns();
        const int result = rpc_step(handle, input, false, out);
        const std::uint64_t end = monotonic_raw_ns();
        if (result != 0) {
            throw std::runtime_error("rt step failed");
        }
        if ((index % 1000) == 0) {
            temperature = read_thermal_c();
        }
        write_rt_row(csv, options, index, end - start, "ns", 1,
                     temperature, end, index < options.rt_warmup);
    }
    std::cout << "{\"mode\":\"rt-step\",\"samples\":" << options.rt_samples
              << "}\n";
    return 0;
}

int run_rt_bench(remote_handle64 handle, const Options& options)
{
    if (options.rt_samples <= 0 || options.rt_batch <= 0) {
        throw std::invalid_argument("invalid rt samples/batch");
    }
    std::ofstream csv = open_rt_csv(options);
    rpc_configure(handle, options.config);
    double temperature = read_thermal_c();
    for (int index = 0; index < options.rt_samples; ++index) {
        std::uint64_t cycles[kMaxRuns] = {};
        std::uint64_t checksum = 0U;
        std::uint64_t status = 0U;
        const std::uint64_t start = monotonic_raw_ns();
        const int result = foc_bench_bench(
            handle, 0, 1, options.rt_batch,
            options.config.kp_d_q15, options.config.ki_d_q15,
            options.config.kp_q_q15, options.config.ki_q_q15,
            &cycles[0], &cycles[1], &cycles[2], &cycles[3], &cycles[4],
            &checksum, &status);
        const std::uint64_t end = monotonic_raw_ns();
        if (result != 0 || status != 0U) {
            throw std::runtime_error("rt bench failed");
        }
        if ((index % 1000) == 0) {
            temperature = read_thermal_c();
        }
        write_rt_row(csv, options, index, end - start, "ns",
                     options.rt_batch, temperature, end,
                     index < options.rt_warmup);
    }
    std::cout << "{\"mode\":\"rt-bench\",\"samples\":" << options.rt_samples
              << ",\"batch\":" << options.rt_batch << "}\n";
    return 0;
}

int run_rt_cpu(const Options& options)
{
    if (options.rt_samples <= 0 || options.rt_batch <= 0) {
        throw std::invalid_argument("invalid rt samples/batch");
    }
    std::ofstream csv = open_rt_csv(options);
    Controller controller(options.config);
    Output out{};
    double temperature = read_thermal_c();
    for (int index = 0; index < options.rt_samples; ++index) {
        const std::uint64_t start = monotonic_raw_ns();
        for (int step = 0; step < options.rt_batch; ++step) {
            const std::uint32_t input =
                static_cast<std::uint32_t>(index) *
                    static_cast<std::uint32_t>(options.rt_batch) +
                static_cast<std::uint32_t>(step);
            controller.step(make_bench_input(input), out);
        }
        const std::uint64_t end = monotonic_raw_ns();
        if ((index % 1000) == 0) {
            temperature = read_thermal_c();
        }
        write_rt_row(csv, options, index, end - start, "ns",
                     options.rt_batch, temperature, end,
                     index < options.rt_warmup);
    }
    std::cout << "{\"mode\":\"rt-cpu\",\"samples\":" << options.rt_samples
              << ",\"batch\":" << options.rt_batch << "}\n";
    return 0;
}

int run_rt_timer_cpu(const Options& options)
{
    const int iterations = options.rt_samples > 0 ? options.rt_samples : 1000000;
    volatile std::uint64_t sink = 0U;
    const std::uint64_t start = monotonic_raw_ns();
    for (int i = 0; i < iterations; ++i) {
        sink += monotonic_raw_ns();
    }
    const std::uint64_t end = monotonic_raw_ns();
    (void)sink;
    std::cout << "{\"mode\":\"rt-timer-cpu\",\"calls\":" << iterations
              << ",\"total_ns\":" << (end - start)
              << ",\"ns_per_call\":"
              << (static_cast<double>(end - start) / iterations)
              << "}\n";
    return 0;
}

int run_rt_timer_dsp(remote_handle64 handle, const Options& options)
{
    const int iterations = options.rt_samples > 0 ? options.rt_samples : 1000000;
    std::uint64_t cycles = 0U;
    if (foc_bench_timer_overhead(handle, iterations, &cycles) != 0) {
        throw std::runtime_error("timer_overhead failed");
    }
    std::cout << "{\"mode\":\"rt-timer-dsp\",\"calls\":" << iterations
              << ",\"total_cycles\":" << cycles
              << ",\"cycles_per_call\":"
              << (static_cast<double>(cycles) / iterations)
              << "}\n";
    return 0;
}

int run_bench(remote_handle64 handle, const Options& options)
{
    if (options.runs <= 0 || options.runs > kMaxRuns) {
        throw std::invalid_argument("runs must be in 1..5");
    }

    PerfGuard perf_guard(handle, options.dsp_perf);

    // First touch of the bench entry point pays for module paging/relocation.
    // Measure a single-step call so the same fixed RPC cost can be removed from
    // the timed call. Its measurement outputs are discarded.
    double preflight_wall_us = 0.0;
    std::uint64_t preflight_cycles = 0U;
    std::uint64_t preflight_checksum = 0U;
    std::uint64_t preflight_status = 0U;
    {
        std::uint64_t warm_cycles[kMaxRuns] = {};
        std::uint64_t warm_checksum = 0U;
        std::uint64_t warm_status = 0U;
        const auto preflight_start = std::chrono::steady_clock::now();
        const int warm_result = foc_bench_bench(
            handle, 0, 1, 1,
            options.config.kp_d_q15, options.config.ki_d_q15,
            options.config.kp_q_q15, options.config.ki_q_q15,
            &warm_cycles[0], &warm_cycles[1], &warm_cycles[2],
            &warm_cycles[3], &warm_cycles[4],
            &warm_checksum, &warm_status);
        const auto preflight_end = std::chrono::steady_clock::now();
        if (warm_result != 0) {
            std::cerr << "bench preflight RPC failed: " << warm_result << '\n';
            return 1;
        }
        preflight_wall_us = std::chrono::duration<double, std::micro>(
            preflight_end - preflight_start).count();
        preflight_cycles = warm_cycles[0];
        preflight_checksum = warm_checksum;
        preflight_status = warm_status;
    }

    const BenchCall measured = call_bench(handle, options, options.iterations);

    // qurt_timer_get_ticks() returns zero in this unsigned PD. Derive the
    // effective core clock from the timed call after removing the fixed RPC
    // cost measured above. The result describes this call's average clock;
    // the CDSP clock is load-dependent, so cycles remain the primary metric.
    double effective_clock_hz = options.clock_hz;
    std::string clock_source = "fallback";
    double compute_us = 0.0;

    double total_cycles = 0.0;
    for (int run = 0; run < options.runs; ++run) {
        total_cycles += static_cast<double>(measured.cycles[run]);
    }

    if (options.calibrate && measured.wall_us > preflight_wall_us + 1.0) {
        compute_us = measured.wall_us - preflight_wall_us;
        // One RPC contains all `runs` repetitions, so the wall time covers
        // total_cycles, not the median of a single repetition. Dividing the
        // per-run median by the whole-call wall understated the clock by
        // `runs` (5x in the final run).
        effective_clock_hz = total_cycles / (compute_us * 1.0e-6);
        clock_source = "rpc_overhead_corrected";
    } else {
        compute_us = total_cycles / effective_clock_hz * 1.0e6;
    }

    const double effective_clock_hz_no_correction =
        (measured.wall_us > 0.0)
        ? total_cycles / (measured.wall_us * 1.0e-6)
        : 0.0;

    std::vector<double> per_step_cycles;
    std::vector<double> per_step_ns;
    std::vector<double> per_step_ns_no_correction;
    per_step_cycles.reserve(static_cast<std::size_t>(options.runs));
    per_step_ns.reserve(static_cast<std::size_t>(options.runs));
    per_step_ns_no_correction.reserve(static_cast<std::size_t>(options.runs));
    for (int run = 0; run < options.runs; ++run) {
        const double iterations = static_cast<double>(options.iterations);
        per_step_cycles.push_back(
            static_cast<double>(measured.cycles[run]) / iterations);
        per_step_ns.push_back(
            static_cast<double>(measured.cycles[run]) / iterations /
            effective_clock_hz * 1.0e9);
        per_step_ns_no_correction.push_back(
            (effective_clock_hz_no_correction > 0.0)
            ? static_cast<double>(measured.cycles[run]) / iterations /
              effective_clock_hz_no_correction * 1.0e9
            : 0.0);
    }

    const auto cycles_stats = stats(per_step_cycles);
    const auto ns_stats = stats(per_step_ns);
    const auto ns_no_correction_stats = stats(per_step_ns_no_correction);
    const double rpc_overhead_us = measured.wall_us - compute_us;
    const double preflight_fraction = (measured.wall_us > 0.0)
        ? preflight_wall_us / measured.wall_us
        : 0.0;

    std::cout << std::setprecision(10)
              << "{\"mode\":\"bench\""
              << ",\"warmup\":" << options.warmup
              << ",\"runs\":" << options.runs
              << ",\"iterations\":" << options.iterations
              << ",\"fallback_clock_hz\":" << options.clock_hz
              << ",\"effective_clock_hz\":" << effective_clock_hz
              << ",\"clock_source\":\"" << clock_source << "\""
              << ",\"dsp_perf\":" << (options.dsp_perf ? "true" : "false")
              << ",\"preflight_wall_us\":" << preflight_wall_us
              << ",\"preflight_cycles\":" << preflight_cycles
              << ",\"preflight_checksum\":" << preflight_checksum
              << ",\"preflight_status\":" << preflight_status
              << ",\"preflight_fraction\":" << preflight_fraction
              << ",\"effective_clock_hz_no_correction\":"
              << effective_clock_hz_no_correction
              << ",\"cycles\":[";
    for (int run = 0; run < options.runs; ++run) {
        if (run != 0) std::cout << ',';
        std::cout << measured.cycles[run];
    }
    std::cout << "],\"per_step_cycles\":[";
    for (std::size_t index = 0; index < per_step_cycles.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << per_step_cycles[index];
    }
    std::cout << "],\"per_step_ns\":[";
    for (std::size_t index = 0; index < per_step_ns.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << per_step_ns[index];
    }
    std::cout << "],\"per_step_ns_no_correction\":[";
    for (std::size_t index = 0;
         index < per_step_ns_no_correction.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << per_step_ns_no_correction[index];
    }
    std::cout << "]"
              << ",\"min_cycles_per_step\":" << cycles_stats[0]
              << ",\"median_cycles_per_step\":" << cycles_stats[1]
              << ",\"mean_cycles_per_step\":" << cycles_stats[2]
              << ",\"max_cycles_per_step\":" << cycles_stats[3]
              << ",\"min_ns_per_step\":" << ns_stats[0]
              << ",\"median_ns_per_step\":" << ns_stats[1]
              << ",\"mean_ns_per_step\":" << ns_stats[2]
              << ",\"max_ns_per_step\":" << ns_stats[3]
              << ",\"median_ns_per_step_no_correction\":"
              << ns_no_correction_stats[1]
              << ",\"rpc_wall_us\":" << measured.wall_us
              << ",\"rpc_compute_us\":" << compute_us
              << ",\"rpc_overhead_us\":" << rpc_overhead_us
              << ",\"checksum\":" << measured.checksum
              << ",\"status\":" << measured.status
              << ",\"result\":\"" << (measured.status == 0U ? "PASS" : "FAIL")
              << "\"}\n";

    return measured.status == 0U ? 0 : 1;
}

struct HvxCall {
    std::uint64_t cycles[kMaxRuns] = {};
    std::uint64_t checksum = 0U;
    int mismatches = 0;
    std::uint64_t status = 0U;
};

struct CpuVecAddCall {
    std::vector<double> ns;
    std::uint64_t checksum = 0U;
};

__attribute__((noinline, optimize("no-tree-vectorize")))
void cpu_vecadd_scalar_kernel(std::uint16_t* acc, const std::uint16_t* sum,
                              int lanes, int iterations)
{
    for (int i = 0; i < iterations; ++i) {
        for (int lane = 0; lane < lanes; ++lane) {
            acc[lane] = static_cast<std::uint16_t>(acc[lane] + sum[lane]);
        }
    }
}

__attribute__((noinline))
void cpu_vecadd_neon_kernel(std::uint16_t* acc, const std::uint16_t* sum,
                            int lanes, int iterations)
{
#if defined(__aarch64__)
    for (int i = 0; i < iterations; ++i) {
        for (int lane = 0; lane < lanes; lane += 8) {
            const uint16x8_t va = vld1q_u16(acc + lane);
            const uint16x8_t vs = vld1q_u16(sum + lane);
            vst1q_u16(acc + lane, vaddq_u16(va, vs));
        }
    }
#else
    cpu_vecadd_scalar_kernel(acc, sum, lanes, iterations);
#endif
}

CpuVecAddCall cpu_vecadd(bool neon, int lanes, int iterations, int runs)
{
    std::vector<std::uint16_t> sum(static_cast<std::size_t>(lanes));
    std::vector<std::uint16_t> acc(static_cast<std::size_t>(lanes));
    for (int lane = 0; lane < lanes; ++lane) {
        sum[static_cast<std::size_t>(lane)] = static_cast<std::uint16_t>(
            (lane % 7) + (lane % 5));
    }

    CpuVecAddCall call;
    call.ns.reserve(static_cast<std::size_t>(runs));
    for (int run = 0; run < runs; ++run) {
        std::fill(acc.begin(), acc.end(), static_cast<std::uint16_t>(0U));
        const auto start = std::chrono::steady_clock::now();
        if (neon) {
            cpu_vecadd_neon_kernel(acc.data(), sum.data(), lanes, iterations);
        } else {
            cpu_vecadd_scalar_kernel(acc.data(), sum.data(), lanes, iterations);
        }
        const auto end = std::chrono::steady_clock::now();
        call.ns.push_back(std::chrono::duration<double, std::nano>(
            end - start).count());
        std::uint64_t checksum = 0U;
        for (const std::uint16_t value : acc) {
            checksum += value;
        }
        call.checksum = checksum;
    }
    return call;
}

void print_cpu_vecadd_json(const char* name, const CpuVecAddCall& call,
                           int lanes, int iterations)
{
    const auto values = stats(call.ns);
    const double ns_per_iteration =
        values[1] / static_cast<double>(iterations);
    const double ns_per_element =
        ns_per_iteration / static_cast<double>(lanes);
    std::cout << "\"" << name << "\":{\"runs_ns\":[";
    for (std::size_t i = 0; i < call.ns.size(); ++i) {
        if (i != 0U) std::cout << ',';
        std::cout << call.ns[i];
    }
    std::cout << "],\"median_ns\":" << values[1]
              << ",\"median_ns_per_iteration\":" << ns_per_iteration
              << ",\"ns_per_element\":" << ns_per_element
              << ",\"checksum\":" << call.checksum << "}";
}

HvxCall call_hvx(remote_handle64 handle, int mode, int lanes,
                 int iterations, int runs)
{
    HvxCall call;
    const int result = hvx_bench_run(
        handle, mode, lanes, iterations, runs,
        &call.cycles[0], &call.cycles[1], &call.cycles[2],
        &call.cycles[3], &call.cycles[4],
        &call.checksum, &call.mismatches, &call.status);
    if (result != 0) {
        throw std::runtime_error(
            "hvx_bench RPC failed: " + std::to_string(result));
    }
    return call;
}

void print_hvx_stats(const char* name, const HvxCall& call, int lanes,
                     int iterations, int runs)
{
    std::vector<double> per_iteration;
    per_iteration.reserve(static_cast<std::size_t>(runs));
    for (int run = 0; run < runs; ++run) {
        per_iteration.push_back(
            static_cast<double>(call.cycles[run]) /
            static_cast<double>(iterations));
    }
    const auto values = stats(per_iteration);
    std::cout << "\"" << name << "\":{\"cycles\":[";
    for (int run = 0; run < runs; ++run) {
        if (run != 0) std::cout << ',';
        std::cout << call.cycles[run];
    }
    std::cout << "],\"cycles_per_iteration\":[";
    for (int run = 0; run < runs; ++run) {
        if (run != 0) std::cout << ',';
        std::cout << per_iteration[static_cast<std::size_t>(run)];
    }
    std::cout << "],\"min_cycles_per_iteration\":" << values[0]
              << ",\"median_cycles_per_iteration\":" << values[1]
              << ",\"mean_cycles_per_iteration\":" << values[2]
              << ",\"max_cycles_per_iteration\":" << values[3]
              << ",\"cycles_per_element\":"
              << (values[1] / static_cast<double>(lanes))
              << ",\"derived_ns_per_element_at_1_17GHz\":"
              << (values[1] / static_cast<double>(lanes) /
                  (kDirectClockHz * 1.0e-9))
              << ",\"checksum\":" << call.checksum
              << ",\"mismatches\":" << call.mismatches
              << ",\"status\":" << call.status << "}";
}

int run_hvx(const Options& options)
{
    if (options.hvx_runs <= 0 || options.hvx_runs > kMaxRuns ||
        options.hvx_iterations <= 0) {
        throw std::invalid_argument(
            "hvx runs must be 1..5 and iterations must be positive");
    }
    if (options.hvx_lanes < 64 || options.hvx_lanes > 4096 ||
        (options.hvx_lanes % 64) != 0) {
        throw std::invalid_argument(
            "hvx-lanes must be a multiple of 64 in 64..4096");
    }

    HvxSession session;
    const HvxCall scalar = call_hvx(
        session.handle(), 0, options.hvx_lanes,
        options.hvx_iterations, options.hvx_runs);
    const HvxCall hvx = call_hvx(
        session.handle(), 1, options.hvx_lanes,
        options.hvx_iterations, options.hvx_runs);
    const HvxCall hvx_multi = call_hvx(
        session.handle(), 2, options.hvx_lanes,
        options.hvx_iterations, options.hvx_runs);
    const CpuVecAddCall cpu_scalar = cpu_vecadd(
        false, options.hvx_lanes, options.hvx_iterations, options.hvx_runs);
    const CpuVecAddCall cpu_neon = cpu_vecadd(
        true, options.hvx_lanes, options.hvx_iterations, options.hvx_runs);

    std::vector<double> scalar_totals;
    std::vector<double> hvx_totals;
    std::vector<double> hvx_multi_totals;
    for (int run = 0; run < options.hvx_runs; ++run) {
        scalar_totals.push_back(static_cast<double>(scalar.cycles[run]));
        hvx_totals.push_back(static_cast<double>(hvx.cycles[run]));
        hvx_multi_totals.push_back(
            static_cast<double>(hvx_multi.cycles[run]));
    }
    const double scalar_median = stats(scalar_totals)[1];
    const double hvx_median = stats(hvx_totals)[1];
    const double hvx_multi_median = stats(hvx_multi_totals)[1];
    const double speedup = (hvx_median > 0.0)
        ? scalar_median / hvx_median
        : 0.0;
    const double speedup_multiacc = (hvx_multi_median > 0.0)
        ? scalar_median / hvx_multi_median
        : 0.0;
    const bool checksum_match =
        scalar.checksum == hvx.checksum &&
        scalar.checksum == hvx_multi.checksum &&
        cpu_scalar.checksum == cpu_neon.checksum &&
        cpu_scalar.checksum == scalar.checksum;
    const bool pass = scalar.status == 0U && hvx.status == 0U &&
        hvx_multi.status == 0U && scalar.mismatches == 0 &&
        hvx.mismatches == 0 && hvx_multi.mismatches == 0 &&
        checksum_match;

    std::cout << std::setprecision(10)
              << "{\"mode\":\"hvx\""
              << ",\"lanes\":" << options.hvx_lanes
              << ",\"iterations\":" << options.hvx_iterations
              << ",\"runs\":" << options.hvx_runs
              << ",\"warmup_runs\":1"
              << ",";
    print_hvx_stats("scalar", scalar, options.hvx_lanes,
                    options.hvx_iterations, options.hvx_runs);
    std::cout << ",";
    print_hvx_stats("hvx", hvx, options.hvx_lanes,
                    options.hvx_iterations, options.hvx_runs);
    std::cout << ",";
    print_hvx_stats("hvx_multiacc", hvx_multi, options.hvx_lanes,
                    options.hvx_iterations, options.hvx_runs);
    std::cout << ",";
    print_cpu_vecadd_json("cpu_scalar", cpu_scalar, options.hvx_lanes,
                          options.hvx_iterations);
    std::cout << ",";
    print_cpu_vecadd_json("cpu_neon", cpu_neon, options.hvx_lanes,
                          options.hvx_iterations);
    std::cout << ",\"checksum_match\":"
              << (checksum_match ? "true" : "false")
              << ",\"speedup_cycles\":" << speedup
              << ",\"speedup_cycles_multiacc\":" << speedup_multiacc
              << ",\"result\":\"" << (pass ? "PASS" : "FAIL")
              << "\"}\n";
    return pass ? 0 : 1;
}

struct PiClampReport {
    int steps = 0;
    std::uint64_t rpc_failures = 0U;
    std::uint64_t exact_mismatches = 0U;
    int32_t integral_d_peak = 0;
    int32_t integral_q_trough = 0;
    bool saturated_d = false;
    bool saturated_q = false;
    bool recovered_d = false;
    bool recovered_q = false;
    int recovery_step_d = -1;
    int recovery_step_q = -1;
    bool reset_ok = false;
    bool reset_flag_ok = false;
    bool configure_reset_ok = false;
};

PiClampReport run_pi_clamp(remote_handle64 handle, int32_t kp, int32_t ki)
{
    PiClampReport report;
    const Config config{static_cast<std::int16_t>(kp),
                        static_cast<std::int16_t>(ki),
                        static_cast<std::int16_t>(kp),
                        static_cast<std::int16_t>(ki)};

    rpc_configure(handle, config);
    Controller cpu(config);
    validation::foc::Reference reference(config);
    rpc_reset(handle);
    cpu.reset();
    reference.reset();

    auto step_once = [&](const Input& in, bool reset_flag) {
        Output dsp{};
        const int rpc_result = rpc_step(handle, in, reset_flag, dsp);
        if (rpc_result != 0) {
            ++report.rpc_failures;
            return;
        }
        ++report.steps;
        Output cpu_out{};
        cpu.step(in, cpu_out);
        validation::foc::ReferenceOutput ref{};
        reference.step(in, ref);
        if (!same_output(dsp, cpu_out)) {
            ++report.exact_mismatches;
        }
    };

    // Positive d-axis / negative q-axis large-error hold. With ki=32767 the
    // integrators reach their clamps in two steps.
    for (int step = 0; step < 8; ++step) {
        const Input in{30000, -30000, 32767, -32767,
                       static_cast<std::uint16_t>(step * 1024)};
        step_once(in, false);
        report.integral_d_peak = std::max(report.integral_d_peak, cpu.integral_d());
        report.integral_q_trough = std::min(report.integral_q_trough, cpu.integral_q());
        if (cpu.integral_d() >= 32767) {
            report.saturated_d = true;
        }
        if (cpu.integral_q() <= -32767) {
            report.saturated_q = true;
        }
    }

    // Reverse the error without resetting. The integrators must leave the
    // clamp and the output must recover.
    for (int step = 0; step < 8; ++step) {
        const Input in{-30000, 30000, -32767, 32767,
                       static_cast<std::uint16_t>(step * 1024)};
        step_once(in, false);
        if (!report.recovered_d && cpu.integral_d() < 32767) {
            report.recovered_d = true;
            report.recovery_step_d = step + 1;
        }
        if (!report.recovered_q && cpu.integral_q() > -32767) {
            report.recovered_q = true;
            report.recovery_step_q = step + 1;
        }
    }

    // Explicit reset RPC: after saturation, the next output must match a fresh
    // controller, not the saturated continuation.
    rpc_reset(handle);
    cpu.reset();
    reference.reset();
    const Input reset_input{30000, -30000, 32767, -32767, 0};
    {
        Output dsp{};
        Output cpu_out{};
        if (rpc_step(handle, reset_input, false, dsp) != 0) {
            ++report.rpc_failures;
        } else {
            cpu.step(reset_input, cpu_out);
            report.reset_ok = same_output(dsp, cpu_out);
        }
    }

    // Per-step reset flag: saturate again, then reset inside `step`.
    for (int step = 0; step < 4; ++step) {
        const Input in{30000, -30000, 32767, -32767,
                       static_cast<std::uint16_t>(step * 1024)};
        step_once(in, false);
    }
    {
        Output dsp{};
        Output cpu_out{};
        if (rpc_step(handle, reset_input, true, dsp) != 0) {
            ++report.rpc_failures;
        } else {
            cpu.reset();
            cpu.step(reset_input, cpu_out);
            report.reset_flag_ok = same_output(dsp, cpu_out);
        }
    }

    // Configure must also clear state.
    for (int step = 0; step < 4; ++step) {
        const Input in{30000, -30000, 32767, -32767,
                       static_cast<std::uint16_t>(step * 1024)};
        step_once(in, false);
    }
    {
        rpc_configure(handle, config);
        cpu.configure(config);
        Output dsp{};
        Output cpu_out{};
        if (rpc_step(handle, reset_input, false, dsp) != 0) {
            ++report.rpc_failures;
        } else {
            cpu.step(reset_input, cpu_out);
            report.configure_reset_ok = same_output(dsp, cpu_out);
        }
    }

    return report;
}

struct PiLongReport {
    int steps = 0;
    std::uint64_t dsp_steps = 0U;
    std::uint64_t rpc_failures = 0U;
    std::uint64_t exact_mismatches = 0U;
    Accuracy accuracy;
    int32_t integral_min = 0;
    int32_t integral_max = 0;
    std::uint64_t integral_high_steps = 0U;
    std::uint64_t integral_low_steps = 0U;
    std::array<double, 10> bin_max_abs_lsb{};
};

PiLongReport run_pi_long(remote_handle64 handle, const Options& options)
{
    PiLongReport report;
    const Config config{8192, static_cast<std::int16_t>(options.pi_ki),
                        8192, static_cast<std::int16_t>(options.pi_ki)};

    rpc_configure(handle, config);
    rpc_reset(handle);
    Controller cpu(config);
    validation::foc::Reference reference(config);
    cpu.reset();
    reference.reset();

    const std::vector<validation::foc::Case> cases =
        validation::foc::random_cases(
            static_cast<std::size_t>(options.pi_steps), options.seed);
    report.steps = static_cast<int>(cases.size());

    std::ofstream csv;
    if (!options.pi_csv_path.empty()) {
        csv.open(options.pi_csv_path);
        if (!csv) {
            throw std::runtime_error(
                "cannot open PI CSV output: " + options.pi_csv_path);
        }
        csv << "case,index,ia_q15,ib_q15,id_ref_q15,iq_ref_q15,angle,"
               "dsp_id,cpu_id,ref_id,dsp_iq,cpu_iq,ref_iq,"
               "dsp_vd,cpu_vd,ref_vd,dsp_vq,cpu_vq,ref_vq,"
               "dsp_duty_a,cpu_duty_a,ref_duty_a,"
               "dsp_duty_b,cpu_duty_b,ref_duty_b,"
               "dsp_duty_c,cpu_duty_c,ref_duty_c,integral_d,integral_q\n";
        csv << std::setprecision(10);
    }

    const int bin_width = std::max(1, report.steps / 10);
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const validation::foc::Case& item = cases[index];
        Output dsp{};
        const int rpc_result = rpc_step(handle, item.input, false, dsp);
        if (rpc_result != 0) {
            ++report.rpc_failures;
            continue;
        }
        ++report.dsp_steps;

        Output cpu_out{};
        cpu.step(item.input, cpu_out);
        validation::foc::ReferenceOutput ref{};
        reference.step(item.input, ref);

        if (!same_output(dsp, cpu_out)) {
            ++report.exact_mismatches;
        }

        account(report.accuracy, ref.id_q15, cpu_out.id_q15);
        account(report.accuracy, ref.iq_q15, cpu_out.iq_q15);
        account(report.accuracy, ref.vd_q15, cpu_out.vd_q15);
        account(report.accuracy, ref.vq_q15, cpu_out.vq_q15);
        account(report.accuracy, ref.duty_a_q15, cpu_out.duty_a_q15);
        account(report.accuracy, ref.duty_b_q15, cpu_out.duty_b_q15);
        account(report.accuracy, ref.duty_c_q15, cpu_out.duty_c_q15);

        const int32_t id_int = cpu.integral_d();
        const int32_t iq_int = cpu.integral_q();
        report.integral_min = std::min(report.integral_min,
                                       std::min(id_int, iq_int));
        report.integral_max = std::max(report.integral_max,
                                       std::max(id_int, iq_int));
        if (id_int == 32767 || iq_int == 32767) {
            ++report.integral_high_steps;
        }
        if (id_int == -32767 || iq_int == -32767) {
            ++report.integral_low_steps;
        }

        const double step_error = std::max({
            std::fabs(ref.id_q15 - cpu_out.id_q15),
            std::fabs(ref.iq_q15 - cpu_out.iq_q15),
            std::fabs(ref.vd_q15 - cpu_out.vd_q15),
            std::fabs(ref.vq_q15 - cpu_out.vq_q15),
            std::fabs(ref.duty_a_q15 - cpu_out.duty_a_q15),
            std::fabs(ref.duty_b_q15 - cpu_out.duty_b_q15),
            std::fabs(ref.duty_c_q15 - cpu_out.duty_c_q15),
        });
        const int bin = std::min(9, static_cast<int>(index) / bin_width);
        report.bin_max_abs_lsb[static_cast<std::size_t>(bin)] =
            std::max(report.bin_max_abs_lsb[static_cast<std::size_t>(bin)],
                     step_error);

        if (csv) {
            csv << item.name << ',' << index
                << ',' << item.input.ia_q15 << ',' << item.input.ib_q15
                << ',' << item.input.id_ref_q15 << ',' << item.input.iq_ref_q15
                << ',' << item.input.electrical_angle
                << ',' << dsp.id_q15 << ',' << cpu_out.id_q15 << ',' << ref.id_q15
                << ',' << dsp.iq_q15 << ',' << cpu_out.iq_q15 << ',' << ref.iq_q15
                << ',' << dsp.vd_q15 << ',' << cpu_out.vd_q15 << ',' << ref.vd_q15
                << ',' << dsp.vq_q15 << ',' << cpu_out.vq_q15 << ',' << ref.vq_q15
                << ',' << dsp.duty_a_q15 << ',' << cpu_out.duty_a_q15 << ',' << ref.duty_a_q15
                << ',' << dsp.duty_b_q15 << ',' << cpu_out.duty_b_q15 << ',' << ref.duty_b_q15
                << ',' << dsp.duty_c_q15 << ',' << cpu_out.duty_c_q15 << ',' << ref.duty_c_q15
                << ',' << id_int << ',' << iq_int << '\n';
        }
    }

    return report;
}

int run_pi(remote_handle64 handle, const Options& options)
{
    const PiClampReport clamp = run_pi_clamp(handle, 8192, 32767);
    const PiLongReport long_run = run_pi_long(handle, options);
    const double mean_abs = long_run.accuracy.values == 0U
        ? 0.0
        : long_run.accuracy.sum_abs /
          static_cast<double>(long_run.accuracy.values);

    const bool clamp_ok = clamp.rpc_failures == 0U &&
        clamp.exact_mismatches == 0U && clamp.saturated_d &&
        clamp.saturated_q && clamp.recovered_d && clamp.recovered_q &&
        clamp.reset_ok && clamp.reset_flag_ok && clamp.configure_reset_ok;
    const bool long_ok = long_run.rpc_failures == 0U &&
        long_run.exact_mismatches == 0U && long_run.steps > 0;
    const bool pass = clamp_ok && long_ok;

    std::cout << std::setprecision(10)
              << "{\"mode\":\"pi\""
              << ",\"clamp\":{\"ki\":32767"
              << ",\"steps\":" << clamp.steps
              << ",\"rpc_failures\":" << clamp.rpc_failures
              << ",\"exact_mismatches\":" << clamp.exact_mismatches
              << ",\"saturated_d\":" << (clamp.saturated_d ? "true" : "false")
              << ",\"saturated_q\":" << (clamp.saturated_q ? "true" : "false")
              << ",\"recovered_d\":" << (clamp.recovered_d ? "true" : "false")
              << ",\"recovered_q\":" << (clamp.recovered_q ? "true" : "false")
              << ",\"recovery_step_d\":" << clamp.recovery_step_d
              << ",\"recovery_step_q\":" << clamp.recovery_step_q
              << ",\"integral_d_peak\":" << clamp.integral_d_peak
              << ",\"integral_q_trough\":" << clamp.integral_q_trough
              << ",\"reset_ok\":" << (clamp.reset_ok ? "true" : "false")
              << ",\"reset_flag_ok\":" << (clamp.reset_flag_ok ? "true" : "false")
              << ",\"configure_reset_ok\":"
              << (clamp.configure_reset_ok ? "true" : "false")
              << "}"
              << ",\"long\":{\"ki\":" << options.pi_ki
              << ",\"steps\":" << long_run.steps
              << ",\"dsp_steps\":" << long_run.dsp_steps
              << ",\"rpc_failures\":" << long_run.rpc_failures
              << ",\"exact_mismatches\":" << long_run.exact_mismatches
              << ",\"fp32_values\":" << long_run.accuracy.values
              << ",\"fp32_max_abs_lsb\":" << long_run.accuracy.max_abs
              << ",\"fp32_mean_abs_lsb\":" << mean_abs
              << ",\"hist_le1\":" << long_run.accuracy.hist_le1
              << ",\"hist_le2\":" << long_run.accuracy.hist_le2
              << ",\"hist_le5\":" << long_run.accuracy.hist_le5
              << ",\"hist_le16\":" << long_run.accuracy.hist_le16
              << ",\"hist_le32\":" << long_run.accuracy.hist_le32
              << ",\"hist_gt32\":" << long_run.accuracy.hist_gt32
              << ",\"integral_min\":" << long_run.integral_min
              << ",\"integral_max\":" << long_run.integral_max
              << ",\"integral_high_steps\":" << long_run.integral_high_steps
              << ",\"integral_low_steps\":" << long_run.integral_low_steps
              << ",\"bin_max_abs_lsb\":[";
    for (std::size_t index = 0; index < long_run.bin_max_abs_lsb.size();
         ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << long_run.bin_max_abs_lsb[index];
    }
    std::cout << "]}"
              << ",\"result\":\"" << (pass ? "PASS" : "FAIL")
              << "\"}\n";
    return pass ? 0 : 1;
}

[[noreturn]] void usage(const char* program)
{
    std::cerr
        << "usage: " << program
        << " <echo|validate|bench|cpu|hvx|pi|rpc|rpc-scaling|rt-dsp|rt-step|rt-bench|rt-cpu|rt-timer-cpu|rt-timer-dsp|all> [options]\n"
        << "  --warmup N        bench warm-up steps (default 256)\n"
        << "  --cpu-warmup N    CPU-only clock ramp steps (default 2000000)\n"
        << "  --runs N          timed repetitions, 1..5 (default 5)\n"
        << "  --iterations N    steps per repetition (default 16384)\n"
        << "  --kp-d N          Q15 d-axis proportional gain (default 8192)\n"
        << "  --ki-d N          Q15 d-axis integral gain (default 0)\n"
        << "  --kp-q N          Q15 q-axis proportional gain (default 8192)\n"
        << "  --ki-q N          Q15 q-axis integral gain (default 0)\n"
        << "  --seed N          random-vector seed (default 0x5EED1234)\n"
        << "  --random N        random vector count (default 4096)\n"
        << "  --no-sweep        skip the 16384-point angle sweep\n"
        << "  --no-calibrate    use the fallback clock instead of deriving it\n"
        << "                    from RPC-overhead-corrected wall time\n"
        << "  --dsp-perf        request CDSP DCVS performance mode for bench\n"
        << "  --rpc-count N     echo/step RPCs per latency series (default 10000)\n"
        << "  --rpc-repeats N   repetitions per rpc-scaling size (default 30)\n"
        << "  --rpc-csv PATH    write every rpc-scaling sample (steps,repeat,wall_us)\n"
        << "  --rt-samples N    samples per RT series (default 100000)\n"
        << "  --rt-batch N      steps per sample/batch (default 16)\n"
        << "  --rt-warmup N     first N samples marked warm-up (default 1000)\n"
        << "  --rt-round N      round label for interleaved runs\n"
        << "  --rt-condition N  condition label (0 idle, 1 CPU, 2 memory)\n"
        << "  --rt-config S     configuration name written to CSV\n"
        << "  --rt-csv PATH     raw CSV output (required for RT modes)\n"
        << "  --clock-hz N      fallback clock when timer ticks are unavailable\n"
        << "                    (default 1160000000; normally measured on DSP)\n"
        << "  --csv PATH        write per-case CSV from validate\n"
        << "  --hvx-lanes N     vector-add lanes, multiple of 64 (default 64)\n"
        << "  --hvx-iterations N  HVX vector-add accumulations (default 5000)\n"
        << "  --hvx-runs N      HVX timed repetitions, 1..5 (default 5)\n"
        << "  --pi-steps N      long nonzero-ki PI steps (default 100000)\n"
        << "  --pi-ki N         Q15 integral gain for the long PI run (default 120)\n"
        << "  --pi-csv PATH     write per-step CSV from the PI stress test\n";
    std::exit(2);
}

long long parse_integer(const char* text, const char* name)
{
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

Options parse_options(int argc, char** argv, int start)
{
    Options options;
    for (int index = start; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto value = [&](const char* name) -> const char* {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string(name) + " needs a value");
            }
            return argv[++index];
        };

        if (argument == "--warmup") {
            options.warmup = static_cast<int>(parse_integer(value("--warmup"), "warmup"));
        } else if (argument == "--cpu-warmup") {
            options.cpu_warmup = static_cast<int>(
                parse_integer(value("--cpu-warmup"), "cpu-warmup"));
        } else if (argument == "--runs") {
            options.runs = static_cast<int>(parse_integer(value("--runs"), "runs"));
        } else if (argument == "--iterations") {
            options.iterations = static_cast<int>(
                parse_integer(value("--iterations"), "iterations"));
        } else if (argument == "--kp-d") {
            options.config.kp_d_q15 = static_cast<std::int16_t>(
                parse_integer(value("--kp-d"), "kp-d"));
        } else if (argument == "--ki-d") {
            options.config.ki_d_q15 = static_cast<std::int16_t>(
                parse_integer(value("--ki-d"), "ki-d"));
        } else if (argument == "--kp-q") {
            options.config.kp_q_q15 = static_cast<std::int16_t>(
                parse_integer(value("--kp-q"), "kp-q"));
        } else if (argument == "--ki-q") {
            options.config.ki_q_q15 = static_cast<std::int16_t>(
                parse_integer(value("--ki-q"), "ki-q"));
        } else if (argument == "--seed") {
            options.seed = static_cast<std::uint32_t>(
                parse_integer(value("--seed"), "seed"));
        } else if (argument == "--random") {
            options.random_count = static_cast<std::size_t>(
                parse_integer(value("--random"), "random"));
        } else if (argument == "--no-sweep") {
            options.sweep = false;
        } else if (argument == "--no-calibrate") {
            options.calibrate = false;
        } else if (argument == "--dsp-perf") {
            options.dsp_perf = true;
        } else if (argument == "--rpc-count") {
            options.rpc_count = static_cast<int>(
                parse_integer(value("--rpc-count"), "rpc-count"));
        } else if (argument == "--rpc-repeats") {
            options.rpc_repeats = static_cast<int>(
                parse_integer(value("--rpc-repeats"), "rpc-repeats"));
        } else if (argument == "--rpc-csv") {
            options.rpc_csv_path = value("--rpc-csv");
        } else if (argument == "--rt-samples") {
            options.rt_samples = static_cast<int>(
                parse_integer(value("--rt-samples"), "rt-samples"));
        } else if (argument == "--rt-batch") {
            options.rt_batch = static_cast<int>(
                parse_integer(value("--rt-batch"), "rt-batch"));
        } else if (argument == "--rt-warmup") {
            options.rt_warmup = static_cast<int>(
                parse_integer(value("--rt-warmup"), "rt-warmup"));
        } else if (argument == "--rt-round") {
            options.rt_round = static_cast<int>(
                parse_integer(value("--rt-round"), "rt-round"));
        } else if (argument == "--rt-condition") {
            options.rt_condition = static_cast<int>(
                parse_integer(value("--rt-condition"), "rt-condition"));
        } else if (argument == "--rt-config") {
            options.rt_config = value("--rt-config");
        } else if (argument == "--rt-csv") {
            options.rt_csv_path = value("--rt-csv");
        } else if (argument == "--clock-hz") {
            options.clock_hz = static_cast<double>(
                parse_integer(value("--clock-hz"), "clock-hz"));
        } else if (argument == "--csv") {
            options.csv_path = value("--csv");
        } else if (argument == "--hvx-iterations") {
            options.hvx_iterations = static_cast<int>(
                parse_integer(value("--hvx-iterations"), "hvx-iterations"));
        } else if (argument == "--hvx-lanes") {
            options.hvx_lanes = static_cast<int>(
                parse_integer(value("--hvx-lanes"), "hvx-lanes"));
        } else if (argument == "--hvx-runs") {
            options.hvx_runs = static_cast<int>(
                parse_integer(value("--hvx-runs"), "hvx-runs"));
        } else if (argument == "--pi-steps") {
            options.pi_steps = static_cast<int>(
                parse_integer(value("--pi-steps"), "pi-steps"));
        } else if (argument == "--pi-ki") {
            options.pi_ki = static_cast<int>(
                parse_integer(value("--pi-ki"), "pi-ki"));
        } else if (argument == "--pi-csv") {
            options.pi_csv_path = value("--pi-csv");
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }

    if (options.warmup < 0 || options.cpu_warmup < 0 ||
        options.iterations <= 0 || options.clock_hz <= 0.0 ||
        options.hvx_iterations <= 0 || options.hvx_runs <= 0 ||
        options.hvx_runs > kMaxRuns || options.pi_steps <= 0 ||
        options.pi_ki < std::numeric_limits<std::int16_t>::min() ||
        options.pi_ki > std::numeric_limits<std::int16_t>::max()) {
        throw std::invalid_argument("warmup/iterations/clock/hvx/pi out of range");
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage(argv[0]);
    }

    const std::string mode = argv[1];
    try {
        const Options options = parse_options(argc, argv, 2);

        if (mode == "echo") {
            Session session;
            return run_echo(session.handle());
        }
        if (mode == "validate") {
            Session session;
            return run_validate(session.handle(), options);
        }
        if (mode == "bench") {
            Session session;
            return run_bench(session.handle(), options);
        }
        if (mode == "cpu") {
            return run_cpu(options);
        }
        if (mode == "rpc") {
            Session session;
            return run_rpc_latency(session.handle(), options);
        }
        if (mode == "rpc-scaling") {
            Session session;
            return run_rpc_scaling(session.handle(), options);
        }
        if (mode == "rt-dsp") {
            Session session;
            return run_rt_dsp(session.handle(), options);
        }
        if (mode == "rt-step") {
            Session session;
            return run_rt_step(session.handle(), options);
        }
        if (mode == "rt-bench") {
            Session session;
            return run_rt_bench(session.handle(), options);
        }
        if (mode == "rt-cpu") {
            return run_rt_cpu(options);
        }
        if (mode == "rt-timer-cpu") {
            return run_rt_timer_cpu(options);
        }
        if (mode == "rt-timer-dsp") {
            Session session;
            return run_rt_timer_dsp(session.handle(), options);
        }
        if (mode == "hvx") {
            return run_hvx(options);
        }
        if (mode == "pi") {
            Session session;
            return run_pi(session.handle(), options);
        }
        if (mode == "all") {
            Session session;
            const int echo_result = run_echo(session.handle());
            const int validate_result = run_validate(session.handle(), options);
            const int bench_result = run_bench(session.handle(), options);
            const int cpu_result = run_cpu(options);
            const int hvx_result = run_hvx(options);
            const int pi_result = run_pi(session.handle(), options);
            return (echo_result == 0 && validate_result == 0 &&
                    bench_result == 0 && cpu_result == 0 &&
                    hvx_result == 0 && pi_result == 0)
                ? 0 : 1;
        }
        usage(argv[0]);
    } catch (const std::exception& error) {
        std::cerr << "foc_runner: " << error.what() << '\n';
        return 1;
    }
}
