#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace pet {
struct Vec { double x = 0, y = 0; };
struct Bounds { double left = 0, top = 0, right = 1920, bottom = 1040; };
struct Body {
    // Position is the centre of the feet, in physical desktop pixels.
    Vec p, v;
    double width = 128, height = 140;
    bool grounded = true;
    double impact = 0;
    unsigned bounces = 0;
    void constrain(const Bounds& b) {
        p.x = std::clamp(p.x, b.left + width / 2, std::max(b.left + width / 2, b.right - width / 2));
        p.y = std::clamp(p.y, b.top + height, std::max(b.top + height, b.bottom));
    }
    void launch(Vec speed) { v = speed; grounded = false; bounces = 0; }
    void step(double seconds, const Bounds& b, double scale = 1.0, double restitution = .48) {
        // Fixed small substeps avoid tunnelling and frame-rate-dependent bounce loss.
        seconds = std::clamp(seconds, 0.0, .25);
        while (seconds > 1e-8) {
            double dt = std::min(seconds, 1.0 / 240.0); seconds -= dt;
            impact = std::max(0.0, impact - dt * 5);
            if (!grounded) v.y += 1850 * scale * dt;
            p.x += v.x * dt; p.y += v.y * dt;
            double l = b.left + width / 2, r = std::max(l, b.right - width / 2);
            if (p.x < l) { p.x = l; if (v.x < 0) v.x *= -.55; }
            if (p.x > r) { p.x = r; if (v.x > 0) v.x *= -.55; }
            if (p.y < b.top + height) { p.y = b.top + height; if (v.y < 0) v.y *= -.35; }
            if (p.y >= b.bottom) {
                p.y = b.bottom;
                if (!grounded && v.y > 0) {
                    impact = std::min(1.0, v.y / (900 * scale));
                    ++bounces;
                    v.y *= -restitution;
                    if (std::abs(v.y) < 140 * scale) { grounded = true; v.y = 0; }
                }
            } else if (grounded) grounded = false;
            v.x *= std::exp(-(grounded ? 7.0 : .28) * dt);
            if (std::abs(v.x) < 2 * scale) v.x = 0;
        }
    }
};
class ThrowHistory {
    struct Sample { Vec p; double t; };
    std::deque<Sample> samples;
public:
    void clear() { samples.clear(); }
    void add(Vec p, double t) {
        samples.push_back({p,t});
        while (samples.size() > 2 && samples.front().t < t - .12) samples.pop_front();
    }
    Vec velocity(double now, double maxSpeed = 2400) const {
        if (samples.size() < 2 || now - samples.back().t > .10) return {};
        const auto& a = samples.front(); const auto& b = samples.back();
        double dt = b.t - a.t;
        if (dt < .008) return {};
        Vec v{(b.p.x-a.p.x)/dt,(b.p.y-a.p.y)/dt};
        double speed = std::hypot(v.x,v.y);
        if (speed > maxSpeed) { v.x *= maxSpeed/speed; v.y *= maxSpeed/speed; }
        return v;
    }
};
struct Track {
    std::vector<int> frames;
    std::vector<double> durations;
    bool loop = true;
    std::string fallback = "idle";
    double length() const { double n = 0; for (auto d : durations) n += d; return n; }
};
inline int frameAt(const std::map<std::string,Track>& tracks, std::string name, double elapsed) {
    // Bound fallback traversal, including mutually recursive user-defined tracks.
    for (int depth = 0; depth < 16; ++depth) {
        auto it = tracks.find(name);
        if (it == tracks.end() || it->second.frames.empty()) return 0;
        const auto& t = it->second;
        double length = t.length();
        if (length <= 0) return t.frames.front();
        if (elapsed >= length) {
            if (t.loop) elapsed = std::fmod(elapsed,length);
            else { elapsed -= length; name = t.fallback; continue; }
        }
        for (size_t i = 0; i < t.frames.size(); ++i) {
            if (elapsed < t.durations[i]) return t.frames[i];
            elapsed -= t.durations[i];
        }
        return t.frames.back();
    }
    return 0;
}
inline int lookDirection(double dx, double dy) {
    constexpr double pi = 3.14159265358979323846;
    double a = std::atan2(dx,-dy); if (a < 0) a += 2*pi;
    return static_cast<int>(std::floor(a/(2*pi/16)+.5)) % 16;
}
}
