#ifndef PAUSE_MENU_MODEL_H
#define PAUSE_MENU_MODEL_H

// What a page of the pause-style menu shows: sections made of groups made of rows.
//
// Rows are rebuilt every frame, either from the menu's WidgetInfo data (the same data the old ImGui menu
// drew) or by the hand-built pages (controller bindings, input viewer). A row carries its current value
// and closures that change it, so the renderer knows nothing about cvars.

#include "PmDraw.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct WidgetInfo;

namespace Ship {
class GuiWindow;
}

namespace Pm {

enum class RowKind {
    Toggle,
    Slider,
    Segmented,
    Dropdown,
    Button,
    Text,    // one line of secondary text
    Sub,     // sub-heading inside a group
    Para,    // paragraph, line breaks kept
    Warn,    // warning box
    HostPort, // label + host and port fields
    Drawn,   // row that measures and draws itself (binding rows, stick test)
    ImGui,   // legacy widget drawn by its own ImGui code
};

// The menu's sounds, played with the game's own pause-menu sound effects.
enum class Sound { Open, Close, Cursor, Decide, Cancel, Error, PageLeft, PageRight };

struct Option {
    int32_t value = 0;
    std::string label;
};

class RowUi;

struct Row {
    RowKind kind = RowKind::Text;
    // Stable identity: survives rebuilds, used for focus, hit-testing and search.
    ImGuiID id = 0;
    std::string label;
    std::string desc;
    bool disabled = false;
    // Why it is disabled; shown under the label, or once under the group header when shared.
    std::string reason;
    bool showReason = true;
    // Rows that can be selected with the cursor. Static rows only are when they have a description.
    bool Selectable() const {
        return Interactive() || !desc.empty();
    }
    bool Interactive() const {
        return kind == RowKind::Toggle || kind == RowKind::Slider || kind == RowKind::Segmented ||
               kind == RowKind::Dropdown || kind == RowKind::Button || kind == RowKind::Drawn;
    }
    // Shows up in search results.
    bool searchable = true;

    // Toggle
    bool on = false;
    // Toggle tap / button press.
    std::function<void()> activate;

    // Button
    bool latched = false;
    // Called every frame the button is held down.
    std::function<void()> whileHeld;
    // "All"/"None" buttons are lifted into the group header.
    bool bulkAll = false, bulkNone = false;

    // Slider
    double value = 0, min = 0, max = 1, step = 1;
    std::string valueText;
    std::function<void(double)> setValue;

    // Segmented / dropdown
    std::vector<Option> options;
    int32_t current = 0;
    std::function<void(int32_t)> setOption;
    bool forceSegmented = false;

    // Host & port
    std::string hostCvar, portCvar, hostDefault;
    int32_t portDefault = 0;

    // Drawn
    std::function<float(float width)> measure;
    std::function<void(RowUi& ui, const Rect& rect)> draw;

    // ImGui
    std::function<void()> drawImGui;
};

struct Group {
    std::string title;
    std::vector<Row> rows;
    // Filled in by the builder / renderer.
    std::function<void()> bulkAll, bulkNone;
    std::string note;
    int togglesOn = 0, togglesTotal = 0;
    // Search result groups mix settings from one section and show no on/total count.
    bool showCount = true;
};

struct Section {
    std::string title;
    std::vector<Group> groups;
    int togglesOn = 0, togglesTotal = 0;
    // Former pop-out window whose content is drawn on the page.
    std::shared_ptr<Ship::GuiWindow> embedded;
};

// Interaction helpers handed to rows that draw themselves.
class RowUi {
  public:
    virtual ~RowUi() = default;
    // A tappable area. Returns true on the frame it is tapped. `sub` distinguishes areas inside one row.
    virtual bool Tap(int sub, const Rect& r, bool* hovered = nullptr, bool* held = nullptr) = 0;
    // True while the cursor is on this row and the pad's A button was just pressed on sub-target `sub`.
    // Rows with several targets register them in order; left/right move between them.
    virtual bool PadTarget(int sub, const Rect& r) = 0;
    virtual const PageColors& Colors() = 0;
    virtual bool Disabled() = 0;
    // Replaces the footer plate text while something is waiting for input.
    virtual void SetPlate(const std::string& title, const std::string& text) = 0;
    virtual void PlaySound(Sound sound) = 0;
    // Call every frame while the row waits for a raw input (a binding being recorded): the menu ignores
    // its own keys until the row stops asking.
    virtual void CaptureInput() = 0;
    // Asks for a yes/no confirmation styled like the quit dialog.
    virtual void Confirm(const std::string& title, const std::string& message, std::function<void()> onYes) = 0;
};

// FNV-1a; never returns 0, which means "no row".
inline ImGuiID RowId(std::string_view key) {
    uint32_t h = 2166136261u;
    for (char c : key) {
        h ^= (uint8_t)c;
        h *= 16777619u;
    }
    return h != 0 ? h : 1;
}

} // namespace Pm

#endif // PAUSE_MENU_MODEL_H
