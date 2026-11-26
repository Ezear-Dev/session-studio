// Session Studio -- reusable widgets of the redesign (step 1, 2026-11-26).
#include "ui_widgets.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>

namespace ui {
namespace {

struct Pal { ImVec4 base, hover, active, text; };

Pal PalOf(Kind k) {
    using namespace theme;
    switch (k) {
        case Kind::Primary: return {Accent, AccentHi, AccentLo, OnAccent};
        case Kind::Danger: return {ImVec4(0.62f, 0.22f, 0.20f, 1.f), Bad, ImVec4(0.50f, 0.17f, 0.16f, 1.f), Text};
        case Kind::Ghost: return {ImVec4(0, 0, 0, 0), Raised, Line, Text};
        default: return {Surface, Raised, Line, Text};
    }
}

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

// moves a stored 0..1 value toward the target (about 0.1 s for a full fade)
float Ease(ImGuiID id, bool target) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    const float t = st->GetFloat(id, target ? 1.f : 0.f);
    const float step = ImGui::GetIO().DeltaTime * 10.f;
    const float n = target ? std::min(1.f, t + step) : std::max(0.f, t - step);
    st->SetFloat(id, n);
    return n;
}

bool Styled(const char* label, const Pal& p, const ImVec2& size) {
    const ImGuiID id = ImGui::GetID(label);
    const ImGuiID fid = id + 0x9E37u;   // the fade value lives next to the item id
    const float t = ImGui::GetStateStorage()->GetFloat(fid, 0.f);
    const ImVec4 cur = Mix(p.base, p.hover, t);
    ImGui::PushStyleColor(ImGuiCol_Button, cur);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cur);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.active);
    ImGui::PushStyleColor(ImGuiCol_Text, p.text);
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    Ease(fid, ImGui::IsItemHovered());
    return r;
}

} // namespace

bool Button(const char* label, Kind kind, const ImVec2& size) { return Styled(label, PalOf(kind), size); }

bool Toggle(const char* label, bool on, bool dim, const ImVec2& size) {
    if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
    const bool r = Styled(label, PalOf(on ? Kind::Primary : Kind::Normal), size);
    if (dim) ImGui::PopStyleVar();
    return r;
}

bool IconButton(const char* id, theme::Icon icon, const char* fallback, bool on, Kind kind) {
    const bool icons = theme::HasIcons();
    const std::string label = (icons ? theme::Glyph(icon) : std::string(fallback)) + "##" + id;
    const float h = ImGui::GetFrameHeight();
    const Pal p = PalOf(on ? Kind::Primary : kind);
    return Styled(label.c_str(), p, icons ? ImVec2(h, h) : ImVec2(0, 0));
}

bool Segmented(const char* id, const char* const* items, int count, int* current) {
    ImGui::PushID(id);
    const ImGuiStyle& s = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight(), pad = std::max(2.f, std::round(h * 0.08f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float w = pad;
    std::vector<float> ws((size_t)count);
    for (int i = 0; i < count; i++) { ws[i] = ImGui::CalcTextSize(items[i], nullptr, true).x + s.FramePadding.x * 2.f; w += ws[i] + pad; }
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(theme::Surface), s.FrameRounding);
    bool changed = false;
    float x = p.x + pad;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s.FramePadding.x, std::max(0.f, s.FramePadding.y - pad)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::max(0.f, s.FrameRounding - pad * 0.5f));
    for (int i = 0; i < count; i++) {
        const bool sel = i == *current;
        ImGui::SetCursorScreenPos(ImVec2(x, p.y + pad));
        ImGui::PushID(i);
        const Pal pal = sel ? Pal{theme::Alpha(theme::Accent, 0.20f), theme::Alpha(theme::Accent, 0.28f), theme::Alpha(theme::Accent, 0.34f), theme::AccentHi}
                            : Pal{ImVec4(0, 0, 0, 0), theme::Raised, theme::Line, theme::Text2};
        if (Styled(items[i], pal, ImVec2(ws[i], h - pad * 2.f)) && !sel) { *current = i; changed = true; }
        ImGui::PopID();
        x += ws[i] + pad;
    }
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return changed;
}

bool Switch(const char* id, bool* v, const char* label) {
    const float h = ImGui::GetFrameHeight();
    const float th = std::round(h * 0.62f), tw = std::round(th * 1.8f);
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float lw = label ? ImGui::CalcTextSize(label, nullptr, true).x : 0.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(tw + (label ? gap + lw : 0.f), h));
    if (clicked) *v = !*v;
    const float k = Ease(ImGui::GetItemID() + 0x51u, *v);   // knob position 0 (off) .. 1 (on)
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x, p.y + (h - th) * 0.5f), b(p.x + tw, a.y + th);
    const ImVec4 off = hov ? ImVec4(0.227f, 0.251f, 0.298f, 1.f) : theme::Line;
    dl->AddRectFilled(a, b, ImGui::GetColorU32(Mix(off, hov ? theme::AccentHi : theme::Accent, k)), th * 0.5f);
    const float r = th * 0.5f - 2.f;
    const ImVec2 c(a.x + th * 0.5f + (tw - th) * k, a.y + th * 0.5f);
    dl->AddCircleFilled(c, r, ImGui::GetColorU32(Mix(theme::Text2, theme::OnAccent, k)));
    if (label) dl->AddText(ImVec2(b.x + gap, p.y + (h - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(theme::Text), label);
    return clicked;
}

bool RecordToggle(const char* label, bool on) {
    const float fs = ImGui::GetFontSize();
    const std::string lab = std::string("      ") + label;   // room for the dot
    const Pal p = on ? Pal{theme::Alpha(theme::Bad, 0.16f), theme::Alpha(theme::Bad, 0.26f), theme::Alpha(theme::Bad, 0.34f), theme::Text}
                     : PalOf(Kind::Normal);
    const bool r = Styled(lab.c_str(), p, ImVec2(0, 0));
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const ImVec2 c(mn.x + ImGui::GetStyle().FramePadding.x + fs * 0.35f, (mn.y + mx.y) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (on) dl->AddCircleFilled(c, fs * 0.3f, ImGui::GetColorU32(theme::Bad));
    else dl->AddCircle(c, fs * 0.28f, ImGui::GetColorU32(theme::Text3), 0, 1.5f);
    return r;
}

void Chip(const char* text, const ImVec4& col) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float px = ImGui::GetFontSize() * 0.6f, py = ImGui::GetStyle().FramePadding.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz(ts.x + px * 2.f, ts.y + py * 2.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(theme::Alpha(col, 0.14f)), sz.y * 0.5f);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(theme::Alpha(col, 0.45f)), sz.y * 0.5f);
    dl->AddText(ImVec2(p.x + px, p.y + py), ImGui::GetColorU32(col), text);
    ImGui::Dummy(sz);
}

void StatusDot(const ImVec4& col) {
    const float r = ImGui::GetFontSize() * 0.22f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + h * 0.5f), r, ImGui::GetColorU32(col));
    ImGui::Dummy(ImVec2(r * 2.f, h));
    ImGui::SameLine(0.f, r * 1.5f);
}

} // namespace ui
