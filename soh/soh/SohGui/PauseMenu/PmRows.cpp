#include "PauseMenuImpl.h"

#include <imgui_internal.h>
#include <libultraship/libultraship.h>
#include <algorithm>
#include <cmath>

// Rows of the pause-style menu. Sizes are design pixels and come from the design handoff: a row is a
// slot at least 46 tall, with a label block on the left and its control on the right.

namespace Pm {

namespace {
constexpr float kRowMinH = 46.0f;
constexpr float kRowGap = 3.0f;
constexpr float kGroupHeaderH = 32.0f;
constexpr float kControlH = 34.0f;
constexpr float kDisabledAlpha = 0.45f;

ImGuiID SubId(ImGuiID row, int sub) {
    return RowId(std::to_string(row) + "/" + std::to_string(sub));
}

float Snap(double value, double min, double max, double step) {
    if (step > 0) {
        value = min + std::round((value - min) / step) * step;
    }
    return (float)std::clamp(value, min, max);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// RowUi
// ---------------------------------------------------------------------------------------------------------------------
bool PauseMenu::Impl::Tap(int sub, const Rect& r, bool* hovered, bool* held) {
    if (curRowDisabled) {
        if (hovered != nullptr) {
            *hovered = false;
        }
        if (held != nullptr) {
            *held = false;
        }
        return false;
    }
    return TapId(SubId(curRow, sub), r, hovered, held);
}

bool PauseMenu::Impl::PadTarget(int sub, const Rect& r) {
    int index = subCount++;
    if (pass != Pass::Live || !curRowFocused || !subMode || zone != Zone::Content || !usingPad) {
        return false;
    }
    if (index != subIndex) {
        return false;
    }
    float phase = 0.5f + 0.5f * (float)std::sin(now * IM_PI * 2.0);
    CursorBrackets(r, phase);
    if (padA && !curRowDisabled) {
        padA = false;
        return true;
    }
    return false;
}

const PageColors& PauseMenu::Impl::Colors() {
    return *pc;
}

bool PauseMenu::Impl::Disabled() {
    return curRowDisabled;
}

void PauseMenu::Impl::SetPlate(const std::string& title, const std::string& text) {
    plateOverride = true;
    plateTitle = title;
    plateText = text;
}

void PauseMenu::Impl::CaptureInput() {
    captureRequested = true;
}

void PauseMenu::Impl::Confirm(const std::string& title, const std::string& message, std::function<void()> onYes) {
    dialog = Dialog();
    dialog.open = true;
    dialog.title = title;
    dialog.message = message;
    dialog.onYes = std::move(onYes);
}

// ---------------------------------------------------------------------------------------------------------------------
// Measuring
// ---------------------------------------------------------------------------------------------------------------------
float PauseMenu::Impl::LabelBlockHeight(const Row& row, float w) const {
    float h = Layout(Type::RowLabel, row.label, w).h;
    if (row.disabled && row.showReason && !row.reason.empty()) {
        h += 2.0f + Layout(Type::Hint, row.reason, w).h;
    }
    return h;
}

// Draws the label and, when disabled, the reason under it. Returns the block height.
float PauseMenu::Impl::DrawLabelBlock(const Row& row, float x, float y, float w, float rowH, bool centre) {
    TextLayout label = Layout(Type::RowLabel, row.label, w);
    bool hint = row.disabled && row.showReason && !row.reason.empty();
    TextLayout reason;
    float h = label.h;
    if (hint) {
        reason = Layout(Type::Hint, row.reason, w);
        h += 2.0f + reason.h;
    }
    float top = centre ? y + (rowH - h) * 0.5f : y;
    if (row.disabled) {
        PushAlpha(kDisabledAlpha);
    }
    DrawLayout(label, Type::RowLabel, x, top, C(Tok::Text));
    if (row.disabled) {
        PopAlpha();
    }
    if (hint) {
        DrawLayout(reason, Type::Hint, x, top + label.h + 2.0f, C(Tok::Warn));
    }
    return h;
}

bool PauseMenu::Impl::UseSegments(const Row& row) const {
    if (row.kind == RowKind::Segmented || row.forceSegmented) {
        return true;
    }
    // Short choices become segmented buttons: 2-3 options whose labels total 26 characters or fewer.
    if (row.options.size() < 2 || row.options.size() > 3) {
        return false;
    }
    size_t chars = 0;
    for (const auto& o : row.options) {
        chars += o.label.size();
    }
    return chars <= 26;
}

float PauseMenu::Impl::SegmentsWidth(const Row& row) const {
    float w = 0;
    for (const auto& o : row.options) {
        w += TextW(Type::Control, o.label) + 22.0f;
    }
    return w + 2.0f * (float)std::max<size_t>(1, row.options.size()) - 2.0f;
}

static std::string CurrentLabel(const Row& row) {
    for (const auto& o : row.options) {
        if (o.value == row.current) {
            return o.label;
        }
    }
    return "";
}

static float DropdownWidth(const Row& row) {
    float text = std::min(162.0f, TextW(Type::Control, CurrentLabel(row)));
    return 12.0f + text + 8.0f + 10.0f + 8.0f;
}

float PauseMenu::Impl::MeasureRow(const Row& row, float w) {
    switch (row.kind) {
        case RowKind::Toggle: {
            float tw = TextW(Type::OnOff, row.on ? "ON" : "OFF");
            float labelW = w - 12 - 8 - 10 - tw - 10 - 30;
            return std::max(kRowMinH, 10.0f + LabelBlockHeight(row, labelW));
        }
        case RowKind::Slider: {
            float vw = TextW(Type::SliderValue, row.valueText);
            float labelW = w - 12 - 4 - 8 - 10 - vw;
            float line1 = std::max(LabelBlockHeight(row, labelW), LineH(Type::SliderValue));
            return 8.0f + line1 + 38.0f + 2.0f;
        }
        case RowKind::Segmented:
        case RowKind::Dropdown: {
            float inner = w - 20;
            float ctrl = UseSegments(row) ? SegmentsWidth(row) : DropdownWidth(row);
            if (120.0f + 10.0f + ctrl <= inner) {
                float labelW = inner - 10 - ctrl;
                return std::max(kRowMinH, 10.0f + std::max(LabelBlockHeight(row, labelW), kControlH));
            }
            return std::max(kRowMinH, 10.0f + LabelBlockHeight(row, inner) + 6.0f + kControlH);
        }
        case RowKind::Button: {
            float textH = Layout(Type::ButtonLabel, row.label, w - 16 - 28).h;
            float h = std::max(40.0f, textH + 12.0f) + 12.0f;
            if (row.disabled && row.showReason && !row.reason.empty()) {
                h += Layout(Type::Hint, row.reason, w - 24).h + 4.0f;
            }
            return h;
        }
        case RowKind::Text:
            return 18.0f + Layout(Type::Body, row.label, w - 24).h;
        case RowKind::Sub:
            return 18.0f + LineH(Type::SubHeading);
        case RowKind::Para:
            return 20.0f + Layout(Type::Para, row.label, w - 24).h;
        case RowKind::Warn:
            return 18.0f + LineH(Type::SubHeading) + 4.0f + Layout(Type::WarnText, row.label, w - 22).h;
        case RowKind::HostPort:
            return kRowMinH;
        case RowKind::Drawn:
            return row.measure ? std::max(kRowMinH, row.measure(w)) : kRowMinH;
        case RowKind::ImGui: {
            auto it = imguiRowHeights.find(row.id);
            return it != imguiRowHeights.end() ? it->second : kRowMinH;
        }
    }
    return kRowMinH;
}

static float GroupTitleWidth(const Group& group, float w) {
    float right = 6.0f;
    if (group.bulkAll && group.bulkNone) {
        right += TextW(Type::SmallButton, "All") + 24 + 4 + TextW(Type::SmallButton, "None") + 24 + 8;
    }
    if (group.showCount && group.togglesTotal > 1) {
        right += TextW(Type::Count, std::to_string(group.togglesOn) + "/" + std::to_string(group.togglesTotal)) +
                 4 + 8;
    }
    return w - 10.0f - right;
}

static float GroupHeaderHeight(const Group& group, float w) {
    float h = Layout(Type::GroupTitle, group.title, GroupTitleWidth(group, w)).h;
    return std::max(kGroupHeaderH, h + 10.0f);
}

float PauseMenu::Impl::MeasureGroup(const Group& group, float w) {
    float h = GroupHeaderHeight(group, w);
    if (!group.note.empty()) {
        h += 11.0f + Layout(Type::Note, group.note, w - 20).h;
    }
    for (const auto& row : group.rows) {
        h += kRowGap + MeasureRow(row, w);
    }
    return h;
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------------
void PauseMenu::Impl::DrawGroup(Group& group, const Rect& r, int column, float contentTop) {
    float headerH = GroupHeaderHeight(group, r.w);
    Rect header = { r.x, r.y, r.w, headerH };
    if (header.b() > contentClip.y && header.y < contentClip.b()) {
        Fill(header, C(pc->headerDark));
        Bevel(header, White(0.16f), Black(Tok::LoA));
        float right = header.r() - 6.0f;
        curRow = 0;
        curRowDisabled = false;
        if (group.showCount && group.togglesTotal > 1) {
            std::string count = std::to_string(group.togglesOn) + "/" + std::to_string(group.togglesTotal);
            float cw = TextW(Type::Count, count);
            right -= 4.0f + cw;
            DrawLine(Type::Count, count, right, header.cy(), C(pc->light));
            right -= 8.0f;
        }
        if (group.bulkAll && group.bulkNone) {
            const char* names[2] = { "None", "All" };
            for (int i = 0; i < 2; i++) {
                float bw = TextW(Type::SmallButton, names[i]) + 24.0f;
                Rect b = { right - bw, header.cy() - 13.0f, bw, 26.0f };
                bool hovered = false, held = false;
                bool tapped = pass == Pass::Live &&
                              TapId(RowId(group.title + "/bulk/" + names[i] + std::to_string(view.page)), b,
                                    &hovered, &held);
                float push = held ? 1.0f : 0.0f;
                Rect d = { b.x, b.y + push, b.w, b.h };
                Fill(d, C(Tok::Button));
                Raised(d);
                DrawLine(Type::SmallButton, names[i], d.x, d.cy(), C(Tok::Text), AlignCenter, d.w);
                if (tapped) {
                    (i == 0 ? group.bulkNone : group.bulkAll)();
                    countsTime = -1000;
                }
                right -= bw + 4.0f;
            }
            right -= 4.0f;
        }
        TextLayout title = Layout(Type::GroupTitle, group.title, GroupTitleWidth(group, r.w));
        DrawLayout(title, Type::GroupTitle, header.x + 10.0f, header.cy() - title.h * 0.5f, C(pc->light));
    }

    float y = header.b();
    if (!group.note.empty()) {
        TextLayout note = Layout(Type::Note, group.note, r.w - 20);
        DrawLayout(note, Type::Note, r.x + 10.0f, y + 7.0f, C(Tok::Warn));
        y += 11.0f + note.h;
    }
    for (auto& row : group.rows) {
        y += kRowGap;
        float h = MeasureRow(row, r.w);
        Rect rr = { r.x, y, r.w, h };
        if (row.Selectable() && pass == Pass::Live) {
            navItems.push_back({ row.id, column, { rr.x, rr.y - contentTop, rr.w, rr.h } });
        }
        // Rows that draw ImGui items must run even when scrolled away, or their height never settles.
        if ((rr.b() > contentClip.y - 8.0f && rr.y < contentClip.b() + 8.0f) || row.kind == RowKind::ImGui) {
            DrawRow(row, rr);
        }
        y += h;
    }
}

void PauseMenu::Impl::DrawRow(Row& row, const Rect& r) {
    curRow = row.id;
    curRowDisabled = row.disabled;
    curRowFocused = pass == Pass::Live && view.focus == row.id;
    int subStart = subCount;

    if (row.kind != RowKind::Warn) {
        Fill(r, C(Tok::Slot, 0.55f));
        Sunken(r);
    }

    switch (row.kind) {
        case RowKind::Toggle:
            DrawToggle(row, r);
            break;
        case RowKind::Slider:
            DrawSlider(row, r);
            break;
        case RowKind::Segmented:
        case RowKind::Dropdown:
            DrawChoice(row, r);
            break;
        case RowKind::Button:
            DrawButton(row, r);
            break;
        case RowKind::Text: {
            if (pass == Pass::Live && row.Selectable()) {
                TapId(row.id, r);
            }
            TextLayout t = Layout(Type::Body, row.label, r.w - 24);
            DrawLayout(t, Type::Body, r.x + 12, r.y + 9, C(Tok::Text2));
        } break;
        case RowKind::Sub:
            DrawLine(Type::SubHeading, row.label, r.x + 12, r.y + 10 + LineH(Type::SubHeading) * 0.5f,
                     C(pc->light, row.disabled ? kDisabledAlpha : 1.0f));
            break;
        case RowKind::Para: {
            if (pass == Pass::Live && row.Selectable()) {
                TapId(row.id, r);
            }
            TextLayout t = Layout(Type::Para, row.label, r.w - 24);
            DrawLayout(t, Type::Para, r.x + 12, r.y + 10, C(Tok::Text2));
        } break;
        case RowKind::Warn: {
            Fill(r, C(Tok::WarnBoxBg));
            Frame(r, C(Tok::Warn));
            float lh = LineH(Type::SubHeading);
            DrawLine(Type::SubHeading, "WARNING", r.x + 11, r.y + 9 + lh * 0.5f, C(Tok::Warn));
            TextLayout t = Layout(Type::WarnText, row.label, r.w - 22);
            DrawLayout(t, Type::WarnText, r.x + 11, r.y + 9 + lh + 4, C(Tok::WarnBoxText));
        } break;
        case RowKind::HostPort:
            DrawHostPort(row, r);
            break;
        case RowKind::Drawn:
            if (pass == Pass::Live && !row.disabled) {
                // The row itself is a target so tapping its empty space selects it.
                TapId(row.id, r);
            }
            if (row.draw) {
                if (row.disabled) {
                    PushAlpha(kDisabledAlpha);
                }
                row.draw(*this, r);
                if (row.disabled) {
                    PopAlpha();
                }
            }
            break;
        case RowKind::ImGui:
            DrawImGuiRow(row, r);
            break;
    }

    if (curRowFocused) {
        // Rows with their own pad targets: A steps into the row, B steps back out (see HandleKeys).
        int subs = subCount - subStart;
        if (row.kind == RowKind::Drawn && subs > 0) {
            subCountPrev = subs;
            if (usingPad && zone == Zone::Content && padA && !subMode && !row.disabled) {
                padA = false;
                subMode = true;
                subIndex = 0;
            }
        }
        bool inner = (subMode && row.kind == RowKind::Drawn) || (sliderAdjust && row.kind == RowKind::Slider);
        if (!inner) {
            double t = std::fmod(now, 1.0);
            float x = (float)(t < 0.5 ? t / 0.5 : 1.0 - (t - 0.5) / 0.5);
            float eased = x < 0.5f ? 2.0f * x * x : 1.0f - (-2.0f * x + 2.0f) * (-2.0f * x + 2.0f) * 0.5f;
            CursorBrackets(r, eased);
        }
    }
    curRow = 0;
    curRowDisabled = false;
    curRowFocused = false;
}

void PauseMenu::Impl::DrawToggle(Row& row, const Rect& r) {
    bool live = pass == Pass::Live;
    bool tapped = live && TapId(row.id, r) && !row.disabled;
    bool byPad = live && curRowFocused && usingPad && zone == Zone::Content && padA && !row.disabled;
    if (byPad) {
        padA = false;
    }
    const char* text = row.on ? "ON" : "OFF";
    float tw = TextW(Type::OnOff, text);
    float labelW = r.w - 12 - 8 - 10 - tw - 10 - 30;
    DrawLabelBlock(row, r.x + 12, r.y, labelW, r.h, true);
    if (row.disabled) {
        PushAlpha(kDisabledAlpha);
    }
    float sx = r.r() - 8 - 30;
    DrawLine(Type::OnOff, text, sx - 10 - tw, r.cy(), C(row.on ? Tok::On : Tok::Muted));
    GemSocket(sx, r.cy() - 16, row.on);
    if (row.disabled) {
        PopAlpha();
    }
    if ((tapped || byPad) && row.activate) {
        row.activate();
        countsTime = -1000;
    }
}

void PauseMenu::Impl::DrawSlider(Row& row, const Rect& r) {
    bool live = pass == Pass::Live;
    if (live) {
        TapId(row.id, r);
    }
    float vw = TextW(Type::SliderValue, row.valueText);
    float labelW = r.w - 12 - 4 - 8 - 10 - vw;
    float line1 = std::max(LabelBlockHeight(row, labelW), LineH(Type::SliderValue));
    DrawLabelBlock(row, r.x + 12, r.y + 8, labelW, line1, true);
    if (row.disabled) {
        PushAlpha(kDisabledAlpha);
    }
    DrawLine(Type::SliderValue, row.valueText, r.r() - 4 - 8 - vw, r.y + 8 + line1 * 0.5f, C(Tok::Gold));

    float ly = r.y + 8 + line1;
    Rect minus = { r.x + 2, ly, 44, 38 };
    Rect plus = { r.r() - 4 - 44, ly, 44, 38 };
    Rect track = { minus.r() + 2, ly, plus.x - 2 - (minus.r() + 2), 38 };
    double value = row.value;
    bool changed = false;
    bool usable = live && !row.disabled;

    bool hovMinus = false, heldMinus = false, hovPlus = false, heldPlus = false;
    if (usable) {
        if (TapRepeat(SubId(row.id, 1), minus, &hovMinus, &heldMinus)) {
            value = Snap(value - row.step, row.min, row.max, row.step);
            changed = true;
        }
        if (TapRepeat(SubId(row.id, 2), plus, &hovPlus, &heldPlus)) {
            value = Snap(value + row.step, row.min, row.max, row.step);
            changed = true;
        }
    }
    // The bar: 12 tall with a 2px light border and a 1px black ring outside it.
    Rect bar = { track.x, track.cy() - 6, track.w, 12 };
    Rect inner = bar.Inset(2);
    if (usable && DragId(SubId(row.id, 3), track)) {
        float t = std::clamp((mouse.x - inner.x) / std::max(1.0f, inner.w), 0.0f, 1.0f);
        double v = Snap(row.min + t * (row.max - row.min), row.min, row.max, row.step);
        if (v != value) {
            value = v;
            changed = true;
        }
    }
    // Pad: A steps into the slider, left / right change it, A or B step back out.
    if (live && curRowFocused && usingPad && zone == Zone::Content && !row.disabled) {
        if (padA) {
            padA = false;
            sliderAdjust = !sliderAdjust;
        }
        if (sliderAdjust) {
            if (padLeft) {
                value = Snap(value - row.step, row.min, row.max, row.step);
                changed = true;
            }
            if (padRight) {
                value = Snap(value + row.step, row.min, row.max, row.step);
                changed = true;
            }
        }
    }

    if (hovMinus) {
        Fill(minus, White(0.06f));
    }
    if (hovPlus) {
        Fill(plus, White(0.06f));
    }
    IconMinus(minus.cx(), minus.cy() + (heldMinus ? 1.0f : 0.0f), 10, C(Tok::Text2));
    IconPlus(plus.cx(), plus.cy() + (heldPlus ? 1.0f : 0.0f), 10, C(Tok::Text2));

    Fill({ bar.x - 1, bar.y - 1, bar.w + 2, bar.h + 2 }, Black(0.7f));
    Fill(bar, C(Tok::SliderFrame));
    Fill(inner, C(Tok::Bg0));
    float pct = row.max > row.min ? (float)std::clamp((value - row.min) / (row.max - row.min), 0.0, 1.0) : 0.0f;
    if (pct > 0) {
        FillGradV({ inner.x, inner.y, inner.w * pct, inner.h }, C(pc->light), C(pc->header));
    }
    Rect thumb = { inner.x + inner.w * pct - 4, bar.cy() - 11, 8, 22 };
    Fill(thumb, C(Tok::ThumbBorder));
    Fill(thumb.Inset(1), C(Tok::Text));
    Fill({ thumb.x + 1, thumb.b() - 4, thumb.w - 2, 3 }, Black(0.2f));
    if (row.disabled) {
        PopAlpha();
    }
    if (live && curRowFocused && sliderAdjust) {
        float phase = 0.5f + 0.5f * (float)std::sin(now * IM_PI * 2.0);
        CursorBrackets({ minus.x + 4, ly + 4, plus.r() - minus.x - 8, 30 }, phase);
    }
    if (changed && row.setValue) {
        row.setValue(value);
    }
}

void PauseMenu::Impl::DrawChoice(Row& row, const Rect& r) {
    bool live = pass == Pass::Live;
    if (live) {
        TapId(row.id, r);
    }
    bool segments = UseSegments(row);
    float inner = r.w - 20;
    float ctrl = segments ? SegmentsWidth(row) : DropdownWidth(row);
    bool oneLine = 120.0f + 10.0f + ctrl <= inner;
    float cx = r.r() - 8 - ctrl;
    float cy;
    if (oneLine) {
        DrawLabelBlock(row, r.x + 12, r.y, inner - 10 - ctrl, r.h, true);
        cy = r.cy() - kControlH * 0.5f;
    } else {
        float bh = LabelBlockHeight(row, inner);
        float top = r.y + (r.h - (bh + 6 + kControlH)) * 0.5f;
        DrawLabelBlock(row, r.x + 12, top, inner, bh, false);
        cy = top + bh + 6;
    }
    bool usable = live && !row.disabled;
    bool byPad = usable && curRowFocused && usingPad && zone == Zone::Content && padA;
    if (byPad) {
        padA = false;
    }
    if (row.disabled) {
        PushAlpha(kDisabledAlpha);
    }
    int32_t pick = row.current;
    bool picked = false;
    if (segments) {
        float x = cx;
        int currentIndex = -1;
        for (size_t i = 0; i < row.options.size(); i++) {
            const Option& o = row.options[i];
            float w = TextW(Type::Control, o.label) + 22.0f;
            Rect b = { x, cy, w, kControlH };
            bool selected = o.value == row.current;
            if (selected) {
                currentIndex = (int)i;
            }
            if (usable && TapId(SubId(row.id, 10 + (int)i), b) && !selected) {
                pick = o.value;
                picked = true;
            }
            Fill(b, C(selected ? pc->header : Tok::Button));
            if (selected) {
                Bevel(b, Black(Tok::LoA), White(0.25f));
            } else {
                Raised(b);
            }
            DrawLine(Type::Control, o.label, b.x, b.cy(), C(selected ? Tok::Text : Tok::Text2), AlignCenter, b.w);
            x += w + 2.0f;
        }
        if (byPad && !row.options.empty()) {
            // A steps to the next option and wraps.
            pick = row.options[(size_t)(currentIndex + 1) % row.options.size()].value;
            picked = true;
        }
    } else {
        Rect b = { cx, cy, ctrl, kControlH };
        bool hovered = false, held = false;
        bool tapped = usable && TapId(SubId(row.id, 9), b, &hovered, &held);
        Rect d = { b.x, b.y + (held ? 1.0f : 0.0f), b.w, b.h };
        Fill(d, C(Tok::Button));
        Raised(d);
        DrawLineEllipsis(Type::Control, CurrentLabel(row), d.x + 12, d.cy(), 162.0f, C(Tok::Text));
        TriDown(d.r() - 8 - 10, d.cy() - 3.5f, 10, 7, C(Tok::Gold));
        if (tapped || byPad) {
            picker = Picker();
            picker.open = true;
            picker.rowId = row.id;
            picker.title = row.label;
            picker.options = row.options;
            picker.current = row.current;
            picker.set = row.setOption;
            picker.colors = pc;
            picker.reveal = true;
            for (size_t i = 0; i < row.options.size(); i++) {
                if (row.options[i].value == row.current) {
                    picker.highlight = (int)i;
                }
            }
        }
    }
    if (row.disabled) {
        PopAlpha();
    }
    if (picked && row.setOption) {
        row.setOption(pick);
    }
}

void PauseMenu::Impl::DrawButton(Row& row, const Rect& r) {
    bool live = pass == Pass::Live;
    TextLayout text = Layout(Type::ButtonLabel, row.label, r.w - 16 - 28);
    float bh = std::max(40.0f, text.h + 12.0f);
    Rect b = { r.x + 8, r.y + 6, r.w - 16, bh };
    bool usable = live && !row.disabled;
    bool hovered = false, held = false;
    if (live) {
        TapId(row.id, r);
    }
    bool tapped = usable && TapId(SubId(row.id, 1), b, &hovered, &held);
    bool byPad = usable && curRowFocused && usingPad && zone == Zone::Content && padA;
    if (byPad) {
        padA = false;
    }
    if (row.disabled) {
        PushAlpha(kDisabledAlpha);
    }
    Rect d = { b.x, b.y + (held ? 1.0f : 0.0f), b.w, b.h };
    Fill(d, C(row.latched ? Tok::Gold : Tok::Button));
    Bevel(d, White(0.2f), Black(Tok::LoA));
    DrawLayout(text, Type::ButtonLabel, d.x, d.cy() - text.h * 0.5f, C(row.latched ? Tok::DarkOnGold : Tok::Text),
               AlignCenter, d.w, row.latched ? 0.0f : 1.0f);
    if (row.disabled) {
        PopAlpha();
        if (row.showReason && !row.reason.empty()) {
            TextLayout reason = Layout(Type::Hint, row.reason, r.w - 24);
            DrawLayout(reason, Type::Hint, r.x + 12, b.b() + 4, C(Tok::Warn));
        }
    }
    if (usable && held && row.whileHeld) {
        row.whileHeld();
    }
    if ((tapped || byPad) && row.activate) {
        row.activate();
        countsTime = -1000;
    }
}

bool PauseMenu::Impl::TextField(const char* id, const Rect& r, char* buf, size_t bufSize, const char* hint,
                                const TextStyle& st, bool decimalOnly, bool framed) {
    float padX = framed ? 10.0f : 0.0f;
    bool active = false;
    bool changed = false;
    ImVec2 a = Px(r.x + padX, r.y), b = Px(r.r() - padX, r.b());
    if (pass == Pass::Live) {
        ImGuiIO& io = ImGui::GetIO();
        float px = 0;
        ImFont* font = ImFontFor(st, &px);
        active = ImGui::GetActiveID() == ImGui::GetID(id);
        if (framed) {
            Fill(r, C(Tok::Bg0));
            Frame(r, C(active ? Tok::Gold : Tok::Line));
        }
        ImGui::PushFont(font);
        ImGui::SetWindowFontScale(px / std::max(1.0f, font->FontSize * io.FontGlobalScale));
        float fontH = ImGui::GetFontSize();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, std::floor((b.y - a.y - fontH) * 0.5f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, C(Tok::Text));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, C(Tok::Muted));
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, C(Tok::Line));
        ImGui::SetCursorScreenPos(a);
        ImGui::SetNextItemWidth(b.x - a.x);
        ImGui::BeginDisabled(curRowDisabled || !inputEnabled);
        ImGuiInputTextFlags flags = decimalOnly ? ImGuiInputTextFlags_CharsDecimal : ImGuiInputTextFlags_None;
        changed = ImGui::InputTextWithHint(id, hint, buf, bufSize, flags);
        ImGui::EndDisabled();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
    } else {
        if (framed) {
            Fill(r, C(Tok::Bg0));
            Frame(r, C(Tok::Line));
        }
        bool empty = buf[0] == '\0';
        DrawLine(st, empty ? hint : buf, r.x + padX, r.cy(), C(empty ? Tok::Muted : Tok::Text), AlignLeft, 0, 0.0f);
    }
    return changed;
}

void PauseMenu::Impl::DrawHostPort(Row& row, const Rect& r) {
    DrawLine(Type::RowLabel, row.label, r.x + 12, r.cy(), C(Tok::Text, row.disabled ? kDisabledAlpha : 1.0f));
    Rect port = { r.r() - 8 - 70, r.cy() - 17, 70, 34 };
    float colonW = TextW(Type::Body, ":");
    Rect host = { port.x - 6 - colonW - 6 - 118, r.cy() - 17, 118, 34 };
    DrawLine(Type::Body, ":", host.r() + 6, r.cy(), C(Tok::Muted));

    char hostBuf[128];
    char portBuf[16];
    snprintf(hostBuf, sizeof(hostBuf), "%s", CVarGetString(row.hostCvar.c_str(), row.hostDefault.c_str()));
    snprintf(portBuf, sizeof(portBuf), "%d", CVarGetInteger(row.portCvar.c_str(), row.portDefault));
    std::string hostId = "##pmhost" + std::to_string(row.id);
    std::string portId = "##pmport" + std::to_string(row.id);
    if (TextField(hostId.c_str(), host, hostBuf, sizeof(hostBuf), row.hostDefault.c_str(), Type::Body, false,
                  true)) {
        CVarSetString(row.hostCvar.c_str(), hostBuf);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    std::string portHint = std::to_string(row.portDefault);
    if (TextField(portId.c_str(), port, portBuf, sizeof(portBuf), portHint.c_str(), Type::Body, true, true)) {
        CVarSetInteger(row.portCvar.c_str(), atoi(portBuf));
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

void PauseMenu::Impl::DrawImGuiRow(Row& row, const Rect& r) {
    if (pass != Pass::Live || !row.drawImGui) {
        return;
    }
    ImVec2 a = Px(r.x + 10, r.y + 8);
    float wpx = (r.w - 20) * Scale();
    ImGui::SetCursorScreenPos(a);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushID((int)row.id);
    if (ImGui::BeginChild("##pmrow", ImVec2(wpx, 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground)) {
        ImGui::BeginDisabled(row.disabled || !inputEnabled);
        row.drawImGui();
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopStyleColor();
    float h = ImGui::GetItemRectSize().y / Scale() + 16.0f;
    imguiRowHeights[row.id] = std::max(24.0f, h);
}

} // namespace Pm
