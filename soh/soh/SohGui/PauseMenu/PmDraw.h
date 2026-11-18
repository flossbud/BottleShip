#ifndef PAUSE_MENU_DRAW_H
#define PAUSE_MENU_DRAW_H

// Drawing layer of the pause-style menu.
//
// The menu is designed on a 1024x576 canvas and scaled uniformly to the screen. Everything in this
// header takes design pixels; conversion to screen pixels (with snapping so 1px bevels stay crisp)
// happens here and nowhere else.

#include <imgui.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Pm {

constexpr float kDesignW = 1024.0f;
constexpr float kDesignH = 576.0f;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float r() const {
        return x + w;
    }
    float b() const {
        return y + h;
    }
    float cx() const {
        return x + w * 0.5f;
    }
    float cy() const {
        return y + h * 0.5f;
    }
    bool Contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    Rect Inset(float d) const {
        return { x + d, y + d, w - d * 2, h - d * 2 };
    }
    Rect Intersect(const Rect& o) const;
};

// ---------------------------------------------------------------------------------------------------------------------
// Colours (design tokens, sRGB hex)
// ---------------------------------------------------------------------------------------------------------------------
namespace Tok {
constexpr uint32_t Bg0 = 0x04060b;
constexpr uint32_t Slab = 0x141822;
constexpr uint32_t Panel = 0x0f141d; // 60%
constexpr uint32_t Slot = 0x030409;  // 55%
constexpr uint32_t Button = 0x282e3b;
constexpr uint32_t Line = 0x3c4250;
constexpr uint32_t Gold = 0xf4d660;
constexpr uint32_t DarkOnGold = 0x221a03;
constexpr uint32_t Text = 0xf6f2e7;
constexpr uint32_t Text2 = 0xc3cbd8;
constexpr uint32_t Muted = 0x979fab;
constexpr uint32_t On = 0x47c86e;
constexpr uint32_t Warn = 0xf9be78;
constexpr uint32_t SliderFrame = 0xd5d1c6;
constexpr uint32_t OverlayDim = 0x010207;  // 45%
constexpr uint32_t ModalScrim = 0x000103;  // 72%
constexpr uint32_t ScrollThumb = 0x494f5d; // oklch(0.40 0.03 265)
constexpr uint32_t ThumbBorder = 0x12161f;

constexpr uint32_t GlyphBlue = 0x2971c6;
constexpr uint32_t GlyphGreen = 0x008d3c;
constexpr uint32_t GlyphRed = 0xc53637;
constexpr uint32_t GlyphYellow = 0xe1c34b;
constexpr uint32_t GlyphGrey = 0x9b9ea6;
constexpr uint32_t GlyphGreyText = 0x12161f;
constexpr uint32_t Shoulder = 0xc1c4cb;
constexpr uint32_t ShoulderText = 0x161b24;

constexpr uint32_t GemLight = 0xacffbd;
constexpr uint32_t GemMid = 0x3cc96a;
constexpr uint32_t GemDark = 0x007f35;
constexpr uint32_t GemOff = 0x1d222b;

constexpr uint32_t WarnBoxBg = 0x372108;
constexpr uint32_t WarnBoxText = 0xfde4bb;
constexpr uint32_t QuitTop = 0xac3031;
constexpr uint32_t QuitBottom = 0x6f191a;
constexpr uint32_t QuitYes = 0x972527;

constexpr float HiA = 0.13f; // bevel light alpha (white)
constexpr float LoA = 0.55f; // bevel dark alpha (black)
} // namespace Tok

struct PageColors {
    uint32_t header, headerDark, sidebar, light;
};

// Page colours by page name; unknown pages get the Settings blue.
const PageColors& ColorsForPage(const std::string& pageName);

// ---------------------------------------------------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------------------------------------------------

// Sets the draw target and the design->screen transform for everything below. Call once per frame.
void BeginCanvas(ImDrawList* drawList, ImVec2 displayPos, ImVec2 displaySize);
ImDrawList* DL();
float Scale();
// Design -> screen, snapped to whole pixels.
ImVec2 Px(float x, float y);
// Design -> screen, not snapped (for smooth motion).
ImVec2 PxF(float x, float y);
// Screen -> design.
ImVec2 ToDesign(ImVec2 screen);
// The part of the design canvas that is on screen (wider than 1024 on screens wider than 16:9).
Rect ScreenRect();

// Colour with alpha, multiplied by the current alpha stack.
ImU32 C(uint32_t rgb, float alpha = 1.0f);
ImU32 White(float alpha);
ImU32 Black(float alpha);
void PushAlpha(float alpha);
void PopAlpha();
float CurrentAlpha();

void PushClip(const Rect& r, bool intersect = true);
void PopClip();

// ---------------------------------------------------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------------------------------------------------
void Fill(const Rect& r, ImU32 col);
void FillGradV(const Rect& r, ImU32 top, ImU32 bottom);
void FillGradH(const Rect& r, ImU32 left, ImU32 right);
// Rounded fill. `corners` is an ImDrawFlags_RoundCorners* mask.
void FillRound(const Rect& r, ImU32 col, float radius, ImDrawFlags corners = ImDrawFlags_RoundCornersAll);
void FillRoundGradV(const Rect& r, ImU32 top, ImU32 bottom, float radius,
                    ImDrawFlags corners = ImDrawFlags_RoundCornersAll);
void StrokeRound(const Rect& r, ImU32 col, float radius, float width = 1.0f);
// Inset edge lines: `tl` along the top and left, `br` along the bottom and right; `tl` wins the shared corners.
void Bevel(const Rect& r, ImU32 tl, ImU32 br, float width = 1.0f);
void Raised(const Rect& r);
void Sunken(const Rect& r);
// 1px (design) frame inside the rect.
void Frame(const Rect& r, ImU32 col, float width = 1.0f);
void Tri(float x1, float y1, float x2, float y2, float x3, float y3, ImU32 col);
void Line(float x1, float y1, float x2, float y2, ImU32 col, float width = 1.0f);
void Circle(float cx, float cy, float radius, ImU32 col);
void CircleStroke(float cx, float cy, float radius, ImU32 col, float width = 1.0f);
void CircleDashed(float cx, float cy, float radius, ImU32 col, float width = 1.0f);
// Soft shadow under a modal panel.
void DropShadow(const Rect& r, float offsetY, float blur, float alpha);

// Direction markers. (x, y) is the top-left of the bounding box.
void TriLeft(float x, float y, float w, float h, ImU32 col);
void TriRight(float x, float y, float w, float h, ImU32 col);
void TriDown(float x, float y, float w, float h, ImU32 col);
void TriUp(float x, float y, float w, float h, ImU32 col);

// ---------------------------------------------------------------------------------------------------------------------
// Pieces
// ---------------------------------------------------------------------------------------------------------------------

// Round bevelled button glyph. `d` is the diameter.
void GlyphDisc(float cx, float cy, float d, uint32_t rgb, float hiAlpha = 0.30f, float loAlpha = 0.30f);
// Shoulder badge with its letter, 26x20 at (x, y).
void ShoulderBadge(float x, float y, const char* letter);
// Toggle gem, 14x22 centred in its 30x32 socket at (x, y).
void GemSocket(float x, float y, bool on);
// Selection cursor: four gold corner brackets. `phase` is 0..1 along the pulse.
void CursorBrackets(const Rect& r, float phase);
// 7x7 diamond pip centred at (cx, cy).
void Pip(float cx, float cy, bool current);
void IconSearch(float x, float y, float size, ImU32 col);
void IconUndo(float x, float y, float size, ImU32 col);
void IconPower(float x, float y, float size, ImU32 col);
// "+", "−" and "×" drawn as strokes, centred.
void IconPlus(float cx, float cy, float size, ImU32 col, float width = 1.6f);
void IconMinus(float cx, float cy, float size, ImU32 col, float width = 1.6f);
void IconCross(float cx, float cy, float size, ImU32 col, float width = 1.5f);
// Arrow glyphs for D-pad / stick / C-button rows, centred. dir: 0 up, 1 down, 2 left, 3 right.
void IconArrow(float cx, float cy, float size, int dir, ImU32 col);

// ---------------------------------------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------------------------------------
enum class Face { Display, Body, Medium, Bold, Mono };

struct TextStyle {
    Face face = Face::Body;
    float size = 14.0f;       // CSS px
    float lineHeight = 0.0f;  // multiple of size; 0 = the font's natural line
    float tracking = 0.0f;    // em
    bool upper = false;
};

namespace Type {
extern const TextStyle PageTitle;    // Marcellus 21, uppercase, .22em
extern const TextStyle SectionTitle; // Marcellus 20, .04em
extern const TextStyle SliderValue;  // Marcellus 15
extern const TextStyle DialogTitle;  // Marcellus 14, uppercase, .2em
extern const TextStyle GroupTitle;   // Marcellus 12, uppercase, .14em
extern const TextStyle ButtonLabel;  // Marcellus 12, uppercase, .1em
extern const TextStyle DialogButton; // Marcellus 12, uppercase, .12em
extern const TextStyle SubHeading;   // Marcellus 11, uppercase, .14em
extern const TextStyle RowLabel;     // Alegreya Sans 500 15 / 1.25
extern const TextStyle Sidebar;      // Alegreya Sans 500 15
extern const TextStyle Control;      // Alegreya Sans 500 14
extern const TextStyle Body;         // Alegreya Sans 400 14
extern const TextStyle Para;         // Alegreya Sans 400 14 / 1.45
extern const TextStyle WarnText;     // Alegreya Sans 500 14 / 1.4
extern const TextStyle DialogBody;   // Alegreya Sans 400 15 / 1.4
extern const TextStyle SearchInput;  // Alegreya Sans 400 15
extern const TextStyle Description;  // Alegreya Sans 400 13.5 / 18px
extern const TextStyle Helper;       // Alegreya Sans 400 13 / 1.35
extern const TextStyle Subtitle;     // Alegreya Sans 500 13
extern const TextStyle SmallButton;  // Alegreya Sans 500 13
extern const TextStyle Hint;         // Alegreya Sans 500 12 / 1.3
extern const TextStyle Note;         // Alegreya Sans 500 12 / 1.35
extern const TextStyle Count;        // Alegreya Sans 700 12
extern const TextStyle OnOff;        // Alegreya Sans 700 11, uppercase, .08em
extern const TextStyle GlyphLetter;  // Alegreya Sans 700 13
extern const TextStyle PromptLetter; // Alegreya Sans 700 16
extern const TextStyle Readout;      // monospace 15 / 1.2
} // namespace Type

// Loads the menu's font faces into the ImGui atlas. Must run before the atlas is built.
void LoadFonts();
// The ImGui font to use for real ImGui widgets (text inputs) at the given style, plus the scale to apply.
ImFont* ImFontFor(const TextStyle& st, float* outFontSizePx);

struct TextLine {
    int begin = 0, end = 0;
    float w = 0;
};

struct TextLayout {
    std::string text;
    std::vector<TextLine> lines;
    float w = 0, h = 0;
    float lineH = 0;
    bool truncated = false;
};

// Lays text out. maxW <= 0 means a single unbounded line per '\n'. maxLines > 0 cuts with an ellipsis.
TextLayout Layout(const TextStyle& st, std::string_view text, float maxW = 0.0f, int maxLines = 0);
float TextW(const TextStyle& st, std::string_view text);
// Height of one line box.
float LineH(const TextStyle& st);

enum Align { AlignLeft = 0, AlignCenter = 1, AlignRight = 2 };

// Draws at (x, y) = top-left of the first line box. For centre/right alignment `boxW` is the box width.
// shadow: design px the black 55% shadow is dropped by (the menu's text shadow); 0 = none.
void DrawLayout(const TextLayout& layout, const TextStyle& st, float x, float y, ImU32 col,
                Align align = AlignLeft, float boxW = 0.0f, float shadow = 1.0f, int firstLine = 0,
                int lineCount = -1);
// Single line, vertically centred on `cy`.
void DrawLine(const TextStyle& st, std::string_view text, float x, float cy, ImU32 col, Align align = AlignLeft,
              float boxW = 0.0f, float shadow = 1.0f);
// Single line cut to maxW with an ellipsis, vertically centred on `cy`. Returns the drawn width.
float DrawLineEllipsis(const TextStyle& st, std::string_view text, float x, float cy, float maxW, ImU32 col,
                       float shadow = 1.0f);

} // namespace Pm

#endif // PAUSE_MENU_DRAW_H
