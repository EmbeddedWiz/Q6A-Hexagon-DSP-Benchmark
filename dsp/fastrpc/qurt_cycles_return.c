#include <limits.h>
#include <stdint.h>
#include <qurt_cycles.h>

volatile uint32_t cycle_return_sink;

int main(void)
{
    unsigned long long start;
    unsigned long long end;

    start = qurt_get_core_pcycles();

    for (volatile unsigned int i = 0; i < 10000U; ++i)
        cycle_return_sink += i;

    end = qurt_get_core_pcycles();

    if (end <= start)
        return -21;

    unsigned long long delta = end - start;

    /* FastRPC runner transports the DSP main() result as a signed int. */
    if (delta == 0 || delta > INT_MAX)
        return -22;

    return (int)delta;
}
