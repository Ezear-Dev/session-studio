// Session Studio -- one timeline over two clips (2026-11-27, user: "one timeline, two files"): a trick clip, then the
// landing clip of that trick. Part 0 = the trick (0 .. first), part 1 = the landing (0 .. second). On the shared
// timeline the landing starts one frame after the trick's end, so the trick's last frame (the ground contact) and the
// landing's first frame can both be reached. Pure time arithmetic, no clip.
#pragma once

struct TwoPartTime {
    float first = 0.f;    // length of the trick clip (s)
    float second = 0.f;   // length of the landing clip (s); 0 = no second part
    float gap = 1.f / 30.f;   // one frame between the two parts on the shared timeline
    float SecondStart() const { return first + gap; }
    float Total() const { return second > 0.f ? first + gap + second : first; }
};

// shared time -> part and local time (clamped to the timeline; inside the gap: the nearest end)
void SplitTime(const TwoPartTime& tp, float shared, int& part, float& local);
// part and local time -> shared time
float JoinTime(const TwoPartTime& tp, int part, float local);
// playback: moves by dt across the two parts; at the end, back to the start when loop, else stops there.
// Returns false when playback stopped at the end.
bool AdvanceTime(const TwoPartTime& tp, int& part, float& local, float dt, bool loop);
