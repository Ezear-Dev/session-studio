// One timeline over a trick clip and its landing clip (2026-11-27)
#include "check.h"
#include "../../../SessionStudio/model/two_part_time.h"

TEST(two_part_split_and_join) {
    TwoPartTime tp;
    tp.first = 0.77f; tp.second = 1.2f;
    CHECK_NEAR(tp.SecondStart(), 0.77f + 1.f / 30.f, 1e-6);
    CHECK_NEAR(tp.Total(), 0.77f + 1.f / 30.f + 1.2f, 1e-6);
    int part; float local;
    SplitTime(tp, 0.f, part, local);   CHECK(part == 0); CHECK_NEAR(local, 0.f, 1e-6);
    SplitTime(tp, 0.77f, part, local); CHECK(part == 0); CHECK_NEAR(local, 0.77f, 1e-6);   // the trick's end can be reached
    SplitTime(tp, tp.SecondStart(), part, local); CHECK(part == 1); CHECK_NEAR(local, 0.f, 1e-6);   // and the landing's start
    SplitTime(tp, 0.77f + 0.01f, part, local); CHECK(part == 0); CHECK_NEAR(local, 0.77f, 1e-6);   // in the gap: nearest end
    SplitTime(tp, 0.77f + 0.025f, part, local); CHECK(part == 1); CHECK_NEAR(local, 0.f, 1e-6);
    SplitTime(tp, 99.f, part, local);  CHECK(part == 1); CHECK_NEAR(local, 1.2f, 1e-6);
    SplitTime(tp, -5.f, part, local);  CHECK(part == 0); CHECK_NEAR(local, 0.f, 1e-6);
    for (float g : {0.f, 0.3f, 0.77f, 0.81f, 1.5f, 2.0f}) {   // split then join gives the time back (outside the gap)
        SplitTime(tp, g, part, local);
        CHECK_NEAR(JoinTime(tp, part, local), g, 1e-5);
    }
    TwoPartTime one;   // no landing: a single part, as before
    one.first = 2.f;
    CHECK_NEAR(one.Total(), 2.f, 1e-6);
    SplitTime(one, 3.f, part, local); CHECK(part == 0); CHECK_NEAR(local, 2.f, 1e-6);
    CHECK_NEAR(JoinTime(one, 1, 0.5f), 0.5f, 1e-6);
}

TEST(two_part_playback) {
    TwoPartTime tp;
    tp.first = 1.f; tp.second = 0.5f;
    int part = 0; float local = 0.9f;
    CHECK(AdvanceTime(tp, part, local, 0.2f, false));   // past the trick's end: into the landing
    CHECK(part == 1); CHECK_NEAR(local, 1.1f - tp.SecondStart(), 1e-5);
    part = 0; local = 0.99f;
    CHECK(AdvanceTime(tp, part, local, 0.02f, false));   // a step that ends in the gap goes to the landing's start
    CHECK(part == 1); CHECK_NEAR(local, 0.f, 1e-6);
    part = 1; local = 0.45f;
    CHECK(!AdvanceTime(tp, part, local, 0.2f, false));   // the end: stops there
    CHECK(part == 1); CHECK_NEAR(local, 0.5f, 1e-6);
    part = 1; local = 0.45f;
    CHECK(AdvanceTime(tp, part, local, 0.2f, true));     // loop: back to the trick's start
    CHECK(part == 0); CHECK(local < 0.2f);
    part = 0; local = 0.5f;
    CHECK(AdvanceTime(tp, part, local, -1.f, false));    // never backwards
    CHECK(part == 0); CHECK_NEAR(local, 0.5f, 1e-6);
}
