#include "foc_fixed.h"
#include "motor_control_fixed.h"

#include <cstdint>

namespace motor_control::foc {
namespace {

constexpr int32_t Q15_ONE = 32767;
constexpr int32_t HALF_Q15 = 16384;
constexpr int32_t INV_SQRT3_Q15 = 18919;
constexpr int32_t SQRT3_OVER_2_Q15 = 28378;

int16_t sat16(int64_t x)
{
    if (x > 32767) return 32767;
    if (x < -32767) return -32767;
    return static_cast<int16_t>(x);
}

int32_t mul_q15(int32_t a, int32_t b)
{
    return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> 15);
}

uint16_t clamp_duty(int32_t duty)
{
    if (duty < 0) duty = 0;
    if (duty > Q15_ONE) duty = Q15_ONE;
    return static_cast<uint16_t>(duty);
}

int32_t max3(int32_t a, int32_t b, int32_t c)
{
    int32_t m = (a > b) ? a : b;
    return (m > c) ? m : c;
}

int32_t min3(int32_t a, int32_t b, int32_t c)
{
    int32_t m = (a < b) ? a : b;
    return (m < c) ? m : c;
}

}

Controller::Controller(const Config& config)
    : config_(config), integral_d_(0), integral_q_(0)
{
}

void Controller::reset()
{
    integral_d_ = 0;
    integral_q_ = 0;
}

void Controller::configure(const Config& config)
{
    config_ = config;
    reset();
}

int16_t Controller::pi(int32_t error, int16_t kp, int16_t ki,
                       int32_t& integral)
{
    const int32_t proportional = mul_q15(error, kp);
    const int32_t increment = mul_q15(error, ki);

    integral += increment;
    if (integral > 32767) integral = 32767;
    if (integral < -32767) integral = -32767;
    return sat16(static_cast<int64_t>(proportional) + integral);
}

void Controller::step(const Input& in, Output& out)
{
    // Clarke: alpha = ia; beta = (ia + 2*ib)/sqrt(3).
    const int32_t alpha = in.ia_q15;
    const int32_t beta = mul_q15(
        static_cast<int32_t>(in.ia_q15) +
        2 * static_cast<int32_t>(in.ib_q15),
        INV_SQRT3_Q15);

    // One CORDIC pass provides both components for the same angle.
    int16_t sn = 0;
    int16_t cs = 0;
    motor_control::fixed::sincos_q15(in.electrical_angle, sn, cs);

    // Park transform.
    out.id_q15 = sat16(
        static_cast<int64_t>(mul_q15(alpha, cs)) +
        mul_q15(beta, sn));

    out.iq_q15 = sat16(
        -static_cast<int64_t>(mul_q15(alpha, sn)) +
        mul_q15(beta, cs));

    // Current PI controllers. Gains must be tuned for the target plant
    // and sampling period; these are supplied by the caller.
    const int32_t err_d =
        static_cast<int32_t>(in.id_ref_q15) - out.id_q15;
    const int32_t err_q =
        static_cast<int32_t>(in.iq_ref_q15) - out.iq_q15;

    out.vd_q15 = pi(err_d, config_.kp_d_q15,
                    config_.ki_d_q15, integral_d_);
    out.vq_q15 = pi(err_q, config_.kp_q_q15,
                    config_.ki_q_q15, integral_q_);

    // Inverse Park.
    const int32_t v_alpha = sat16(
        static_cast<int64_t>(mul_q15(out.vd_q15, cs)) -
        mul_q15(out.vq_q15, sn));

    const int32_t v_beta = sat16(
        static_cast<int64_t>(mul_q15(out.vd_q15, sn)) +
        mul_q15(out.vq_q15, cs));

    // Three-phase reference voltages.
    const int32_t va = v_alpha;
    const int32_t vb =
        -v_alpha / 2 + mul_q15(v_beta, SQRT3_OVER_2_Q15);
    const int32_t vc =
        -v_alpha / 2 - mul_q15(v_beta, SQRT3_OVER_2_Q15);

    // Centered SVPWM common-mode injection.
    const int32_t vmax = max3(va, vb, vc);
    const int32_t vmin = min3(va, vb, vc);
    const int32_t common = -(vmax + vmin) / 2;

    out.duty_a_q15 = clamp_duty(HALF_Q15 + (va + common) / 2);
    out.duty_b_q15 = clamp_duty(HALF_Q15 + (vb + common) / 2);
    out.duty_c_q15 = clamp_duty(HALF_Q15 + (vc + common) / 2);
}

}
