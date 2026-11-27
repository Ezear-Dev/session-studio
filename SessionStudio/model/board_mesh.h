// Session Studio -- the skateboard of the 3D view (PLAN-MANNEQUIN stage 2, 2026-11-26): built from the board bones
// of the pose (SKXX_SkateSkel_*), so it moves as the game's board did in the recording. Not editable, not pickable.
//
// Measured on the user's figures (2026-11-26): deck bone = deck frame (X towards the nose = front truck, Y across,
// Z up), at 5.4 cm while riding; trucks at +-21.3 cm along X, 1 cm under the deck bone; wheel centres at +-23.4 / +-8.2
// cm, 2.7 cm above the ground (wheel radius); toe bones 9-12 cm high, so the grip is about 4 cm above the deck bone.
// Old figures (before positions were recorded, e.g. Manual) have the board rotations but no positions: the board is then
// rebuilt rigidly from the deck's rotation and set on its lowest wheels.
#pragma once
#include "mannequin.h"
#include "../../SessionAnimLoader/Native/src/board.h"

struct BoardRig {
    int root = -1, deck = -1;
    int truck[2] = {-1, -1};               // front, back
    int wheel[4] = {-1, -1, -1, -1};       // FL, FR, BL, BR
    bool ok = false;                        // the deck bone exists
};
BoardRig BuildBoardRig(const SkeletonFile& s);

// colours (R | G << 8 | B << 16 | A << 24)
constexpr uint32_t kBoardGrip = 0xFF2E2B2Au, kBoardUnder = 0xFF3C64C8u, kBoardWood = 0xFF78A0C8u;
constexpr uint32_t kBoardMetal = 0xFFB4ACA8u, kBoardWheel = 0xFFD2E6F0u, kBoardNose = 0xFFE6E6E6u;

// deck geometry in deck bone space (cm)
constexpr float kDeckTop = 4.0f, kDeckThick = 1.2f, kDeckHalfWidth = 10.25f, kDeckHalfLength = 39.75f;
constexpr float kWheelRadius = 2.7f;

// the board for pose cs. positionsRecorded: the figure holds the board bone positions (EditorDoc::OriginPos of the
// deck bone). false (nothing drawn) when the deck bone is absent or not finite.
bool BuildBoardMesh(const BoardRig& r, const std::vector<FTr>& cs, bool positionsRecorded, Mesh& out);

// 2026-11-27: the game's visible board as recorded (BoardPose = its "Flipper" component). Trucks and wheels are rigid:
// they were not measured. While riding, the Flipper sits at 7.0 cm and the skeleton's deck bone at 5.3 (probe of
// 2026-11-27), so the dimensions above are lowered by 1.7 cm: derived from those two numbers, wheels at -0.1 cm.
constexpr float kFlipperAboveDeckBone = 1.7f;
bool BuildRealBoardMesh(const BoardPose& board, int deckBone, Mesh& out);
