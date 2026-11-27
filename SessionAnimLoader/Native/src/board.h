// The game's VISIBLE board (2026-11-27). It is not the skeleton's board bones (they do not flip): it is its own actor,
// and its "Flipper" component is the deck seen in game. The Lua reads that component and the skater mesh every frame;
// here the deck is turned into the mesh's component space, the space of the pose and of the move recordings.
// Measured with a probe on 2026-11-27 (regular stance): riding, the Flipper sits at (0, 0, 7.0) with no rotation
// (the skeleton's deck bone at 5.3); a kickflip turns it 360 degrees about its X while the bone stays still.
#pragma once
#include "math.h"

struct BoardPose {
    float p[3] = {0.f, 0.f, 0.f};   // the deck (Flipper component) in the mesh's component space, cm
    Quat q = QIdent;
};

constexpr int kBoardValues = 17;           // numbers the Lua sends, in the order below
constexpr double kBoardMaxDist = 200.0;    // cm from the skater mesh: farther is a loose or a stale board

// in: mesh position (3), mesh rotation quaternion x y z w (4), mesh scale (3), deck position (3), deck quaternion (4),
// all in world space. false (why says it in a few words) if a value is not finite, a quaternion is not a unit one,
// the mesh is scaled (not seen in game: the conversion would be wrong) or the deck is too far from the skater.
bool BoardInMesh(const double in[kBoardValues], BoardPose& out, const char*& why);
