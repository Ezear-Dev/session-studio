// Session Studio -- reusable widgets of the redesign (step 1, 2026-11-26). They draw with the theme (ui_theme.h)
// and behave like the ImGui items they replace: same return value, hover tests, tooltips, BeginDisabled dimming.
#pragma once
#include "imgui.h"
#include "ui_theme.h"

namespace ui {

enum class Kind { Normal, Primary, Danger, Ghost };

// a button in the theme; hover colour fades in and out (a few frames)
bool Button(const char* label, Kind kind = Kind::Normal, const ImVec2& size = ImVec2(0, 0));
// a toggle shown "on" (orange) or "off"; dim = drawn faded but still clickable (it explains itself on click)
bool Toggle(const char* label, bool on, bool dim = false, const ImVec2& size = ImVec2(0, 0));
// an icon button (Windows icon font); without the icon font it shows `fallback` as text. `id` makes it unique.
bool IconButton(const char* id, theme::Icon icon, const char* fallback, bool on = false, Kind kind = Kind::Normal);
// a row of joined segments; returns true when the user picks another one (*current changes)
bool Segmented(const char* id, const char* const* items, int count, int* current);
// an on / off switch with an optional label on its right; returns true when it changes (*v flipped)
bool Switch(const char* id, bool* v, const char* label = nullptr);
// a record-style toggle: a dot (red when on) and a label, e.g. "Auto key"
bool RecordToggle(const char* label, bool on);
// a coloured pill with a label, laid out like a text item (hover works with IsItemHovered)
void Chip(const char* text, const ImVec4& col);
// a status dot then the cursor on the same line (status strip items)
void StatusDot(const ImVec4& col);

} // namespace ui
