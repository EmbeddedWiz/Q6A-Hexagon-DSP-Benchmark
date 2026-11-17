/*
 * DSP-side implementation of the project-owned HVX vector-add microbenchmark.
 *
 * The measured algorithm is the historical one: 64 uint16 lanes and 5000
 * accumulations of a precomputed vector sum. The scalar and HVX paths use the
 * same data and the same number of iterations. Only the implementation differs.
 *
 * This translation unit is compiled with HVX enabled. The FOC skeleton is a
 * separate shared object and is compiled without these flags, so this module
 * cannot perturb the FOC cycle measurements.
 */
#include "hvx_bench.h"

#include <cstdint>

#include <hexagon_protos.h>
#include <hexagon_types.h>
#include <qurt_cycles.h>
#include <qurt_hvx.h>

namespace {

constexpr int kLanes = 64;
constexpr int kMaxLanes = 4096;
constexpr int kMaxRuns = 5;
constexpr int kMaxIterations = 1000000;
constexpr uint64_t kMaxCycles = 10000000000ULL;

alignas(128) uint16_t g_a[kMaxLanes];
alignas(128) uint16_t g_b[kMaxLanes];
alignas(128) uint16_t g_sum[kMaxLanes];
alignas(128) uint16_t g_acc[kMaxLanes];

void init_data(int lanes)
{
    for (int lane = 0; lane < lanes; ++lane) {
        g_a[lane] = static_cast<uint16_t>(lane % 7);
        g_b[lane] = static_cast<uint16_t>(lane % 5);
        g_sum[lane] = static_cast<uint16_t>(g_a[lane] + g_b[lane]);
        g_acc[lane] = 0U;
    }
}

__attribute__((noinline)) void run_scalar(int lanes, uint32_t iterations)
{
    for (int lane = 0; lane < lanes; ++lane) {
        g_acc[lane] = 0U;
    }
    for (volatile uint32_t i = 0; i < iterations; ++i) {
        for (int lane = 0; lane < lanes; ++lane) {
            g_acc[lane] = static_cast<uint16_t>(g_acc[lane] + g_sum[lane]);
        }
    }
}

__attribute__((noinline)) void run_hvx(int lanes, uint32_t iterations)
{
    for (int base = 0; base < lanes; base += kLanes) {
        HVX_Vector vacc = Q6_V_vzero();
        const HVX_Vector vsum =
            *reinterpret_cast<const HVX_Vector*>(g_sum + base);
        for (volatile uint32_t i = 0; i < iterations; ++i) {
            vacc = Q6_Vh_vadd_VhVh(vacc, vsum);
        }
        *reinterpret_cast<HVX_Vector*>(g_acc + base) = vacc;
    }
}

// Same work and checksum as run_hvx(), but four independent dependency chains.
// The four partial sums are combined modulo 2^16, which is exactly the same
// result as one accumulator after `iterations` additions.
__attribute__((noinline)) void run_hvx_multiacc(int lanes, uint32_t iterations)
{
    const uint32_t groups = iterations / 4U;
    const uint32_t remainder = iterations % 4U;
    for (int base = 0; base < lanes; base += kLanes) {
        HVX_Vector v0 = Q6_V_vzero();
        HVX_Vector v1 = Q6_V_vzero();
        HVX_Vector v2 = Q6_V_vzero();
        HVX_Vector v3 = Q6_V_vzero();
        const HVX_Vector vsum =
            *reinterpret_cast<const HVX_Vector*>(g_sum + base);
        for (volatile uint32_t i = 0; i < groups; ++i) {
            v0 = Q6_Vh_vadd_VhVh(v0, vsum);
            v1 = Q6_Vh_vadd_VhVh(v1, vsum);
            v2 = Q6_Vh_vadd_VhVh(v2, vsum);
            v3 = Q6_Vh_vadd_VhVh(v3, vsum);
        }
        for (volatile uint32_t i = 0; i < remainder; ++i) {
            v0 = Q6_Vh_vadd_VhVh(v0, vsum);
        }
        const HVX_Vector vacc = Q6_Vh_vadd_VhVh(
            Q6_Vh_vadd_VhVh(v0, v1), Q6_Vh_vadd_VhVh(v2, v3));
        *reinterpret_cast<HVX_Vector*>(g_acc + base) = vacc;
    }
}

void run_mode(int mode, int lanes, uint32_t iterations)
{
    if (mode == 1) {
        run_hvx(lanes, iterations);
    } else if (mode == 2) {
        run_hvx_multiacc(lanes, iterations);
    } else {
        run_scalar(lanes, iterations);
    }
}

uint64_t checksum(int lanes)
{
    uint64_t sum = 0U;
    for (int lane = 0; lane < lanes; ++lane) {
        sum += g_acc[lane];
    }
    return sum;
}

int mismatch_count(int lanes, uint32_t iterations)
{
    int errors = 0;
    for (int lane = 0; lane < lanes; ++lane) {
        const uint16_t expected = static_cast<uint16_t>(
            static_cast<uint64_t>(g_sum[lane]) * iterations);
        if (g_acc[lane] != expected) {
            ++errors;
        }
    }
    return errors;
}

} // namespace

extern "C" int hvx_bench_open(const char* uri, remote_handle64* handle)
{
    if (uri == nullptr || handle == nullptr) {
        return -1;
    }
    *handle = 1U;
    return 0;
}

extern "C" int hvx_bench_close(remote_handle64 handle)
{
    (void)handle;
    return 0;
}

extern "C" int hvx_bench_run(remote_handle64 handle, int mode,
                             int lanes,
                             int iterations, int runs,
                             uint64_t* cycles0, uint64_t* cycles1,
                             uint64_t* cycles2, uint64_t* cycles3,
                             uint64_t* cycles4, uint64_t* checksum_out,
                             int* mismatches_out, uint64_t* status)
{
    if (handle == 0U || cycles0 == nullptr || cycles1 == nullptr ||
        cycles2 == nullptr || cycles3 == nullptr || cycles4 == nullptr ||
        checksum_out == nullptr || mismatches_out == nullptr ||
        status == nullptr) {
        return -1;
    }
    if ((mode != 0 && mode != 1 && mode != 2) || lanes < kLanes ||
        lanes > kMaxLanes || (lanes % kLanes) != 0 ||
        iterations <= 0 || iterations > kMaxIterations ||
        runs <= 0 || runs > kMaxRuns) {
        return -2;
    }

    uint64_t* const outputs[kMaxRuns] = {
        cycles0, cycles1, cycles2, cycles3, cycles4
    };
    for (int run = 0; run < kMaxRuns; ++run) {
        *outputs[run] = 0U;
    }
    *checksum_out = 0U;
    *mismatches_out = 0;
    *status = 0U;

    init_data(lanes);

    if ((mode == 1 || mode == 2) &&
        qurt_hvx_lock(QURT_HVX_MODE_128B) != 0) {
        *status |= 16U;
        return 0;
    }

    const uint32_t count = static_cast<uint32_t>(iterations);

    // One untimed warm-up pass. The algorithm, data and iteration count are
    // unchanged; the warm-up only removes first-pass effects.
    run_mode(mode, lanes, count);

    uint64_t checksums[kMaxRuns] = {};
    int mismatches[kMaxRuns] = {};

    for (int run = 0; run < runs; ++run) {
        const uint64_t start = qurt_get_core_pcycles();
        run_mode(mode, lanes, count);
        const uint64_t end = qurt_get_core_pcycles();
        const uint64_t elapsed = (end > start) ? (end - start) : 0U;
        *outputs[run] = elapsed;
        checksums[run] = checksum(lanes);
        mismatches[run] = mismatch_count(lanes, count);
    }

    if (mode == 1 || mode == 2) {
        qurt_hvx_unlock();
    }

    *checksum_out = checksums[runs - 1];
    *mismatches_out = mismatches[runs - 1];

    for (int run = 0; run < runs; ++run) {
        if (*outputs[run] == 0U || *outputs[run] > kMaxCycles) {
            *status |= 1U;
        }
        if (mismatches[run] != 0) {
            *status |= 2U;
        }
        if (checksums[run] != checksums[0]) {
            *status |= 4U;
        }
    }

    return 0;
}
