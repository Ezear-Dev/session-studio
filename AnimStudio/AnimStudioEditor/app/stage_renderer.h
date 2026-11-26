// AnimStudio Editor -- draws the mannequin (and later the board) with a depth buffer into an off-screen texture,
// shown inside the 3D view by ImGui (PLAN-MANNEQUIN task 2). Direct3D 11, shaders compiled at start, no library.
#pragma once
#include <string>
#include <vector>
#include "../model/mannequin.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

class StageRenderer {
public:
    StageRenderer();
    ~StageRenderer();
    StageRenderer(const StageRenderer&) = delete;
    StageRenderer& operator=(const StageRenderer&) = delete;
    bool Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::string& err);
    bool Ready() const;
    // draws the meshes (UE component space) with viewProj (StageViewProj) into a w x h picture with a transparent
    // background; eyeDir = direction the camera looks (UE component space). nullptr if nothing could be drawn.
    ID3D11ShaderResourceView* Render(int w, int h, const float viewProj[16], const float eyeDir[3], const std::vector<const Mesh*>& meshes);
    void Release();

private:
    struct Impl;
    Impl* d_;
};
