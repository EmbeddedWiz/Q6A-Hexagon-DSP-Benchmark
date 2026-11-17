#pragma once

#include <cstdint>

namespace motor_control::foc {

struct Config {
    // Q15 gains. Ki is the per-sample integral coefficient.
    int16_t kp_d_q15;
    int16_t ki_d_q15;
    int16_t kp_q_q15;
    int16_t ki_q_q15;
};

struct Input {
    int16_t ia_q15;
    int16_t ib_q15;
    int16_t id_ref_q15;
    int16_t iq_ref_q15;
    uint16_t electrical_angle; // 14-bit turn: 0..16383
};

struct Output {
    int16_t id_q15;
    int16_t iq_q15;
    int16_t vd_q15;
    int16_t vq_q15;
    uint16_t duty_a_q15; // 0..32767
    uint16_t duty_b_q15;
    uint16_t duty_c_q15;
};

class Controller {
public:
    explicit Controller(const Config& config);
    void configure(const Config& config);
    void reset();
    void step(const Input& input, Output& output);

    // Read-only accessors used by the host-side PI stress test. They do not
    // participate in the measured FOC loop.
    int32_t integral_d() const { return integral_d_; }
    int32_t integral_q() const { return integral_q_; }

private:
    Config config_;
    int32_t integral_d_;
    int32_t integral_q_;

    int16_t pi(int32_t error, int16_t kp, int16_t ki,
               int32_t& integral);
};

}
