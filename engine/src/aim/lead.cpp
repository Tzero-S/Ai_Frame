#include "aim/lead.h"
#include <algorithm>

namespace aim {

void LeadPredictor::SetWindow(size_t k) {
    window_ = (k ? k : 1);
    while (samples_.size() > window_) samples_.pop_front();
}

void LeadPredictor::AddSample(double t_ms, const Vec2F& p) {
    samples_.push_back(Sample{ t_ms, p });
    while (samples_.size() > window_) samples_.pop_front();
}

Vec2F LeadPredictor::Velocity() const {
    if (samples_.size() < 2) return Vec2F{};

    const double n = static_cast<double>(samples_.size());
    double t_bar = 0.0;
    double px_bar = 0.0;
    double py_bar = 0.0;
    for (const auto& s : samples_) {
        t_bar += s.t;
        px_bar += s.p.x;
        py_bar += s.p.y;
    }
    t_bar /= n;
    px_bar /= n;
    py_bar /= n;

    double num_x = 0.0;
    double num_y = 0.0;
    double den = 0.0;
    for (const auto& s : samples_) {
        const double dt = s.t - t_bar;
        num_x += dt * (s.p.x - px_bar);
        num_y += dt * (s.p.y - py_bar);
        den += dt * dt;
    }
    if (den <= 1e-12) return Vec2F{};
    return Vec2F{ static_cast<float>(num_x / den), static_cast<float>(num_y / den) };
}

Vec2F LeadPredictor::Predict(double latency_ms, float lead_gain) const {
    if (samples_.empty()) return Vec2F{};
    const Vec2F now = samples_.back().p;
    const float gain = std::clamp(lead_gain, 0.f, 1.f);
    if (gain <= 0.f || latency_ms <= 0.0) return now;

    const Vec2F v = Velocity();
    const float k = static_cast<float>(latency_ms) * gain;
    return Vec2F{ now.x + v.x * k, now.y + v.y * k };
}

} // namespace aim
