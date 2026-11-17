/*
 * DSP-side implementation of the project-owned FOC benchmark contract.
 *
 * The generated skeleton (foc_bench_skel.c) forwards to these functions.
 * Everything here is scalar fixed-point code: the FOC loop is stateful and
 * is not a candidate for wholesale HVX vectorization.
 */
#include "foc_bench.h"

#include "foc_fixed.h"

#include <cstdint>
#include <limits>

#if defined(__hexagon__)
#include <qurt_cycles.h>
#include <HAP_power.h>
#endif

namespace {

using motor_control::foc::Config;
using motor_control::foc::Controller;
using motor_control::foc::Input;
using motor_control::foc::Output;

constexpr int kMaxRuns = 5;
constexpr int kMaxSamples = 100000;
constexpr uint64_t kMaxCyclesPerStep = 1000000ULL;

Controller g_controller{Config{8192, 0, 8192, 0}};
uint16_t g_samples[kMaxSamples];
uint32_t g_sample_steps = 0U;
int g_sample_count = 0;

bool fits_i16(int value)
{
    return value >= std::numeric_limits<int16_t>::min() &&
           value <= std::numeric_limits<int16_t>::max();
}

int16_t narrow_i16(int value)
{
    return static_cast<int16_t>(value);
}

Input make_input(uint32_t index)
{
    return Input{
        narrow_i16(static_cast<int>((index * 7919u) & 0xffffu)),
        narrow_i16(static_cast<int>((index * 3571u) & 0xffffu)),
        0,
        10000,
        static_cast<uint16_t>(index & 0x3fffu)
    };
}

} // namespace

extern "C" int foc_bench_open(const char* uri, remote_handle64* handle)
{
    if (uri == nullptr || handle == nullptr) {
        return -1;
    }
    *handle = 1U;
    return 0;
}

extern "C" int foc_bench_close(remote_handle64 handle)
{
    (void)handle;
    return 0;
}

extern "C" int foc_bench_echo(remote_handle64 handle, uint64_t input,
                              uint64_t* output)
{
    if (handle == 0U || output == nullptr) {
        return -1;
    }
    /* Invert the value so the host verifies a real round trip of both halves. */
    *output = ~input;
    return 0;
}

extern "C" int foc_bench_configure(remote_handle64 handle,
                                   int kp_d_q15, int ki_d_q15,
                                   int kp_q_q15, int ki_q_q15)
{
    if (handle == 0U) {
        return -1;
    }
    if (!fits_i16(kp_d_q15) || !fits_i16(ki_d_q15) ||
        !fits_i16(kp_q_q15) || !fits_i16(ki_q_q15)) {
        return -2;
    }
    g_controller.configure(Config{
        narrow_i16(kp_d_q15),
        narrow_i16(ki_d_q15),
        narrow_i16(kp_q_q15),
        narrow_i16(ki_q_q15)
    });
    return 0;
}

extern "C" int foc_bench_reset(remote_handle64 handle)
{
    if (handle == 0U) {
        return -1;
    }
    g_controller.reset();
    return 0;
}

extern "C" int foc_bench_set_perf(remote_handle64 handle, int enable)
{
    if (handle == 0U) {
        return -1;
    }
#if defined(__hexagon__)
    HAP_power_request_t request{};
    request.type = HAP_power_set_DCVS_v2;
    request.dcvs_v2.dcvs_enable = TRUE;
    request.dcvs_v2.dcvs_option = (enable != 0)
        ? HAP_DCVS_V2_PERFORMANCE_MODE
        : HAP_DCVS_V2_ADJUST_UP_DOWN;
    request.dcvs_v2.set_latency = (enable != 0) ? TRUE : FALSE;
    request.dcvs_v2.latency = 1000U;
    request.dcvs_v2.set_dcvs_params = (enable != 0) ? TRUE : FALSE;
    if (enable != 0) {
        request.dcvs_v2.dcvs_params.min_corner = HAP_DCVS_VCORNER_SVS;
        request.dcvs_v2.dcvs_params.max_corner = HAP_DCVS_VCORNER_TURBO;
        request.dcvs_v2.dcvs_params.target_corner = HAP_DCVS_VCORNER_TURBO;
    }
    return HAP_power_set(nullptr, &request) == 0 ? 0 : -1;
#else
    (void)enable;
    return 0;
#endif
}

extern "C" int foc_bench_step(remote_handle64 handle,
                              int ia_q15, int ib_q15,
                              int id_ref_q15, int iq_ref_q15,
                              int electrical_angle, int reset_state,
                              int* id_q15, int* iq_q15,
                              int* vd_q15, int* vq_q15,
                              int* duty_a_q15, int* duty_b_q15,
                              int* duty_c_q15)
{
    if (handle == 0U || id_q15 == nullptr || iq_q15 == nullptr ||
        vd_q15 == nullptr || vq_q15 == nullptr ||
        duty_a_q15 == nullptr || duty_b_q15 == nullptr ||
        duty_c_q15 == nullptr) {
        return -1;
    }
    if (!fits_i16(ia_q15) || !fits_i16(ib_q15) ||
        !fits_i16(id_ref_q15) || !fits_i16(iq_ref_q15) ||
        electrical_angle < 0 || electrical_angle > 0xFFFF) {
        return -2;
    }
    if (reset_state != 0) {
        g_controller.reset();
    }

    const Input in{
        narrow_i16(ia_q15),
        narrow_i16(ib_q15),
        narrow_i16(id_ref_q15),
        narrow_i16(iq_ref_q15),
        static_cast<uint16_t>(electrical_angle)
    };
    Output out{};
    g_controller.step(in, out);

    *id_q15 = out.id_q15;
    *iq_q15 = out.iq_q15;
    *vd_q15 = out.vd_q15;
    *vq_q15 = out.vq_q15;
    *duty_a_q15 = out.duty_a_q15;
    *duty_b_q15 = out.duty_b_q15;
    *duty_c_q15 = out.duty_c_q15;
    return 0;
}

extern "C" int foc_bench_bench(remote_handle64 handle,
                               int warmup, int runs, int iterations,
                               int kp_d_q15, int ki_d_q15,
                               int kp_q_q15, int ki_q_q15,
                               uint64_t* cycles0, uint64_t* cycles1,
                               uint64_t* cycles2, uint64_t* cycles3,
                               uint64_t* cycles4, uint64_t* checksum,
                               uint64_t* status)
{
    if (handle == 0U || cycles0 == nullptr || cycles1 == nullptr ||
        cycles2 == nullptr || cycles3 == nullptr || cycles4 == nullptr ||
        checksum == nullptr || status == nullptr) {
        return -1;
    }
    if (warmup < 0 || runs <= 0 || runs > static_cast<int>(kMaxRuns) ||
        iterations <= 0) {
        return -2;
    }
    if (!fits_i16(kp_d_q15) || !fits_i16(ki_d_q15) ||
        !fits_i16(kp_q_q15) || !fits_i16(ki_q_q15)) {
        return -2;
    }

    uint64_t* const outputs[kMaxRuns] = {
        cycles0, cycles1, cycles2, cycles3, cycles4
    };
    for (int run = 0; run < static_cast<int>(kMaxRuns); ++run) {
        *outputs[run] = 0U;
    }
    *checksum = 0U;
    *status = 0U;

    g_controller.configure(Config{
        narrow_i16(kp_d_q15),
        narrow_i16(ki_d_q15),
        narrow_i16(kp_q_q15),
        narrow_i16(ki_q_q15)
    });

    Output out{};
    for (int i = 0; i < warmup; ++i) {
        g_controller.step(make_input(static_cast<uint32_t>(i)), out);
    }

    uint64_t checksums[kMaxRuns] = {};
    const uint32_t count = static_cast<uint32_t>(iterations);

    for (int run = 0; run < runs; ++run) {
        g_controller.reset();
        uint64_t sum = 0U;

#if defined(__hexagon__)
        const uint64_t start = qurt_get_core_pcycles();
#endif
        for (uint32_t i = 0; i < count; ++i) {
            g_controller.step(make_input(i), out);
            sum += static_cast<uint16_t>(out.id_q15);
            sum += static_cast<uint16_t>(out.iq_q15);
            sum += static_cast<uint16_t>(out.vd_q15);
            sum += static_cast<uint16_t>(out.vq_q15);
            sum += out.duty_a_q15;
            sum += out.duty_b_q15;
            sum += out.duty_c_q15;
        }
#if defined(__hexagon__)
        const uint64_t end = qurt_get_core_pcycles();
        const uint64_t elapsed = end - start;
#else
        const uint64_t elapsed = 0U;
#endif

        *outputs[run] = elapsed;
        checksums[run] = sum;
    }

    *checksum = checksums[runs - 1];

    for (int run = 0; run < runs; ++run) {
        const uint64_t elapsed = *outputs[run];
        if (elapsed == 0U) {
            *status |= 1U;
        }
        if (checksums[run] == 0U) {
            *status |= 2U;
        }
        const uint64_t per_step = elapsed / count;
        if (per_step == 0U || per_step > kMaxCyclesPerStep) {
            *status |= 4U;
        }
        if (checksums[run] != checksums[0]) {
            *status |= 8U;
        }
    }

    return 0;
}

extern "C" int foc_bench_sampler_run(remote_handle64 handle,
                                     int batch_steps, int sample_count,
                                     int kp_d_q15, int ki_d_q15,
                                     int kp_q_q15, int ki_q_q15,
                                     uint64_t* total_cycles,
                                     uint64_t* checksum, uint64_t* status)
{
    if (handle == 0U || total_cycles == nullptr ||
        checksum == nullptr || status == nullptr) {
        return -1;
    }
    if (batch_steps <= 0 || batch_steps > 64 ||
        sample_count <= 0 || sample_count > kMaxSamples ||
        !fits_i16(kp_d_q15) || !fits_i16(ki_d_q15) ||
        !fits_i16(kp_q_q15) || !fits_i16(ki_q_q15)) {
        return -2;
    }

    *status = 0U;
    g_controller.configure(Config{
        narrow_i16(kp_d_q15), narrow_i16(ki_d_q15),
        narrow_i16(kp_q_q15), narrow_i16(ki_q_q15)
    });
    Output out{};
    for (int i = 0; i < 256; ++i) {
        g_controller.step(make_input(static_cast<uint32_t>(i)), out);
    }

    g_controller.reset();
    uint64_t total = 0U;
    uint64_t sum = 0U;
    uint64_t state = 0U;
    const uint32_t batch = static_cast<uint32_t>(batch_steps);
    for (int sample = 0; sample < sample_count; ++sample) {
        uint64_t batch_sum = 0U;
#if defined(__hexagon__)
        const uint64_t start = qurt_get_core_pcycles();
#endif
        for (uint32_t step = 0; step < batch; ++step) {
            const uint32_t index =
                static_cast<uint32_t>(sample) * batch + step;
            g_controller.step(make_input(index), out);
            batch_sum += static_cast<uint16_t>(out.id_q15);
            batch_sum += static_cast<uint16_t>(out.iq_q15);
            batch_sum += static_cast<uint16_t>(out.vd_q15);
            batch_sum += static_cast<uint16_t>(out.vq_q15);
            batch_sum += out.duty_a_q15;
            batch_sum += out.duty_b_q15;
            batch_sum += out.duty_c_q15;
        }
#if defined(__hexagon__)
        const uint64_t end = qurt_get_core_pcycles();
        const uint64_t elapsed = (end > start) ? (end - start) : 0U;
#else
        const uint64_t elapsed = 0U;
#endif
        if (elapsed > 0xffffU) { *status |= 4U; }
        g_samples[sample] = static_cast<uint16_t>(elapsed);
        total += elapsed;
        sum += batch_sum;
        state ^= batch_sum + 0x9e3779b97f4a7c15ULL +
                 (state << 6U) + (state >> 2U);
        if (elapsed == 0U) {
            *status |= 1U;
        }
    }

    g_sample_steps = batch;
    g_sample_count = sample_count;
    *total_cycles = total;
    *checksum = sum ^ state;
    if (*checksum == 0U) {
        *status |= 2U;
    }
    return 0;
}

extern "C" int foc_bench_sampler_count(remote_handle64 handle, int* count)
{
    if (handle == 0U || count == nullptr) {
        return -1;
    }
    *count = g_sample_count;
    return 0;
}

extern "C" int foc_bench_sampler_get(remote_handle64 handle, int index,
                                     uint64_t* cycles, uint64_t* steps)
{
    if (handle == 0U || cycles == nullptr || steps == nullptr) {
        return -1;
    }
    if (index < 0 || index >= g_sample_count) {
        return -2;
    }
    *cycles = static_cast<uint64_t>(g_samples[index]);
    *steps = g_sample_steps;
    return 0;
}

extern "C" int foc_bench_timer_overhead(remote_handle64 handle,
                                        int iterations, uint64_t* cycles)
{
    if (handle == 0U || cycles == nullptr) {
        return -1;
    }
    if (iterations <= 0 || iterations > 10000000) {
        return -2;
    }
#if defined(__hexagon__)
    volatile uint64_t sink = 0U;
    const uint64_t start = qurt_get_core_pcycles();
    for (int i = 0; i < iterations; ++i) {
        sink += qurt_get_core_pcycles();
    }
    const uint64_t end = qurt_get_core_pcycles();
    (void)sink;
    *cycles = (end > start) ? (end - start) : 0U;
#else
    (void)iterations;
    *cycles = 0U;
#endif
    return 0;
}
