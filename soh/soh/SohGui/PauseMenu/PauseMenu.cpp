#include "PauseMenuImpl.h"
#include "PmFx.h"

#include <imgui_internal.h>
#include <libultraship/libultraship.h>
#include <algorithm>
#include <cmath>

// Shell of the pause-style menu: backdrop, panel, header, sidebar, content layout, footer, the two
// modals, the cube page turn, and all pointer / pad input. Rows are in PmRows.cpp.
//
// Layout numbers are design pixels on the 1024x576 canvas, taken from the design handoff.

namespace Pm {

namespace {
const Rect kPanel = { 8, 8, 1008, 560 };
constexpr float kHeaderH = 54.0f;
constexpr float kFooterMinH = 60.0f;
constexpr float kSidebarW = 198.0f;
constexpr float kTurnSeconds = 0.52f;
constexpr float kDescExpandChars = 200;
const char* kMainWindow = "Main Menu";
const char* kModalWindow = "Main Menu Modal";

// CSS cubic-bezier(x1, y1, x2, y2) evaluated at time x.
float CubicBezier(float x1, float y1, float x2, float y2, float x) {
    auto bez = [](float a, float b, float t) {
        float u = 1.0f - t;
        return 3.0f * u * u * t * a + 3.0f * u * t * t * b + t * t * t;
    };
    float t = x;
    for (int i = 0; i < 8; i++) {
        float err = bez(x1, x2, t) - x;
        float u = 1.0f - t;
        float d = 3.0f * u * u * x1 + 6.0f * u * t * (x2 - x1) + 3.0f * t * t * (1.0f - x2);
        if (std::fabs(d) < 1e-5f) {
            break;
        }
        t = std::clamp(t - err / d, 0.0f, 1.0f);
    }
    return bez(y1, y2, t);
}

std::string OneLine(const std::string& text) {
    std::string out;
    bool space = false;
    for (char c : text) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            space = !out.empty();
        } else {
            if (space) {
                out.push_back(' ');
                space = false;
            }
            out.push_back(c);
        }
    }
    return out;
}

const Row* FindRow(const std::vector<Group>& groups, ImGuiID id) {
    if (id == 0) {
        return nullptr;
    }
    for (const auto& g : groups) {
        for (const auto& r : g.rows) {
            if (r.id == id) {
                return &r;
            }
        }
    }
    return nullptr;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------------------------------
PauseMenu::PauseMenu(Source* source) : mImpl(std::make_unique<Impl>()) {
    mImpl->src = source;
}

PauseMenu::~PauseMenu() = default;

bool PauseMenu::IsOpen() const {
    return mImpl->open;
}

void PauseMenu::Update(bool visible) {
    Impl& m = *mImpl;
    if (!visible && m.open) {
        m.open = false;
        m.picker = Impl::Picker();
        m.dialog = Impl::Dialog();
        m.turn = Impl::Turn();
        m.ClearTransient();
        m.inputCaptured = false;
        m.captureRequested = false;
    }
}

void PauseMenu::Impl::ClearTransient() {
    sliderAdjust = false;
    subMode = false;
    subIndex = 0;
    view.descExpanded = false;
    activeId = 0;
    activeRow = 0;
    dragScrolling = false;
    scrollVelocity = 0;
}

void PauseMenu::Draw() {
    Impl& m = *mImpl;
    const auto& pages = m.src->Pages();
    if (pages.empty()) {
        return;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    if (!m.open) {
        m.open = true;
        m.openTime = ImGui::GetTime();
        m.view = View();
        std::string page = m.src->CurrentPage();
        for (size_t i = 0; i < pages.size(); i++) {
            if (pages[i] == page) {
                m.view.page = (int)i;
            }
        }
        m.view.section = m.src->CurrentSection(pages[m.view.page]);
        m.searchBuf[0] = '\0';
        m.zone = Zone::Sidebar;
        m.countsTime = -1000;
        ImGui::SetNextWindowFocus();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking;
    bool visible = ImGui::Begin(kMainWindow, nullptr, flags);
    ImGui::PopStyleVar(3);
    if (!visible) {
        ImGui::End();
        return;
    }
    BeginCanvas(ImGui::GetWindowDrawList(), vp->Pos, vp->Size);
    m.ReadInput();
    m.DrawBackdrop();

    bool modal = m.picker.open || m.dialog.open;
    // Before drawing, so a page turn asked for this frame (even mid-turn) starts this frame.
    m.HandleKeys(Section());
    if (m.turn.active && Fx::Available()) {
        m.inputEnabled = false;
        ImDrawList* dl = DL();
        if (m.turn.captureOut) {
            Fx::BeginPageCapture(dl, 0);
            m.DrawPanel(m.turn.out, Pass::Captured);
            m.turn.captureOut = false;
        }
        Fx::BeginPageCapture(dl, 1);
        m.DrawPanel(m.view, Pass::Captured);
        Fx::EndPageCapture(dl);
        m.DrawTurn();
        Fx::EndPageCompose(dl);
    } else {
        m.turn.active = false;
        m.inputEnabled = !modal && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        m.DrawPanel(m.view, Pass::Live);
    }
    ImGui::End();

    m.DrawModals();
    m.EndInput();
}

// ---------------------------------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------------------------------
void PauseMenu::Impl::ReadInput() {
    ImGuiIO& io = ImGui::GetIO();
    now = ImGui::GetTime();
    dt = io.DeltaTime;
    if (ImGui::IsMousePosValid(&io.MousePos)) {
        mouse = ToDesign(io.MousePos);
    }
    mouseDown = io.MouseDown[0];
    mousePressed = ImGui::IsMouseClicked(0);
    mouseReleased = ImGui::IsMouseReleased(0);
    wheel = io.MouseWheel;
    if (mousePressed) {
        pressing = true;
        pressPos = mouse;
        dragScrolling = false;
        repeatFired = false;
        inertiaOwner = 0;
        scrollVelocity = 0;
        usingPad = false;
        sliderAdjust = false;
        subMode = false;
    }
    candidateId = 0;
    candidateRow = 0;
    candidateFlags = 0;
    hotNext = 0;
    navItems.clear();
    subCount = 0;
    plateOverride = false;
    hitClip.clear();

    bool typing = io.WantTextInput;
    auto key = [](ImGuiKey k, bool repeat) { return ImGui::IsKeyPressed(k, repeat); };
    padUp = key(ImGuiKey_GamepadDpadUp, true) || key(ImGuiKey_GamepadLStickUp, true) ||
            (!typing && key(ImGuiKey_UpArrow, true));
    padDown = key(ImGuiKey_GamepadDpadDown, true) || key(ImGuiKey_GamepadLStickDown, true) ||
              (!typing && key(ImGuiKey_DownArrow, true));
    padLeft = key(ImGuiKey_GamepadDpadLeft, true) || key(ImGuiKey_GamepadLStickLeft, true) ||
              (!typing && key(ImGuiKey_LeftArrow, true));
    padRight = key(ImGuiKey_GamepadDpadRight, true) || key(ImGuiKey_GamepadLStickRight, true) ||
               (!typing && key(ImGuiKey_RightArrow, true));
    padA = key(ImGuiKey_GamepadFaceDown, false) || (!typing && key(ImGuiKey_Enter, false));
    padB = key(ImGuiKey_GamepadFaceRight, false) || (!typing && key(ImGuiKey_Backspace, false));
    padPrev = key(ImGuiKey_GamepadL1, false) || (!typing && key(ImGuiKey_PageUp, false));
    padNext = key(ImGuiKey_GamepadR1, false) || (!typing && key(ImGuiKey_PageDown, false));
    if (inputCaptured) {
        padUp = padDown = padLeft = padRight = padA = padB = padPrev = padNext = false;
    }
    if (padUp || padDown || padLeft || padRight || padA || padB || padPrev || padNext) {
        usingPad = true;
    }
}

void PauseMenu::Impl::EndInput() {
    if (mousePressed) {
        activeId = candidateId;
        activeRow = candidateRow;
        activeFlags = candidateFlags;
    }
    if (mouseReleased) {
        if (!dragScrolling && activeRow != 0) {
            if (view.focus != activeRow) {
                view.focus = activeRow;
                view.descExpanded = false;
            }
            zone = Zone::Content;
        }
        if (dragScrolling) {
            inertiaOwner = scrollOwner;
        }
        activeId = 0;
        activeRow = 0;
        activeFlags = 0;
        pressing = false;
        dragScrolling = false;
        repeatFired = false;
    } else if (!mouseDown) {
        pressing = false;
        activeId = 0;
        activeRow = 0;
        dragScrolling = false;
    }
    hotId = hotNext;
    navItemsPrev = navItems;
    // Holds for the next frame only; a row that is no longer drawn stops asking.
    inputCaptured = captureRequested;
    captureRequested = false;
}

void PauseMenu::Impl::PushHitClip(const Rect& r) {
    hitClip.push_back(hitClip.empty() ? r : hitClip.back().Intersect(r));
}

void PauseMenu::Impl::PopHitClip() {
    if (!hitClip.empty()) {
        hitClip.pop_back();
    }
}

bool PauseMenu::Impl::Hoverable(const Rect& r) const {
    if (!hitClip.empty() && !hitClip.back().Contains(mouse.x, mouse.y)) {
        return false;
    }
    return r.Contains(mouse.x, mouse.y);
}

bool PauseMenu::Impl::TapId(ImGuiID id, const Rect& r, bool* hovered, bool* held, int flags) {
    bool inside = inputEnabled && pass == Pass::Live && Hoverable(r);
    if (inside) {
        hotNext = id;
        if (mousePressed) {
            candidateId = id;
            candidateRow = curRow;
            candidateFlags = flags;
        }
    }
    bool isActive = inputEnabled && id != 0 && activeId == id;
    if (hovered != nullptr) {
        *hovered = inside && hotId == id && !dragScrolling && (!mouseDown || isActive);
    }
    if (held != nullptr) {
        *held = isActive && mouseDown && inside && !dragScrolling;
    }
    return isActive && mouseReleased && inside && !dragScrolling;
}

bool PauseMenu::Impl::TapRepeat(ImGuiID id, const Rect& r, bool* hovered, bool* held) {
    bool isHeld = false;
    TapId(id, r, hovered, &isHeld);
    if (held != nullptr) {
        *held = isHeld;
    }
    if (!isHeld) {
        return false;
    }
    if (!repeatFired) {
        repeatFired = true;
        nextRepeat = now + 0.4;
        return true;
    }
    if (now >= nextRepeat) {
        nextRepeat = now + 0.05;
        return true;
    }
    return false;
}

bool PauseMenu::Impl::DragId(ImGuiID id, const Rect& r) {
    TapId(id, r, nullptr, nullptr, TapCaptor);
    return inputEnabled && activeId == id && mouseDown;
}

// True while a real ImGui item (a text field, a widget of an embedded window) is being held. Pressing
// anywhere on a window makes ImGui mark the window itself as active, which does not count.
static bool ImGuiItemHeld() {
    ImGuiContext& g = *GImGui;
    if (g.ActiveId == 0) {
        return false;
    }
    for (ImGuiWindow* window : g.Windows) {
        if (window->MoveId == g.ActiveId) {
            return false;
        }
    }
    return true;
}

void PauseMenu::Impl::ScrollArea(ImGuiID id, const Rect& viewport, float& scroll, float contentH) {
    float maxScroll = std::max(0.0f, contentH - viewport.h);
    if (pass == Pass::Live && inputEnabled) {
        if (viewport.Contains(mouse.x, mouse.y) && wheel != 0.0f) {
            scroll -= wheel * 60.0f;
            inertiaOwner = 0;
        }
        if (pressing && mouseDown && !dragScrolling && maxScroll > 0 && viewport.Contains(pressPos.x, pressPos.y) &&
            !(activeFlags & TapCaptor) && !ImGuiItemHeld() && std::fabs(mouse.y - pressPos.y) > 6.0f) {
            dragScrolling = true;
            scrollOwner = id;
            lastMouseY = mouse.y;
        }
        if (dragScrolling && scrollOwner == id) {
            float d = mouse.y - lastMouseY;
            scroll -= d;
            if (dt > 0) {
                scrollVelocity = scrollVelocity * 0.5f + (-d / dt) * 0.5f;
            }
            lastMouseY = mouse.y;
        } else if (inertiaOwner == id && std::fabs(scrollVelocity) > 4.0f) {
            scroll += scrollVelocity * dt;
            scrollVelocity *= std::exp(-dt * 5.0f);
        }
    }
    scroll = std::clamp(scroll, 0.0f, maxScroll);
}

static void DrawScrollThumb(const Rect& viewport, float scroll, float contentH) {
    float maxScroll = contentH - viewport.h;
    if (maxScroll <= 0.5f) {
        return;
    }
    float thumbH = std::max(24.0f, viewport.h * viewport.h / contentH);
    float y = viewport.y + (viewport.h - thumbH) * (scroll / maxScroll);
    Fill({ viewport.r() - 5.0f, y, 3.0f, thumbH }, C(Tok::ScrollThumb));
}

void PauseMenu::Impl::SelectSection(const std::string& section) {
    const auto& pages = src->Pages();
    src->SetCurrentSection(pages[view.page], section);
    view.section = section;
    view.scroll = 0;
    view.focus = 0;
    view.search.clear();
    searchBuf[0] = '\0';
    ClearTransient();
}

void PauseMenu::Impl::StartTurn(int dir, int targetPage) {
    const auto& pages = src->Pages();
    if (picker.open || dialog.open || pages.size() < 2 || targetPage == view.page) {
        return;
    }
    View out = view;
    view = View();
    view.page = targetPage;
    view.section = src->CurrentSection(pages[targetPage]);
    searchBuf[0] = '\0';
    src->SetCurrentPage(pages[targetPage]);
    ClearTransient();
    zone = Zone::Sidebar;
    navItemsPrev.clear();
    if (Fx::Available()) {
        turn.active = true;
        turn.start = now;
        turn.dir = dir;
        turn.out = out;
        turn.captureOut = true;
    }
}

void PauseMenu::Impl::MoveContentFocus(int dx, int dy) {
    if (navItemsPrev.empty()) {
        return;
    }
    const NavItem* cur = nullptr;
    for (const auto& item : navItemsPrev) {
        if (item.id == view.focus) {
            cur = &item;
        }
    }
    ImGuiID next = view.focus;
    if (cur == nullptr) {
        next = navItemsPrev.front().id;
    } else if (dy != 0) {
        // Items of a column are stored top to bottom.
        const NavItem* found = nullptr;
        bool passed = false;
        for (const auto& item : navItemsPrev) {
            if (item.column != cur->column) {
                continue;
            }
            if (&item == cur) {
                passed = true;
                if (dy < 0) {
                    break;
                }
                continue;
            }
            if (dy < 0) {
                found = &item; // last one before the current
            } else if (passed) {
                found = &item;
                break;
            }
        }
        if (found != nullptr) {
            next = found->id;
        } else if (dy > 0) {
            zoneBeforeFooter = Zone::Content;
            zone = Zone::Footer;
            return;
        }
    } else if (dx != 0) {
        int target = cur->column + dx;
        const NavItem* best = nullptr;
        float bestDist = 1e9f;
        for (const auto& item : navItemsPrev) {
            if (item.column != target) {
                continue;
            }
            float dist = std::fabs(item.rect.cy() - cur->rect.cy());
            if (dist < bestDist) {
                bestDist = dist;
                best = &item;
            }
        }
        if (best != nullptr) {
            next = best->id;
        } else if (dx < 0) {
            zone = Zone::Sidebar;
            return;
        }
    }
    if (next != view.focus) {
        view.focus = next;
        view.descExpanded = false;
        sliderAdjust = false;
        subMode = false;
        focusNeedsReveal = true;
    }
}

void PauseMenu::Impl::HandleKeys(const Section&) {
    const auto& pages = src->Pages();
    int n = (int)pages.size();
    if (picker.open || dialog.open) {
        return; // the modals read the pad themselves
    }
    if (padPrev) {
        StartTurn(-1, (view.page - 1 + n) % n);
        return;
    }
    if (padNext) {
        StartTurn(1, (view.page + 1) % n);
        return;
    }
    if (zone == Zone::Embedded) {
        // ImGui's own navigation drives the embedded window; B comes back out.
        if (padB && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            zone = Zone::Sidebar;
            ImGui::SetWindowFocus(kMainWindow);
        }
        if (!sectionHasEmbedded) {
            zone = Zone::Sidebar;
        }
        padA = padB = padUp = padDown = padLeft = padRight = false;
        return;
    }
    if (padB) {
        padB = false;
        if (sliderAdjust) {
            sliderAdjust = false;
        } else if (subMode) {
            subMode = false;
        } else {
            src->Close();
        }
        return;
    }
    const auto& sections = src->Sections(pages[view.page]);
    auto enterContent = [&]() {
        if (!navItemsPrev.empty()) {
            zone = Zone::Content;
            bool known = false;
            for (const auto& item : navItemsPrev) {
                known = known || item.id == view.focus;
            }
            if (!known) {
                view.focus = navItemsPrev.front().id;
                view.descExpanded = false;
            }
            focusNeedsReveal = true;
        } else if (sectionHasEmbedded) {
            zone = Zone::Embedded;
        }
    };
    switch (zone) {
        case Zone::Sidebar: {
            int index = 0;
            for (size_t i = 0; i < sections.size(); i++) {
                if (sections[i] == view.section) {
                    index = (int)i;
                }
            }
            if (padUp && index > 0) {
                SelectSection(sections[index - 1]);
                sidebarNeedsReveal = true;
            } else if (padDown) {
                if (index + 1 < (int)sections.size()) {
                    SelectSection(sections[index + 1]);
                    sidebarNeedsReveal = true;
                } else {
                    zoneBeforeFooter = Zone::Sidebar;
                    zone = Zone::Footer;
                }
            } else if (padRight || padA) {
                padA = false;
                enterContent();
            }
        } break;
        case Zone::Content:
            if (sliderAdjust) {
                if (padUp || padDown) {
                    sliderAdjust = false;
                    MoveContentFocus(0, padUp ? -1 : 1);
                }
                break; // left / right belong to the slider
            }
            if (subMode) {
                if (padLeft) {
                    subIndex = std::max(0, subIndex - 1);
                } else if (padRight) {
                    subIndex = std::min(std::max(0, subCountPrev - 1), subIndex + 1);
                } else if (padUp || padDown) {
                    subMode = false;
                    MoveContentFocus(0, padUp ? -1 : 1);
                }
                break;
            }
            if (padUp) {
                MoveContentFocus(0, -1);
            } else if (padDown) {
                MoveContentFocus(0, 1);
            } else if (padLeft) {
                MoveContentFocus(-1, 0);
            } else if (padRight) {
                MoveContentFocus(1, 0);
            }
            break;
        case Zone::Footer:
            if (padLeft) {
                footerIndex = std::max(0, footerIndex - 1);
            } else if (padRight) {
                footerIndex = std::min(2, footerIndex + 1);
            } else if (padUp) {
                zone = zoneBeforeFooter;
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Backdrop
// ---------------------------------------------------------------------------------------------------------------------
void PauseMenu::Impl::DrawBackdrop() {
    Rect screen = ScreenRect();
    if (Fx::Available()) {
        ImDrawList* dl = DL();
        // 10 design px of blur, scaled 105% so the soft edge is off screen, fading in over 180ms.
        Fx::QueueBackdrop(dl, 10.0f * Scale());
        float t = std::clamp((float)((now - openTime) / 0.18), 0.0f, 1.0f);
        float fade = 1.0f - (1.0f - t) * (1.0f - t);
        const float inset = (1.0f - 1.0f / 1.05f) * 0.5f;
        ImVec2 a = PxF(screen.x, screen.y), b = PxF(screen.r(), screen.b());
        dl->AddImage(Fx::BackdropTexture(), a, b, { inset, 1.0f - inset }, { 1.0f - inset, inset },
                     IM_COL32(255, 255, 255, (int)(fade * 255.0f)));
    }
    Fill(screen, C(Tok::OverlayDim, 0.45f));
}

// ---------------------------------------------------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------------------------------------------------
void PauseMenu::Impl::PrepareGroups(std::vector<Group>& groups) {
    for (auto& group : groups) {
        // "All" / "None" move into the group header.
        Row* all = nullptr;
        Row* none = nullptr;
        for (auto& row : group.rows) {
            if (row.bulkAll) {
                all = &row;
            } else if (row.bulkNone) {
                none = &row;
            }
        }
        if (all != nullptr && none != nullptr && all->activate && none->activate) {
            group.bulkAll = all->activate;
            group.bulkNone = none->activate;
            group.rows.erase(std::remove_if(group.rows.begin(), group.rows.end(),
                                            [](const Row& r) { return r.bulkAll || r.bulkNone; }),
                             group.rows.end());
        }
        group.togglesOn = group.togglesTotal = 0;
        const Row* firstLocked = nullptr;
        int locked = 0;
        bool sameReason = true;
        for (const auto& row : group.rows) {
            if (row.kind == RowKind::Toggle) {
                group.togglesTotal++;
                group.togglesOn += row.on ? 1 : 0;
            }
            if (row.disabled) {
                locked++;
                if (firstLocked == nullptr) {
                    firstLocked = &row;
                } else if (row.reason != firstLocked->reason) {
                    sameReason = false;
                }
            }
        }
        // Three or more rows locked for the same reason: say it once under the header.
        group.note.clear();
        if (locked >= 3 && sameReason && firstLocked != nullptr && !firstLocked->reason.empty()) {
            group.note = firstLocked->reason;
            for (auto& row : group.rows) {
                if (row.disabled) {
                    row.showReason = false;
                }
            }
        }
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group& g) { return g.rows.empty(); }),
                 groups.end());
}

int PauseMenu::Impl::SectionCount(const std::string& page, const std::string& section) {
    if (now - countsTime > 0.5) {
        counts.clear();
        countsTime = now;
    }
    std::string key = page + "/" + section;
    auto it = counts.find(key);
    if (it != counts.end()) {
        return it->second;
    }
    int count = src->CountOn(page, section);
    counts[key] = count;
    return count;
}

float PauseMenu::Impl::FooterHeight(const View& v, const Row* focusRow) {
    if (plateOverride || focusRow == nullptr || !v.descExpanded || focusRow->desc.size() <= kDescExpandChars) {
        return kFooterMinH;
    }
    float promptsW = 300.0f;
    TextLayout text = Layout(Type::Description, focusRow->desc, kPanel.w - promptsW - 30.0f);
    float lines = std::min(6.0f, (float)text.lines.size());
    return std::max(kFooterMinH, 7.0f + 12.0f * 1.2f + 2.0f + lines * 18.0f + 7.0f);
}

void PauseMenu::Impl::DrawPanel(View& v, Pass p) {
    pass = p;
    const auto& pages = src->Pages();
    v.page = std::clamp(v.page, 0, (int)pages.size() - 1);
    const std::string& pageName = pages[v.page];
    pc = &ColorsForPage(pageName);
    const auto& sections = src->Sections(pageName);
    if (std::find(sections.begin(), sections.end(), v.section) == sections.end() && !sections.empty()) {
        v.section = sections.front();
    }

    // Build what the content area shows.
    Section section;
    bool searching = !v.search.empty();
    int matches = 0;
    if (searching) {
        section.title = "Search";
        matches = src->Search(v.search, section.groups, 80);
    } else {
        section = src->Build(pageName, v.section);
    }
    PrepareGroups(section.groups);
    section.togglesOn = section.togglesTotal = 0;
    for (const auto& g : section.groups) {
        section.togglesOn += g.togglesOn;
        section.togglesTotal += g.togglesTotal;
    }
    const Row* focusRow = FindRow(section.groups, v.focus);
    if (p == Pass::Live) {
        if (v.focus != 0 && focusRow == nullptr) {
            v.focus = 0; // the row went away (hidden, or another section)
            sliderAdjust = false;
            subMode = false;
        }
        sectionHasEmbedded = section.embedded != nullptr;
    }

    float footerH = FooterHeight(v, focusRow);
    Rect header = { kPanel.x, kPanel.y, kPanel.w, kHeaderH };
    Rect footer = { kPanel.x, kPanel.b() - footerH, kPanel.w, footerH };
    Rect body = { kPanel.x, header.b(), kPanel.w, footer.y - header.b() };

    // Panel: 60% fill so the frozen frame shows through the content area; header, sidebar, footer solid.
    FillRound(kPanel, C(Tok::Panel, 0.6f), 18.0f);
    DrawSidebar(v, body);
    DrawContent(v, body, section, searching, matches);
    DrawHeader(v, header);
    DrawFooter(v, footer, section, focusRow, searching);
    // 1px black 80% line around the panel.
    StrokeRound({ kPanel.x - 1, kPanel.y - 1, kPanel.w + 2, kPanel.h + 2 }, Black(0.8f), 19.0f, 1.0f);
}

void PauseMenu::Impl::DrawHeader(const View& v, const Rect& r) {
    const auto& pages = src->Pages();
    int n = (int)pages.size();
    FillRoundGradV(r, C(pc->header), C(pc->headerDark), 18.0f, ImDrawFlags_RoundCornersTop);
    Fill({ r.x + 12, r.y, r.w - 24, 2 }, White(0.18f));
    Fill({ r.x, r.b() - 2, r.w, 2 }, Black(0.4f));
    curRow = 0;

    bool live = pass == Pass::Live;
    // Previous page prompt.
    Rect prev = { r.x, r.y, 210, r.h };
    bool hovered = false, held = false;
    if (live && TapId(RowId("pm/prev"), prev, &hovered, &held)) {
        StartTurn(-1, (v.page - 1 + n) % n);
    }
    if (held) {
        Fill(prev, Black(0.15f));
    } else if (hovered) {
        Fill(prev, White(0.07f));
    }
    float x = prev.x + 18;
    TriLeft(x, r.cy() - 7, 10, 14, C(Tok::Gold));
    x += 10 + 9;
    ShoulderBadge(x, r.cy() - 10, "L");
    x += 26 + 9;
    DrawLine(Type::Sidebar, pages[(v.page - 1 + n) % n], x, r.cy(), C(pc->light));

    // Next page prompt, mirrored.
    Rect next = { r.r() - 210, r.y, 210, r.h };
    hovered = held = false;
    if (live && TapId(RowId("pm/next"), next, &hovered, &held)) {
        StartTurn(1, (v.page + 1) % n);
    }
    if (held) {
        Fill(next, Black(0.15f));
    } else if (hovered) {
        Fill(next, White(0.07f));
    }
    x = next.r() - 18 - 10;
    TriRight(x, r.cy() - 7, 10, 14, C(Tok::Gold));
    x -= 9 + 26;
    ShoulderBadge(x, r.cy() - 10, "R");
    const std::string& nextName = pages[(v.page + 1) % n];
    x -= 9 + TextW(Type::Sidebar, nextName);
    DrawLine(Type::Sidebar, nextName, x, r.cy(), C(pc->light));

    // Page name and pips: a 21px line, 3px, then a 16px row of pips, centred as a block.
    float top = r.y + (r.h - (21 + 3 + 16)) * 0.5f;
    DrawLine(Type::PageTitle, pages[v.page], prev.r(), top + 10.5f, C(Tok::Text), AlignCenter, next.x - prev.r(), 2.0f);
    float pipsW = 34.0f * n;
    float px = r.cx() - pipsW * 0.5f;
    for (int i = 0; i < n; i++) {
        Rect hit = { px + 34.0f * i, top + 24, 34, 16 };
        if (live && TapId(RowId("pm/pip" + std::to_string(i)), hit) && i != v.page) {
            StartTurn(i > v.page ? 1 : -1, i);
        }
        Pip(hit.cx(), hit.cy(), i == v.page);
    }
}

void PauseMenu::Impl::DrawSidebar(View& v, const Rect& body) {
    const auto& pages = src->Pages();
    const std::string& pageName = pages[v.page];
    const auto& sections = src->Sections(pageName);
    bool live = pass == Pass::Live;
    Rect sb = { body.x, body.y, kSidebarW, body.h };
    Fill(sb, C(pc->sidebar));
    Fill({ sb.r() - 1, sb.y, 1, sb.h }, Black(Tok::LoA));
    curRow = 0;

    // Search: always here, searches every page.
    Rect field = { sb.x + 8, sb.y + 8, sb.w - 16, 38 };
    Fill(field, C(Tok::Bg0));
    Sunken(field);
    IconSearch(field.x + 10, field.cy() - 7.5f, 15, C(Tok::Muted));
    Rect input = { field.x + 10 + 15 + 8, field.y, field.w - 10 - 15 - 8 - 10, field.h };
    if (live) {
        if (TextField("##pmsearch", input, searchBuf, sizeof(searchBuf), "Search all pages", Type::SearchInput, false,
                      false)) {
            v.search = searchBuf;
            v.focus = 0;
            v.scroll = 0;
            ClearTransient();
        }
    } else {
        DrawLine(Type::SearchInput, v.search.empty() ? "Search all pages" : v.search, input.x, input.cy(),
                 C(v.search.empty() ? Tok::Muted : Tok::Text), AlignLeft, 0, 0.0f);
    }

    Rect list = { sb.x, sb.y + 50, sb.w - 1, sb.h - 50 };
    float contentH = 4.0f + 47.0f * (float)sections.size() - 3.0f + 8.0f;
    ScrollArea(RowId("pm/sidebar"), list, v.sidebarScroll, contentH);
    PushClip(list);
    PushHitClip(list);
    float y = list.y + 4.0f - v.sidebarScroll;
    for (const auto& name : sections) {
        Rect item = { sb.x + 8, y, sb.w - 16, 44 };
        y += 47.0f;
        bool selected = name == v.section && v.search.empty();
        if (selected && sidebarNeedsReveal && live) {
            float rel = item.y - (list.y - v.sidebarScroll);
            v.sidebarScroll = std::clamp(v.sidebarScroll, rel + item.h + 8.0f - list.h, std::max(0.0f, rel - 4.0f));
            sidebarNeedsReveal = false;
        }
        if (item.b() < list.y || item.y > list.b()) {
            continue;
        }
        bool hovered = false, held = false;
        if (live && TapId(RowId("pm/sec/" + pageName + "/" + name), item, &hovered, &held)) {
            SelectSection(name);
            zone = Zone::Sidebar;
        }
        Rect d = { item.x, item.y + (held ? 1.0f : 0.0f), item.w, item.h };
        if (selected) {
            Fill(d, C(pc->header));
            Bevel(d, White(0.22f), Black(Tok::LoA));
            TriRight(d.x + 8, d.cy() - 5, 8, 10, C(Tok::Gold));
        } else if (hovered) {
            Fill(d, White(0.05f));
        }
        float right = d.r() - 10;
        int count = SectionCount(pageName, name);
        if (count > 0) {
            std::string text = std::to_string(count);
            float cw = TextW(Type::Count, text);
            DrawLine(Type::Count, text, right - cw, d.cy(), C(pc->light));
            right -= cw + 8;
        }
        float tx = d.x + 8 + 8 + 8;
        DrawLineEllipsis(Type::Sidebar, name, tx, d.cy(), right - tx, C(selected ? Tok::Text : Tok::Text2));
        if (selected && live && usingPad && zone == Zone::Sidebar) {
            float phase = 0.5f + 0.5f * (float)std::sin(now * IM_PI * 2.0);
            CursorBrackets(d.Inset(1), phase);
        }
    }
    DrawScrollThumb(list, v.sidebarScroll, contentH);
    PopHitClip();
    PopClip();
}

void PauseMenu::Impl::DrawContent(View& v, const Rect& body, Section& section, bool searching, int matches) {
    const auto& pages = src->Pages();
    bool live = pass == Pass::Live;
    Rect area = { body.x + kSidebarW, body.y, body.w - kSidebarW, body.h };
    // The panel's inner bevel only shows where nothing solid covers it: the right edge of the content.
    Fill({ area.r() - 2, area.y, 2, area.h }, Black(Tok::LoA));
    const float x = area.x + 14.0f;
    const float w = area.w - 14.0f - 12.0f;
    const float colGap = 12.0f;
    const float colW = (w - colGap) * 0.5f;
    contentClip = area;
    contentViewH = area.h;

    bool embedded = section.embedded != nullptr && !searching;
    if (embedded) {
        v.scroll = 0;
    } else if (live) {
        ScrollArea(RowId("pm/content"), area, v.scroll, contentHeight);
    }
    PushClip(area);
    PushHitClip(area);
    curRow = 0;

    float top = area.y - v.scroll;
    float y = top + 10.0f;

    // Title row: section name, a rule fading out, and the count.
    std::string title = searching ? "Search" : section.title;
    std::string subtitle;
    if (searching) {
        subtitle = std::to_string(matches) + (matches == 1 ? " match" : " matches") + " across all pages";
    } else if (section.togglesTotal > 0) {
        subtitle =
            std::to_string(section.togglesOn) + " of " + std::to_string(section.togglesTotal) + " enabled";
    } else {
        subtitle = pages[v.page];
    }
    float titleW = TextW(Type::SectionTitle, title);
    float subW = TextW(Type::Subtitle, subtitle);
    DrawLine(Type::SectionTitle, title, x, y + 11.0f, C(Tok::Text));
    DrawLine(Type::Subtitle, subtitle, x + w - subW, y + 11.0f, C(Tok::Muted));
    Rect rule = { x + titleW + 10.0f, y + 10.0f, w - titleW - subW - 20.0f, 2.0f };
    if (rule.w > 0) {
        FillGradH(rule, C(pc->header), C(pc->header, 0.0f));
    }
    y += 22.0f + 10.0f;

    if (searching && matches == 0) {
        DrawLine(Type::DialogBody, "No settings match your search.", x, y + 50.0f + 10.0f, C(Tok::Muted), AlignCenter,
                 w);
    }

    // Two columns; a group never splits. Balance like CSS columns: the split that gives the shortest page.
    std::vector<float> heights;
    float total = 0;
    for (const auto& g : section.groups) {
        heights.push_back(MeasureGroup(g, colW));
        total += heights.back() + 12.0f;
    }
    size_t split = section.groups.size();
    if (section.groups.size() > 1) {
        float best = 1e9f;
        float left = 0;
        for (size_t k = 1; k <= section.groups.size(); k++) {
            left += heights[k - 1] + 12.0f;
            float tall = std::max(left, total - left);
            if (tall < best - 0.5f) {
                best = tall;
                split = k;
            }
        }
    }
    float colY[2] = { y, y };
    for (size_t i = 0; i < section.groups.size(); i++) {
        int col = i < split ? 0 : 1;
        Rect gr = { x + (colW + colGap) * col, colY[col], colW, heights[i] };
        DrawGroup(section.groups[i], gr, col, top);
        colY[col] += heights[i] + 12.0f;
    }
    float bottom = std::max(colY[0], colY[1]);

    if (embedded) {
        Rect er = { x, bottom, w, area.b() - 8.0f - bottom };
        if (er.h > 60.0f) {
            DrawEmbedded(er, section);
        }
    }
    if (live) {
        contentHeight = bottom - top + 4.0f;
        if (focusNeedsReveal) {
            for (const auto& item : navItems) {
                if (item.id == v.focus) {
                    float lo = item.rect.b() + 12.0f - area.h;
                    float hi = std::max(0.0f, item.rect.y - 12.0f);
                    v.scroll = std::clamp(v.scroll, std::min(lo, hi), hi);
                }
            }
            focusNeedsReveal = false;
        }
    }
    if (!embedded) {
        DrawScrollThumb(area, v.scroll, bottom - top + 4.0f);
    }
    PopHitClip();
    PopClip();
}

void PauseMenu::Impl::DrawEmbedded(const Rect& area, Section& section) {
    Fill(area, C(Tok::Slot, 0.55f));
    Sunken(area);
    if (pass != Pass::Live) {
        return;
    }
    ImGui::SetCursorScreenPos(Px(area.x + 8, area.y + 8));
    ImVec2 a = Px(area.x + 8, area.y + 8), b = Px(area.r() - 8, area.b() - 8);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushID(section.title.c_str());
    if (ImGui::BeginChild("##pmembedded", ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        if (zone == Zone::Embedded && !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
            ImGui::SetWindowFocus();
        }
        ImGui::BeginDisabled(!inputEnabled);
        section.embedded->DrawElement();
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopStyleColor();
}

void PauseMenu::Impl::DrawFooter(View& v, const Rect& r, const Section& section, const Row* focusRow, bool searching) {
    bool live = pass == Pass::Live;
    FillRound(r, C(Tok::Bg0), 18.0f, ImDrawFlags_RoundCornersBottom);
    Fill({ r.x, r.y, r.w, 2 }, Black(Tok::LoA));
    Fill({ r.x, r.y + 2, r.w, 1 }, White(Tok::HiA));
    curRow = 0;

    // Prompts, right to left: Close (B), Quit, Reset.
    struct Prompt {
        const char* label;
        uint32_t color;
        int icon;
    };
    const Prompt prompts[3] = { { "Reset", Tok::GlyphYellow, 0 }, { "Quit", Tok::GlyphRed, 1 },
                                { "Close", Tok::GlyphGreen, 2 } };
    float right = r.r() - 10.0f;
    float left = right;
    Rect rects[3];
    for (int i = 2; i >= 0; i--) {
        float pw = 9 + 28 + 7 + TextW(Type::Control, prompts[i].label) + 9;
        rects[i] = { left - pw, r.b() - 60 + 8, pw, 44 };
        left -= pw + 2.0f;
    }
    left += 2.0f;
    for (int i = 0; i < 3; i++) {
        bool hovered = false, held = false;
        bool tapped = live && TapId(RowId(std::string("pm/prompt/") + prompts[i].label), rects[i], &hovered, &held);
        bool padFocus = live && usingPad && zone == Zone::Footer && footerIndex == i;
        if (padFocus && padA) {
            padA = false;
            tapped = true;
        }
        Rect d = { rects[i].x, rects[i].y + (held ? 1.0f : 0.0f), rects[i].w, rects[i].h };
        if (hovered) {
            Fill(d, White(0.06f));
        }
        float gx = d.x + 9 + 14, gy = d.cy();
        if (prompts[i].icon == 0) {
            GlyphDisc(gx, gy, 28, prompts[i].color, 0.35f, 0.25f);
            IconUndo(gx - 7.5f, gy - 7.5f, 15, C(Tok::DarkOnGold));
        } else if (prompts[i].icon == 1) {
            GlyphDisc(gx, gy, 28, prompts[i].color);
            IconPower(gx - 7.5f, gy - 7.5f, 15, C(Tok::Text));
        } else {
            GlyphDisc(gx, gy, 28, prompts[i].color);
            DrawLine(Type::PromptLetter, "B", gx - 14, gy, C(Tok::Text), AlignCenter, 28);
        }
        DrawLine(Type::Control, prompts[i].label, d.x + 9 + 28 + 7, d.cy(), C(Tok::Text2));
        if (padFocus) {
            float phase = 0.5f + 0.5f * (float)std::sin(now * IM_PI * 2.0);
            CursorBrackets(d.Inset(2), phase);
        }
        if (tapped) {
            if (i == 0) {
                dialog = Dialog();
                dialog.open = true;
                dialog.title = "Reset";
                dialog.message = "Reset the game? Progress since your last save will be lost.";
                dialog.onYes = [this]() { src->Reset(); };
            } else if (i == 1) {
                dialog = Dialog();
                dialog.open = true;
                dialog.quit = true;
                dialog.title = "Quit SoH";
                dialog.message = "Are you sure you want to quit SoH?";
                dialog.onYes = [this]() { src->Quit(); };
            } else {
                src->Close();
            }
        }
    }

    // Description plate.
    Rect plate = { r.x, r.y, left - 6.0f - r.x, r.h };
    std::string title, text;
    bool hasMore = false;
    if (plateOverride) {
        title = plateTitle;
        text = plateText;
    } else if (focusRow != nullptr) {
        title = focusRow->label;
        text = focusRow->desc.empty() ? "No description for this setting." : focusRow->desc;
        hasMore = focusRow->desc.size() > kDescExpandChars;
    } else {
        title = searching ? "Search" : section.title;
        text = "Select a setting to read what it does.";
    }
    bool expanded = hasMore && v.descExpanded;
    if (live && hasMore && TapId(RowId("pm/plate"), plate)) {
        v.descExpanded = !v.descExpanded;
    }
    float textW = plate.w - 18.0f - 12.0f;
    TextLayout body = expanded ? Layout(Type::Description, text, textW, 6)
                               : Layout(Type::Description, OneLine(text), textW, 2);
    float titleH = 12.0f * 1.2f;
    float blockH = titleH + 2.0f + body.h;
    float ty = plate.y + std::max(7.0f, (plate.h - blockH) * 0.5f);
    float moreW = hasMore ? TextW(Type::OnOff, expanded ? "LESS" : "MORE") : 0.0f;
    float used = DrawLineEllipsis(Type::GroupTitle, title, plate.x + 18.0f, ty + titleH * 0.5f,
                                  textW - (hasMore ? moreW + 10.0f : 0.0f), C(Tok::Gold));
    if (hasMore) {
        DrawLine(Type::OnOff, expanded ? "LESS" : "MORE", plate.x + 18.0f + used + 10.0f, ty + titleH * 0.5f + 0.5f,
                 C(Tok::Muted));
    }
    DrawLayout(body, Type::Description, plate.x + 18.0f, ty + titleH + 2.0f, C(Tok::Text2));
}

// ---------------------------------------------------------------------------------------------------------------------
// Page turn
// ---------------------------------------------------------------------------------------------------------------------
void PauseMenu::Impl::DrawTurn() {
    // The pages are inner walls of a box seen from its centre. Each wall turns about a vertical axis half
    // a panel width in front of the screen: perspective(2400) translateZ(R) rotateY(a) translateZ(-R).
    float t = std::clamp((float)((now - turn.start) / kTurnSeconds), 0.0f, 1.0f);
    float e = CubicBezier(0.4f, 0.0f, 0.2f, 1.0f, t);
    const float R = kPanel.w * 0.5f;
    const float d = 2400.0f;
    const float cx = kPanel.cx(), cy = kPanel.cy();
    const Rect src = { kPanel.x - 1, kPanel.y - 1, kPanel.w + 2, kPanel.h + 2 };
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 dispPos = ImGui::GetMainViewport()->Pos;
    ImVec2 dispSize = io.DisplaySize;
    ImDrawList* dl = DL();
    const int strips = 64;

    auto wall = [&](int slot, float degrees) {
        float a = degrees * IM_PI / 180.0f;
        float c = std::cos(a), s = std::sin(a);
        auto project = [&](float x, float y) {
            float xr = x - cx, yr = y - cy;
            float xp = xr * c - R * s;
            float z = -xr * s - R * c + R;
            float k = d / (d - z);
            return PxF(cx + xp * k, cy + yr * k);
        };
        auto uv = [&](float x, float y) {
            ImVec2 p = PxF(x, y);
            return ImVec2((p.x - dispPos.x) / dispSize.x, 1.0f - (p.y - dispPos.y) / dispSize.y);
        };
        ImTextureID tex = Fx::PageTexture(slot);
        for (int i = 0; i < strips; i++) {
            float x0 = src.x + src.w * (float)i / strips;
            float x1 = src.x + src.w * (float)(i + 1) / strips;
            dl->AddImageQuad(tex, project(x0, src.y), project(x1, src.y), project(x1, src.b()), project(x0, src.b()),
                             uv(x0, src.y), uv(x1, src.y), uv(x1, src.b()), uv(x0, src.b()));
        }
    };
    // Going to the next page the outgoing wall turns 0 -> +90 (leaves to the left) while the incoming one
    // turns -90 -> 0 (arrives from the right); the previous page is the mirror image.
    wall(0, 90.0f * turn.dir * e);
    wall(1, -90.0f * turn.dir * (1.0f - e));
    if (t >= 1.0f) {
        turn.active = false;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Modals
// ---------------------------------------------------------------------------------------------------------------------
void PauseMenu::Impl::DrawModals() {
    if (!picker.open && !dialog.open) {
        return;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin(kModalWindow, nullptr, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        BeginCanvas(ImGui::GetWindowDrawList(), vp->Pos, vp->Size);
        pass = Pass::Live;
        inputEnabled = true;
        hitClip.clear();
        curRow = 0;
        curRowDisabled = false;
        Fill(ScreenRect(), C(Tok::ModalScrim, 0.72f));
        if (dialog.open) {
            DrawDialog();
        } else {
            DrawPicker();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

static void ModalPanel(const Rect& r) {
    DropShadow(r, 12.0f, 34.0f, 0.7f);
    Fill({ r.x - 1, r.y - 1, r.w + 2, r.h + 2 }, Black(0.8f));
    Fill(r, C(Tok::Slab));
}

static void ModalBevel(const Rect& r) {
    Bevel(r, White(Tok::HiA), Black(Tok::LoA), 2.0f);
}

void PauseMenu::Impl::DrawPicker() {
    const PageColors& colors = picker.colors != nullptr ? *picker.colors : ColorsForPage("Settings");
    const float w = 340.0f;
    TextLayout title = Layout(Type::GroupTitle, picker.title, w - 28.0f);
    float headerH = 22.0f + title.h;
    int n = (int)picker.options.size();
    float listH = 16.0f + 44.0f * n + 3.0f * std::max(0, n - 1);
    float h = std::min(kDesignH - 40.0f, headerH + listH);
    Rect panel = { (kDesignW - w) * 0.5f, (kDesignH - h) * 0.5f, w, h };

    // Tapping the scrim, or B, closes without change.
    bool close = TapId(RowId("pm/picker/scrim"), ScreenRect());
    TapId(RowId("pm/picker/panel"), panel);
    if (padB) {
        padB = false;
        close = true;
    }
    if (padUp) {
        picker.highlight = std::max(0, picker.highlight - 1);
        picker.reveal = true;
    }
    if (padDown) {
        picker.highlight = std::min(n - 1, picker.highlight + 1);
        picker.reveal = true;
    }

    ModalPanel(panel);
    Rect header = { panel.x, panel.y, panel.w, headerH };
    FillGradV(header, C(colors.header), C(colors.headerDark));
    Fill({ header.x, header.y, header.w, 2 }, White(0.18f));
    Fill({ header.x, header.b() - 2, header.w, 2 }, Black(0.4f));
    DrawLayout(title, Type::GroupTitle, header.x + 14.0f, header.y + 11.0f, C(Tok::Text));

    Rect list = { panel.x, header.b(), panel.w, panel.h - headerH };
    ScrollArea(RowId("pm/picker/list"), list, picker.scroll, listH);
    PushClip(list);
    PushHitClip(list);
    int chosen = -1;
    float y = list.y + 8.0f - picker.scroll;
    for (int i = 0; i < n; i++) {
        Rect item = { panel.x + 8, y, panel.w - 16, 44 };
        y += 47.0f;
        if (picker.reveal && i == picker.highlight) {
            float rel = item.y - (list.y - picker.scroll);
            picker.scroll = std::clamp(picker.scroll, rel + item.h + 8.0f - list.h, std::max(0.0f, rel - 8.0f));
            picker.reveal = false;
        }
        if (item.b() < list.y || item.y > list.b()) {
            continue;
        }
        bool current = picker.options[i].value == picker.current;
        bool hovered = false;
        if (TapId(RowId("pm/picker/" + std::to_string(i)), item, &hovered)) {
            chosen = i;
        }
        Fill(item, current ? C(colors.headerDark) : C(Tok::Slot, 0.55f));
        if (hovered && !current) {
            Fill(item, White(0.04f));
        }
        Sunken(item);
        if (current) {
            TriRight(item.x + 8, item.cy() - 5, 8, 10, C(Tok::Gold));
        }
        DrawLineEllipsis(Type::Sidebar, picker.options[i].label, item.x + 8 + 8 + 9, item.cy(), item.w - 37.0f,
                         C(current ? Tok::Text : Tok::Text2));
        if (usingPad && i == picker.highlight) {
            float phase = 0.5f + 0.5f * (float)std::sin(now * IM_PI * 2.0);
            CursorBrackets(item.Inset(1), phase);
        }
    }
    DrawScrollThumb(list, picker.scroll, listH);
    PopHitClip();
    PopClip();
    ModalBevel(panel);

    if (padA && n > 0) {
        padA = false;
        chosen = std::clamp(picker.highlight, 0, n - 1);
    }
    if (chosen >= 0) {
        if (picker.set) {
            picker.set(picker.options[chosen].value);
        }
        countsTime = -1000;
        close = true;
    }
    if (close) {
        picker = Picker();
    }
}

void PauseMenu::Impl::DrawDialog() {
    const float w = 340.0f;
    TextLayout body = Layout(Type::DialogBody, dialog.message, w - 40.0f);
    float headerH = 22.0f + LineH(Type::DialogTitle);
    float h = headerH + 18.0f + body.h + 16.0f + 44.0f + 14.0f;
    Rect panel = { (kDesignW - w) * 0.5f, (kDesignH - h) * 0.5f, w, h };

    // No, the scrim and B all dismiss.
    bool dismiss = TapId(RowId("pm/dialog/scrim"), ScreenRect());
    TapId(RowId("pm/dialog/panel"), panel);
    if (padB) {
        padB = false;
        dismiss = true;
    }
    if (padLeft) {
        dialog.highlight = 0;
    }
    if (padRight) {
        dialog.highlight = 1;
    }

    ModalPanel(panel);
    Rect header = { panel.x, panel.y, panel.w, headerH };
    FillGradV(header, C(Tok::QuitTop), C(Tok::QuitBottom));
    Fill({ header.x, header.y, header.w, 2 }, White(0.18f));
    Fill({ header.x, header.b() - 2, header.w, 2 }, Black(0.4f));
    DrawLine(Type::DialogTitle, dialog.title, header.x, header.cy(), C(Tok::Text), AlignCenter, header.w);
    DrawLayout(body, Type::DialogBody, panel.x + 20.0f, header.b() + 18.0f, C(Tok::Text2), AlignCenter, w - 40.0f);

    float by = header.b() + 18.0f + body.h + 16.0f;
    float bw = (w - 28.0f - 8.0f) * 0.5f;
    bool yes = false;
    for (int i = 0; i < 2; i++) {
        Rect b = { panel.x + 14.0f + (bw + 8.0f) * i, by, bw, 44 };
        bool held = false;
        bool tapped = TapId(RowId(i == 0 ? "pm/dialog/no" : "pm/dialog/yes"), b, nullptr, &held);
        if (usingPad && dialog.highlight == i && padA) {
            padA = false;
            tapped = true;
        }
        Rect d = { b.x, b.y + (held ? 1.0f : 0.0f), b.w, b.h };
        Fill(d, C(i == 0 ? Tok::Button : Tok::QuitYes));
        if (i == 0) {
            Raised(d);
        } else {
            Bevel(d, White(0.2f), Black(Tok::LoA));
        }
        DrawLine(Type::DialogButton, i == 0 ? "No" : "Yes", d.x, d.cy(), C(Tok::Text), AlignCenter, d.w);
        if (usingPad && dialog.highlight == i) {
            float phase = 0.5f + 0.5f * (float)std::sin(now * IM_PI * 2.0);
            CursorBrackets(d.Inset(1), phase);
        }
        if (tapped) {
            if (i == 0) {
                dismiss = true;
            } else {
                yes = true;
            }
        }
    }
    ModalBevel(panel);

    if (yes) {
        auto action = dialog.onYes;
        dialog = Dialog();
        if (action) {
            action();
        }
    } else if (dismiss) {
        dialog = Dialog();
    }
}

} // namespace Pm
