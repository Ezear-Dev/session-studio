// Session Studio -- orbit camera, projection, bone picking, matrices for ImGuizmo.
// World = the skater's component space (UE: Z up, centimetres). Column-major matrices (ImGuizmo / OpenGL convention).
#pragma once
#include <vector>
#include "../../SessionAnimLoader/Native/src/math.h"

struct OrbitCamera {
    float yawDeg = 200.f, pitchDeg = 15.f, dist = 260.f, fovDeg = 45.f;
    float target[3] = {0.f, 0.f, 100.f};
    void Eye(float out[3]) const;
    void View(float m[16]) const;
    void Proj(float aspect, float m[16]) const;
};

// false if the point is behind the camera
bool Project(const OrbitCamera& c, float aspect, const float p[3], float vpW, float vpH, float& sx, float& sy);
// pickable bone whose joint is closest to the click (radius in pixels), -1 otherwise
int PickBone(const OrbitCamera& c, float aspect, const std::vector<FTr>& cs, const std::vector<bool>& selectable,
             float vpW, float vpH, float mx, float my, float radiusPx = 12.f);
// 2026-11-26 (mannequin): Proj * View for a UE component space point, with the view's Y mirror and D3D clip depth
// (0..w): screen x / y are the same as Project() of the mirrored point. Column-major. The mirror flips the winding.
void StageViewProj(const OrbitCamera& c, float aspect, float out[16]);
void BoneMatrix(const FTr& t, float m[16]);          // rotation + position, no scale
bool MatrixRotation(const float m[16], Quat& q);     // rotation of a matrix (orthonormalised)
// inverse of QFromRotatorDeg (UE4 FQuat::Rotator): degrees pitch, yaw, roll
void QToRotatorDeg(Quat q, float& pitch, float& yaw, float& roll);
