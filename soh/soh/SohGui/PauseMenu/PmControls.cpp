#include "PmInline.h"

#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "controller/controldevice/controller/mapping/ControllerLEDMapping.h"

#include <algorithm>
#include <cmath>
#include <map>

extern "C" {
#include "z64.h"
}

// Settings > Controls: the controller bindings, drawn on the page.
//
// Every N64 input is a row: its glyph, its name, one chip per mapping and a "+" to add another. Tapping
// a chip or "+" makes it listen: the next button, axis or key becomes the mapping. Mappings live in
// libultraship's controller objects, per port; this file only presents them.

namespace Pm {

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------------------
int sPort = 0;

enum class Target { None, Button, Stick, Rumble, Gyro, Led };

struct Listening {
    Target target = Target::None;
    uint8_t port = 0;
    CONTROLLERBUTTONS_T bitmask = 0;
    Ship::StickIndex stick = Ship::LEFT_STICK;
    Ship::Direction direction = Ship::LEFT;
    // The mapping being replaced; empty when adding one.
    std::string mappingId;
    ImGuiID row = 0;
    double since = 0;
    // The frame the listening row was last drawn. Leaving the page stops it being drawn, which cancels.
    int lastFrame = 0;
};
Listening sListen;

// The input that started listening (a tap or the pad's A button) must be let go before the next press
// counts, or it would be recorded as the mapping.
constexpr double kListenDelay = 0.35;

// Mapping ids in the order they were first seen, so a new mapping goes to the end of its row.
std::map<std::string, std::vector<std::string>> sOrder;

std::shared_ptr<Ship::Controller> Pad(uint8_t port) {
    return Ship::Context::GetInstance()->GetControlDeck()->GetControllerByPort(port);
}

void SaveCvars() {
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

template <typename Map> std::vector<std::string> Ordered(const std::string& key, const Map& mappings) {
    auto& order = sOrder[key];
    order.erase(std::remove_if(order.begin(), order.end(), [&](const std::string& id) { return !mappings.contains(id); }),
                order.end());
    for (const auto& [id, mapping] : mappings) {
        if (std::find(order.begin(), order.end(), id) == order.end()) {
            order.push_back(id);
        }
    }
    return order;
}

// Mapping names from libultraship use icon-font glyphs for arrows and a few buttons. The menu's fonts do
// not have those, so swap them for plain characters.
std::string PlainName(const std::string& name) {
    static const std::pair<const char*, const char*> swaps[] = {
        { ICON_FA_ARROW_UP, "\xe2\x86\x91" },   { ICON_FA_ARROW_DOWN, "\xe2\x86\x93" },
        { ICON_FA_ARROW_LEFT, "\xe2\x86\x90" }, { ICON_FA_ARROW_RIGHT, "\xe2\x86\x92" },
        { ICON_FA_PLUS, "+" },                  { ICON_FA_MINUS, "\xe2\x88\x92" },
        { ICON_FA_BARS, "Start" },
    };
    std::string out = name;
    for (const auto& [icon, plain] : swaps) {
        size_t at = 0;
        while ((at = out.find(icon, at)) != std::string::npos) {
            out.replace(at, strlen(icon), plain);
            at += strlen(plain);
        }
    }
    return out;
}

void StopListening() {
    sListen = Listening();
}

bool IsListening(ImGuiID row, const std::string& mappingId) {
    return sListen.target != Target::None && sListen.row == row && sListen.mappingId == mappingId;
}

// ---------------------------------------------------------------------------------------------------------------------
// Binding rows
// ---------------------------------------------------------------------------------------------------------------------
enum class GlyphColor { Blue, Green, Red, Yellow, Grey };

struct Glyph {
    GlyphColor color = GlyphColor::Grey;
    // Either a letter or an arrow (0 up, 1 down, 2 left, 3 right), or a plus for device rows.
    std::string letter;
    int arrow = -1;
    bool plus = false;
};

struct Chip {
    std::string id;
    std::string label;
};

struct BindRow {
    Target target = Target::Button;
    uint8_t port = 0;
    CONTROLLERBUTTONS_T bitmask = 0;
    Ship::StickIndex stick = Ship::LEFT_STICK;
    Ship::Direction direction = Ship::LEFT;
    Glyph glyph;
    std::vector<Chip> chips;
    // Device rows hold at most this many mappings (the gyro has one).
    int maxChips = 99;
    std::function<void(const std::string& id)> remove;
};

void DrawGlyph(const Glyph& glyph, float cx, float cy) {
    uint32_t fill = Tok::GlyphGrey, ink = Tok::GlyphGreyText;
    switch (glyph.color) {
        case GlyphColor::Blue:
            fill = Tok::GlyphBlue;
            ink = Tok::Text;
            break;
        case GlyphColor::Green:
            fill = Tok::GlyphGreen;
            ink = Tok::Text;
            break;
        case GlyphColor::Red:
            fill = Tok::GlyphRed;
            ink = Tok::Text;
            break;
        case GlyphColor::Yellow:
            fill = Tok::GlyphYellow;
            ink = Tok::DarkOnGold;
            break;
        default:
            break;
    }
    GlyphDisc(cx, cy, 30, fill);
    if (glyph.plus) {
        IconPlus(cx, cy, 10, C(ink), 1.8f);
    } else if (glyph.arrow >= 0) {
        IconArrow(cx, cy, 9, glyph.arrow, C(ink));
    } else {
        DrawLine(Type::GlyphLetter, glyph.letter, cx - 15, cy, C(ink), AlignCenter, 30, 0.0f);
    }
}

float ChipWidth(const std::string& label) {
    return 11.0f + TextW(Type::Control, label) + 2.0f + 30.0f;
}

struct ChipLayout {
    // x offset from the right edge of the chip area, line index, width; the last entry is the add button.
    struct Item {
        float x, w;
        int line;
    };
    std::vector<Item> items;
    int lines = 1;
};

// Chips flow right-aligned and wrap; the "+" button is the last item.
ChipLayout LayoutChips(const BindRow& bind, const std::string& label, ImGuiID rowId, float w) {
    float labelW = TextW(Type::RowLabel, label);
    float avail = std::max(80.0f, w - 10 - 6 - 30 - 9 - labelW - 9);
    std::vector<float> widths;
    for (const auto& chip : bind.chips) {
        widths.push_back(ChipWidth(IsListening(rowId, chip.id) ? "Press\xe2\x80\xa6" : chip.label));
    }
    if ((int)bind.chips.size() < bind.maxChips) {
        widths.push_back(34.0f);
    }
    ChipLayout out;
    std::vector<std::vector<size_t>> lines(1);
    float lineW = 0;
    for (size_t i = 0; i < widths.size(); i++) {
        float need = widths[i] + (lines.back().empty() ? 0.0f : 4.0f);
        if (lineW + need > avail && !lines.back().empty()) {
            lines.emplace_back();
            lineW = 0;
            need = widths[i];
        }
        lines.back().push_back(i);
        lineW += need;
    }
    out.items.resize(widths.size());
    out.lines = (int)lines.size();
    for (size_t line = 0; line < lines.size(); line++) {
        float total = 0;
        for (size_t i : lines[line]) {
            total += widths[i];
        }
        total += 4.0f * (float)(lines[line].empty() ? 0 : lines[line].size() - 1);
        float x = -total;
        for (size_t i : lines[line]) {
            out.items[i] = { x, widths[i], (int)line };
            x += widths[i] + 4.0f;
        }
    }
    return out;
}

// Polls for the input that completes the mapping this row is listening for.
void PollListening(RowUi& ui, const Row& row, const BindRow& bind) {
    if (sListen.target == Target::None || sListen.row != row.id) {
        return;
    }
    sListen.lastFrame = ImGui::GetFrameCount();
    ui.CaptureInput();
    const char* verb = sListen.mappingId.empty() ? "add" : "edit";
    std::string text;
    if (bind.target == Target::Button || bind.target == Target::Stick) {
        text = std::string("Press any button, move any axis, or press any key to ") + verb +
               " mapping. Tap it again to cancel.";
    } else {
        text = "Press any button or move any axis on the device to use. Tap it again to cancel.";
    }
    ui.SetPlate(row.label, text);
    if (ImGui::GetTime() - sListen.since < kListenDelay) {
        return;
    }
    auto pad = Pad(bind.port);
    bool done = false;
    switch (bind.target) {
        case Target::Button:
            done = pad->GetButton(bind.bitmask)->AddOrEditButtonMappingFromRawPress(bind.bitmask, sListen.mappingId);
            break;
        case Target::Stick: {
            auto stick = bind.stick == Ship::LEFT_STICK ? pad->GetLeftStick() : pad->GetRightStick();
            done = stick->AddOrEditAxisDirectionMappingFromRawPress(bind.direction, sListen.mappingId);
        } break;
        case Target::Rumble:
            done = pad->GetRumble()->AddRumbleMappingFromRawPress();
            break;
        case Target::Gyro:
            done = pad->GetGyro()->SetGyroMappingFromRawPress();
            break;
        case Target::Led:
            done = pad->GetLED()->AddLEDMappingFromRawPress();
            break;
        default:
            break;
    }
    if (done) {
        StopListening();
    }
}

void DrawBindRow(RowUi& ui, const Row& row, const BindRow& bind, const Rect& r) {
    PollListening(ui, row, bind);
    DrawGlyph(bind.glyph, r.x + 10 + 15, r.cy());
    DrawLine(Type::RowLabel, row.label, r.x + 10 + 30 + 9, r.cy(), C(Tok::Text));

    ChipLayout layout = LayoutChips(bind, row.label, row.id, r.w);
    float blockH = layout.lines * 34.0f + (layout.lines - 1) * 4.0f;
    float top = r.cy() - blockH * 0.5f;
    float right = r.r() - 6.0f;
    auto listen = [&](const std::string& mappingId) {
        if (IsListening(row.id, mappingId)) {
            StopListening();
            return;
        }
        sListen = Listening();
        sListen.target = bind.target;
        sListen.port = bind.port;
        sListen.bitmask = bind.bitmask;
        sListen.stick = bind.stick;
        sListen.direction = bind.direction;
        sListen.mappingId = mappingId;
        sListen.row = row.id;
        sListen.since = ImGui::GetTime();
        sListen.lastFrame = ImGui::GetFrameCount();
    };

    for (size_t i = 0; i < bind.chips.size(); i++) {
        const Chip& chip = bind.chips[i];
        const auto& item = layout.items[i];
        bool on = IsListening(row.id, chip.id);
        Rect c = { right + item.x, top + item.line * 38.0f, item.w, 34 };
        Rect edit = { c.x, c.y, c.w - 30, c.h };
        Rect remove = { c.r() - 30, c.y, 30, c.h };
        // Button and stick mappings can be re-recorded in place; a device chip is only removed and re-added.
        bool editable = bind.target == Target::Button || bind.target == Target::Stick;
        bool tapEdit = editable && ui.Tap(100 + (int)i * 2, edit);
        bool tapRemove = ui.Tap(101 + (int)i * 2, remove);
        Fill(c, C(on ? Tok::Gold : Tok::Button));
        Raised(c);
        DrawLine(Type::Control, on ? "Press\xe2\x80\xa6" : chip.label, c.x + 11, c.cy(), C(on ? Tok::DarkOnGold : Tok::Text),
                 AlignLeft, 0, on ? 0.0f : 1.0f);
        IconCross(remove.cx(), remove.cy(), 8, C(on ? Tok::DarkOnGold : Tok::Muted));
        if (editable && ui.PadTarget(100 + (int)i * 2, edit)) {
            tapEdit = true;
        }
        if (ui.PadTarget(101 + (int)i * 2, remove)) {
            tapRemove = true;
        }
        if (tapRemove) {
            StopListening();
            if (bind.remove) {
                bind.remove(chip.id);
            }
        } else if (tapEdit) {
            listen(chip.id);
        }
    }
    if ((int)bind.chips.size() < bind.maxChips) {
        const auto& item = layout.items.back();
        bool on = IsListening(row.id, "");
        Rect add = { right + item.x, top + item.line * 38.0f, 34, 34 };
        bool tapAdd = ui.Tap(99, add);
        Fill(add, C(on ? Tok::Gold : Tok::Button));
        Raised(add);
        IconPlus(add.cx(), add.cy(), 10, C(on ? Tok::DarkOnGold : Tok::Text), 1.8f);
        if (ui.PadTarget(99, add)) {
            tapAdd = true;
        }
        if (tapAdd) {
            listen("");
        }
    }
}

const char* kBindTip = "Tap a mapping to change it, \xc3\x97 to remove it, + to add another.";

Row MakeBindRow(const std::string& key, const std::string& label, BindRow bind, const std::string& desc = kBindTip) {
    Row row;
    row.kind = RowKind::Drawn;
    row.id = RowId("controls/" + key + "@" + std::to_string(bind.port));
    row.label = label;
    row.desc = desc;
    row.searchable = false;
    ImGuiID id = row.id;
    row.measure = [bind, label, id](float w) {
        ChipLayout layout = LayoutChips(bind, label, id, w);
        return 10.0f + layout.lines * 34.0f + (layout.lines - 1) * 4.0f;
    };
    row.draw = [bind, row](RowUi& ui, const Rect& r) { DrawBindRow(ui, row, bind, r); };
    return row;
}

Row ButtonBind(uint8_t port, const std::string& label, CONTROLLERBUTTONS_T bitmask, Glyph glyph) {
    BindRow bind;
    bind.target = Target::Button;
    bind.port = port;
    bind.bitmask = bitmask;
    bind.glyph = glyph;
    auto button = Pad(port)->GetButton(bitmask);
    auto mappings = button->GetAllButtonMappings();
    for (const auto& id : Ordered("b/" + std::to_string(port) + "/" + std::to_string(bitmask), mappings)) {
        bind.chips.push_back({ id, PlainName(mappings.at(id)->GetPhysicalInputName()) });
    }
    bind.remove = [port, bitmask](const std::string& id) { Pad(port)->GetButton(bitmask)->ClearButtonMapping(id); };
    return MakeBindRow("button/" + std::to_string(bitmask), label, bind);
}

Row StickBind(uint8_t port, Ship::StickIndex stick, const std::string& label, Ship::Direction direction, int arrow) {
    BindRow bind;
    bind.target = Target::Stick;
    bind.port = port;
    bind.stick = stick;
    bind.direction = direction;
    bind.glyph.arrow = arrow;
    auto controllerStick = stick == Ship::LEFT_STICK ? Pad(port)->GetLeftStick() : Pad(port)->GetRightStick();
    auto mappings = controllerStick->GetAllAxisDirectionMappingByDirection(direction);
    std::string key = "s/" + std::to_string(port) + "/" + std::to_string(stick) + "/" + std::to_string(direction);
    for (const auto& id : Ordered(key, mappings)) {
        bind.chips.push_back({ id, PlainName(mappings.at(id)->GetPhysicalInputName()) });
    }
    bind.remove = [port, stick, direction](const std::string& id) {
        auto s = stick == Ship::LEFT_STICK ? Pad(port)->GetLeftStick() : Pad(port)->GetRightStick();
        s->ClearAxisDirectionMapping(direction, id);
    };
    return MakeBindRow("stick/" + std::to_string(stick) + "/" + std::to_string(direction), label, bind);
}

// ---------------------------------------------------------------------------------------------------------------------
// Stick test
// ---------------------------------------------------------------------------------------------------------------------
struct StickDot {
    float x = 0, y = 0;
};
std::map<int, StickDot> sDots;

Row StickTestRow(uint8_t port, Ship::StickIndex stick) {
    Row row;
    row.kind = RowKind::Drawn;
    row.id = RowId("controls/sticktest/" + std::to_string(stick) + "@" + std::to_string(port));
    row.label = "Stick Test";
    row.desc = "Shows the live stick position with this section's sensitivity and deadzone applied, so you can check "
               "a mapping works once it is registered.";
    row.searchable = false;
    const char* helper = "Move the mapped stick to check it responds. The dashed ring is the deadzone.";
    row.measure = [helper](float w) {
        float textW = w - 24 - 104 - 16;
        float textH = LineH(Type::RowLabel) + 4 + LineH(Type::Readout) + 4 + Layout(Type::Helper, helper, textW).h;
        return 20.0f + std::max(104.0f, textH);
    };
    row.draw = [port, stick, helper](RowUi& ui, const Rect& r) {
        auto controllerStick = stick == Ship::LEFT_STICK ? Pad(port)->GetLeftStick() : Pad(port)->GetRightStick();
        // The value the game receives, after this stick's sensitivity and deadzone. Up is positive Y.
        int8_t sx = 0, sy = 0;
        controllerStick->Process(sx, sy);
        float deadzone = controllerStick->GetDeadzonePercentage() / 100.0f;

        const float d = 104.0f;
        float cx = r.x + 12 + d * 0.5f, cy = r.cy();
        CircleStroke(cx, cy, d * 0.5f + 1.0f, C(Tok::SliderFrame), 2.0f);
        Circle(cx, cy, d * 0.5f, C(Tok::Bg0));
        Line(cx, cy - d * 0.5f + 8, cx, cy + d * 0.5f - 8, C(Tok::Line));
        Line(cx - d * 0.5f + 8, cy, cx + d * 0.5f - 8, cy, C(Tok::Line));
        CircleDashed(cx, cy, std::max(2.0f, deadzone * d * 0.5f), C(Tok::Muted));

        // The dot travels up to 39px and eases toward the stick over about 70ms.
        float tx = std::clamp(sx / 85.0f, -1.0f, 1.0f) * 39.0f;
        float ty = -std::clamp(sy / 85.0f, -1.0f, 1.0f) * 39.0f;
        StickDot& dot = sDots[port * 2 + stick];
        float k = 1.0f - std::exp(-ImGui::GetIO().DeltaTime / 0.03f);
        dot.x += (tx - dot.x) * k;
        dot.y += (ty - dot.y) * k;
        bool live = sx != 0 || sy != 0;
        Circle(cx + dot.x, cy + dot.y + 2.0f, 14.0f, Black(0.35f));
        GlyphDisc(cx + dot.x, cy + dot.y, 26, live ? Tok::Gold : Tok::GlyphGrey, 0.35f, 0.30f);

        float tx0 = r.x + 12 + d + 16;
        float textW = r.r() - 12 - tx0;
        TextLayout help = Layout(Type::Helper, helper, textW);
        float blockH = LineH(Type::RowLabel) + 4 + LineH(Type::Readout) + 4 + help.h;
        float y = r.cy() - blockH * 0.5f;
        DrawLine(Type::RowLabel, "Stick Test", tx0, y + LineH(Type::RowLabel) * 0.5f, C(Tok::Text));
        y += LineH(Type::RowLabel) + 4;
        char readout[32];
        snprintf(readout, sizeof(readout), "X:%3d, Y:%3d", sx, sy);
        DrawLine(Type::Readout, readout, tx0, y + LineH(Type::Readout) * 0.5f, C(Tok::Gold));
        y += LineH(Type::Readout) + 4;
        DrawLayout(help, Type::Helper, tx0, y, C(Tok::Text2));
    };
    return row;
}

// ---------------------------------------------------------------------------------------------------------------------
// Plain rows
// ---------------------------------------------------------------------------------------------------------------------
Row Toggle(const char* label, const char* cvar, bool defaultOn, const char* desc = "",
           std::function<void()> changed = nullptr) {
    Row row;
    row.kind = RowKind::Toggle;
    row.id = RowId(std::string("controls/cvar/") + cvar);
    row.label = label;
    row.desc = desc;
    row.on = CVarGetInteger(cvar, defaultOn);
    row.activate = [cvar, defaultOn, changed]() {
        CVarSetInteger(cvar, !CVarGetInteger(cvar, defaultOn));
        SaveCvars();
        if (changed) {
            changed();
        }
    };
    return row;
}

Row IntSlider(const std::string& key, const char* label, int value, int min, int max, const char* format,
              std::function<void(int)> set, const char* desc = "") {
    Row row;
    row.kind = RowKind::Slider;
    row.id = RowId("controls/" + key);
    row.label = label;
    row.desc = desc;
    row.value = value;
    row.min = min;
    row.max = max;
    row.step = 1;
    char text[32];
    snprintf(text, sizeof(text), format, value);
    row.valueText = text;
    row.setValue = [set](double v) { set((int)std::lround(v)); };
    return row;
}

// A cvar stored as a factor (1.0 = 100%) shown and edited as a percentage.
Row PercentCvar(const char* label, const char* cvar, float minPct, float maxPct, float defaultFactor, const char* desc = "") {
    int value = (int)std::lround(CVarGetFloat(cvar, defaultFactor) * 100.0f);
    return IntSlider(std::string("cvar/") + cvar, label, value, (int)minPct, (int)maxPct, "%d%%", [cvar](int v) {
        CVarSetFloat(cvar, v / 100.0f);
        SaveCvars();
    }, desc);
}

Row IntCvar(const char* label, const char* cvar, int min, int max, int defaultValue, const char* desc = "") {
    return IntSlider(std::string("cvar/") + cvar, label, CVarGetInteger(cvar, defaultValue), min, max, "%d", [cvar](int v) {
        CVarSetInteger(cvar, v);
        SaveCvars();
    }, desc);
}

Row Disabled(Row row, bool disabled, const char* reason) {
    if (disabled) {
        row.disabled = true;
        row.reason = reason;
    }
    return row;
}

Row SubHeading(const char* label) {
    Row row;
    row.kind = RowKind::Sub;
    row.id = RowId(std::string("controls/sub/") + label);
    row.label = label;
    row.searchable = false;
    return row;
}

Glyph Letter(const char* letter, GlyphColor color = GlyphColor::Grey) {
    Glyph g;
    g.letter = letter;
    g.color = color;
    return g;
}

Glyph Arrow(int dir, GlyphColor color = GlyphColor::Grey) {
    Glyph g;
    g.arrow = dir;
    g.color = color;
    return g;
}

// ---------------------------------------------------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------------------------------------------------
Group ControllerGroup() {
    Group group{ "Controller" };
    Row port;
    port.kind = RowKind::Segmented;
    port.forceSegmented = true;
    port.id = RowId("controls/port");
    port.label = "Controller Port";
    port.desc = "Chooses which N64 controller port the mappings below belong to.";
    port.options = { { 0, "Port 1" }, { 1, "Port 2" }, { 2, "Port 3" }, { 3, "Port 4" } };
    port.current = sPort;
    port.setOption = [](int32_t value) {
        sPort = std::clamp(value, 0, 3);
        StopListening();
    };
    port.searchable = false;
    group.rows.push_back(port);

    uint8_t p = (uint8_t)sPort;
    Row defaults;
    defaults.kind = RowKind::Drawn;
    defaults.id = RowId("controls/defaults");
    defaults.label = "Set Defaults";
    defaults.desc = "Clears the keyboard and gamepad mappings on this port and restores the defaults.";
    defaults.searchable = false;
    Row clear = defaults;
    clear.id = RowId("controls/clear");
    clear.label = "Clear All";
    clear.desc = "Removes every mapping on this port.";
    auto buttonRow = [](Row& row, std::function<void(RowUi&)> onPress) {
        std::string label = row.label;
        row.measure = [](float) { return 52.0f; };
        row.draw = [label, onPress](RowUi& ui, const Rect& r) {
            Rect b = { r.x + 8, r.y + 6, r.w - 16, 40 };
            bool held = false;
            bool tapped = ui.Tap(1, b, nullptr, &held);
            Rect d = { b.x, b.y + (held ? 1.0f : 0.0f), b.w, b.h };
            Fill(d, C(Tok::Button));
            Bevel(d, White(0.2f), Black(Tok::LoA));
            DrawLine(Type::ButtonLabel, label, d.x, d.cy(), C(Tok::Text), AlignCenter, d.w);
            if (ui.PadTarget(1, d)) {
                tapped = true;
            }
            if (tapped) {
                onPress(ui);
            }
        };
    };
    buttonRow(defaults, [p](RowUi& ui) {
        ui.Confirm("Set Defaults",
                   "This will clear all existing mappings for Keyboard and Gamepad (SDL) on port " +
                       std::to_string(p + 1) + ". Continue?",
                   [p]() {
                       StopListening();
                       for (auto type : { Ship::PhysicalDeviceType::Keyboard, Ship::PhysicalDeviceType::SDLGamepad }) {
                           Pad(p)->ClearAllMappingsForDeviceType(type);
                           Pad(p)->AddDefaultMappings(type);
                       }
                   });
    });
    buttonRow(clear, [p](RowUi& ui) {
        ui.Confirm("Clear All", "This will clear all mappings for port " + std::to_string(p + 1) + ". Continue?", [p]() {
            StopListening();
            Pad(p)->ClearAllMappings();
        });
    });
    group.rows.push_back(defaults);
    group.rows.push_back(clear);
    return group;
}

Group ButtonsGroup(uint8_t p) {
    Group group{ "Buttons" };
    group.rows.push_back(ButtonBind(p, "A", BTN_A, Letter("A", GlyphColor::Blue)));
    group.rows.push_back(ButtonBind(p, "B", BTN_B, Letter("B", GlyphColor::Green)));
    group.rows.push_back(ButtonBind(p, "Start", BTN_START, Letter("S", GlyphColor::Red)));
    group.rows.push_back(ButtonBind(p, "L", BTN_L, Letter("L")));
    group.rows.push_back(ButtonBind(p, "R", BTN_R, Letter("R")));
    group.rows.push_back(ButtonBind(p, "Z", BTN_Z, Letter("Z")));
    group.rows.push_back(ButtonBind(p, "C Up", BTN_CUP, Arrow(0, GlyphColor::Yellow)));
    group.rows.push_back(ButtonBind(p, "C Down", BTN_CDOWN, Arrow(1, GlyphColor::Yellow)));
    group.rows.push_back(ButtonBind(p, "C Left", BTN_CLEFT, Arrow(2, GlyphColor::Yellow)));
    group.rows.push_back(ButtonBind(p, "C Right", BTN_CRIGHT, Arrow(3, GlyphColor::Yellow)));
    return group;
}

Group DpadGroup(uint8_t p) {
    Group group{ "D-Pad" };
    group.rows.push_back(ButtonBind(p, "D Up", BTN_DUP, Arrow(0)));
    group.rows.push_back(ButtonBind(p, "D Down", BTN_DDOWN, Arrow(1)));
    group.rows.push_back(ButtonBind(p, "D Left", BTN_DLEFT, Arrow(2)));
    group.rows.push_back(ButtonBind(p, "D Right", BTN_DRIGHT, Arrow(3)));
    return group;
}

Group StickGroup(uint8_t p, Ship::StickIndex index, const char* title) {
    Group group{ title };
    std::string key = "stick" + std::to_string(index) + "@" + std::to_string(p);
    group.rows.push_back(StickTestRow(p, index));
    group.rows.push_back(StickBind(p, index, "Up", Ship::UP, 0));
    group.rows.push_back(StickBind(p, index, "Down", Ship::DOWN, 1));
    group.rows.push_back(StickBind(p, index, "Left", Ship::LEFT, 2));
    group.rows.push_back(StickBind(p, index, "Right", Ship::RIGHT, 3));
    auto stick = index == Ship::LEFT_STICK ? Pad(p)->GetLeftStick() : Pad(p)->GetRightStick();
    auto get = [p, index]() { return index == Ship::LEFT_STICK ? Pad(p)->GetLeftStick() : Pad(p)->GetRightStick(); };
    group.rows.push_back(IntSlider(key + "/sens", "Sensitivity", stick->GetSensitivityPercentage(), 0, 200, "%d%%",
                                   [get](int v) { get()->SetSensitivity((uint8_t)v); }));
    group.rows.push_back(IntSlider(key + "/dead", "Deadzone", stick->GetDeadzonePercentage(), 0, 100, "%d%%",
                                   [get](int v) { get()->SetDeadzone((uint8_t)v); }));
    group.rows.push_back(IntSlider(key + "/notch", "Notch Snap Angle", stick->GetNotchSnapAngle(), 0, 45, "%d\xc2\xb0",
                                   [get](int v) { get()->SetNotchSnapAngle((uint8_t)v); }));
    return group;
}

Glyph DeviceGlyph() {
    Glyph g;
    g.plus = true;
    return g;
}

Group RumbleGroup(uint8_t p) {
    Group group{ "Rumble" };
    auto mappings = Pad(p)->GetRumble()->GetAllRumbleMappings();
    BindRow bind;
    bind.target = Target::Rumble;
    bind.port = p;
    bind.glyph = DeviceGlyph();
    auto ids = Ordered("r/" + std::to_string(p), mappings);
    for (const auto& id : ids) {
        bind.chips.push_back({ id, mappings.at(id)->GetPhysicalDeviceName() });
    }
    bind.remove = [p](const std::string& id) { Pad(p)->GetRumble()->ClearRumbleMapping(id); };
    group.rows.push_back(
        MakeBindRow("rumble", "Rumble device", bind, "The devices that rumble. + adds one, \xc3\x97 removes it."));

    // The motor strengths apply to every rumble device on the port.
    bool none = ids.empty();
    int small = none ? 50 : mappings.at(ids.front())->GetHighFrequencyIntensityPercentage();
    int large = none ? 50 : mappings.at(ids.front())->GetLowFrequencyIntensityPercentage();
    group.rows.push_back(Disabled(IntSlider("rumble/small@" + std::to_string(p), "Small Motor Intensity", small, 0, 100,
                                            "%d%%",
                                            [p](int v) {
                                                for (auto& [id, m] : Pad(p)->GetRumble()->GetAllRumbleMappings()) {
                                                    m->SetHighFrequencyIntensity((uint8_t)v);
                                                }
                                            }),
                                  none, "Add a rumble device first"));
    group.rows.push_back(Disabled(IntSlider("rumble/large@" + std::to_string(p), "Large Motor Intensity", large, 0, 100,
                                            "%d%%",
                                            [p](int v) {
                                                for (auto& [id, m] : Pad(p)->GetRumble()->GetAllRumbleMappings()) {
                                                    m->SetLowFrequencyIntensity((uint8_t)v);
                                                }
                                            }),
                                  none, "Add a rumble device first"));
    return group;
}

Group GyroGroup(uint8_t p) {
    Group group{ "Gyro" };
    auto mapping = Pad(p)->GetGyro()->GetGyroMapping();
    BindRow bind;
    bind.target = Target::Gyro;
    bind.port = p;
    bind.glyph = DeviceGlyph();
    bind.maxChips = 1;
    if (mapping != nullptr) {
        bind.chips.push_back({ mapping->GetGyroMappingId(), mapping->GetPhysicalDeviceName() });
    }
    bind.remove = [p](const std::string&) { Pad(p)->GetGyro()->ClearGyroMapping(); };
    group.rows.push_back(
        MakeBindRow("gyro", "Gyro device", bind, "The device whose gyro is used. + sets it, \xc3\x97 removes it."));

    bool none = mapping == nullptr;
    group.rows.push_back(Disabled(IntSlider("gyro/sens@" + std::to_string(p), "Sensitivity",
                                            none ? 100 : mapping->GetSensitivityPercent(), 0, 100, "%d%%",
                                            [p](int v) {
                                                if (auto m = Pad(p)->GetGyro()->GetGyroMapping()) {
                                                    m->SetSensitivity((uint8_t)v);
                                                }
                                            }),
                                  none, "Add a gyro device first"));
    Row recal;
    recal.kind = RowKind::Button;
    recal.id = RowId("controls/gyro/recalibrate@" + std::to_string(p));
    recal.label = "Recalibrate";
    recal.desc = "Hold the device still and press to take its current reading as level.";
    recal.searchable = false;
    recal.activate = [p]() {
        if (auto m = Pad(p)->GetGyro()->GetGyroMapping()) {
            m->Recalibrate();
        }
    };
    group.rows.push_back(Disabled(recal, none, "Add a gyro device first"));
    return group;
}

Group LedGroup(uint8_t p) {
    Group group{ "LEDs" };
    auto mappings = Pad(p)->GetLED()->GetAllLEDMappings();
    BindRow bind;
    bind.target = Target::Led;
    bind.port = p;
    bind.glyph = DeviceGlyph();
    auto ids = Ordered("l/" + std::to_string(p), mappings);
    for (const auto& id : ids) {
        bind.chips.push_back({ id, mappings.at(id)->GetPhysicalDeviceName() });
    }
    bind.remove = [p](const std::string& id) { Pad(p)->GetLED()->ClearLEDMapping(id); };
    group.rows.push_back(
        MakeBindRow("led", "LED device", bind, "The devices whose light bar is driven. + adds one, \xc3\x97 removes it."));

    bool none = ids.empty();
    // What drives each device's light: nothing, one saved colour, or the game.
    int colorSource = none ? LED_COLOR_SOURCE_GAME : mappings.at(ids.front())->GetColorSource();
    Row mode;
    mode.kind = RowKind::Segmented;
    mode.forceSegmented = true;
    mode.id = RowId("controls/led/mode@" + std::to_string(p));
    mode.label = "LED Color";
    mode.desc = "Off: light off. Set: one fixed color. Game: the color follows the game, using Source below.";
    mode.options = { { LED_COLOR_SOURCE_OFF, "Off" }, { LED_COLOR_SOURCE_SET, "Set" }, { LED_COLOR_SOURCE_GAME, "Game" } };
    mode.current = colorSource;
    mode.searchable = false;
    mode.setOption = [p](int32_t value) {
        for (auto& [id, m] : Pad(p)->GetLED()->GetAllLEDMappings()) {
            m->SetColorSource((uint8_t)value);
        }
    };
    group.rows.push_back(Disabled(mode, none, "Add an LED device first"));

    if (!none && colorSource == LED_COLOR_SOURCE_SET) {
        Row color;
        color.kind = RowKind::ImGui;
        color.id = RowId("controls/led/saved@" + std::to_string(p));
        color.label = "Color";
        color.searchable = false;
        color.drawImGui = [p]() {
            for (auto& [id, m] : Pad(p)->GetLED()->GetAllLEDMappings()) {
                Color_RGB8 saved = m->GetSavedColor();
                ImVec4 value = { saved.r / 255.0f, saved.g / 255.0f, saved.b / 255.0f, 1.0f };
                if (ImGui::ColorEdit3(("Color##" + id).c_str(), (float*)&value, ImGuiColorEditFlags_NoInputs)) {
                    m->SetSavedColor(
                        { (uint8_t)(value.x * 255.0f), (uint8_t)(value.y * 255.0f), (uint8_t)(value.z * 255.0f) });
                }
            }
        };
        group.rows.push_back(color);
    }

    bool game = !none && colorSource == LED_COLOR_SOURCE_GAME;
    const char* reason = none ? "Add an LED device first" : "LED Color is not set to Game";
    Row source;
    source.kind = RowKind::Dropdown;
    source.id = RowId("controls/led/source");
    source.label = "Source";
    source.desc = "Health\n- Red when health critical (13-20% depending on max health)\n- Yellow when health < 40%. "
                  "Green otherwise.\n\nTunics: colors will mirror currently equipped tunic, whether original or the "
                  "current values in Cosmetics Editor.\n\nCustom: single, solid color";
    source.options = { { LED_SOURCE_TUNIC_ORIGINAL, "Original Tunic Colors" },
                       { LED_SOURCE_TUNIC_COSMETICS, "Cosmetics Tunic Colors" },
                       { LED_SOURCE_HEALTH, "Health Colors" },
                       { LED_SOURCE_NAVI_ORIGINAL, "Original Navi Targeting Colors" },
                       { LED_SOURCE_NAVI_COSMETICS, "Cosmetics Navi Targeting Colors" },
                       { LED_SOURCE_CUSTOM, "Custom" } };
    source.current = CVarGetInteger(CVAR_SETTING("LEDColorSource"), LED_SOURCE_TUNIC_ORIGINAL);
    source.setOption = [](int32_t value) {
        CVarSetInteger(CVAR_SETTING("LEDColorSource"), value);
        SaveCvars();
    };
    group.rows.push_back(Disabled(source, !game, reason));

    if (game && source.current == LED_SOURCE_CUSTOM) {
        Row custom;
        custom.kind = RowKind::ImGui;
        custom.id = RowId("controls/led/custom");
        custom.label = "Custom Color";
        custom.searchable = false;
        custom.drawImGui = []() {
            Color_RGB8 stored = CVarGetColor24(CVAR_SETTING("LEDPort1Color"), { 255, 255, 255 });
            ImVec4 value = { stored.r / 255.0f, stored.g / 255.0f, stored.b / 255.0f, 1.0f };
            if (ImGui::ColorEdit3("Custom Color", (float*)&value, ImGuiColorEditFlags_NoInputs)) {
                Color_RGB8 color = { (uint8_t)(value.x * 255.0f), (uint8_t)(value.y * 255.0f),
                                     (uint8_t)(value.z * 255.0f) };
                CVarSetColor24(CVAR_SETTING("LEDPort1Color"), color);
                SaveCvars();
            }
        };
        group.rows.push_back(custom);
    }

    group.rows.push_back(
        Disabled(PercentCvar("Brightness", CVAR_SETTING("LEDBrightness"), 0, 100, 1.0f,
                             "Sets the brightness of controller LEDs. 0% brightness = LEDs off."),
                 !game, reason));
    Row critical = Toggle("Critical Health Override", CVAR_SETTING("LEDCriticalOverride"), true,
                          "Shows red color when health is critical, otherwise displays according to color source.");
    if (!game) {
        critical = Disabled(critical, true, reason);
    } else if (source.current == LED_SOURCE_HEALTH) {
        critical = Disabled(critical, true, "Override redundant for health source.");
    }
    group.rows.push_back(critical);
    return group;
}

Group ModifierGroup(uint8_t p) {
    Group group{ "Modifier Buttons" };
    group.rows.push_back(ButtonBind(p, "M1", BTN_CUSTOM_MODIFIER1, Letter("M1")));
    group.rows.push_back(ButtonBind(p, "M2", BTN_CUSTOM_MODIFIER2, Letter("M2")));
    group.rows.push_back(Toggle("Enable speed modifiers", CVAR_SETTING("WalkModifier.Enabled"), false,
                                "Hold the assigned button to change the maximum walking or swimming speed"));
    if (CVarGetInteger(CVAR_SETTING("WalkModifier.Enabled"), 0)) {
        group.rows.push_back(
            Toggle("Toggle modifier instead of holding", CVAR_SETTING("WalkModifier.SpeedToggle"), false));
        group.rows.push_back(
            Toggle("Don't affect jump distance/velocity", CVAR_SETTING("WalkModifier.DoesntChangeJump"), false));
        group.rows.push_back(PercentCvar("Walk Modifier 1", CVAR_SETTING("WalkModifier.Mapping1"), 0, 500, 1.0f));
        group.rows.push_back(PercentCvar("Walk Modifier 2", CVAR_SETTING("WalkModifier.Mapping2"), 0, 500, 1.0f));
        group.rows.push_back(PercentCvar("Swim Modifier 1", CVAR_SETTING("WalkModifier.SwimMapping1"), 0, 500, 1.0f));
        group.rows.push_back(PercentCvar("Swim Modifier 2", CVAR_SETTING("WalkModifier.SwimMapping2"), 0, 500, 1.0f));
    }
    if (CVarGetInteger(CVAR_SETTING("DisableChanges"), 0)) {
        for (auto& row : group.rows) {
            if (row.kind != RowKind::Drawn) {
                row = Disabled(row, true, "Race Lockout Active");
            }
        }
    }
    return group;
}

Group OcarinaGroup(uint8_t p) {
    Group group{ "Ocarina Controls" };
    group.rows.push_back(Toggle("Dpad Ocarina Playback", CVAR_SETTING("CustomOcarina.Dpad"), false));
    group.rows.push_back(Toggle("Right Stick Ocarina Playback", CVAR_SETTING("CustomOcarina.RightStick"), false));
    group.rows.push_back(Toggle("Customize Ocarina Controls", CVAR_SETTING("CustomOcarina.Enabled"), false));
    if (CVarGetInteger(CVAR_SETTING("CustomOcarina.Enabled"), 0)) {
        group.rows.push_back(SubHeading("Notes"));
        group.rows.push_back(ButtonBind(p, "A (D4)", BTN_CUSTOM_OCARINA_NOTE_D4, Letter("A", GlyphColor::Blue)));
        group.rows.push_back(ButtonBind(p, "Down (F4)", BTN_CUSTOM_OCARINA_NOTE_F4, Arrow(1, GlyphColor::Yellow)));
        group.rows.push_back(ButtonBind(p, "Right (A4)", BTN_CUSTOM_OCARINA_NOTE_A4, Arrow(3, GlyphColor::Yellow)));
        group.rows.push_back(ButtonBind(p, "Left (B4)", BTN_CUSTOM_OCARINA_NOTE_B4, Arrow(2, GlyphColor::Yellow)));
        group.rows.push_back(ButtonBind(p, "Up (D5)", BTN_CUSTOM_OCARINA_NOTE_D5, Arrow(0, GlyphColor::Yellow)));
        group.rows.push_back(SubHeading("Song Detection"));
        Glyph ban = Letter("\xc3\x98");
        group.rows.push_back(ButtonBind(p, "Disable song detection", BTN_CUSTOM_OCARINA_DISABLE_SONGS, ban));
        group.rows.push_back(SubHeading("Pitch"));
        group.rows.push_back(ButtonBind(p, "Pitch Up", BTN_CUSTOM_OCARINA_PITCH_UP, Arrow(0)));
        group.rows.push_back(ButtonBind(p, "Pitch Down", BTN_CUSTOM_OCARINA_PITCH_DOWN, Arrow(1)));
    }
    return group;
}

Group CameraGroup() {
    Group group{ "Camera Controls" };
    group.rows.push_back(Toggle("Enable Mouse Controls", CVAR_SETTING("EnableMouse"), false,
                                "Allows for using the mouse to control the camera (must enable Free Look), aim with "
                                "the shield, and perform quickspin attacks (quickly rotate the mouse then press B)"));
    group.rows.push_back(SubHeading("Aiming/First-Person Camera"));
    group.rows.push_back(Toggle("Right Stick Aiming", CVAR_SETTING("Controls.RightStickAim"), false,
                                "Allows for aiming with the right stick in:\n-First-Person/C-Up view\n-Weapon Aiming"));
    if (CVarGetInteger(CVAR_SETTING("Controls.RightStickAim"), 0)) {
        group.rows.push_back(Toggle("Allow moving while in first person mode", CVAR_SETTING("MoveInFirstPerson"), false,
                                    "Changes the left stick to move the player while in first person mode"));
    }
    group.rows.push_back(Toggle("Invert Aiming X Axis", CVAR_SETTING("Controls.InvertAimingXAxis"), false,
                                "Inverts the Camera X Axis in:\n-First-Person/C-Up view\n-Weapon Aiming"));
    group.rows.push_back(Toggle("Invert Aiming Y Axis", CVAR_SETTING("Controls.InvertAimingYAxis"), true,
                                "Inverts the Camera Y Axis in:\n-First-Person/C-Up view\n-Weapon Aiming"));
    group.rows.push_back(Toggle("Invert Shield Aiming X Axis", CVAR_SETTING("Controls.InvertShieldAimingXAxis"), true,
                                "Inverts the Shield Aiming X Axis"));
    group.rows.push_back(Toggle("Invert Shield Aiming Y Axis", CVAR_SETTING("Controls.InvertShieldAimingYAxis"), false,
                                "Inverts the Shield Aiming Y Axis"));
    group.rows.push_back(Toggle("Invert Z-Weapon Aiming Y Axis", CVAR_SETTING("Controls.InvertZAimingYAxis"), true,
                                "Inverts the Camera Y Axis in:\n-Z-Weapon Aiming"));
    group.rows.push_back(Toggle("Disable Auto-Centering in First-Person View",
                                CVAR_SETTING("DisableFirstPersonAutoCenterView"), false,
                                "Prevents the C-Up view from auto-centering, allowing for Gyro Aiming"));
    group.rows.push_back(Toggle("Enable Custom Aiming/First-Person sensitivity",
                                CVAR_SETTING("FirstPersonCameraSensitivity.Enabled"), false, "", []() {
                                    if (!CVarGetInteger(CVAR_SETTING("FirstPersonCameraSensitivity.Enabled"), 0)) {
                                        CVarClear(CVAR_SETTING("FirstPersonCameraSensitivity.X"));
                                        CVarClear(CVAR_SETTING("FirstPersonCameraSensitivity.Y"));
                                        SaveCvars();
                                    }
                                }));
    if (CVarGetInteger(CVAR_SETTING("FirstPersonCameraSensitivity.Enabled"), 0)) {
        group.rows.push_back(PercentCvar("Aiming/First-Person Horizontal Sensitivity",
                                         CVAR_SETTING("FirstPersonCameraSensitivity.X"), 1, 500, 1.0f));
        group.rows.push_back(PercentCvar("Aiming/First-Person Vertical Sensitivity",
                                         CVAR_SETTING("FirstPersonCameraSensitivity.Y"), 1, 500, 1.0f));
    }
    group.rows.push_back(SubHeading("Third-Person Camera"));
    group.rows.push_back(Toggle("Free Look", CVAR_SETTING("FreeLook.Enabled"), false,
                                "Enables free look camera control\nNote: You must remap C buttons off of the right "
                                "stick in the controller config menu, and map the camera stick to the right stick."));
    group.rows.push_back(Toggle("Invert Camera X Axis", CVAR_SETTING("FreeLook.InvertXAxis"), false,
                                "Inverts the Camera X Axis in:\n-Free look"));
    group.rows.push_back(Toggle("Invert Camera Y Axis", CVAR_SETTING("FreeLook.InvertYAxis"), true,
                                "Inverts the Camera Y Axis in:\n-Free look"));
    group.rows.push_back(
        PercentCvar("Third-Person Horizontal Sensitivity", CVAR_SETTING("FreeLook.CameraSensitivity.X"), 1, 500, 1.0f));
    group.rows.push_back(
        PercentCvar("Third-Person Vertical Sensitivity", CVAR_SETTING("FreeLook.CameraSensitivity.Y"), 1, 500, 1.0f));
    group.rows.push_back(IntCvar("Camera Distance", CVAR_SETTING("FreeLook.MaxCameraDistance"), 100, 900, 185));
    group.rows.push_back(IntCvar("Camera Transition Speed", CVAR_SETTING("FreeLook.TransitionSpeed"), 0, 900, 25));
    return group;
}

Group DpadControlsGroup() {
    Group group{ "D-Pad Controls" };
    group.rows.push_back(Toggle("D-pad Support on Pause Screen", CVAR_SETTING("DPadOnPause"), false,
                                "Navigate Pause with the D-pad\nIf used with \"D-pad as Equip Items\", you must hold "
                                "C-Up to equip instead of navigate"));
    group.rows.push_back(Toggle("D-pad Support in Text Boxes", CVAR_SETTING("DpadInText"), false,
                                "Navigate choices in text boxes, shop item selection, and the file select / name "
                                "entry screens with the D-pad"));
    bool unused = !CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CVarGetInteger(CVAR_SETTING("DpadInText"), 0);
    group.rows.push_back(
        Disabled(Toggle("D-pad hold change", CVAR_SETTING("DpadHoldChange"), true,
                        "The cursor will only move a single space no matter how long a D-pad direction is held"),
                 unused, "Needs D-pad support on the pause screen or in text boxes"));
    return group;
}

} // namespace

void BuildControlsPage(std::vector<Group>& groups) {
    // A row that is listening but was not drawn last frame is on a page the player left: stop.
    if (sListen.target != Target::None && ImGui::GetFrameCount() - sListen.lastFrame > 2) {
        StopListening();
    }
    uint8_t p = (uint8_t)std::clamp(sPort, 0, 3);
    groups.push_back(ControllerGroup());
    groups.push_back(ButtonsGroup(p));
    groups.push_back(DpadGroup(p));
    groups.push_back(StickGroup(p, Ship::LEFT_STICK, "Analog Stick"));
    // The rest belongs to the player: only port 1 has it.
    if (p == 0) {
        groups.push_back(StickGroup(p, Ship::RIGHT_STICK, "Additional (\"Right\") Stick"));
        groups.push_back(RumbleGroup(p));
        groups.push_back(GyroGroup(p));
        groups.push_back(LedGroup(p));
        groups.push_back(ModifierGroup(p));
        groups.push_back(OcarinaGroup(p));
        groups.push_back(CameraGroup());
        groups.push_back(DpadControlsGroup());
    }
}

int CountControlsPage() {
    std::vector<Group> groups;
    BuildControlsPage(groups);
    int on = 0;
    for (const auto& group : groups) {
        for (const auto& row : group.rows) {
            if (row.kind == RowKind::Toggle && row.on) {
                on++;
            }
        }
    }
    return on;
}

} // namespace Pm
