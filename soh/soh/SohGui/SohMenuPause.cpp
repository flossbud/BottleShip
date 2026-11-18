#include "SohMenu.h"
#include "SohGui.hpp"
#include "PauseMenu/PmInline.h"
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "window/gui/ConsoleWindow.h"

#include <algorithm>
#include <spdlog/fmt/fmt.h>

#if defined(__ANDROID__) || defined(__IOS__)
#include "port/mobile/MobileImpl.h"
#endif

// SohMenu as the data source of the pause-style menu.
//
// The menu's widgets (labels, cvars, tooltips, hide and disable rules) are still declared with AddWidget
// in SohMenuSettings.cpp and friends. This file turns them into the rows the new menu draws, so both
// stay in step and nothing about a setting is written down twice.

extern "C" {
// Non-zero while the game's audio is in its pause state (code_800EC960.c).
extern s8 D_80130608;
}

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;

namespace {
const char* kPageCvar = CVAR_SETTING("Menu.ActiveHeader");

void SaveCvars() {
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// The label to show for a widget name: no "##id" suffix, no icon-font glyphs, no trailing colon.
std::string CleanLabel(const std::string& name) {
    std::string s = name.substr(0, name.find("##"));
    std::string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        // FontAwesome lives in the private use area U+F000..U+F8FF: 0xEF 0x80..0xA3 0x80..0xBF.
        if (c == 0xEF && i + 2 < s.size() && (unsigned char)s[i + 1] >= 0x80 && (unsigned char)s[i + 1] <= 0xA3) {
            i += 3;
            continue;
        }
        out.push_back(s[i]);
        i++;
    }
    size_t a = out.find_first_not_of(' ');
    size_t b = out.find_last_not_of(' ');
    if (a == std::string::npos) {
        return "";
    }
    return out.substr(a, b - a + 1);
}

// Slider names carry their value format ("Master Volume: %d %%", "Crawl Speed %dx"). Splits one into the
// label and the format; `format` is left empty when the name has none.
void SplitSliderName(const std::string& name, std::string& label, std::string& format) {
    std::string s = CleanLabel(name);
    size_t pct = s.find('%');
    if (pct == std::string::npos) {
        label = s;
        format.clear();
    } else {
        size_t start = s.rfind(' ', pct);
        start = start == std::string::npos ? 0 : start + 1;
        label = s.substr(0, start);
        format = s.substr(start);
    }
    while (!label.empty() && (label.back() == ' ' || label.back() == ':')) {
        label.pop_back();
    }
}

// True when `format` is a printf format with exactly one conversion, of the wanted kind.
bool FormatIsSafe(const std::string& format, bool wantFloat) {
    int conversions = 0;
    for (size_t i = 0; i < format.size(); i++) {
        if (format[i] != '%') {
            continue;
        }
        if (i + 1 < format.size() && format[i + 1] == '%') {
            i++;
            continue;
        }
        size_t j = i + 1;
        while (j < format.size() && (isdigit((unsigned char)format[j]) || format[j] == '.' || format[j] == '+' ||
                                     format[j] == '-' || format[j] == ' ')) {
            j++;
        }
        if (j >= format.size()) {
            return false;
        }
        char conv = format[j];
        bool isFloat = conv == 'f';
        bool isInt = conv == 'd' || conv == 'i';
        if ((wantFloat && !isFloat) || (!wantFloat && !isInt)) {
            return false;
        }
        conversions++;
        i = j;
    }
    return conversions == 1;
}

std::string FormatInt(const std::string& format, int32_t value) {
    char buf[96];
    if (FormatIsSafe(format, false)) {
        snprintf(buf, sizeof(buf), format.c_str(), value);
    } else {
        snprintf(buf, sizeof(buf), "%d", value);
    }
    return buf;
}

std::string FormatFloat(const std::string& format, float value, float step) {
    char buf[96];
    if (FormatIsSafe(format, true)) {
        snprintf(buf, sizeof(buf), format.c_str(), value);
    } else {
        snprintf(buf, sizeof(buf), step >= 1.0f ? "%.0f" : step >= 0.1f ? "%.1f" : "%.2f", value);
    }
    return buf;
}

std::vector<Pm::Option> OptionsFromMap(const std::unordered_map<int32_t, const char*>& map) {
    std::vector<Pm::Option> options;
    for (const auto& [value, label] : map) {
        options.push_back({ value, label != nullptr ? label : "" });
    }
    std::sort(options.begin(), options.end(),
              [](const Pm::Option& a, const Pm::Option& b) { return a.value < b.value; });
    return options;
}

ImGuiID WidgetId(const WidgetInfo& widget) {
    // Widgets live in vectors that are filled once at start-up, so the address identifies one.
    return Pm::RowId(std::to_string((uintptr_t)&widget));
}

bool IsPopout(const WidgetInfo& widget) {
    return widget.type == WIDGET_WINDOW_BUTTON && widget.name.rfind("Popout", 0) == 0;
}

std::shared_ptr<Ship::GuiWindow> WindowOf(const WidgetInfo& widget) {
    if (widget.windowName == nullptr || widget.windowName[0] == '\0') {
        return nullptr;
    }
    return Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow(widget.windowName);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Pages and sections
// ---------------------------------------------------------------------------------------------------------------------
const std::vector<std::string>& SohMenu::Pages() {
    return menuOrder;
}

const std::vector<std::string>& SohMenu::Sections(const std::string& page) {
    return menuEntries.at(page).sidebarOrder;
}

std::string SohMenu::CurrentPage() {
    std::string page = CVarGetString(kPageCvar, "Settings");
    return menuEntries.contains(page) ? page : menuOrder.front();
}

void SohMenu::SetCurrentPage(const std::string& page) {
    CVarSetString(kPageCvar, page.c_str());
    SaveCvars();
}

std::string SohMenu::CurrentSection(const std::string& page) {
    auto& entry = menuEntries.at(page);
    std::string section = CVarGetString(entry.sidebarCvar, "");
    return entry.sidebars.contains(section) ? section : entry.sidebarOrder.front();
}

void SohMenu::SetCurrentSection(const std::string& page, const std::string& section) {
    CVarSetString(menuEntries.at(page).sidebarCvar, section.c_str());
    SaveCvars();
}

void SohMenu::PlaySound(Pm::Sound sound) {
    // The game's own pause screen: func_800F64E0 plays the open / close sound and puts the audio into or
    // out of its pause state, which softens the music. Leave that state alone if the game is already in it.
    bool enabled = CVarGetInteger(CVAR_SETTING("Menu.Sounds"), 1);
    if (sound == Pm::Sound::Close && mAudioPaused) {
        mAudioPaused = false;
        if (enabled) {
            func_800F64E0(0);
        } else {
            // Menu Sounds was turned off while the menu was open: leave the pause state without the sound.
            D_80130608 = 0;
            Audio_QueueCmdS32(0xF2000000, 0);
        }
        return;
    }
    if (!enabled) {
        return;
    }
    if (sound == Pm::Sound::Open && D_80130608 == 0) {
        func_800F64E0(1);
        mAudioPaused = true;
        return;
    }
    u16 sfx = NA_SE_SY_CURSOR;
    switch (sound) {
        case Pm::Sound::Open:
            sfx = NA_SE_SY_WIN_OPEN;
            break;
        case Pm::Sound::Close:
            sfx = NA_SE_SY_WIN_CLOSE;
            break;
        case Pm::Sound::Cursor:
            sfx = NA_SE_SY_CURSOR;
            break;
        case Pm::Sound::Decide:
            sfx = NA_SE_SY_DECIDE;
            break;
        case Pm::Sound::Cancel:
            sfx = NA_SE_SY_CANCEL;
            break;
        case Pm::Sound::Error:
            sfx = NA_SE_SY_ERROR;
            break;
        case Pm::Sound::PageLeft:
            sfx = NA_SE_SY_WIN_SCROLL_LEFT;
            break;
        case Pm::Sound::PageRight:
            sfx = NA_SE_SY_WIN_SCROLL_RIGHT;
            break;
    }
    Audio_PlaySoundGeneral(sfx, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);
}

void SohMenu::Close() {
    if (!IsVisible()) {
        return;
    }
    ToggleVisibility();
    // The same housekeeping the Gui does when a menu key closes the menu.
    auto window = Ship::Context::GetInstance()->GetWindow();
    auto gui = window->GetGui();
    if (window->IsFullscreen()) {
        window->SetMouseCapture(!(gui->GetMenuOrMenubarVisible() || window->ShouldForceCursorVisibility()));
    }
    if (!gui->GetMenuOrMenubarVisible()) {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    }
#if defined(__ANDROID__)
    Ship::Mobile::EnableTouchArea();
#endif
}

void SohMenu::Quit() {
    Close();
    Ship::Context::GetInstance()->GetWindow()->Close();
}

void SohMenu::Reset() {
    std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
        Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
        ->Dispatch("reset");
    // The game is frozen while the menu is open, so the reset only happens once it closes.
    Close();
}

void SohMenu::HidePopoutWindows() {
    // The "Popout ... Window" buttons are gone and those windows are drawn on their page. One that was
    // left open by an older config would float with no button to close it.
    for (auto& [pageName, entry] : menuEntries) {
        for (auto& [sectionName, sidebar] : entry.sidebars) {
            for (auto& column : sidebar.columnWidgets) {
                for (auto& widget : column) {
                    if (IsPopout(widget)) {
                        auto window = WindowOf(widget);
                        if (window != nullptr && window->IsVisible()) {
                            window->Hide();
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Widgets to rows
// ---------------------------------------------------------------------------------------------------------------------
bool SohMenu::PrepareWidget(WidgetInfo& widget, std::string& reason) {
    reason.clear();
    if (widget.options == nullptr) {
        return false;
    }
    if (widget.preFunc != nullptr) {
        widget.ResetDisables();
        widget.preFunc(widget);
        if (widget.isHidden) {
            return false;
        }
        for (auto option : widget.activeDisables) {
            widget.options->disabled = true;
            if (!reason.empty()) {
                reason += "; ";
            }
            reason += disabledMap.at(option).reason;
        }
    }
    if (widget.raceDisable && CVarGetInteger(CVAR_SETTING("DisableChanges"), 0)) {
        widget.options->disabled = true;
        if (!reason.empty()) {
            reason += "; ";
        }
        reason += "Race Lockout Active";
    }
    if (widget.options->disabled && reason.empty()) {
        const char* tip = widget.options->disabledTooltip;
        reason = tip != nullptr && tip[0] != '\0' ? CleanLabel(tip) : "Unavailable";
    }
    return true;
}

bool SohMenu::MakeRow(WidgetInfo& widget, Pm::Row& row) {
    using namespace UIWidgets;
    std::string reason;
    if (!PrepareWidget(widget, reason)) {
        return false;
    }
    WidgetInfo* w = &widget;
    row = Pm::Row();
    row.id = WidgetId(widget);
    row.label = CleanLabel(widget.name);
    row.desc = widget.options->tooltip != nullptr ? widget.options->tooltip : "";
    row.disabled = widget.options->disabled;
    row.reason = reason;
    auto changed = [w]() {
        if (w->callback != nullptr) {
            w->callback(*w);
        }
    };

    switch (widget.type) {
        case WIDGET_CHECKBOX: {
            bool* pointer = std::holds_alternative<bool*>(widget.valuePointer)
                                ? std::get<bool*>(widget.valuePointer)
                                : nullptr;
            if (pointer == nullptr) {
                return false;
            }
            row.kind = Pm::RowKind::Toggle;
            row.on = *pointer;
            row.activate = [pointer, changed]() {
                *pointer = !*pointer;
                changed();
            };
        } break;
        case WIDGET_CVAR_CHECKBOX: {
            auto options = std::static_pointer_cast<CheckboxOptions>(widget.options);
            row.kind = Pm::RowKind::Toggle;
            row.on = CVarGetInteger(widget.cVar, options->defaultValue);
            row.activate = [w, options, changed]() {
                CVarSetInteger(w->cVar, !CVarGetInteger(w->cVar, options->defaultValue));
                SaveCvars();
                ShipInit::Init(w->cVar);
                changed();
            };
        } break;
        case WIDGET_COMBOBOX: {
            int32_t* pointer = std::holds_alternative<int32_t*>(widget.valuePointer)
                                   ? std::get<int32_t*>(widget.valuePointer)
                                   : nullptr;
            if (pointer == nullptr) {
                return false;
            }
            auto options = std::static_pointer_cast<ComboboxOptions>(widget.options);
            row.kind = Pm::RowKind::Dropdown;
            row.options = OptionsFromMap(options->comboMap);
            row.current = *pointer;
            row.setOption = [pointer, changed](int32_t value) {
                *pointer = value;
                changed();
            };
        } break;
        case WIDGET_CVAR_COMBOBOX: {
            auto options = std::static_pointer_cast<ComboboxOptions>(widget.options);
            row.kind = Pm::RowKind::Dropdown;
            row.options = OptionsFromMap(options->comboMap);
            row.current = CVarGetInteger(widget.cVar, options->defaultIndex);
            row.setOption = [w, changed](int32_t value) {
                CVarSetInteger(w->cVar, value);
                SaveCvars();
                ShipInit::Init(w->cVar);
                changed();
            };
        } break;
        case WIDGET_AUDIO_BACKEND: {
            auto audio = Ship::Context::GetInstance()->GetAudio();
            row.kind = Pm::RowKind::Dropdown;
            row.desc = "Sets the audio API used by the game. Requires a relaunch to take effect.";
            for (auto backend : *audio->GetAvailableAudioBackends()) {
                if (audioBackendsMap.contains(backend)) {
                    row.options.push_back({ (int32_t)backend, audioBackendsMap.at(backend) });
                }
            }
            row.current = (int32_t)audio->GetCurrentAudioBackend();
            if (row.options.size() <= 1) {
                row.disabled = true;
                row.reason = "Only one audio API is available on this platform.";
            }
            row.setOption = [](int32_t value) {
                Ship::Context::GetInstance()->GetAudio()->SetCurrentAudioBackend((Ship::AudioBackend)value);
            };
        } break;
        case WIDGET_VIDEO_BACKEND: {
            row.kind = Pm::RowKind::Dropdown;
            row.desc = "Sets the renderer API used by the game.";
            for (const auto& [backend, label] : availableWindowBackendsMap) {
                row.options.push_back({ (int32_t)backend, label });
            }
            std::sort(row.options.begin(), row.options.end(),
                      [](const Pm::Option& a, const Pm::Option& b) { return a.value < b.value; });
            row.current = (int32_t)configWindowBackend;
            if (row.options.size() <= 1) {
                row.disabled = true;
                row.reason = "Only one renderer API is available on this platform.";
            }
            row.setOption = [this](int32_t value) {
                configWindowBackend = (Ship::WindowBackend)value;
                auto config = Ship::Context::GetInstance()->GetConfig();
                config->SetInt("Window.Backend.Id", value);
                config->SetString("Window.Backend.Name", windowBackendsMap.at(configWindowBackend));
                config->Save();
                UpdateWindowBackendObjects();
            };
        } break;
        case WIDGET_SLIDER_INT:
        case WIDGET_CVAR_SLIDER_INT: {
            auto options = std::static_pointer_cast<IntSliderOptions>(widget.options);
            int32_t* pointer = nullptr;
            if (widget.type == WIDGET_SLIDER_INT) {
                pointer = std::holds_alternative<int32_t*>(widget.valuePointer)
                              ? std::get<int32_t*>(widget.valuePointer)
                              : nullptr;
                if (pointer == nullptr) {
                    return false;
                }
            }
            std::string format;
            SplitSliderName(widget.name, row.label, format);
            if (format.empty() && options->format != nullptr) {
                format = options->format;
            }
            int32_t value = pointer != nullptr ? *pointer : CVarGetInteger(widget.cVar, options->defaultValue);
            row.kind = Pm::RowKind::Slider;
            row.value = value;
            row.min = options->min;
            row.max = options->max;
            row.step = std::max(1, (int)options->step);
            row.valueText = FormatInt(format, value);
            row.setValue = [w, pointer, changed](double v) {
                int32_t iv = (int32_t)std::lround(v);
                if (pointer != nullptr) {
                    *pointer = iv;
                } else {
                    CVarSetInteger(w->cVar, iv);
                    SaveCvars();
                    ShipInit::Init(w->cVar);
                }
                changed();
            };
        } break;
        case WIDGET_SLIDER_FLOAT:
        case WIDGET_CVAR_SLIDER_FLOAT: {
            auto options = std::static_pointer_cast<FloatSliderOptions>(widget.options);
            float* pointer = nullptr;
            if (widget.type == WIDGET_SLIDER_FLOAT) {
                pointer =
                    std::holds_alternative<float*>(widget.valuePointer) ? std::get<float*>(widget.valuePointer) : nullptr;
                if (pointer == nullptr) {
                    return false;
                }
            }
            std::string format;
            SplitSliderName(widget.name, row.label, format);
            if (format.empty() && options->format != nullptr) {
                format = options->format;
            }
            // Percentages are stored 0..1 and shown 0..100.
            float scale = options->isPercentage ? 100.0f : 1.0f;
            float value = pointer != nullptr ? *pointer : CVarGetFloat(widget.cVar, options->defaultValue);
            row.kind = Pm::RowKind::Slider;
            row.value = value * scale;
            row.min = options->min * scale;
            row.max = options->max * scale;
            row.step = options->step * scale;
            row.valueText = FormatFloat(format, value * scale, options->step * scale);
            row.setValue = [w, pointer, scale, changed](double v) {
                float fv = (float)(v / scale);
                if (pointer != nullptr) {
                    *pointer = fv;
                } else {
                    CVarSetFloat(w->cVar, fv);
                    SaveCvars();
                    ShipInit::Init(w->cVar);
                }
                changed();
            };
        } break;
        case WIDGET_BUTTON: {
            row.kind = Pm::RowKind::Button;
            row.bulkAll = row.label == "All";
            row.bulkNone = row.label == "None";
            // "Enable" buttons that swap their name to "Disable" while on show as latched.
            row.latched = row.label == "Disable";
            row.activate = changed;
            if (widget.heldFunc != nullptr) {
                row.whileHeld = [w]() { w->heldFunc(*w); };
            }
        } break;
        case WIDGET_WINDOW_BUTTON: {
            if (IsPopout(widget)) {
                return false; // its window is drawn on the page instead
            }
            auto window = WindowOf(widget);
            if (window == nullptr) {
                return false;
            }
            // What is left are in-game overlays (trackers, input viewer, timers): plain on/off toggles.
            std::string name = row.label;
            if (name.rfind("Toggle ", 0) == 0) {
                name = name.substr(7);
            }
            if (name.size() > 7 && name.compare(name.size() - 7, 7, " Window") == 0) {
                name = name.substr(0, name.size() - 7);
            }
            row.label = "Show " + name;
            row.kind = Pm::RowKind::Toggle;
            row.on = window->IsVisible();
            row.activate = [window]() { window->ToggleVisibility(); };
        } break;
        case WIDGET_TEXT: {
            if (widget.sameLine) {
                return false; // inline remarks next to another label have no place in a row list
            }
            row.searchable = false;
            if (row.label.rfind("WARNING", 0) == 0) {
                // A "WARNING" line announces the text that follows it: drawn together as one warning box.
                mPendingWarning = true;
                return false;
            }
            if (mPendingWarning) {
                mPendingWarning = false;
                row.kind = Pm::RowKind::Warn;
            } else if (!row.label.empty() && widget.name.back() == ':') {
                row.kind = Pm::RowKind::Sub;
                row.label.pop_back();
            } else if (row.label.find('\n') != std::string::npos || row.label.size() > 70) {
                row.kind = Pm::RowKind::Para;
            } else {
                row.kind = Pm::RowKind::Text;
            }
        } break;
        case WIDGET_CVAR_HOST_PORT:
            row.kind = Pm::RowKind::HostPort;
            row.searchable = false;
            row.hostCvar = widget.cVar;
            row.portCvar = widget.cVar2;
            row.hostDefault = widget.defaultHost;
            row.portDefault = widget.defaultPort;
            break;
        case WIDGET_CUSTOM:
            if (widget.customFunction == nullptr) {
                return false;
            }
            row.kind = Pm::RowKind::ImGui;
            row.searchable = false;
            row.drawImGui = [w]() { w->customFunction(*w); };
            break;
        default:
            return false;
    }
    return true;
}

Pm::Section SohMenu::Build(const std::string& page, const std::string& sectionName) {
    Pm::Section out;
    out.title = sectionName;
    auto& sidebar = menuEntries.at(page).sidebars.at(sectionName);

    auto& updateFuncs = MenuInit::GetUpdateFuncs();
    if (updateFuncs.contains(page) && updateFuncs[page].contains(sectionName)) {
        for (auto& updateFunc : updateFuncs[page][sectionName]) {
            updateFunc();
        }
    }

    for (auto& column : sidebar.columnWidgets) {
        int group = -1;
        mPendingWarning = false;
        for (auto& widget : column) {
            if (widget.type == WIDGET_SEPARATOR_TEXT) {
                out.groups.push_back({ CleanLabel(widget.name) });
                group = (int)out.groups.size() - 1;
                continue;
            }
            if (IsPopout(widget)) {
                // The window this button used to open is drawn on the page.
                if (!Pm::BuildInlinePage(page, sectionName, out.groups)) {
                    out.embedded = WindowOf(widget);
                }
                continue;
            }
            Pm::Row row;
            if (!MakeRow(widget, row)) {
                continue;
            }
            if (group < 0) {
                out.groups.push_back({ sectionName });
                group = (int)out.groups.size() - 1;
            }
            out.groups[group].rows.push_back(std::move(row));
        }
    }
    return out;
}

int SohMenu::CountOn(const std::string& page, const std::string& sectionName) {
    int on = 0;
    auto& sidebar = menuEntries.at(page).sidebars.at(sectionName);
    for (auto& column : sidebar.columnWidgets) {
        for (auto& widget : column) {
            if (widget.type != WIDGET_CHECKBOX && widget.type != WIDGET_CVAR_CHECKBOX &&
                !(widget.type == WIDGET_WINDOW_BUTTON && !IsPopout(widget))) {
                continue;
            }
            Pm::Row row;
            if (MakeRow(widget, row) && row.kind == Pm::RowKind::Toggle && row.on) {
                on++;
            }
        }
    }
    on += Pm::CountInlinePage(page, sectionName);
    return on;
}

int SohMenu::Search(const std::string& query, std::vector<Pm::Group>& out, int limit) {
    std::string needle = Lower(query);
    size_t a = needle.find_first_not_of(' ');
    if (a == std::string::npos) {
        return 0;
    }
    needle = needle.substr(a, needle.find_last_not_of(' ') - a + 1);
    int count = 0;
    for (auto& page : menuOrder) {
        auto& entry = menuEntries.at(page);
        for (auto& sectionName : entry.sidebarOrder) {
            Pm::Group hits;
            hits.title = page + " \xe2\x80\xba " + sectionName;
            hits.showCount = false;
            for (auto& column : entry.sidebars.at(sectionName).columnWidgets) {
                for (auto& widget : column) {
                    if (count >= limit) {
                        break;
                    }
                    // Only settings you can change match: toggles, sliders, dropdowns, buttons.
                    if (widget.type == WIDGET_SEPARATOR || widget.type == WIDGET_SEPARATOR_TEXT ||
                        widget.type == WIDGET_TEXT || widget.type == WIDGET_CUSTOM ||
                        widget.type == WIDGET_CVAR_HOST_PORT || widget.type == WIDGET_SEARCH || IsPopout(widget)) {
                        continue;
                    }
                    std::string label, format;
                    SplitSliderName(widget.name, label, format);
                    if (Lower(label).find(needle) == std::string::npos &&
                        !(widget.type == WIDGET_WINDOW_BUTTON && std::string("show").find(needle) == 0)) {
                        continue;
                    }
                    Pm::Row row;
                    if (!MakeRow(widget, row) || !row.searchable) {
                        continue;
                    }
                    if (Lower(row.label).find(needle) == std::string::npos) {
                        continue;
                    }
                    // In results the group header is gone, so "All" / "None" stay ordinary buttons.
                    row.bulkAll = row.bulkNone = false;
                    hits.rows.push_back(std::move(row));
                    count++;
                }
            }
            if (!hits.rows.empty()) {
                out.push_back(std::move(hits));
            }
        }
    }
    return count;
}

} // namespace SohGui
