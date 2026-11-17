#include <stdint.h>
#include <stddef.h>
#include <hexagon_types.h>
#include <hexagon_protos.h>
#include <qurt_hvx.h>

#define VECTOR_BYTES 128
#define LANES (VECTOR_BYTES / sizeof(int16_t))

static int16_t a[LANES] __attribute__((aligned(VECTOR_BYTES)));
static int16_t b[LANES] __attribute__((aligned(VECTOR_BYTES)));
static int16_t out[LANES] __attribute__((aligned(VECTOR_BYTES)));

volatile int hvx_validation_errors;

int main(void)
{
    int rc = qurt_hvx_lock(QURT_HVX_MODE_128B);
    if (rc != 0)
        return 10;

    for (int i = 0; i < LANES; ++i) {
        a[i] = (int16_t)(i - 32);
        b[i] = (int16_t)(100 + i);
    }

    HVX_Vector va = *(const HVX_Vector *)a;
    HVX_Vector vb = *(const HVX_Vector *)b;
    HVX_Vector vsum = Q6_Vh_vadd_VhVh(va, vb);

    *(HVX_Vector *)out = vsum;

    int errors = 0;
    for (int i = 0; i < LANES; ++i) {
        int16_t expected = (int16_t)(a[i] + b[i]);
        if (out[i] != expected)
            ++errors;
    }

    hvx_validation_errors = errors;
    qurt_hvx_unlock();

    return errors == 0 ? 0 : 20;
}
