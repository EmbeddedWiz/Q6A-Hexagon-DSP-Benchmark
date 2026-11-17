#pragma once

#include "foc_fixed.h"

namespace validation::foc {

// Independent floating-point model of the same controller.
//
// It deliberately does not call the fixed-point helpers: it is used only to
// quantify the quantization error of the fixed-point implementation, never as
// a source of expected DSP results. The CPU fixed controller (compiled from the
// project source) is the bit-exact oracle.
struct ReferenceOutput {
    double id_q15;
    double iq_q15;
    double vd_q15;
    double vq_q15;
    double duty_a_q15;
    double duty_b_q15;
    double duty_c_q15;
};

class Reference {
public:
    explicit Reference(const motor_control::foc::Config& config);

    void configure(const motor_control::foc::Config& config);
    void reset();
    void step(const motor_control::foc::Input& input, ReferenceOutput& output);

private:
    motor_control::foc::Config config_;
    double integral_d_ = 0.0;
    double integral_q_ = 0.0;
};

} // namespace validation::foc
