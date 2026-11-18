#ifndef PAUSE_MENU_IMPL_H
#define PAUSE_MENU_IMPL_H

// Private state of the pause-style menu, shared by PauseMenu.cpp (shell, input, modals, page turn) and
// PmRows.cpp (rows).

#include "PauseMenu.h"
#include <unordered_map>

namespace Pm {

// What is being drawn: the live page, or a copy headed for a page-turn texture (no input, no ImGui items).
enum class Pass { Live, Captured };

struct View {
    int page = 0;
    std::string section;
    std::string search;
    float scroll = 0;
    float sidebarScroll = 0;
    ImGuiID focus = 0;
    bool descExpanded = false;
};

enum class Zone { Sidebar, Content, Footer, Embedded };

struct NavItem {
    ImGuiID id = 0;
    int column = 0;
    Rect rect; // in content coordinates (before scrolling)
};

enum TapFlags { TapNone = 0, TapCaptor = 1 };

struct PauseMenu::Impl : public RowUi {
    Source* src = nullptr;
    View view;
    bool open = false;
    bool inputCaptured = false;
    bool captureRequested = false;
    double openTime = 0;
    double now = 0;
    float dt = 0;

    // ---- pointer ----
    ImVec2 mouse = { -1000, -1000 };
    bool mouseDown = false, mousePressed = false, mouseReleased = false;
    float wheel = 0;
    bool inputEnabled = true;
    bool pressing = false;
    ImVec2 pressPos = { 0, 0 };
    ImGuiID activeId = 0, activeRow = 0;
    int activeFlags = 0;
    ImGuiID candidateId = 0, candidateRow = 0;
    int candidateFlags = 0;
    ImGuiID hotId = 0, hotNext = 0;
    bool repeatFired = false;
    double nextRepeat = 0;
    bool dragScrolling = false;
    ImGuiID scrollOwner = 0;
    float lastMouseY = 0;
    float scrollVelocity = 0;
    ImGuiID inertiaOwner = 0;
    std::vector<Rect> hitClip;

    // ---- pad / keyboard ----
    bool usingPad = false;
    Zone zone = Zone::Sidebar;
    Zone zoneBeforeFooter = Zone::Sidebar;
    int footerIndex = 2;
    bool padUp = false, padDown = false, padLeft = false, padRight = false, padA = false, padB = false;
    bool padPrev = false, padNext = false;
    std::vector<NavItem> navItems, navItemsPrev;
    bool sliderAdjust = false;
    bool subMode = false;
    int subIndex = 0;
    int subCount = 0, subCountPrev = 0;
    float contentViewH = 0;
    bool focusNeedsReveal = false;
    bool sidebarNeedsReveal = false;

    // ---- per-pass drawing context ----
    Pass pass = Pass::Live;
    const PageColors* pc = nullptr;
    ImGuiID curRow = 0;
    bool curRowDisabled = false;
    bool curRowFocused = false;
    Rect contentClip;
    std::string plateTitle, plateText;
    bool plateOverride = false;
    bool sectionHasEmbedded = false;

    // ---- text fields ----
    char searchBuf[128] = "";

    // ---- cached layout ----
    std::unordered_map<ImGuiID, float> imguiRowHeights;
    float contentHeight = 0;
    float sidebarHeight = 0;
    std::unordered_map<std::string, int> counts;
    double countsTime = -1000;

    // ---- modals ----
    struct Picker {
        bool open = false;
        ImGuiID rowId = 0;
        std::string title;
        std::vector<Option> options;
        int32_t current = 0;
        std::function<void(int32_t)> set;
        float scroll = 0;
        int highlight = 0;
        bool reveal = false;
        const PageColors* colors = nullptr;
    } picker;
    struct Dialog {
        bool open = false;
        bool quit = false;
        std::string title, message;
        std::function<void()> onYes;
        int highlight = 0; // 0 = No, 1 = Yes
    } dialog;

    // ---- page turn ----
    struct Turn {
        bool active = false;
        double start = 0;
        int dir = 1;
        View out;
        bool captureOut = false;
    } turn;

    // ================================================================================================
    // PauseMenu.cpp
    // ================================================================================================
    void ReadInput();
    void EndInput();
    void HandleKeys(const Section& section);
    void MoveContentFocus(int dx, int dy);
    void DrawBackdrop();
    void DrawPanel(View& v, Pass p);
    void DrawHeader(const View& v, const Rect& panel);
    void DrawSidebar(View& v, const Rect& body);
    void DrawContent(View& v, const Rect& body, Section& section, bool searching, int matches);
    void DrawFooter(View& v, const Rect& footer, const Section& section, const Row* focusRow, bool searching);
    float FooterHeight(const View& v, const Row* focusRow);
    void DrawEmbedded(const Rect& area, Section& section);
    void DrawModals();
    void DrawPicker();
    void DrawDialog();
    void DrawTurn();
    void StartTurn(int dir, int targetPage);
    void SelectSection(const std::string& section);
    void ClearTransient();
    void PrepareGroups(std::vector<Group>& groups);
    int SectionCount(const std::string& page, const std::string& section);

    bool TapId(ImGuiID id, const Rect& r, bool* hovered = nullptr, bool* held = nullptr, int flags = TapNone);
    bool TapRepeat(ImGuiID id, const Rect& r, bool* hovered = nullptr, bool* held = nullptr);
    bool DragId(ImGuiID id, const Rect& r);
    void ScrollArea(ImGuiID id, const Rect& viewport, float& scroll, float contentH);
    void PushHitClip(const Rect& r);
    void PopHitClip();
    bool Hoverable(const Rect& r) const;

    // ================================================================================================
    // PmRows.cpp
    // ================================================================================================
    float MeasureRow(const Row& row, float w);
    float MeasureGroup(const Group& group, float w);
    void DrawGroup(Group& group, const Rect& r, int column, float contentTop);
    void DrawRow(Row& row, const Rect& r);
    void DrawToggle(Row& row, const Rect& r);
    void DrawSlider(Row& row, const Rect& r);
    void DrawChoice(Row& row, const Rect& r);
    void DrawButton(Row& row, const Rect& r);
    void DrawHostPort(Row& row, const Rect& r);
    void DrawImGuiRow(Row& row, const Rect& r);
    float LabelBlockHeight(const Row& row, float w) const;
    float DrawLabelBlock(const Row& row, float x, float y, float w, float rowH, bool centre);
    bool UseSegments(const Row& row) const;
    float SegmentsWidth(const Row& row) const;
    bool TextField(const char* id, const Rect& r, char* buf, size_t bufSize, const char* hint, const TextStyle& st,
                   bool decimalOnly, bool framed);

    // ---- RowUi ----
    bool Tap(int sub, const Rect& r, bool* hovered = nullptr, bool* held = nullptr) override;
    bool PadTarget(int sub, const Rect& r) override;
    const PageColors& Colors() override;
    bool Disabled() override;
    void SetPlate(const std::string& title, const std::string& text) override;
    void CaptureInput() override;
    void Confirm(const std::string& title, const std::string& message, std::function<void()> onYes) override;
};

} // namespace Pm

#endif // PAUSE_MENU_IMPL_H
