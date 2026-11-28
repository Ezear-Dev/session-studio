// Session Studio -- the optional skinned body of the 3D view, see body_model.h
// glTF is right-handed, Y up, in metres; the editor works in UE component space (left-handed, Z up, cm): y and z are
// swapped and lengths multiplied by 100. The bind pose comes from the inverse bind matrices, as glTF skinning defines it.
#include "body_model.h"
#include "view_math.h"
#include "../../SessionAnimLoader/Native/src/json.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr size_t kMaxVerts = 500000, kMaxIndices = 3000000;
constexpr int kSwap[3] = {0, 2, 1};
constexpr float kUnit = 100.f;
constexpr float kStripeFacing = 0.92f;   // cosine: about 23 degrees each side of the bone's +Y
constexpr uint32_t kNoColour = 0xFFC8C8C8u;

uint32_t U32(const char* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
const JValue* Item(const JValue* arr, int i) {
    return arr && arr->t == JValue::Arr && i >= 0 && (size_t)i < arr->a.size() ? &arr->a[(size_t)i] : nullptr;
}
// a whole number >= 0 held by obj[key]; def when the key is absent; -1 when it is not such a number
long long Int(const JValue& obj, const char* key, long long def = -1) {
    const JValue* v = obj.get(key);
    if (!v) return def;
    if (v->t != JValue::Num || !(v->n >= 0) || !(v->n < 4e9) || v->n != std::floor(v->n)) return -1;
    return (long long)v->n;
}
std::string Str(const JValue* v) { return v && v->t == JValue::Str ? v->s : std::string(); }

// see body_model.h: the first word found in the name decides
uint32_t PartColour(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    const struct { const char* word; uint32_t rgba; } words[] = {
        {"hoodie", kBodyTop}, {"shirt", kBodyTop}, {"jacket", kBodyTop}, {"cargo", kBodyTrousers}, {"pants", kBodyTrousers},
        {"jeans", kBodyTrousers}, {"cap", kBodyHat}, {"hat", kBodyHat}, {"hair", kBodyDark}, {"eye", kBodyDark},
        {"head", kBodySkin}, {"body", kBodySkin}, {"skin", kBodySkin}};
    for (const auto& w : words) if (name.find(w.word) != std::string::npos) return w.rgba;
    return kBodyOther;
}

struct Glb {
    JValue json;
    const char* bin = nullptr;
    size_t binSize = 0;
};
// one accessor, checked to lie inside the buffer
struct View {
    const char* data = nullptr;
    size_t count = 0, stride = 0;
    int type = 0, parts = 0;   // component type, components per element
    float Float(size_t e, int c) const {   // floats, and unsigned integers used as weights (normalised)
        const char* p = data + e * stride;
        if (type == 5126) { float f; std::memcpy(&f, p + c * 4, 4); return f; }
        if (type == 5121) return (float)(uint8_t)p[c] / 255.f;
        uint16_t u; std::memcpy(&u, p + c * 2, 2); return (float)u / 65535.f;
    }
    uint32_t Uint(size_t e, int c) const {
        const char* p = data + e * stride;
        if (type == 5121) return (uint8_t)p[c];
        if (type == 5123) { uint16_t u; std::memcpy(&u, p + c * 2, 2); return u; }
        return U32(p + c * 4);
    }
};
bool Access(const Glb& g, long long index, const char* kind, View& v) {
    const JValue* a = index < 0 || index > 1000000 ? nullptr : Item(g.json.get("accessors"), (int)index);
    if (!a || a->get("sparse") || Str(a->get("type")) != kind) return false;
    const long long bv = Int(*a, "bufferView");
    const JValue* b = bv < 0 || bv > 1000000 ? nullptr : Item(g.json.get("bufferViews"), (int)bv);
    if (!b || Int(*b, "buffer") != 0) return false;
    const long long type = Int(*a, "componentType"), count = Int(*a, "count");
    const long long off = Int(*a, "byteOffset", 0), viewOff = Int(*b, "byteOffset", 0), viewLen = Int(*b, "byteLength");
    const int size = type == 5121 ? 1 : type == 5123 ? 2 : type == 5125 || type == 5126 ? 4 : 0;
    v.parts = !std::strcmp(kind, "SCALAR") ? 1 : !std::strcmp(kind, "VEC3") ? 3 : !std::strcmp(kind, "VEC4") ? 4 : 16;
    if (!size || count <= 0 || count > (long long)kMaxIndices || off < 0 || viewOff < 0 || viewLen < 0) return false;
    const long long elem = (long long)size * v.parts, stride = Int(*b, "byteStride", elem);
    if (stride < elem || stride > 65536) return false;
    if (viewOff + viewLen > (long long)g.binSize || off + (count - 1) * stride + elem > viewLen) return false;
    v.data = g.bin + viewOff + off;
    v.count = (size_t)count; v.stride = (size_t)stride; v.type = (int)type;
    return true;
}

bool Open(const std::string& bytes, Glb& g, std::string& err) {
    if (bytes.size() < 20 || U32(bytes.data()) != 0x46546C67u || U32(bytes.data() + 4) != 2) { err = "not a binary glTF 2.0 (.glb) file"; return false; }
    const size_t total = U32(bytes.data() + 8), jsonLen = U32(bytes.data() + 12);
    if (total > bytes.size() || U32(bytes.data() + 16) != 0x4E4F534Au || jsonLen > bytes.size() - 20) { err = "the file is cut short or damaged"; return false; }
    const size_t binAt = 20 + jsonLen;
    if (binAt + 8 > total || U32(bytes.data() + binAt + 4) != 0x004E4942u || U32(bytes.data() + binAt) > total - binAt - 8) { err = "the file is cut short or damaged"; return false; }
    g.bin = bytes.data() + binAt + 8;
    g.binSize = U32(bytes.data() + binAt);
    std::string jerr;
    if (!JParse(bytes.substr(20, jsonLen), g.json, jerr)) { err = "unreadable description: " + jerr; return false; }
    return true;
}

// the joint's bind pose in UE space from its inverse bind matrix (column-major, rotation and translation only)
bool BindFromInverse(const View& v, size_t e, FTr& out) {
    float m[16];
    for (int i = 0; i < 16; i++) { m[i] = v.Float(e, i); if (!std::isfinite(m[i])) return false; }
    for (int a = 0; a < 3; a++)
        for (int b = a; b < 3; b++) {
            const float d = m[a * 4] * m[b * 4] + m[a * 4 + 1] * m[b * 4 + 1] + m[a * 4 + 2] * m[b * 4 + 2];
            if (std::fabs(d - (a == b ? 1.f : 0.f)) > 0.01f) return false;   // a scaled or skewed joint: not handled
        }
    // the bind pose is the inverse: rotation = transpose, position = -transpose * translation
    float pos[3];
    for (int r = 0; r < 3; r++) pos[r] = -(m[r * 4] * m[12] + m[r * 4 + 1] * m[13] + m[r * 4 + 2] * m[14]);
    float u[16] = {};
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) u[c * 4 + r] = m[kSwap[r] * 4 + kSwap[c]];   // transpose, then y and z swapped on both sides
    u[15] = 1.f;
    Quat q;
    if (!MatrixRotation(u, q)) return false;
    q = QNorm(q);
    out = FTr{q.x, q.y, q.z, q.w, pos[kSwap[0]] * kUnit, pos[kSwap[1]] * kUnit, pos[kSwap[2]] * kUnit, 0, 1, 1, 1, 0};
    return true;
}

bool Usable(const FTr& t) {
    return std::isfinite(t.px) && std::isfinite(t.py) && std::isfinite(t.pz) && std::isfinite(t.qx) && std::isfinite(t.qy) &&
           std::isfinite(t.qz) && std::isfinite(t.qw) && std::fabs(t.px) < 1e5f && std::fabs(t.py) < 1e5f && std::fabs(t.pz) < 1e5f;
}
} // namespace

bool LoadBodyModel(const std::string& bytes, BodyModel& out, std::string& err) {
    out = BodyModel{};
    Glb g;
    if (!Open(bytes, g, err)) return false;
    auto fail = [&](const char* why) { err = why; out = BodyModel{}; return false; };

    // the first node that has both a mesh and a skin
    const JValue* nodes = g.json.get("nodes");
    long long meshAt = -1, skinAt = -1;
    for (int i = 0; Item(nodes, i); i++) {
        const long long me = Int(*Item(nodes, i), "mesh"), sk = Int(*Item(nodes, i), "skin");
        if (me >= 0 && sk >= 0) { meshAt = me; skinAt = sk; break; }
    }
    const JValue* mesh = meshAt < 0 || meshAt > 100000 ? nullptr : Item(g.json.get("meshes"), (int)meshAt);
    const JValue* skin = skinAt < 0 || skinAt > 100000 ? nullptr : Item(g.json.get("skins"), (int)skinAt);
    if (!mesh || !skin) return fail("no skinned mesh in the file");

    const JValue* joints = skin->get("joints");
    View ibm;
    if (!joints || joints->t != JValue::Arr || joints->a.empty() || joints->a.size() > 1024 ||
        !Access(g, Int(*skin, "inverseBindMatrices"), "MAT4", ibm) || ibm.type != 5126 || ibm.count < joints->a.size())
        return fail("the skin has no usable joints");
    for (size_t k = 0; k < joints->a.size(); k++) {
        const JValue& j = joints->a[k];
        const JValue* node = j.t == JValue::Num && j.n >= 0 && j.n < 1e6 ? Item(nodes, (int)j.n) : nullptr;
        FTr bind;
        if (!node || !BindFromInverse(ibm, k, bind)) return fail("a joint of the skin cannot be read");
        out.joints.push_back(Str(node->get("name")));
        out.bind.push_back(bind);
    }

    const JValue* prims = mesh->get("primitives");
    for (int i = 0; Item(prims, i); i++) {
        const JValue& p = *Item(prims, i);
        if (Int(p, "mode", 4) != 4) continue;   // triangles only
        const long long mat = Int(p, "material");
        const JValue* material = mat < 0 || mat > 100000 ? nullptr : Item(g.json.get("materials"), (int)mat);
        if (material && Str(material->get("alphaMode")) == "BLEND") continue;
        const uint32_t part = PartColour(material ? Str(material->get("name")) : std::string());
        const JValue* at = p.get("attributes");
        View pos, nrm, jn, wt, ix;
        if (!at || !Access(g, Int(*at, "POSITION"), "VEC3", pos) || !Access(g, Int(*at, "NORMAL"), "VEC3", nrm) ||
            !Access(g, Int(*at, "JOINTS_0"), "VEC4", jn) || !Access(g, Int(*at, "WEIGHTS_0"), "VEC4", wt) ||
            !Access(g, Int(p, "indices"), "SCALAR", ix))
            return fail("a part of the mesh has missing or damaged data");
        if (pos.type != 5126 || nrm.type != 5126 || (jn.type != 5121 && jn.type != 5123) || wt.type == 5125 || ix.type == 5126 ||
            nrm.count != pos.count || jn.count != pos.count || wt.count != pos.count || ix.count % 3 != 0)
            return fail("a part of the mesh uses data this editor does not read");
        const size_t base = out.verts.size();
        if (base + pos.count > kMaxVerts || out.idx.size() + ix.count > kMaxIndices) return fail("the mesh is too large");
        for (size_t v = 0; v < pos.count; v++) {
            BodyVertex bv{};
            bv.rgba = part;
            for (int c = 0; c < 3; c++) {
                bv.pos[c] = pos.Float(v, kSwap[c]) * kUnit;
                bv.nrm[c] = nrm.Float(v, kSwap[c]);
                if (!std::isfinite(bv.pos[c]) || !std::isfinite(bv.nrm[c])) return fail("the mesh holds numbers that are not valid");
            }
            float sum = 0.f;
            for (int c = 0; c < 4; c++) {
                const float w = wt.Float(v, c);
                const uint32_t j = jn.Uint(v, c);
                if (!std::isfinite(w) || w < 0.f) return fail("the mesh holds weights that are not valid");
                if (w > 0.f && j >= out.joints.size()) return fail("a vertex follows a joint that does not exist");
                bv.weight[c] = w;
                bv.joint[c] = w > 0.f ? (int)j : 0;
                sum += w;
            }
            if (!(sum > 1e-4f)) return fail("a vertex follows no joint");
            for (float& w : bv.weight) w /= sum;
            out.verts.push_back(bv);
        }
        for (size_t k = 0; k < ix.count; k++) {
            const uint32_t v = ix.Uint(k, 0);
            if (v >= pos.count) return fail("a triangle points to a vertex that does not exist");
            out.idx.push_back((uint32_t)base + v);
        }
    }
    if (out.idx.empty()) return fail("no triangle to draw");
    return true;
}

bool BindBodyModel(const BodyModel& m, const SkeletonFile& s, BodyRig& out, std::string& err) {
    out = BodyRig{};
    std::vector<char> used(m.joints.size(), 0);
    for (const BodyVertex& v : m.verts)
        for (int c = 0; c < 4; c++)
            if (v.weight[c] > 0.f && (size_t)v.joint[c] < used.size()) used[(size_t)v.joint[c]] = 1;
    for (size_t j = 0; j < m.joints.size(); j++) {
        int b = -1;
        for (size_t i = 0; i < s.names.size() && b < 0; i++) if (s.names[i] == m.joints[j]) b = (int)i;
        if (b < 0 && used[j]) { err = "the model does not fit the game's skeleton: no bone named " + m.joints[j]; out = BodyRig{}; return false; }
        out.bone.push_back(b);
    }
    out.striped.assign(s.names.size(), 0);
    for (size_t i = 0; i < s.names.size(); i++)
        for (const char* limb : {"Neck", "Clavicle", "UpperArm", "Forearm", "Thigh", "Calf"}) {
            const std::string& n = s.names[i];
            const size_t len = std::strlen(limb);
            if (n.size() >= len && n.compare(n.size() - len, len, limb) == 0) out.striped[i] = 1;   // not the IK_UPNODE helpers
        }
    out.ok = !m.idx.empty() && m.bind.size() == m.joints.size();
    return out.ok;
}

void SkinBodyModel(const BodyModel& m, const BodyRig& r, const std::vector<FTr>& cs, const std::vector<uint32_t>& boneTint, Mesh& out, int stripeBone) {
    out.Clear();
    if (!r.ok || r.bone.size() != m.joints.size() || r.bone.size() != m.bind.size()) return;
    // per joint, the move from the bind pose to the pose: v' = turn * v + shift
    struct Move { bool ok = false; Quat turn = QIdent; float shift[3] = {}; };
    std::vector<Move> moves(r.bone.size());
    for (size_t j = 0; j < r.bone.size(); j++) {
        const int b = r.bone[j];
        if (b < 0 || (size_t)b >= cs.size() || !Usable(cs[(size_t)b])) continue;
        Move& mv = moves[j];
        mv.turn = QNorm(QMul(QNorm(TrQ(cs[(size_t)b])), QInv(TrQ(m.bind[j]))));
        float x = m.bind[j].px, y = m.bind[j].py, z = m.bind[j].pz;
        QRotVec(mv.turn, x, y, z);
        mv.shift[0] = cs[(size_t)b].px - x; mv.shift[1] = cs[(size_t)b].py - y; mv.shift[2] = cs[(size_t)b].pz - z;
        mv.ok = true;
    }
    const bool stripe = stripeBone >= 0 && (size_t)stripeBone < r.striped.size() && (size_t)stripeBone < cs.size() &&
                        r.striped[(size_t)stripeBone] && Usable(cs[(size_t)stripeBone]);
    float sy[3] = {0.f, 1.f, 0.f};
    if (stripe) QRotVec(QNorm(TrQ(cs[(size_t)stripeBone])), sy[0], sy[1], sy[2]);

    out.verts.resize(m.verts.size());
    std::vector<char> good(m.verts.size(), 1);
    std::vector<int16_t> main(m.verts.size(), -1);
    for (size_t i = 0; i < m.verts.size(); i++) {
        const BodyVertex& v = m.verts[i];
        float p[3] = {}, n[3] = {}, tint[3] = {}, tinted = 0.f, best = 0.f, onStripe = 0.f;
        for (int c = 0; c < 4; c++) {
            const float w = v.weight[c];
            if (!(w > 0.f)) continue;
            const Move& mv = moves[(size_t)v.joint[c]];
            if (!mv.ok) { good[i] = 0; break; }
            float x = v.pos[0], y = v.pos[1], z = v.pos[2], a = v.nrm[0], b2 = v.nrm[1], d = v.nrm[2];
            QRotVec(mv.turn, x, y, z);
            QRotVec(mv.turn, a, b2, d);
            p[0] += w * (x + mv.shift[0]); p[1] += w * (y + mv.shift[1]); p[2] += w * (z + mv.shift[2]);
            n[0] += w * a; n[1] += w * b2; n[2] += w * d;
            const int bone = r.bone[(size_t)v.joint[c]];
            const uint32_t col = (size_t)bone < boneTint.size() ? boneTint[(size_t)bone] : 0u;
            const float cover = w * (float)(col >> 24) / 255.f;
            tinted += cover;
            for (int k = 0; k < 3; k++) tint[k] += cover * (float)((col >> (8 * k)) & 255u);
            if (w > best) { best = w; main[i] = (int16_t)bone; }
            if (bone == stripeBone) onStripe += w;
        }
        MeshVertex& o = out.verts[i];
        if (!good[i]) { o = MeshVertex{{0, 0, 0}, {0, 0, 1}, kNoColour}; continue; }
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int k = 0; k < 3; k++) { o.pos[k] = p[k]; o.nrm[k] = len > 1e-6f ? n[k] / len : (k == 2 ? 1.f : 0.f); }
        o.rgba = 0xFF000000u;
        for (int k = 0; k < 3; k++) {
            const float mix = (float)((v.rgba >> (8 * k)) & 255u) * std::max(0.f, 1.f - tinted) + tint[k];
            o.rgba |= (uint32_t)std::clamp(std::lround(mix), 0L, 255L) << (8 * k);
        }
        if (stripe && onStripe >= 0.5f && o.nrm[0] * sy[0] + o.nrm[1] * sy[1] + o.nrm[2] * sy[2] >= kStripeFacing) o.rgba = kMannequinStripe;
    }
    out.idx.reserve(m.idx.size());
    out.triBone.reserve(m.idx.size() / 3);
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        const uint32_t a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
        if (a >= good.size() || b >= good.size() || c >= good.size() || !good[a] || !good[b] || !good[c]) continue;
        out.idx.push_back(a); out.idx.push_back(b); out.idx.push_back(c);
        out.triBone.push_back(main[a]);
    }
}
