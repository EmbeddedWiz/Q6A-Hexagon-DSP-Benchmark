#include <stdio.h>
#include <qurt_cycles.h>

volatile unsigned long long cycle_sink;

int main(void)
{
    unsigned long long a, b, c;

    a = qurt_get_core_pcycles();
    for (volatile unsigned int i = 0; i < 100000U; ++i)
        cycle_sink += i;
    b = qurt_get_core_pcycles();

    for (volatile unsigned int i = 0; i < 100000U; ++i)
        cycle_sink += i;
    c = qurt_get_core_pcycles();

    printf("core_pcycles_before=%llu\n", a);
    printf("core_pcycles_after_1=%llu\n", b);
    printf("core_pcycles_after_2=%llu\n", c);
    printf("delta_1=%llu\n", b - a);
    printf("delta_2=%llu\n", c - b);
    printf("checksum=%llu\n", cycle_sink);
    return (b > a && c > b) ? 0 : 21;
}
