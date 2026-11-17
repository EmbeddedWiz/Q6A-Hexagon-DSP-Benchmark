#include "motor_control_fixed.h"

namespace motor_control::fixed {

namespace {

/*
 * External phase representation:
 *
 *     14-bit angle
 *     full turn = 16384
 *
 * Internal CORDIC representation:
 *
 *     18-bit angle
 *     full turn = 262144
 *
 * The extra four fractional angular bits are internal only.
 */
constexpr uint16_t kAngleMask = 0x3FFF;

constexpr int32_t kInternalAngleBits = 18;
constexpr int32_t kInternalFullTurn = 1 << kInternalAngleBits;
constexpr int32_t kInternalHalfTurn = kInternalFullTurn / 2;
constexpr int32_t kInternalQuarterTurn = kInternalFullTurn / 4;

constexpr int32_t kAngleFractionBits =
    kInternalAngleBits - 14;

/*
 * atan(2^-i), represented in the 18-bit internal phase domain:
 *
 *     atan(2^-i) / (2*pi) * 262144
 *
 * rounded to the nearest integer.
 */
constexpr int32_t kAtanTable[] = {
    32768,  // atan(2^0)
    19344,  // atan(2^-1)
    10221,  // atan(2^-2)
     5188,  // atan(2^-3)
     2604,  // atan(2^-4)
     1303,  // atan(2^-5)
      652,  // atan(2^-6)
      326,  // atan(2^-7)
      163,  // atan(2^-8)
       81,  // atan(2^-9)
       41,  // atan(2^-10)
       20,  // atan(2^-11)
       10,  // atan(2^-12)
        5,  // atan(2^-13)
        3,  // atan(2^-14)
        1   // atan(2^-15)
};

constexpr std::size_t kCordicIterations =
    sizeof(kAtanTable) / sizeof(kAtanTable[0]);

/*
 * CORDIC gain compensation.
 *
 *     1 / K ~= 0.607252935
 *
 * Q15:
 *
 *     0.607252935 * 32767 ~= 19898
 */
constexpr int32_t kCordicInitialX = 19898;

struct CordicResult {
    int16_t sin;
    int16_t cos;
};

CordicResult cordic(uint16_t angle)
{
    const uint16_t normalized =
        angle & kAngleMask;

    /*
     * Convert the external 14-bit phase into the
     * higher-resolution internal CORDIC representation.
     */
    const int32_t internal_angle =
        static_cast<int32_t>(normalized)
        << kAngleFractionBits;

    const int32_t quadrant =
        internal_angle / kInternalQuarterTurn;

    /*
     * Reduce the angle to [0, pi/2].
     *
     * The CORDIC kernel itself therefore operates only
     * inside its normal convergence range.
     */
    int32_t reduced_angle = 0;

    switch (quadrant) {
    case 0:
        reduced_angle = internal_angle;
        break;

    case 1:
        reduced_angle =
            kInternalHalfTurn - internal_angle;
        break;

    case 2:
        reduced_angle =
            internal_angle - kInternalHalfTurn;
        break;

    default:
        reduced_angle =
            kInternalFullTurn - internal_angle;
        break;
    }

    int32_t z = reduced_angle;

    /*
     * Q15 vector coordinates.
     */
    int32_t x = kCordicInitialX;
    int32_t y = 0;

    for (std::size_t i = 0;
         i < kCordicIterations;
         ++i) {

        const int32_t x_old = x;
        const int32_t y_old = y;

        const int32_t x_shift =
            y_old >> i;

        const int32_t y_shift =
            x_old >> i;

        if (z >= 0) {
            x = x_old - x_shift;
            y = y_old + y_shift;
            z -= kAtanTable[i];
        } else {
            x = x_old + x_shift;
            y = y_old - y_shift;
            z += kAtanTable[i];
        }
    }

    /*
     * Restore the original quadrant.
     */
    int32_t sin_value = y;
    int32_t cos_value = x;

    switch (quadrant) {
    case 0:
        break;

    case 1:
        sin_value = y;
        cos_value = -x;
        break;

    case 2:
        sin_value = -y;
        cos_value = -x;
        break;

    default:
        sin_value = -y;
        cos_value = x;
        break;
    }

    if (sin_value > 32767)
        sin_value = 32767;

    if (sin_value < -32767)
        sin_value = -32767;

    if (cos_value > 32767)
        cos_value = 32767;

    if (cos_value < -32767)
        cos_value = -32767;

    return CordicResult{
        static_cast<int16_t>(sin_value),
        static_cast<int16_t>(cos_value)
    };
}

} // namespace

int16_t sin_q15(uint16_t angle)
{
    return cordic(angle).sin;
}

int16_t cos_q15(uint16_t angle)
{
    return cordic(angle).cos;
}

void sincos_q15(uint16_t angle, int16_t& sin_value, int16_t& cos_value)
{
    const CordicResult result = cordic(angle);
    sin_value = result.sin;
    cos_value = result.cos;
}

void compute(
    const Input& input,
    Output& output)
{
    const uint16_t angle =
        input.angle & kAngleMask;

    /*
     * Three equally spaced phases:
     *
     *     A = angle
     *     B = angle - 120 degrees
     *     C = angle - 240 degrees
     *
     * 14-bit phase representation:
     *
     *     120 degrees ~= 5461
     *     240 degrees ~= 10923
     */
    constexpr uint16_t kPhaseBOffset = 5461;
    constexpr uint16_t kPhaseCOffset = 10923;

    const uint16_t angle_b =
        static_cast<uint16_t>(
            (static_cast<uint32_t>(angle) +
             16384U -
             kPhaseBOffset) &
            kAngleMask);

    const uint16_t angle_c =
        static_cast<uint16_t>(
            (static_cast<uint32_t>(angle) +
             16384U -
             kPhaseCOffset) &
            kAngleMask);

    const CordicResult a =
        cordic(angle);

    const CordicResult b =
        cordic(angle_b);

    const CordicResult c =
        cordic(angle_c);

    output.sin_a = a.sin;
    output.sin_b = b.sin;
    output.sin_c = c.sin;

    output.cos_a = a.cos;
    output.cos_b = b.cos;
    output.cos_c = c.cos;
}

void compute_batch(
    const uint16_t* angles,
    Output* outputs,
    std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        fixed::compute(
            Input{angles[i]},
            outputs[i]);
    }
}

} // namespace motor_control::fixed
