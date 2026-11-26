// AnimStudio Editor -- off-screen renderer of the 3D view's solid body (PLAN-MANNEQUIN task 2), see stage_renderer.h
#include "stage_renderer.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>

namespace {
const char* kShader = R"(
cbuffer CB : register(b0) { float4x4 vp; float4 eyeDir; };
struct VSI { float3 p : POSITION; float3 n : NORMAL; float4 c : COLOR; };
struct PSI { float4 p : SV_POSITION; float3 n : NORMAL; float4 c : COLOR; };
PSI vs(VSI i) { PSI o; o.p = mul(vp, float4(i.p, 1.0)); o.n = i.n; o.c = i.c; return o; }
// studio light (2026-11-26 second look): sky / ground ambient, a key light above and to the side of the camera,
// a soft fill from the other side, a faint highlight and rim, so the volumes read
float4 ps(PSI i) : SV_Target {
    float3 n = normalize(i.n);
    float3 V = -normalize(eyeDir.xyz);
    float3 up = float3(0.0, 0.0, 1.0);
    float3 right = cross(V, up);
    right = dot(right, right) > 1e-6 ? normalize(right) : float3(1.0, 0.0, 0.0);
    float3 key = normalize(V * 0.55 + up * 0.75 + right * 0.45);
    float3 fill = normalize(V * 0.6 - right * 0.7 + up * 0.05);
    float hemi = 0.5 + 0.5 * n.z;
    float3 amb = lerp(float3(0.16, 0.16, 0.19), float3(0.36, 0.37, 0.40), hemi);
    float k = saturate(dot(n, key));
    float f = saturate(dot(n, fill)) * 0.22;
    float spec = pow(saturate(dot(n, normalize(key + V))), 28.0) * 0.10;
    float rim = pow(1.0 - saturate(dot(n, V)), 3.0) * 0.12;
    float3 c = i.c.rgb * (amb + k * 0.78 + f) + spec + rim;
    return float4(saturate(c), 1.0);
}
)";
template <class T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }
constexpr DXGI_FORMAT kColor = DXGI_FORMAT_R8G8B8A8_UNORM, kDepth = DXGI_FORMAT_D24_UNORM_S8_UINT;
} // namespace

struct StageRenderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    UINT vbCap = 0, ibCap = 0;   // in elements
    ID3D11RasterizerState* rs = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11BlendState* bs = nullptr;
    // targets
    int w = 0, h = 0;
    UINT samples = 1;
    ID3D11Texture2D* colorTex = nullptr;     // multisampled (or the picture itself when samples == 1)
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* depthTex = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11Texture2D* picTex = nullptr;       // resolved picture
    ID3D11ShaderResourceView* srv = nullptr;
    bool ready = false;

    void ReleaseTargets() {
        Rel(srv); Rel(picTex); Rel(dsv); Rel(depthTex); Rel(rtv); Rel(colorTex);
        w = h = 0;
    }
    void ReleaseAll() {
        ReleaseTargets();
        Rel(bs); Rel(dss); Rel(rs); Rel(ib); Rel(vb); Rel(cb); Rel(layout); Rel(ps); Rel(vs);
        vbCap = ibCap = 0;
        ready = false;
        dev = nullptr; ctx = nullptr;
    }
    bool MakeTargets(int W, int H) {
        ReleaseTargets();
        UINT q = 0;
        samples = (SUCCEEDED(dev->CheckMultisampleQualityLevels(kColor, 4, &q)) && q > 0) ? 4 : 1;
        for (int attempt = 0; attempt < 2; attempt++) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = (UINT)W; td.Height = (UINT)H; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = kColor; td.SampleDesc.Count = samples; td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | (samples == 1 ? D3D11_BIND_SHADER_RESOURCE : 0);
            bool ok = SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &colorTex)) && SUCCEEDED(dev->CreateRenderTargetView(colorTex, nullptr, &rtv));
            D3D11_TEXTURE2D_DESC dd = td;
            dd.Format = kDepth; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            ok = ok && SUCCEEDED(dev->CreateTexture2D(&dd, nullptr, &depthTex)) && SUCCEEDED(dev->CreateDepthStencilView(depthTex, nullptr, &dsv));
            if (ok && samples > 1) {
                D3D11_TEXTURE2D_DESC pd = td;
                pd.SampleDesc.Count = 1; pd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                ok = SUCCEEDED(dev->CreateTexture2D(&pd, nullptr, &picTex));
            } else if (ok) {
                picTex = colorTex; picTex->AddRef();
            }
            ok = ok && SUCCEEDED(dev->CreateShaderResourceView(picTex, nullptr, &srv));
            if (ok) { w = W; h = H; return true; }
            ReleaseTargets();
            if (samples == 1) return false;
            samples = 1;   // multisampling refused: one sample
        }
        return false;
    }
    bool Grow(ID3D11Buffer*& buf, UINT& cap, UINT need, UINT stride, UINT bind) {
        if (buf && cap >= need) return true;
        Rel(buf);
        UINT c = std::max<UINT>(need + need / 2, 1024);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = c * stride; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = bind; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &buf))) { cap = 0; return false; }
        cap = c;
        return true;
    }
};

StageRenderer::StageRenderer() : d_(new Impl) {}
StageRenderer::~StageRenderer() { Release(); delete d_; }
bool StageRenderer::Ready() const { return d_->ready; }
void StageRenderer::Release() { d_->ReleaseAll(); }

bool StageRenderer::Init(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::string& err) {
    Release();
    if (!dev || !ctx) { err = "no graphics device"; return false; }
    Impl& d = *d_;
    d.dev = dev; d.ctx = ctx;
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* eb = nullptr;
    const size_t len = std::strlen(kShader);
    if (FAILED(D3DCompile(kShader, len, "stage", nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vsb, &eb))) {
        err = eb ? std::string((const char*)eb->GetBufferPointer(), eb->GetBufferSize()) : "vertex shader";
        Rel(eb); Release(); return false;
    }
    if (FAILED(D3DCompile(kShader, len, "stage", nullptr, nullptr, "ps", "ps_4_0", 0, 0, &psb, &eb))) {
        err = eb ? std::string((const char*)eb->GetBufferPointer(), eb->GetBufferSize()) : "pixel shader";
        Rel(eb); Rel(vsb); Release(); return false;
    }
    bool ok = SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &d.vs)) &&
              SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &d.ps));
    const D3D11_INPUT_ELEMENT_DESC el[3] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    ok = ok && SUCCEEDED(dev->CreateInputLayout(el, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &d.layout));
    Rel(vsb); Rel(psb);
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 80; cbd.Usage = D3D11_USAGE_DYNAMIC; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&cbd, nullptr, &d.cb));
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;   // the view's mirror flips the winding
    rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE; rd.AntialiasedLineEnable = FALSE;
    ok = ok && SUCCEEDED(dev->CreateRasterizerState(&rd, &d.rs));
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc = D3D11_COMPARISON_LESS;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dsd, &d.dss));
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = FALSE; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(dev->CreateBlendState(&bd, &d.bs));
    if (!ok) { err = "could not create the drawing resources"; Release(); return false; }
    d.ready = true;
    return true;
}

ID3D11ShaderResourceView* StageRenderer::Render(int W, int H, const float viewProj[16], const float eyeDir[3], const std::vector<const Mesh*>& meshes) {
    Impl& d = *d_;
    if (!d.ready || W <= 0 || H <= 0) return nullptr;
    W = std::min(W, 4096); H = std::min(H, 4096);
    if ((W != d.w || H != d.h) && !d.MakeTargets(W, H)) return nullptr;
    // gather the meshes into one vertex and index buffer
    UINT nv = 0, ni = 0;
    for (const Mesh* m : meshes) if (m) { nv += (UINT)m->verts.size(); ni += (UINT)m->idx.size(); }
    ID3D11DeviceContext* c = d.ctx;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    c->PSSetShaderResources(0, 1, &nullSrv);
    const float clear[4] = {28.f / 255.f, 30.f / 255.f, 36.f / 255.f, 0.f};   // the view's background colour, transparent
    c->OMSetRenderTargets(1, &d.rtv, d.dsv);
    c->ClearRenderTargetView(d.rtv, clear);
    c->ClearDepthStencilView(d.dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
    if (nv > 0 && ni > 0 && d.Grow(d.vb, d.vbCap, nv, sizeof(MeshVertex), D3D11_BIND_VERTEX_BUFFER) &&
        d.Grow(d.ib, d.ibCap, ni, sizeof(uint32_t), D3D11_BIND_INDEX_BUFFER)) {
        D3D11_MAPPED_SUBRESOURCE mv, mi;
        if (SUCCEEDED(c->Map(d.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mv))) {
            if (SUCCEEDED(c->Map(d.ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &mi))) {
                MeshVertex* pv = (MeshVertex*)mv.pData;
                uint32_t* pi = (uint32_t*)mi.pData;
                UINT base = 0;
                for (const Mesh* m : meshes) {
                    if (!m) continue;
                    std::memcpy(pv, m->verts.data(), m->verts.size() * sizeof(MeshVertex));
                    for (uint32_t k : m->idx) *pi++ = k + base;
                    pv += m->verts.size();
                    base += (UINT)m->verts.size();
                }
                c->Unmap(d.ib, 0);
                D3D11_MAPPED_SUBRESOURCE mc;
                if (SUCCEEDED(c->Map(d.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
                    float* f = (float*)mc.pData;
                    std::memcpy(f, viewProj, 16 * sizeof(float));
                    f[16] = eyeDir[0]; f[17] = eyeDir[1]; f[18] = eyeDir[2]; f[19] = 0.f;
                    c->Unmap(d.cb, 0);
                }
                c->Unmap(d.vb, 0);
                D3D11_VIEWPORT vp{0.f, 0.f, (float)W, (float)H, 0.f, 1.f};
                c->RSSetViewports(1, &vp);
                c->RSSetState(d.rs);
                c->OMSetDepthStencilState(d.dss, 0);
                const float bf[4] = {0, 0, 0, 0};
                c->OMSetBlendState(d.bs, bf, 0xFFFFFFFFu);
                const UINT stride = sizeof(MeshVertex), offset = 0;
                c->IASetInputLayout(d.layout);
                c->IASetVertexBuffers(0, 1, &d.vb, &stride, &offset);
                c->IASetIndexBuffer(d.ib, DXGI_FORMAT_R32_UINT, 0);
                c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                c->VSSetShader(d.vs, nullptr, 0);
                c->VSSetConstantBuffers(0, 1, &d.cb);
                c->PSSetShader(d.ps, nullptr, 0);
                c->PSSetConstantBuffers(0, 1, &d.cb);
                c->GSSetShader(nullptr, nullptr, 0);
                c->DrawIndexed(ni, 0, 0);
            } else c->Unmap(d.vb, 0);
        }
    }
    c->OMSetRenderTargets(0, nullptr, nullptr);
    if (d.samples > 1) c->ResolveSubresource(d.picTex, 0, d.colorTex, 0, kColor);
    return d.srv;
}
