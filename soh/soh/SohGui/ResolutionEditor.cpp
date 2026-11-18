#include "ResolutionEditor.h"
#include <imgui.h>
#include <libultraship/libultraship.h>

#include "soh/SohGui/UIWidgets.hpp"
#include <graphic/Fast3D/gfx_pc.h>
#include "soh/OTRGlobals.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/SohGui.hpp"

/*  Console Variables are grouped under gAdvancedResolution. (e.g. CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled")

    The following cvars are used in Libultraship and can be edited here:
        - Enabled                                       - Turns Advanced Resolution Mode on.
        - AspectRatioX, AspectRatioY                    - Aspect ratio controls. To toggle off, set either to zero.
        - VerticalPixelCount, VerticalResolutionToggle  - Resolution controls.
        - PixelPerfectMode, IntegerScale.Factor         - Pixel Perfect Mode a.k.a. integer scaling controls.
        - IntegerScale.FitAutomatically                 - Automatic resizing for Pixel Perfect Mode.
        - IntegerScale.NeverExceedBounds                - Prevents manual resizing from exceeding screen bounds.

    The following cvars are also implemented in LUS for niche use cases:
        - IgnoreAspectCorrection                        - Stretch framebuffer to fill screen.
            This is something of a power-user setting for niche setups that most people won't need or care about,
            but may be useful if playing the Switch/Wii U ports on a 4:3 television.
        - IntegerScale.ExceedBoundsBy                   - Offset the max screen bounds, usually by +1.
                                                          This isn't that useful at the moment, so it's unused here.
*/

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
enum setting { UPDATE_aspectRatioX, UPDATE_aspectRatioY, UPDATE_verticalPixelCount };

std::unordered_map<int32_t, const char*> aspectRatioPresetLabels = { { 0, "Off" },
                                                                     { 1, "Custom" },
                                                                     { 2, "Original (4:3)" },
                                                                     { 3, "Widescreen (16:9)" },
                                                                     { 4, "Nintendo 3DS (5:3)" },
                                                                     { 5, "16:10 (8:5)" },
                                                                     { 6, "Ultrawide (21:9)" } };
const float aspectRatioPresetsX[] = { 0.0f, 16.0f, 4.0f, 16.0f, 5.0f, 16.0f, 21.0f };
const float aspectRatioPresetsY[] = { 0.0f, 9.0f, 3.0f, 9.0f, 3.0f, 10.0f, 9.0f };
const int default_aspectRatio = 1; // Default combo list option

std::unordered_map<int32_t, const char*> pixelCountPresetLabels = {
    { 0, "Custom" },     { 1, "Native N64 (240p)" }, { 2, "2x (480p)" },       { 3, "3x (720p)" },  { 4, "4x (960p)" },
    { 5, "5x (1200p)" }, { 6, "6x (1440p)" },        { 7, "Full HD (1080p)" }, { 8, "4K (2160p)" }
};
const int pixelCountPresets[] = { 480, 240, 480, 720, 960, 1200, 1440, 1080, 2160 };
const int default_pixelCount = 0; // Default combo list option

// Resolution clamp values as hardcoded in LUS::Gui::ApplyResolutionChanges()
const uint32_t minVerticalPixelCount = SCREEN_HEIGHT;
const uint32_t maxVerticalPixelCount = 4320; // 18x native, or 8K TV resolution
// Upper end of the horizontal pixel count slider: the width of an 8K TV.
const int32_t maxHorizontalPixelCount = 7680;

const unsigned short default_maxIntegerScaleFactor = 6; // Default size of Integer scale factor slider.

// Reasons shown on settings that need Pixel Perfect Mode.
static const char* const reason_pixelPerfectMode = "Requires \xe2\x80\x9cPixel Perfect Mode\xe2\x80\x9d";
static const char* const reason_manualIntegerScale = "Requires Pixel Perfect Mode, with automatic fit off";

// Initialise update flags.
static bool update[3];

// Initialise integer scale bounds.
static short max_integerScaleFactor = default_maxIntegerScaleFactor; // default value, which may or may not get
                                                                     // overridden depending on viewport res

static short integerScale_maximumBounds = 1; // can change when window is resized

// Combo List defaults
static int32_t item_aspectRatio;
static int32_t item_pixelCount;
// Stored Values for non-UIWidgets elements
static float aspectRatioX;
static float aspectRatioY;
static int32_t verticalPixelCount;
// Additional settings
static bool showHorizontalResField;
static int32_t horizontalPixelCount;

using namespace UIWidgets;

static void SaveCvars() {
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

// Clamps the values that are edited through pointers and writes the flagged ones to their cvars.
static void ApplyResolutionUpdates() {
    if (update[UPDATE_aspectRatioX] || update[UPDATE_aspectRatioY] || update[UPDATE_verticalPixelCount]) {
        if (update[UPDATE_aspectRatioX]) {
            if (aspectRatioX < 0.0f) {
                aspectRatioX = 0.0f;
            }
            CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", aspectRatioX);
        }
        if (update[UPDATE_aspectRatioY]) {
            if (aspectRatioY < 0.0f) {
                aspectRatioY = 0.0f;
            }
            CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", aspectRatioY);
        }
        if (update[UPDATE_verticalPixelCount]) {
            // There's a upper and lower clamp on the Libultraship side too,
            // so clamping it here is entirely visual, so the vertical resolution field reflects it.
            if (verticalPixelCount < minVerticalPixelCount) {
                verticalPixelCount = minVerticalPixelCount;
            }
            if (verticalPixelCount > maxVerticalPixelCount) {
                verticalPixelCount = maxVerticalPixelCount;
            }
            CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalPixelCount", verticalPixelCount);
        }
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".UIComboItem.AspectRatio", item_aspectRatio);
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".UIComboItem.PixelCount", item_pixelCount);
        SaveCvars();
    }
    // Nothing is left to write.
    for (bool& flag : update) {
        flag = false;
    }
}

// Reads the values that are edited through pointers from their cvars.
static void LoadResolutionVars() {
    // Combo List defaults
    item_aspectRatio = CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".UIComboItem.AspectRatio", 3);
    item_pixelCount = CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".UIComboItem.PixelCount", default_pixelCount);
    // Stored Values for non-UIWidgets elements
    aspectRatioX = CVarGetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", aspectRatioPresetsX[item_aspectRatio]);
    aspectRatioY = CVarGetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", aspectRatioPresetsY[item_aspectRatio]);
    verticalPixelCount =
        CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalPixelCount", pixelCountPresets[item_pixelCount]);
    // Additional settings
    horizontalPixelCount = (verticalPixelCount / aspectRatioY) * aspectRatioX;
}

// True while an aspect ratio is being enforced, which the horizontal pixel count needs.
static bool IsAspectRatioForced() {
    return (aspectRatioX > 0.0f) && (aspectRatioY > 0.0f);
}

static bool IsPixelPerfectModeOn() {
    return CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".PixelPerfectMode", 0);
}

static bool IsFitAutomaticallyOn() {
    return CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.FitAutomatically", 0);
}

static bool IsAdvancedResolutionOff() {
    return mSohMenu->GetDisabledMap().at(DISABLE_FOR_ADVANCED_RESOLUTION_OFF).active;
}

// Disables the widget while the advanced settings are off. Returns true when it did.
static bool DisableForAdvancedResolutionOff(WidgetInfo& info) {
    if (IsAdvancedResolutionOff()) {
        info.activeDisables.push_back(DISABLE_FOR_ADVANCED_RESOLUTION_OFF);
        return true;
    }
    return false;
}

static void DisableWithReason(WidgetInfo& info, const char* reason) {
    info.options->disabled = true;
    info.options->disabledTooltip = reason;
}

void RegisterResolutionWidgets() {
    WidgetPath path = { "Settings", "Graphics", SECTION_COLUMN_2 };

    // Resolution visualiser
    mSohMenu->AddWidget(path, "Viewport dimensions: {} x {}", WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.name = fmt::format("Viewport dimensions: {} x {}", gfx_current_game_window_viewport.width,
                                    gfx_current_game_window_viewport.height);
        });
    mSohMenu->AddWidget(path, "Internal resolution: {} x {}", WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.name = fmt::format("Internal resolution: {} x {}", gfx_current_dimensions.width,
                                    gfx_current_dimensions.height);
        });

    //  Activator
    mSohMenu->AddWidget(path, "Enable advanced settings.", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled")
        .RaceDisable(false);
    // Error/Warning display
    mSohMenu
        ->AddWidget(path, ICON_FA_EXCLAMATION_TRIANGLE " Significant frame rate (FPS) drops may be occuring.",
                    WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc(
            [](WidgetInfo& info) { info.isHidden = !(!CVarGetInteger(CVAR_LOW_RES_MODE, 0) && IsDroppingFrames()); })
        .Options(TextOptions().Color(Colors::Orange));
    mSohMenu->AddWidget(path, ICON_FA_QUESTION_CIRCLE " \"N64 Mode\" is overriding these settings.", WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { info.isHidden = !CVarGetInteger(CVAR_LOW_RES_MODE, 0); })
        .Options(TextOptions().Color(Colors::LightBlue));
    mSohMenu->AddWidget(path, "Click to disable N64 mode", WIDGET_BUTTON)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { info.isHidden = !CVarGetInteger(CVAR_LOW_RES_MODE, 0); })
        .Callback([](WidgetInfo& info) {
            CVarSetInteger(CVAR_LOW_RES_MODE, 0);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        });

    // Aspect Ratio
    mSohMenu->AddWidget(path, "AspectSep", WIDGET_SEPARATOR).RaceDisable(false).PreFunc([](WidgetInfo& info) {
        if (mSohMenu->GetDisabledMap().at(DISABLE_FOR_ADVANCED_RESOLUTION_OFF).active) {
            info.activeDisables.push_back(DISABLE_FOR_ADVANCED_RESOLUTION_OFF);
        }
    });
    mSohMenu->AddWidget(path, "Force aspect ratio:", WIDGET_TEXT).RaceDisable(false).PreFunc([](WidgetInfo& info) {
        if (mSohMenu->GetDisabledMap().at(DISABLE_FOR_ADVANCED_RESOLUTION_OFF).active) {
            info.activeDisables.push_back(DISABLE_FOR_ADVANCED_RESOLUTION_OFF);
        }
    });
    mSohMenu->AddWidget(path, "(Select \"Off\" to disable.)", WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (mSohMenu->GetDisabledMap().at(DISABLE_FOR_ADVANCED_RESOLUTION_OFF).active) {
                info.activeDisables.push_back(DISABLE_FOR_ADVANCED_RESOLUTION_OFF);
            }
        })
        .SameLine(true)
        .Options(TextOptions().Color(Colors::Gray));
    // Presets
    mSohMenu->AddWidget(path, "Aspect Ratio", WIDGET_COMBOBOX)
        .ValuePointer(&item_aspectRatio)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (mSohMenu->GetDisabledMap().at(DISABLE_FOR_ADVANCED_RESOLUTION_OFF).active) {
                info.activeDisables.push_back(DISABLE_FOR_ADVANCED_RESOLUTION_OFF);
            }
        })
        .Callback([](WidgetInfo& info) {
            if (item_aspectRatio != default_aspectRatio) { // don't change anything if "Custom" is selected.
                aspectRatioX = aspectRatioPresetsX[item_aspectRatio];
                aspectRatioY = aspectRatioPresetsY[item_aspectRatio];

                if (showHorizontalResField) {
                    horizontalPixelCount = (verticalPixelCount / aspectRatioY) * aspectRatioX;
                }

                CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", aspectRatioX);
                CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", aspectRatioY);
            }
            CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".UIComboItem.AspectRatio", item_aspectRatio);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ComboboxOptions().ComboMap(aspectRatioPresetLabels));
    // Aspect ratio sliders, hidden while using one of the presets or the horizontal pixel count.
    WidgetFunc aspectRatioSliderPreFunc = [](WidgetInfo& info) {
        info.isHidden = !(item_aspectRatio == default_aspectRatio && !showHorizontalResField);
        DisableForAdvancedResolutionOff(info);
    };
    WidgetFunc aspectRatioSliderCallback = [](WidgetInfo& info) {
        item_aspectRatio = default_aspectRatio;
        update[UPDATE_aspectRatioX] = true;
        update[UPDATE_aspectRatioY] = true;
        ApplyResolutionUpdates();
    };
    mSohMenu->AddWidget(path, "X", WIDGET_SLIDER_FLOAT)
        .ValuePointer(&aspectRatioX)
        .RaceDisable(false)
        .PreFunc(aspectRatioSliderPreFunc)
        .Callback(aspectRatioSliderCallback)
        .Options(FloatSliderOptions().Min(0.1f).Max(32.0f).Step(0.001f).Format("%.3f"));
    mSohMenu->AddWidget(path, "Y", WIDGET_SLIDER_FLOAT)
        .ValuePointer(&aspectRatioY)
        .RaceDisable(false)
        .PreFunc(aspectRatioSliderPreFunc)
        .Callback(aspectRatioSliderCallback)
        .Options(FloatSliderOptions().Min(0.1f).Max(24.0f).Step(0.001f).Format("%.3f"));
    // Show calculated aspect ratio
    mSohMenu->AddWidget(path, "Aspect ratio: {:.2f}:1", WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !(showHorizontalResField && item_aspectRatio);
            const float resolvedAspectRatio = (float)gfx_current_dimensions.width / gfx_current_dimensions.height;
            info.name = fmt::format("Aspect ratio: {:.2f}:1", resolvedAspectRatio);
        });

    // Vertical Resolution
    mSohMenu->AddWidget(path, "Set fixed vertical resolution (disables resolution slider)", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { DisableForAdvancedResolutionOff(info); })
        .Options(CheckboxOptions().Tooltip(
            "Override the resolution scale slider and use the settings below, irrespective of window size."));
    mSohMenu->AddWidget(path, "Pixel Count Presets", WIDGET_COMBOBOX)
        .ValuePointer(&item_pixelCount)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { DisableForAdvancedResolutionOff(info); })
        .Callback([](WidgetInfo& info) {
            if (item_pixelCount != default_pixelCount) { // "Custom" keeps the pixel count as it is.
                verticalPixelCount = pixelCountPresets[item_pixelCount];

                if (showHorizontalResField) {
                    horizontalPixelCount = (verticalPixelCount / aspectRatioY) * aspectRatioX;
                }

                CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalPixelCount", verticalPixelCount);
            }
            CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".UIComboItem.PixelCount", item_pixelCount);
            SaveCvars();
        })
        .Options(ComboboxOptions().ComboMap(pixelCountPresetLabels));
    // Horizontal Resolution, if visibility is enabled for it.
    // Only show the field if Aspect Ratio is being enforced.
    mSohMenu->AddWidget(path, "Horiz. Pixel Count", WIDGET_SLIDER_INT)
        .ValuePointer(&horizontalPixelCount)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !(showHorizontalResField && IsAspectRatioForced());
            DisableForAdvancedResolutionOff(info);
        })
        .Callback([](WidgetInfo& info) {
            // So basically we're "faking" this one by setting aspectRatioX instead.
            item_aspectRatio = default_aspectRatio;
            if (horizontalPixelCount < SCREEN_WIDTH) {
                horizontalPixelCount = SCREEN_WIDTH;
            }
            aspectRatioX = horizontalPixelCount;
            aspectRatioY = verticalPixelCount;
            update[UPDATE_aspectRatioX] = true;
            update[UPDATE_aspectRatioY] = true;
            ApplyResolutionUpdates();
        })
        .Options(IntSliderOptions().Min(SCREEN_WIDTH).Max(maxHorizontalPixelCount).Step(8));
    // Display a notice instead.
    mSohMenu->AddWidget(path, ICON_FA_QUESTION_CIRCLE " \"Force aspect ratio\" required.", WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { info.isHidden = !(showHorizontalResField && !IsAspectRatioForced()); })
        .Options(TextOptions().Color(Colors::LightBlue));
    mSohMenu->AddWidget(path, "Click to resolve", WIDGET_BUTTON)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !(showHorizontalResField && !IsAspectRatioForced());
            DisableForAdvancedResolutionOff(info);
        })
        .Callback([](WidgetInfo& info) {
            item_aspectRatio = default_aspectRatio; // Set it to Custom
            aspectRatioX = aspectRatioPresetsX[2];  // but use the 4:3 defaults
            aspectRatioY = aspectRatioPresetsY[2];
            update[UPDATE_aspectRatioX] = true;
            update[UPDATE_aspectRatioY] = true;
            horizontalPixelCount = (verticalPixelCount / aspectRatioY) * aspectRatioX;
            ApplyResolutionUpdates();
        });
    // Vertical Resolution part 2
    mSohMenu->AddWidget(path, "Vertical Pixel Count", WIDGET_SLIDER_INT)
        .ValuePointer(&verticalPixelCount)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { DisableForAdvancedResolutionOff(info); })
        .Callback([](WidgetInfo& info) {
            item_pixelCount = default_pixelCount;
            update[UPDATE_verticalPixelCount] = true;

            // Account for the natural instinct to enter horizontal first.
            // Ignore vertical resolutions that are below the lower clamp constant.
            if (showHorizontalResField && !(verticalPixelCount < minVerticalPixelCount)) {
                item_aspectRatio = default_aspectRatio;
                aspectRatioX = horizontalPixelCount;
                aspectRatioY = verticalPixelCount;
                update[UPDATE_aspectRatioX] = true;
                update[UPDATE_aspectRatioY] = true;
            }
            ApplyResolutionUpdates();
        })
        .Options(IntSliderOptions().Min(minVerticalPixelCount).Max(maxVerticalPixelCount).Step(8));

    // Integer scaling settings group (Pixel Perfect Mode)
    mSohMenu->AddWidget(path, "Integer Scaling Settings:", WIDGET_TEXT).RaceDisable(false);
    // Pixel Perfect Mode
    mSohMenu->AddWidget(path, "Pixel Perfect Mode", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_PREFIX_ADVANCED_RESOLUTION ".PixelPerfectMode")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!DisableForAdvancedResolutionOff(info) &&
                mSohMenu->GetDisabledMap().at(DISABLE_FOR_VERTICAL_RESOLUTION_OFF).active) {
                info.activeDisables.push_back(DISABLE_FOR_VERTICAL_RESOLUTION_OFF);
            }
        })
        .Options(CheckboxOptions().Tooltip("Don't scale image to fill window."));
    // Integer Scaling
    mSohMenu->AddWidget(path, "Integer scale factor: %d", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.Factor")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            // The slider reaches as far as the viewport allows.
            std::static_pointer_cast<IntSliderOptions>(info.options)->max = max_integerScaleFactor;
            if (!DisableForAdvancedResolutionOff(info) && (!IsPixelPerfectModeOn() || IsFitAutomaticallyOn())) {
                DisableWithReason(info, reason_manualIntegerScale);
            }
        })
        .Options(IntSliderOptions()
                     .Min(1)
                     .Max(default_maxIntegerScaleFactor)
                     .DefaultValue(1)
                     .Tooltip("Integer scales the image. Only available in Pixel Perfect Mode."));
    // Display warning if size is being clamped or if framebuffer is larger than viewport.
    mSohMenu->AddWidget(path, ICON_FA_EXCLAMATION_TRIANGLE " Window exceeded.", WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.isHidden =
                !(!IsAdvancedResolutionOff() && IsPixelPerfectModeOn() &&
                  CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.NeverExceedBounds", 1) &&
                  CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.Factor", 1) >
                      integerScale_maximumBounds);
        })
        .Options(TextOptions().Color(Colors::Yellow));
    mSohMenu->AddWidget(path, "Automatically scale image to fit viewport", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.FitAutomatically")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!DisableForAdvancedResolutionOff(info) && !IsPixelPerfectModeOn()) {
                DisableWithReason(info, reason_pixelPerfectMode);
            }
        })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Automatically sets scale factor to fit window. Only available in Pixel Perfect Mode."));

    // Additional settings
    mSohMenu->AddWidget(path, "Additional Settings:", WIDGET_TEXT).RaceDisable(false);
#if defined(__SWITCH__) || defined(__WIIU__)
    // Disable aspect correction, stretching the framebuffer to fill the viewport.
    // This option is only really needed on systems limited to 16:9 TV resolutions, such as consoles.
    // The associated cvar is still functional on PC platforms if you want to use it though.
    mSohMenu->AddWidget(path, "Disable aspect correction and stretch the output image.", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_PREFIX_ADVANCED_RESOLUTION ".IgnoreAspectCorrection")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!DisableForAdvancedResolutionOff(info) && IsPixelPerfectModeOn()) {
                DisableWithReason(info, "Not available in Pixel Perfect Mode");
            }
        })
        .Options(CheckboxOptions().Tooltip("Might be useful for 4:3 televisions!\n"
                                           "Not available in Pixel Perfect Mode."));
#else
    // This setting is intentionally not exposed on PC platforms,
    // but may be accidentally activated for varying reasons.
    // Having this button should hopefully prevent support headaches.
    WidgetFunc aspectCorrectionIgnoredPreFunc = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IgnoreAspectCorrection", 0);
    };
    mSohMenu
        ->AddWidget(path, ICON_FA_QUESTION_CIRCLE " If the image is stretched and you don't know why, click this.",
                    WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc(aspectCorrectionIgnoredPreFunc)
        .Options(TextOptions().Color(Colors::LightBlue));
    mSohMenu->AddWidget(path, "Click to reenable aspect correction.", WIDGET_BUTTON)
        .RaceDisable(false)
        .PreFunc(aspectCorrectionIgnoredPreFunc)
        .Callback([](WidgetInfo& info) {
            CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IgnoreAspectCorrection", 0);
            SaveCvars();
        });
#endif
    // A requested addition; an alternative way of displaying the resolution field.
    mSohMenu->AddWidget(path, "Show a horizontal resolution field, instead of aspect ratio.", WIDGET_CHECKBOX)
        .ValuePointer(&showHorizontalResField)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) {
            if (!showHorizontalResField && (aspectRatioX > 0.0f)) { // when turning this setting off
                // Refresh relevant values
                aspectRatioX = aspectRatioY * horizontalPixelCount / verticalPixelCount;
                horizontalPixelCount = (verticalPixelCount / aspectRatioY) * aspectRatioX;
            } else { // when turning this setting on
                item_aspectRatio = default_aspectRatio;
                if (aspectRatioX > 0.0f) {
                    // Refresh relevant values in the opposite order
                    horizontalPixelCount = (verticalPixelCount / aspectRatioY) * aspectRatioX;
                    aspectRatioX = aspectRatioY * horizontalPixelCount / verticalPixelCount;
                }
            }
            update[UPDATE_aspectRatioX] = true;
            ApplyResolutionUpdates();
        });
    // Integer Scaling - Never Exceed Bounds.
    mSohMenu->AddWidget(path, "Prevent integer scaling from exceeding screen bounds.", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.NeverExceedBounds")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!DisableForAdvancedResolutionOff(info) && (!IsPixelPerfectModeOn() || IsFitAutomaticallyOn())) {
                DisableWithReason(info, reason_manualIntegerScale);
            }
        })
        .Callback([](WidgetInfo& info) {
            // Initialise the "Exceed Bounds By" cvar if it's been changed.
            if (CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy", 0)) {
                CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy", 0);
                SaveCvars();
            }
        })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Makes screen bounds take priority over specified factor.\n\n"
            "Prevents integer scaling factor from exceeding screen bounds.\n\n"
            "Enabled: Will clamp the scaling factor and display a gentle warning in the "
            "resolution editor.\n"
            "Disabled: Will allow scaling to exceed screen bounds, for users who want to crop "
            "overscan.\n\n"
            "Please note that exceeding screen bounds may show a scroll bar on-screen."));
    // Integer Scaling - Exceed Bounds By 1x/Offset.
    // A popular feature in some retro frontends/upscalers, sometimes called "crop overscan" or "1080p 5x".
    mSohMenu->AddWidget(path, "Allow integer scale factor to go +1 above maximum screen bounds.", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy")
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            if (!DisableForAdvancedResolutionOff(info) && !IsPixelPerfectModeOn()) {
                DisableWithReason(info, reason_pixelPerfectMode);
            }
        });
    // Display an info message about the scroll bar.
    mSohMenu
        ->AddWidget(path, ICON_FA_INFO_CIRCLE " A scroll bar may become visible if screen bounds are exceeded.",
                    WIDGET_TEXT)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !(!CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.NeverExceedBounds", 1) ||
                              CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy", 0));
        })
        .Options(TextOptions().Color(Colors::Green));
    // Another support helper button, to disable the "Exceed Bounds By" cvar.
    mSohMenu->AddWidget(path, "Click to reset a console variable that may be causing this.", WIDGET_BUTTON)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            info.isHidden = !CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy", 0);
        })
        .Callback([](WidgetInfo& info) {
            CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy", 0);
            SaveCvars();
        });

    // A search can list these settings before the Graphics section was ever opened, so the values behind
    // the pointers are read once up front.
    LoadResolutionVars();
}

void UpdateResolutionVars() {
    ApplyResolutionUpdates();

    // Initialise integer scale bounds.
    max_integerScaleFactor = default_maxIntegerScaleFactor; // default value, which may or may not get
                                                            // overridden depending on viewport res
    integerScale_maximumBounds = 1;                         // can change when window is resized
    // This is mostly just for UX purposes, as Fit Automatically logic is part of LUS.
    if (((float)gfx_current_game_window_viewport.width / gfx_current_game_window_viewport.height) >
        ((float)gfx_current_dimensions.width / gfx_current_dimensions.height)) {
        // Scale to window height
        integerScale_maximumBounds = gfx_current_game_window_viewport.height / gfx_current_dimensions.height;
    } else {
        // Scale to window width
        integerScale_maximumBounds = gfx_current_game_window_viewport.width / gfx_current_dimensions.width;
    }
    // Lower-clamping maximum bounds value to 1 is no-longer necessary as that's accounted for in LUS.
    // Letting it go below 1 in this Editor will even allow for checking if screen bounds are being exceeded.
    if (default_maxIntegerScaleFactor < integerScale_maximumBounds) {
        max_integerScaleFactor = integerScale_maximumBounds +
                                 CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy", 0);
    }

    LoadResolutionVars();

    // Pixel Perfect Mode needs the fixed vertical resolution.
    if (!CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 0) && IsPixelPerfectModeOn()) {
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".PixelPerfectMode", 0);
        SaveCvars();
    }
    // This is just here to update the value shown on the slider.
    // The function in LUS to handle this setting will ignore IntegerScaleFactor while active.
    if (IsFitAutomaticallyOn() &&
        CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.Factor", 1) != integerScale_maximumBounds) {
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.Factor", integerScale_maximumBounds);
        SaveCvars();
    }
}

bool IsDroppingFrames() {
    // a rather imprecise way of checking for frame drops.
    // but it's mostly there to inform the player of large drops.
    const short targetFPS = OTRGlobals::Instance->GetInterpolationFPS();
    const float threshold = targetFPS / 20.0f + 4.1f;
    return ImGui::GetIO().Framerate < targetFPS - threshold;
}

static RegisterMenuUpdateFunc updateFunc(UpdateResolutionVars, "Settings", "Graphics");
static RegisterMenuInitFunc initFunc(RegisterResolutionWidgets);

} // namespace SohGui
