// UI redesign Phase 4 (2026-11-25): timeline zoom / pan / fit (AnimStudioEditor/model/time_view).
#include "check.h"
#include "../../AnimStudioEditor/model/time_view.h"
#include <cmath>

TEST(time_view_fit_maps_the_whole_clip) {
    TimeView v;
    CHECK(v.Fitted(2.f));
    CHECK_NEAR(v.ToX(0.f, 100.f, 400.f, 2.f), 100.0, 1e-4);
    CHECK_NEAR(v.ToX(2.f, 100.f, 400.f, 2.f), 500.0, 1e-4);
    CHECK_NEAR(v.ToT(300.f, 100.f, 400.f, 2.f), 1.0, 1e-4);
    CHECK_NEAR(v.Zoom(2.f), 1.0, 1e-5);
}

TEST(time_view_zoom_keeps_the_anchor_in_place) {
    TimeView v;
    const float dur = 3.f, x0 = 0.f, w = 600.f;
    const float anchor = 1.2f;
    const float xBefore = v.ToX(anchor, x0, w, dur);
    v.ZoomAt(4.f, anchor, dur);
    CHECK_NEAR(v.Span(dur), 0.75, 1e-4);
    CHECK_NEAR(v.ToX(anchor, x0, w, dur), xBefore, 0.5);
    CHECK_NEAR(v.Zoom(dur), 4.0, 1e-3);
    // zooming back out past the clip returns to "fit"
    v.ZoomAt(0.1f, anchor, dur);
    CHECK(v.Fitted(dur));
    CHECK_NEAR(v.Start(dur), 0.0, 1e-6);
}

TEST(time_view_limits_and_pan) {
    TimeView v;
    const float dur = 2.f;
    v.ZoomAt(1000.f, 1.f, dur);                           // never closer than 8 frames
    CHECK_NEAR(v.Span(dur), TimeView::kMinSpan, 1e-5);
    v.Pan(-5.f, dur);                                      // never before 0
    CHECK_NEAR(v.Start(dur), 0.0, 1e-6);
    v.Pan(50.f, dur);                                      // never after the end
    CHECK_NEAR(v.Start(dur) + v.Span(dur), dur, 1e-4);
    // a shorter clip (another clip opened): the view stays inside it
    v.Clamp(0.2f);
    CHECK(v.Fitted(0.2f));
}

TEST(time_view_follow_brings_the_playhead_back) {
    TimeView v;
    const float dur = 4.f;
    v.ZoomAt(4.f, 0.f, dur);                               // shows 0 .. 1 s
    CHECK(v.Visible(0.5f, dur));
    CHECK(!v.Visible(2.5f, dur));
    v.Follow(2.5f, dur);
    CHECK(v.Visible(2.5f, dur));
    const float before = v.Start(dur);
    v.Follow(2.6f, dur);                                   // already visible: no jump
    CHECK_NEAR(v.Start(dur), before, 1e-6);
}

TEST(time_view_ruler_step) {
    CHECK_NEAR(RulerStep(3000.f, 60.f), 1.0 / 30.0, 1e-6);   // very close: every frame
    CHECK_NEAR(RulerStep(300.f, 60.f), 0.25, 1e-6);
    CHECK_NEAR(RulerStep(50.f, 60.f), 2.0, 1e-6);
}
