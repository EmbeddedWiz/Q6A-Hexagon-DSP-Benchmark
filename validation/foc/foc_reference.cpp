#include "foc_reference.h"

#include <algorithm>
#include <cmath>

namespace validation::foc {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInvSqrt3 = 1.0 / 1.73205080756887729353;
constexpr double kSqrt3Over2 = 0.86602540378443864676;
constexpr double kQ15 = 32768.0;

double clamp_duty(double value)
{
    return std::clamp(value, 0.0, 32767.0);
}

double pi(double error, double kp_q15, double ki_q15, double& integral)
{
    integral += error * ki_q15 / kQ15;
    integral = std::clamp(integral, -32767.0, 32767.0);
    return std::clamp(error * kp_q15 / kQ15 + integral, -32767.0, 32767.0);
}

} // namespace

Reference::Reference(const motor_control::foc::Config& config)
    : config_(config)
{
}

void Reference::configure(const motor_control::foc::Config& config)
{
    config_ = config;
    reset();
}

void Reference::reset()
{
    integral_d_ = 0.0;
    integral_q_ = 0.0;
}

void Reference::step(const motor_control::foc::Input& in, ReferenceOutput& out)
{
    const double alpha = static_cast<double>(in.ia_q15);
    const double beta = (static_cast<double>(in.ia_q15) +
                         2.0 * static_cast<double>(in.ib_q15)) * kInvSqrt3;

    const double theta = 2.0 * kPi *
        static_cast<double>(in.electrical_angle) / 16384.0;
    const double sn = std::sin(theta);
    const double cs = std::cos(theta);

    out.id_q15 = std::clamp(alpha * cs + beta * sn, -32767.0, 32767.0);
    out.iq_q15 = std::clamp(-alpha * sn + beta * cs, -32767.0, 32767.0);

    const double err_d = static_cast<double>(in.id_ref_q15) - out.id_q15;
    const double err_q = static_cast<double>(in.iq_ref_q15) - out.iq_q15;
    out.vd_q15 = pi(err_d, config_.kp_d_q15, config_.ki_d_q15, integral_d_);
    out.vq_q15 = pi(err_q, config_.kp_q_q15, config_.ki_q_q15, integral_q_);

    const double v_alpha = std::clamp(out.vd_q15 * cs - out.vq_q15 * sn,
                                      -32767.0, 32767.0);
    const double v_beta = std::clamp(out.vd_q15 * sn + out.vq_q15 * cs,
                                     -32767.0, 32767.0);

    const double va = v_alpha;
    const double vb = -v_alpha / 2.0 + v_beta * kSqrt3Over2;
    const double vc = -v_alpha / 2.0 - v_beta * kSqrt3Over2;

    const double vmax = std::max({va, vb, vc});
    const double vmin = std::min({va, vb, vc});
    const double common = -(vmax + vmin) / 2.0;

    out.duty_a_q15 = clamp_duty(16384.0 + (va + common) / 2.0);
    out.duty_b_q15 = clamp_duty(16384.0 + (vb + common) / 2.0);
    out.duty_c_q15 = clamp_duty(16384.0 + (vc + common) / 2.0);
}

} // namespace validation::foc
