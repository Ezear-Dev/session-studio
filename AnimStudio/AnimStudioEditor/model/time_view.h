// UI redesign Phase 4 (2026-11-25): the visible part of the timeline (zoom / pan / fit), without any UI.
// Times in seconds, x in pixels. The clip always starts at 0 and lasts `dur`.
#pragma once

struct TimeView {
    float t0 = 0.f;      // first visible time
    float span = 0.f;    // visible length; <= 0 (or >= dur): the whole clip ("fit")

    static constexpr float kMinSpan = 8.f / 30.f;   // never closer than 8 frames on the whole width

    bool Fitted(float dur) const { return span <= 0.f || span >= dur - 1e-4f; }
    float Span(float dur) const;               // visible length, clamped
    float Start(float dur) const;              // first visible time, clamped
    float Zoom(float dur) const { return dur > 0.f ? dur / Span(dur) : 1.f; }   // 1 = whole clip
    void Fit() { t0 = 0.f; span = 0.f; }
    void Clamp(float dur);                     // keeps the view inside the clip

    // the track area runs from x0 to x0 + w (no margin here: the caller passes its inner area)
    float ToX(float t, float x0, float w, float dur) const;
    float ToT(float x, float x0, float w, float dur) const;   // not clamped to the clip

    // factor > 1 zooms in; the time under `anchor` stays where it is on screen
    void ZoomAt(float factor, float anchor, float dur);
    void Pan(float dt, float dur);             // moves the view by dt seconds (clamped)
    // brings t into view: when it is outside, the view jumps so that t sits at `where` (0..1) of the width
    void Follow(float t, float dur, float where = 0.1f);
    bool Visible(float t, float dur) const { const float a = Start(dur); return t >= a - 1e-4f && t <= a + Span(dur) + 1e-4f; }
};

// ruler step: the smallest "round" step (1 frame, 2 frames, 0.1 s, 0.25 s, 0.5 s, 1 s, 2 s, 5 s, 10 s) whose spacing
// is at least minPx pixels at pxPerSec
float RulerStep(float pxPerSec, float minPx);
