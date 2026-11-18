#include "PmInline.h"

#include <libultraship/libultraship.h>
#include <cmath>

#include "soh/Enhancements/controls/InputViewer.h"
#include "soh/ShipInit.hpp"
#include "soh/SohGui/UIWidgets.hpp"

// Settings > Input Viewer: the input viewer's settings as ordinary rows. The cvars, defaults, ranges and
// the conditions under which a setting shows up are those of InputViewerSettingsWindow.

namespace Pm {

namespace {
// Text colors
const Color_RGBA8 kTextColorDefault = { 255, 255, 255, 255 };
const Color_RGBA8 kRange1ColorDefault = { 255, 178, 0, 255 };
const Color_RGBA8 kRange2ColorDefault = { 0, 255, 0, 255 };

const std::vector<Option> kButtonOutlineOptions = {
    { BUTTON_OUTLINE_ALWAYS_SHOWN, "Always Shown" },
    { BUTTON_OUTLINE_NOT_PRESSED, "Shown Only While Not Pressed" },
    { BUTTON_OUTLINE_PRESSED, "Shown Only While Pressed" },
    { BUTTON_OUTLINE_ALWAYS_HIDDEN, "Always Hidden" },
};
const std::vector<Option> kButtonOutlineOptionsVerbose = {
    { BUTTON_OUTLINE_ALWAYS_SHOWN, "Outline Always Shown" },
    { BUTTON_OUTLINE_NOT_PRESSED, "Outline Shown Only While Not Pressed" },
    { BUTTON_OUTLINE_PRESSED, "Outline Shown Only While Pressed" },
    { BUTTON_OUTLINE_ALWAYS_HIDDEN, "Outline Always Hidden" },
};
const std::vector<Option> kStickModeOptions = {
    { STICK_MODE_ALWAYS_SHOWN, "Always" },
    { STICK_MODE_HIDDEN_IN_DEADZONE, "While In Use" },
    { STICK_MODE_ALWAYS_HIDDEN, "Never" },
};

// One button of the viewer: whether its layers are drawn, and its own outline mode for when the buttons
// do not all share one.
struct ButtonLayer {
    const char* name;
    const char* cvar;
    const char* outlineCvar;
    bool shownByDefault;
};
const ButtonLayer kButtonLayers[] = {
    { "A-Button", CVAR_INPUT_VIEWER("ABtn"), CVAR_INPUT_VIEWER("ABtnOutlineMode"), true },
    { "B-Button", CVAR_INPUT_VIEWER("BBtn"), CVAR_INPUT_VIEWER("BBtnOutlineMode"), true },
    { "C-Up", CVAR_INPUT_VIEWER("CUp"), CVAR_INPUT_VIEWER("CUpOutlineMode"), true },
    { "C-Right", CVAR_INPUT_VIEWER("CRight"), CVAR_INPUT_VIEWER("CRightOutlineMode"), true },
    { "C-Down", CVAR_INPUT_VIEWER("CDown"), CVAR_INPUT_VIEWER("CDownOutlineMode"), true },
    { "C-Left", CVAR_INPUT_VIEWER("CLeft"), CVAR_INPUT_VIEWER("CLeftOutlineMode"), true },
    { "L-Button", CVAR_INPUT_VIEWER("LBtn"), CVAR_INPUT_VIEWER("LBtnOutlineMode"), true },
    { "R-Button", CVAR_INPUT_VIEWER("RBtn"), CVAR_INPUT_VIEWER("RBtnOutlineMode"), true },
    { "Z-Button", CVAR_INPUT_VIEWER("ZBtn"), CVAR_INPUT_VIEWER("ZBtnOutlineMode"), true },
    { "Start Button", CVAR_INPUT_VIEWER("StartBtn"), CVAR_INPUT_VIEWER("StartBtnOutlineMode"), true },
    { "D-Pad", CVAR_INPUT_VIEWER("Dpad"), CVAR_INPUT_VIEWER("DpadOutlineMode"), false },
    { "Modifier Button 1", CVAR_INPUT_VIEWER("Mod1"), CVAR_INPUT_VIEWER("Mod1OutlineMode"), false },
    { "Modifier Button 2", CVAR_INPUT_VIEWER("Mod2"), CVAR_INPUT_VIEWER("Mod2OutlineMode"), false },
};

// Called after a cvar was written: saves the config and lets code that depends on the cvar catch up.
void CvarChanged(const char* cvar) {
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ShipInit::Init(cvar);
}

// Every row of this page stores one cvar, which also names the row.
Row CvarRow(RowKind kind, const char* label, const char* cvar, const char* desc) {
    Row row;
    row.kind = kind;
    row.id = RowId(std::string("inputviewer/") + cvar);
    row.label = label;
    row.desc = desc;
    return row;
}

Row Toggle(const char* label, const char* cvar, bool defaultValue, const char* desc = "") {
    Row row = CvarRow(RowKind::Toggle, label, cvar, desc);
    row.on = CVarGetInteger(cvar, defaultValue);
    row.activate = [cvar, defaultValue]() {
        CVarSetInteger(cvar, !CVarGetInteger(cvar, defaultValue));
        CvarChanged(cvar);
    };
    return row;
}

// `format` takes the value as an int ("%dpx").
Row IntSlider(const char* label, const char* cvar, int32_t defaultValue, int32_t min, int32_t max,
              const char* format, const char* desc = "") {
    Row row = CvarRow(RowKind::Slider, label, cvar, desc);
    int32_t value = CVarGetInteger(cvar, defaultValue);
    row.value = value;
    row.min = min;
    row.max = max;
    row.step = 1;
    char buf[64];
    snprintf(buf, sizeof(buf), format, value);
    row.valueText = buf;
    row.setValue = [cvar](double v) {
        CVarSetInteger(cvar, (int32_t)std::lround(v));
        CvarChanged(cvar);
    };
    return row;
}

// `format` takes the value as a double ("%.2f").
Row FloatSlider(const char* label, const char* cvar, float defaultValue, float min, float max, float step,
                const char* format, const char* desc = "") {
    Row row = CvarRow(RowKind::Slider, label, cvar, desc);
    float value = CVarGetFloat(cvar, defaultValue);
    row.value = value;
    row.min = min;
    row.max = max;
    row.step = step;
    char buf[64];
    snprintf(buf, sizeof(buf), format, (double)value);
    row.valueText = buf;
    row.setValue = [cvar](double v) {
        CVarSetFloat(cvar, (float)v);
        CvarChanged(cvar);
    };
    return row;
}

Row Dropdown(const char* label, const char* cvar, int32_t defaultValue, const std::vector<Option>& options,
             const char* desc = "") {
    Row row = CvarRow(RowKind::Dropdown, label, cvar, desc);
    row.options = options;
    row.current = CVarGetInteger(cvar, defaultValue);
    row.setOption = [cvar](int32_t value) {
        CVarSetInteger(cvar, value);
        CvarChanged(cvar);
    };
    return row;
}

// The menu has no colour picker of its own yet, so these rows draw the ImGui one.
Row ColorPicker(const char* label, const char* cvar, Color_RGBA8 defaultColor) {
    Row row = CvarRow(RowKind::ImGui, label, cvar, "");
    row.searchable = false;
    row.drawImGui = [label, cvar, defaultColor]() {
        UIWidgets::CVarColorPicker(label, cvar, defaultColor, true,
                                   UIWidgets::ColorPickerRandomButton | UIWidgets::ColorPickerResetButton);
    };
    return row;
}

Group SettingsGroup() {
    Group group;
    group.title = "Input Viewer Settings";
    group.rows.push_back(FloatSlider("Input Viewer Scale", CVAR_INPUT_VIEWER("Scale"), 1.0f, 0.1f, 5.0f, 0.01f,
                                     "%.2f", "Sets the on screen size of the input viewer"));
    group.rows.push_back(Toggle("Enable Dragging", CVAR_INPUT_VIEWER("EnableDragging"), true));
    group.rows.push_back(Toggle("Show Background Layer", CVAR_INPUT_VIEWER("ShowBackground"), true));
    return group;
}

Group ButtonsGroup() {
    Group group;
    group.title = "Buttons";
    bool useGlobalOutline = CVarGetInteger(CVAR_INPUT_VIEWER("UseGlobalButtonOutlineMode"), 1);

    Row outline = Dropdown("Button Outlines/Backgrounds", CVAR_INPUT_VIEWER("ButtonOutlineMode"),
                           BUTTON_OUTLINE_NOT_PRESSED, kButtonOutlineOptions,
                           "Sets the desired visibility behavior for the button outline/background layers. Useful "
                           "for custom input viewers.");
    if (!useGlobalOutline) {
        outline.disabled = true;
        outline.reason = "Disabled because Global Button Outline is off";
    }
    group.rows.push_back(std::move(outline));
    group.rows.push_back(Toggle("Use for all buttons", CVAR_INPUT_VIEWER("UseGlobalButtonOutlineMode"), true));

    for (const ButtonLayer& button : kButtonLayers) {
        std::string name = button.name;
        group.rows.push_back(Toggle(("Show " + name + " Layers").c_str(), button.cvar, button.shownByDefault));
        // A button that is shown gets its own outline mode when the buttons do not share one.
        if (!useGlobalOutline && CVarGetInteger(button.cvar, button.shownByDefault)) {
            group.rows.push_back(Dropdown((name + " Outline").c_str(), button.outlineCvar, BUTTON_OUTLINE_NOT_PRESSED,
                                          kButtonOutlineOptionsVerbose));
        }
    }
    return group;
}

Group AnalogStickGroup() {
    Group group;
    group.title = "Analog Stick";
    group.rows.push_back(
        Dropdown("Analog Stick Visibility", CVAR_INPUT_VIEWER("AnalogStick.VisibilityMode"), STICK_MODE_ALWAYS_SHOWN,
                 kStickModeOptions,
                 "Determines the conditions under which the moving layer of the analog stick texture is visible."));
    group.rows.push_back(
        Dropdown("Analog Stick Outline/Background Visibility", CVAR_INPUT_VIEWER("AnalogStick.OutlineMode"),
                 STICK_MODE_ALWAYS_SHOWN, kStickModeOptions,
                 "Determines the conditions under which the analog stick outline/background texture is visible."));
    group.rows.push_back(
        IntSlider("Analog Stick Movement", CVAR_INPUT_VIEWER("AnalogStick.Movement"), 12, 0, 200, "%dpx",
                  "Sets the distance to move the analog stick in the input viewer. Useful for custom input viewers."));
    return group;
}

Group RightStickGroup() {
    Group group;
    group.title = "Additional (\"Right\") Stick";
    group.rows.push_back(
        Dropdown("Right Stick Visibility", CVAR_INPUT_VIEWER("RightStick.VisibilityMode"), STICK_MODE_ALWAYS_HIDDEN,
                 kStickModeOptions,
                 "Determines the conditions under which the moving layer of the right stick texture is visible."));
    group.rows.push_back(
        Dropdown("Right Stick Outline/Background Visibility", CVAR_INPUT_VIEWER("RightStick.OutlineMode"),
                 STICK_MODE_ALWAYS_HIDDEN, kStickModeOptions,
                 "Determines the conditions under which the right stick outline/background texture is visible."));
    group.rows.push_back(
        IntSlider("Right Stick Movement", CVAR_INPUT_VIEWER("RightStick.Movement"), 7, 0, 200, "%dpx",
                  "Sets the distance to move the right stick in the input viewer. Useful for custom input viewers."));
    return group;
}

Group AnalogAnglesGroup() {
    Group group;
    group.title = "Analog Angle Values";
    group.rows.push_back(Toggle("Show Analog Stick Angle Values", CVAR_INPUT_VIEWER("AnalogAngles.Enabled"), false,
                                "Displays analog stick angle values in the input viewer"));
    if (!CVarGetInteger(CVAR_INPUT_VIEWER("AnalogAngles.Enabled"), 0)) {
        return group;
    }
    group.rows.push_back(ColorPicker("Text Color", CVAR_INPUT_VIEWER("AnalogAngles.TextColor"), kTextColorDefault));
    // Stored as a factor (1.0 is normal size), shown and edited as a percentage.
    {
        const char* cvar = CVAR_INPUT_VIEWER("AnalogAngles.Scale");
        Row scale = CvarRow(RowKind::Slider, "Angle Text Scale", cvar, "");
        int percent = (int)std::lround(CVarGetFloat(cvar, 1.0f) * 100.0f);
        scale.value = percent;
        scale.min = 10;
        scale.max = 500;
        scale.step = 1;
        scale.valueText = std::to_string(percent) + "%";
        scale.setValue = [cvar](double v) {
            CVarSetFloat(cvar, (float)(v / 100.0));
            CvarChanged(cvar);
        };
        group.rows.push_back(std::move(scale));
    }
    group.rows.push_back(IntSlider("Angle Text Offset", CVAR_INPUT_VIEWER("AnalogAngles.Offset"), 0, 0, 400, "%dpx"));

    group.rows.push_back(
        Toggle("Highlight ESS Position", CVAR_INPUT_VIEWER("AnalogAngles.Range1.Enabled"), false,
               "Highlights the angle value text when the analog stick is in ESS position (on flat ground)"));
    if (CVarGetInteger(CVAR_INPUT_VIEWER("AnalogAngles.Range1.Enabled"), 0)) {
        group.rows.push_back(
            ColorPicker("ESS Color", CVAR_INPUT_VIEWER("AnalogAngles.Range1.Color"), kRange1ColorDefault));
    }

    group.rows.push_back(
        Toggle("Highlight Walking Speed Angles", CVAR_INPUT_VIEWER("AnalogAngles.Range2.Enabled"), false,
               "Highlights the angle value text when the analog stick is at an angle that would produce a walking "
               "speed (on flat ground)\n\n"
               "Useful for 1.0 Empty Jumpslash Quick Put Away"));
    if (CVarGetInteger(CVAR_INPUT_VIEWER("AnalogAngles.Range2.Enabled"), 0)) {
        group.rows.push_back(
            ColorPicker("Walking Speed Color", CVAR_INPUT_VIEWER("AnalogAngles.Range2.Color"), kRange2ColorDefault));
    }
    return group;
}
} // namespace

void BuildInputViewerPage(std::vector<Group>& groups) {
    groups.push_back(SettingsGroup());
    groups.push_back(ButtonsGroup());
    groups.push_back(AnalogStickGroup());
    groups.push_back(RightStickGroup());
    groups.push_back(AnalogAnglesGroup());
}

int CountInputViewerPage() {
    std::vector<Group> groups;
    BuildInputViewerPage(groups);
    int on = 0;
    for (const Group& group : groups) {
        for (const Row& row : group.rows) {
            if (row.kind == RowKind::Toggle && row.on) {
                on++;
            }
        }
    }
    return on;
}

} // namespace Pm
