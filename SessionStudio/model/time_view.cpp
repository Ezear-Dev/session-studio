// UI redesign Phase 4 (2026-11-25): timeline view (see time_view.h).
#include "time_view.h"
#include <algorithm>
#include <cmath>

float TimeView::Span(float dur) const {
    if (dur <= 0.f) return 1.f;
    if (Fitted(dur)) return dur;
    return std::clamp(span, std::min(kMinSpan, dur), dur);
}

float TimeView::Start(float dur) const {
    if (dur <= 0.f) return 0.f;
    return std::clamp(t0, 0.f, std::max(0.f, dur - Span(dur)));
}

void TimeView::Clamp(float dur) {
    if (Fitted(dur)) { Fit(); return; }
    span = Span(dur);
    t0 = Start(dur);
}

float TimeView::ToX(float t, float x0, float w, float dur) const {
    return x0 + w * (t - Start(dur)) / Span(dur);
}

float TimeView::ToT(float x, float x0, float w, float dur) const {
    if (w <= 0.f) return Start(dur);
    return Start(dur) + (x - x0) / w * Span(dur);
}

void TimeView::ZoomAt(float factor, float anchor, float dur) {
    if (dur <= 0.f || factor <= 0.f) return;
    const float s0 = Span(dur), a0 = Start(dur);
    const float s1 = std::clamp(s0 / factor, std::min(kMinSpan, dur), dur);
    const float u = s0 > 0.f ? (anchor - a0) / s0 : 0.f;   // where the anchor sits on the width
    span = s1;
    t0 = anchor - u * s1;
    Clamp(dur);
}

void TimeView::Pan(float dt, float dur) {
    if (Fitted(dur)) return;
    t0 = Start(dur) + dt;
    Clamp(dur);
}

void TimeView::Follow(float t, float dur, float where) {
    if (Fitted(dur) || Visible(t, dur)) return;
    t0 = t - where * Span(dur);
    Clamp(dur);
}

float RulerStep(float pxPerSec, float minPx) {
    static const float kSteps[] = {1.f / 30.f, 2.f / 30.f, 0.1f, 0.25f, 0.5f, 1.f, 2.f, 5.f, 10.f};
    for (float s : kSteps) if (s * pxPerSec >= minPx) return s;
    return 10.f;
}
