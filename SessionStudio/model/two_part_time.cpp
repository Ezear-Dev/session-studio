#include "two_part_time.h"
#include <algorithm>
#include <cmath>

void SplitTime(const TwoPartTime& tp, float shared, int& part, float& local) {
    const float g = std::clamp(std::isfinite(shared) ? shared : 0.f, 0.f, tp.Total());
    if (tp.second <= 0.f || g <= tp.first + tp.gap * 0.5f) { part = 0; local = std::min(g, tp.first); return; }
    part = 1;
    local = std::clamp(g - tp.SecondStart(), 0.f, tp.second);
}

float JoinTime(const TwoPartTime& tp, int part, float local) {
    if (part == 1 && tp.second > 0.f) return tp.SecondStart() + std::clamp(local, 0.f, tp.second);
    return std::clamp(local, 0.f, tp.first);
}

bool AdvanceTime(const TwoPartTime& tp, int& part, float& local, float dt, bool loop) {
    float g = JoinTime(tp, part, local) + std::max(0.f, dt);
    const float total = tp.Total();
    bool running = true;
    if (g > total) {
        if (loop && total > 0.f) g = std::fmod(g, total);
        else { g = total; running = false; }
    }
    if (tp.second > 0.f && g > tp.first && g < tp.SecondStart()) g = tp.SecondStart();   // playback steps over the gap
    SplitTime(tp, g, part, local);
    return running;
}
