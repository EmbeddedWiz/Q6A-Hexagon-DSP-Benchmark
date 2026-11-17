#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <qurt_cycles.h>
#include <qurt_hvx.h>
#include <hexagon_types.h>
#include <hexagon_protos.h>

#define LANES 64
#define ITERATIONS 5000

static uint16_t a[LANES]   __attribute__((aligned(128)));
static uint16_t b[LANES]   __attribute__((aligned(128)));
static uint16_t sum[LANES] __attribute__((aligned(128)));
static uint16_t acc[LANES] __attribute__((aligned(128)));

int main(void)
{
    for (unsigned int j = 0; j < LANES; ++j) {
        a[j] = (uint16_t)(j % 7);
        b[j] = (uint16_t)(j % 5);
        sum[j] = (uint16_t)(a[j] + b[j]);
        acc[j] = 0;
    }

#ifdef USE_HVX
    if (qurt_hvx_lock(QURT_HVX_MODE_128B) != 0)
        return -31;

    HVX_Vector vsum = *(const HVX_Vector *)sum;
    HVX_Vector vacc = *(const HVX_Vector *)acc;
#endif

    unsigned long long start = qurt_get_core_pcycles();

#ifdef USE_HVX
    for (volatile unsigned int i = 0; i < ITERATIONS; ++i)
        vacc = Q6_Vh_vadd_VhVh(vacc, vsum);

    *(HVX_Vector *)acc = vacc;
#else
    for (volatile unsigned int i = 0; i < ITERATIONS; ++i) {
        for (unsigned int j = 0; j < LANES; ++j)
            acc[j] = (uint16_t)(acc[j] + sum[j]);
    }
#endif

    unsigned long long end = qurt_get_core_pcycles();

#ifdef USE_HVX
    qurt_hvx_unlock();
#endif

    if (end <= start)
        return -32;

    for (unsigned int j = 0; j < LANES; ++j) {
        uint16_t expected =
            (uint16_t)((uint32_t)sum[j] * ITERATIONS);
        if (acc[j] != expected)
            return -41;
    }

    unsigned long long delta = end - start;
    if (delta == 0 || delta > INT_MAX)
        return -33;

    return (int)delta;
}
