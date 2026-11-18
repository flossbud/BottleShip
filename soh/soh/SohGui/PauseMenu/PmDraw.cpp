#include "PmDraw.h"

#include <imgui_internal.h>
#include <libultraship/libultraship.h>
#include "window/gui/resource/Font.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Pm {

// ---------------------------------------------------------------------------------------------------------------------
// Canvas state
// ---------------------------------------------------------------------------------------------------------------------
static ImDrawList* sDrawList = nullptr;
static float sScale = 1.0f;
static ImVec2 sOrigin = { 0, 0 };
static ImVec2 sDisplayPos = { 0, 0 };
static ImVec2 sDisplaySize = { 0, 0 };
static std::vector<float> sAlphaStack;
static float sAlpha = 1.0f;

Rect Rect::Intersect(const Rect& o) const {
    float nx = std::max(x, o.x), ny = std::max(y, o.y);
    float nr = std::min(r(), o.r()), nb = std::min(b(), o.b());
    return { nx, ny, std::max(0.0f, nr - nx), std::max(0.0f, nb - ny) };
}

const PageColors& ColorsForPage(const std::string& pageName) {
    static const std::unordered_map<std::string, PageColors> pages = {
        { "Settings", { 0x215d96, 0x12375a, 0x0d2034, 0xb2d9ff } },
        { "Enhancements", { 0x206b38, 0x124020, 0x0e2513, 0xb4e2bc } },
        { "Randomizer", { 0x8e3e3a, 0x562421, 0x321614, 0xffc3bd } },
        { "Network", { 0x69498c, 0x3e2a54, 0x241930, 0xdfc9fa } },
        { "Dev Tools", { 0x7e5000, 0x4b2e00, 0x2c1c04, 0xf0cea1 } },
    };
    auto it = pages.find(pageName);
    return it != pages.end() ? it->second : pages.at("Settings");
}

void BeginCanvas(ImDrawList* drawList, ImVec2 displayPos, ImVec2 displaySize) {
    sDrawList = drawList;
    sDisplayPos = displayPos;
    sDisplaySize = displaySize;
    sScale = std::max(0.1f, std::min(displaySize.x / kDesignW, displaySize.y / kDesignH));
    sOrigin.x = std::floor(displayPos.x + (displaySize.x - kDesignW * sScale) * 0.5f);
    sOrigin.y = std::floor(displayPos.y + (displaySize.y - kDesignH * sScale) * 0.5f);
    sAlphaStack.clear();
    sAlpha = 1.0f;
}

ImDrawList* DL() {
    return sDrawList;
}

float Scale() {
    return sScale;
}

ImVec2 Px(float x, float y) {
    return { std::floor(sOrigin.x + x * sScale + 0.5f), std::floor(sOrigin.y + y * sScale + 0.5f) };
}

ImVec2 PxF(float x, float y) {
    return { sOrigin.x + x * sScale, sOrigin.y + y * sScale };
}

ImVec2 ToDesign(ImVec2 screen) {
    return { (screen.x - sOrigin.x) / sScale, (screen.y - sOrigin.y) / sScale };
}

Rect ScreenRect() {
    ImVec2 a = ToDesign(sDisplayPos);
    ImVec2 b = ToDesign({ sDisplayPos.x + sDisplaySize.x, sDisplayPos.y + sDisplaySize.y });
    return { a.x, a.y, b.x - a.x, b.y - a.y };
}

// Width of a design-px line in whole screen pixels, never less than one.
static float LinePx(float designWidth) {
    return std::max(1.0f, std::floor(designWidth * sScale + 0.5f));
}

ImU32 C(uint32_t rgb, float alpha) {
    float a = std::clamp(alpha * sAlpha, 0.0f, 1.0f);
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, (int)(a * 255.0f + 0.5f));
}

ImU32 White(float alpha) {
    return C(0xffffff, alpha);
}

ImU32 Black(float alpha) {
    return C(0x000000, alpha);
}

void PushAlpha(float alpha) {
    sAlphaStack.push_back(sAlpha);
    sAlpha *= alpha;
}

void PopAlpha() {
    if (!sAlphaStack.empty()) {
        sAlpha = sAlphaStack.back();
        sAlphaStack.pop_back();
    }
}

float CurrentAlpha() {
    return sAlpha;
}

void PushClip(const Rect& r, bool intersect) {
    ImGui::PushClipRect(Px(r.x, r.y), Px(r.r(), r.b()), intersect);
}

void PopClip() {
    ImGui::PopClipRect();
}

// ---------------------------------------------------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------------------------------------------------
void Fill(const Rect& r, ImU32 col) {
    if ((col & IM_COL32_A_MASK) == 0 || r.w <= 0 || r.h <= 0) {
        return;
    }
    sDrawList->AddRectFilled(Px(r.x, r.y), Px(r.r(), r.b()), col);
}

void FillGradV(const Rect& r, ImU32 top, ImU32 bottom) {
    sDrawList->AddRectFilledMultiColor(Px(r.x, r.y), Px(r.r(), r.b()), top, top, bottom, bottom);
}

void FillGradH(const Rect& r, ImU32 left, ImU32 right) {
    sDrawList->AddRectFilledMultiColor(Px(r.x, r.y), Px(r.r(), r.b()), left, right, right, left);
}

void FillRound(const Rect& r, ImU32 col, float radius, ImDrawFlags corners) {
    sDrawList->AddRectFilled(Px(r.x, r.y), Px(r.r(), r.b()), col, radius * sScale, corners);
}

void FillRoundGradV(const Rect& r, ImU32 top, ImU32 bottom, float radius, ImDrawFlags corners) {
    ImVec2 a = Px(r.x, r.y), b = Px(r.r(), r.b());
    int start = sDrawList->VtxBuffer.Size;
    sDrawList->AddRectFilled(a, b, IM_COL32_WHITE, radius * sScale, corners);
    int end = sDrawList->VtxBuffer.Size;
    // Keeps the anti-aliased edge alpha of the white fill, replaces its colour with the gradient.
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(sDrawList, start, end, a, { a.x, b.y }, top, bottom);
    // KeepAlpha keeps the white fill's alpha, so apply the gradient's own alpha on top.
    float alphaTop = ((top >> IM_COL32_A_SHIFT) & 0xff) / 255.0f;
    if (alphaTop < 0.999f) {
        for (int i = start; i < end; i++) {
            ImU32 c = sDrawList->VtxBuffer[i].col;
            ImU32 a8 = (ImU32)(((c >> IM_COL32_A_SHIFT) & 0xff) * alphaTop);
            sDrawList->VtxBuffer[i].col = (c & ~IM_COL32_A_MASK) | (a8 << IM_COL32_A_SHIFT);
        }
    }
}

void StrokeRound(const Rect& r, ImU32 col, float radius, float width) {
    ImVec2 a = Px(r.x, r.y), b = Px(r.r(), r.b());
    float w = LinePx(width);
    sDrawList->AddRect({ a.x + w * 0.5f, a.y + w * 0.5f }, { b.x - w * 0.5f, b.y - w * 0.5f }, col, radius * sScale,
                       0, w);
}

void Bevel(const Rect& r, ImU32 tl, ImU32 br, float width) {
    ImVec2 a = Px(r.x, r.y), b = Px(r.r(), r.b());
    float w = LinePx(width);
    if (b.x - a.x < w * 2 || b.y - a.y < w * 2) {
        return;
    }
    if (br & IM_COL32_A_MASK) {
        sDrawList->AddRectFilled({ a.x, b.y - w }, b, br);          // bottom
        sDrawList->AddRectFilled({ b.x - w, a.y + w }, { b.x, b.y - w }, br); // right
    }
    if (tl & IM_COL32_A_MASK) {
        sDrawList->AddRectFilled(a, { b.x, a.y + w }, tl);          // top
        sDrawList->AddRectFilled({ a.x, a.y + w }, { a.x + w, b.y }, tl); // left
    }
}

void Raised(const Rect& r) {
    Bevel(r, White(Tok::HiA), Black(Tok::LoA));
}

void Sunken(const Rect& r) {
    Bevel(r, Black(Tok::LoA), White(Tok::HiA));
}

void Frame(const Rect& r, ImU32 col, float width) {
    ImVec2 a = Px(r.x, r.y), b = Px(r.r(), r.b());
    float w = LinePx(width);
    sDrawList->AddRectFilled(a, { b.x, a.y + w }, col);
    sDrawList->AddRectFilled({ a.x, b.y - w }, b, col);
    sDrawList->AddRectFilled({ a.x, a.y + w }, { a.x + w, b.y - w }, col);
    sDrawList->AddRectFilled({ b.x - w, a.y + w }, { b.x, b.y - w }, col);
}

void Tri(float x1, float y1, float x2, float y2, float x3, float y3, ImU32 col) {
    sDrawList->AddTriangleFilled(PxF(x1, y1), PxF(x2, y2), PxF(x3, y3), col);
}

void Line(float x1, float y1, float x2, float y2, ImU32 col, float width) {
    sDrawList->AddLine(PxF(x1, y1), PxF(x2, y2), col, std::max(1.0f, width * sScale));
}

void Circle(float cx, float cy, float radius, ImU32 col) {
    sDrawList->AddCircleFilled(PxF(cx, cy), radius * sScale, col, 48);
}

void CircleStroke(float cx, float cy, float radius, ImU32 col, float width) {
    sDrawList->AddCircle(PxF(cx, cy), radius * sScale, col, 64, std::max(1.0f, width * sScale));
}

void CircleDashed(float cx, float cy, float radius, ImU32 col, float width) {
    if (radius <= 0.5f) {
        return;
    }
    // Dash and gap of about 3 design px each, as a 1px dashed border renders.
    int dashes = std::max(6, (int)(2.0f * IM_PI * radius / 6.0f));
    float step = 2.0f * IM_PI / dashes;
    ImVec2 c = PxF(cx, cy);
    for (int i = 0; i < dashes; i++) {
        sDrawList->PathArcTo(c, radius * sScale, i * step, i * step + step * 0.5f, 6);
        sDrawList->PathStroke(col, 0, std::max(1.0f, width * sScale));
    }
}

void DropShadow(const Rect& r, float offsetY, float blur, float alpha) {
    const int layers = 10;
    for (int i = layers; i >= 1; i--) {
        float spread = blur * 0.5f * (float)i / layers;
        float a = alpha * 0.16f * (1.0f - (float)(i - 1) / layers);
        Rect s = { r.x - spread, r.y - spread + offsetY, r.w + spread * 2, r.h + spread * 2 };
        sDrawList->AddRectFilled(PxF(s.x, s.y), PxF(s.r(), s.b()), Black(a), spread * sScale);
    }
}

void TriLeft(float x, float y, float w, float h, ImU32 col) {
    Tri(x, y + h * 0.5f, x + w, y, x + w, y + h, col);
}

void TriRight(float x, float y, float w, float h, ImU32 col) {
    Tri(x, y, x + w, y + h * 0.5f, x, y + h, col);
}

void TriDown(float x, float y, float w, float h, ImU32 col) {
    Tri(x, y, x + w, y, x + w * 0.5f, y + h, col);
}

void TriUp(float x, float y, float w, float h, ImU32 col) {
    Tri(x + w * 0.5f, y, x + w, y + h, x, y + h, col);
}

// ---------------------------------------------------------------------------------------------------------------------
// Pieces
// ---------------------------------------------------------------------------------------------------------------------
static ImU32 Mix(uint32_t rgb, uint32_t over, float t) {
    auto ch = [&](int shift) {
        float a = (float)((rgb >> shift) & 0xff), b = (float)((over >> shift) & 0xff);
        return (uint32_t)(a + (b - a) * t + 0.5f);
    };
    return C((ch(16) << 16) | (ch(8) << 8) | ch(0));
}

void GlyphDisc(float cx, float cy, float d, uint32_t rgb, float hiAlpha, float loAlpha) {
    // CSS: inset 0 2px 0 white, inset 0 -2px 0 black on a circle. The lit crescents are the disc minus
    // the disc shifted by 2px, so draw the two half discs in the bevel colours and the lens between them
    // in the base colour.
    const float r = d * 0.5f;
    const float bevel = 2.0f;
    const int seg = 40;
    ImVec2 c = PxF(cx, cy);
    float rp = r * sScale, bp = bevel * sScale;
    sDrawList->PathArcTo(c, rp, IM_PI, IM_PI * 2.0f, seg);
    sDrawList->PathFillConvex(Mix(rgb, 0xffffff, hiAlpha));
    sDrawList->PathArcTo(c, rp, 0.0f, IM_PI, seg);
    sDrawList->PathFillConvex(Mix(rgb, 0x000000, loAlpha));
    // Lens: for each angle the point on the disc, pulled toward the horizontal axis by the bevel.
    for (int i = 0; i < seg * 2; i++) {
        float a = (float)i / (seg * 2) * IM_PI * 2.0f;
        float sx = std::cos(a) * rp, sy = std::sin(a) * rp;
        float y = sy > 0 ? std::max(0.0f, sy - bp) : std::min(0.0f, sy + bp);
        sDrawList->PathLineTo({ c.x + sx, c.y + y });
    }
    sDrawList->PathFillConvex(C(rgb));
}

void ShoulderBadge(float x, float y, const char* letter) {
    // 26x20, radii 7/7/3/3, inset 0 -2px 0 black 25%.
    const float w = 26, h = 20;
    auto path = [&](float top, float bottom) {
        float rt = 7 * sScale, rb = 3 * sScale;
        ImVec2 a = PxF(x, top), b = PxF(x + w, bottom);
        sDrawList->PathArcTo({ a.x + rt, a.y + rt }, rt, IM_PI, IM_PI * 1.5f, 8);
        sDrawList->PathArcTo({ b.x - rt, a.y + rt }, rt, IM_PI * 1.5f, IM_PI * 2.0f, 8);
        sDrawList->PathArcTo({ b.x - rb, b.y - rb }, rb, 0.0f, IM_PI * 0.5f, 6);
        sDrawList->PathArcTo({ a.x + rb, b.y - rb }, rb, IM_PI * 0.5f, IM_PI, 6);
    };
    path(y, y + h);
    sDrawList->PathFillConvex(Mix(Tok::Shoulder, 0x000000, 0.25f));
    path(y, y + h - 2);
    sDrawList->PathFillConvex(C(Tok::Shoulder));
    DrawLine(Type::Count, letter, x, y + h * 0.5f - 0.5f, C(Tok::ShoulderText), AlignCenter, w, 0.0f);
}

// Clips a convex polygon to the half-plane dot(p, n) <= d.
static void ClipPoly(std::vector<ImVec2>& poly, ImVec2 n, float d) {
    std::vector<ImVec2> out;
    size_t count = poly.size();
    for (size_t i = 0; i < count; i++) {
        ImVec2 a = poly[i], b = poly[(i + 1) % count];
        float da = a.x * n.x + a.y * n.y - d, db = b.x * n.x + b.y * n.y - d;
        if (da <= 0) {
            out.push_back(a);
        }
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
            float t = da / (da - db);
            out.push_back({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t });
        }
    }
    poly.swap(out);
}

void GemSocket(float x, float y, bool on) {
    Rect socket = { x, y, 30, 32 };
    Fill(socket, C(Tok::Bg0));
    Sunken(socket);

    // Hexagon 14x22: 50% 0, 100% 27%, 100% 73%, 50% 100%, 0 73%, 0 27%.
    const float gw = 14, gh = 22;
    float gx = x + (30 - gw) * 0.5f, gy = y + (32 - gh) * 0.5f;
    const float hx[6] = { 0.5f, 1.0f, 1.0f, 0.5f, 0.0f, 0.0f };
    const float hy[6] = { 0.0f, 0.27f, 0.73f, 1.0f, 0.73f, 0.27f };
    std::vector<ImVec2> hex;
    for (int i = 0; i < 6; i++) {
        hex.push_back(PxF(gx + hx[i] * gw, gy + hy[i] * gh));
    }
    if (!on) {
        sDrawList->AddConvexPolyFilled(hex.data(), (int)hex.size(), C(Tok::GemOff));
        return;
    }
    // linear-gradient(125deg, light 0 28%, mid 28% 62%, dark 62%): hard stops along the 125deg axis.
    const float ang = 125.0f * IM_PI / 180.0f;
    ImVec2 dir = { std::sin(ang), -std::cos(ang) };
    float wpx = gw * sScale, hpx = gh * sScale;
    float len = std::fabs(wpx * dir.x) + std::fabs(hpx * dir.y);
    ImVec2 centre = PxF(gx + gw * 0.5f, gy + gh * 0.5f);
    float base = centre.x * dir.x + centre.y * dir.y - len * 0.5f;
    const float stops[4] = { -1.0f, 0.28f, 0.62f, 2.0f };
    const uint32_t cols[3] = { Tok::GemLight, Tok::GemMid, Tok::GemDark };
    for (int band = 0; band < 3; band++) {
        std::vector<ImVec2> poly = hex;
        ClipPoly(poly, dir, base + stops[band + 1] * len);
        ClipPoly(poly, { -dir.x, -dir.y }, -(base + stops[band] * len));
        if (poly.size() >= 3) {
            sDrawList->AddConvexPolyFilled(poly.data(), (int)poly.size(), C(cols[band]));
        }
    }
}

void CursorBrackets(const Rect& r, float phase) {
    // Brackets breathe between 1px and 4px outside the row while opacity goes 55% -> 100%.
    float out = 1.0f + 3.0f * phase;
    float alpha = 0.55f + 0.45f * phase;
    Rect o = { r.x - out, r.y - out, r.w + out * 2, r.h + out * 2 };
    const float len = 13.0f, th = 2.0f;
    auto corner = [&](float cx, float cy, float sx, float sy, ImU32 col, float grow) {
        // Horizontal then vertical arm, growing outwards for the glow passes.
        float x0 = sx > 0 ? cx - grow : cx - len - grow, x1 = sx > 0 ? cx + len + grow : cx + grow;
        float y0 = sy > 0 ? cy - grow : cy - th - grow, y1 = sy > 0 ? cy + th + grow : cy + grow;
        sDrawList->AddRectFilled(PxF(x0, y0), PxF(x1, y1), col, grow * sScale);
        x0 = sx > 0 ? cx - grow : cx - th - grow;
        x1 = sx > 0 ? cx + th + grow : cx + grow;
        y0 = sy > 0 ? cy - grow : cy - len - grow;
        y1 = sy > 0 ? cy + len + grow : cy + grow;
        sDrawList->AddRectFilled(PxF(x0, y0), PxF(x1, y1), col, grow * sScale);
    };
    // drop-shadow(0 0 3px gold 70%), approximated with three widening passes.
    const float glow[3] = { 3.0f, 2.0f, 1.0f };
    const float glowA[3] = { 0.07f, 0.12f, 0.20f };
    for (int pass = 0; pass < 4; pass++) {
        float grow = pass < 3 ? glow[pass] : 0.0f;
        ImU32 col = C(Tok::Gold, alpha * (pass < 3 ? glowA[pass] : 1.0f));
        corner(o.x, o.y, 1, 1, col, grow);
        corner(o.r(), o.y, -1, 1, col, grow);
        corner(o.x, o.b(), 1, -1, col, grow);
        corner(o.r(), o.b(), -1, -1, col, grow);
    }
}

void Pip(float cx, float cy, bool current) {
    // 7x7 square rotated 45deg with a 1px black 50% outline.
    const float h = 7.0f * 0.70710678f;
    ImVec2 c = PxF(cx, cy);
    auto diamond = [&](float half, ImU32 col) {
        float p = half * sScale;
        ImVec2 pts[4] = { { c.x, c.y - p }, { c.x + p, c.y }, { c.x, c.y + p }, { c.x - p, c.y } };
        sDrawList->AddConvexPolyFilled(pts, 4, col);
    };
    diamond(h + 1.0f * 1.41421356f, Black(0.5f));
    diamond(h, current ? C(Tok::Gold) : White(0.32f));
}

void IconSearch(float x, float y, float size, ImU32 col) {
    float k = size / 24.0f;
    float w = std::max(1.0f, 2.2f * k * sScale);
    sDrawList->AddCircle(PxF(x + 11 * k, y + 11 * k), 8 * k * sScale, col, 32, w);
    sDrawList->AddLine(PxF(x + 21 * k, y + 21 * k), PxF(x + 16.7f * k, y + 16.7f * k), col, w);
}

void IconUndo(float x, float y, float size, ImU32 col) {
    float k = size / 24.0f;
    float w = std::max(1.0f, 2.4f * k * sScale);
    sDrawList->PathLineTo(PxF(x + 9 * k, y + 14 * k));
    sDrawList->PathLineTo(PxF(x + 4 * k, y + 9 * k));
    sDrawList->PathLineTo(PxF(x + 9 * k, y + 4 * k));
    sDrawList->PathStroke(col, 0, w);
    sDrawList->PathLineTo(PxF(x + 4 * k, y + 9 * k));
    sDrawList->PathLineTo(PxF(x + 14.5f * k, y + 9 * k));
    sDrawList->PathArcTo(PxF(x + 14.5f * k, y + 14.5f * k), 5.5f * k * sScale, -IM_PI * 0.5f, IM_PI * 0.5f, 16);
    sDrawList->PathLineTo(PxF(x + 11 * k, y + 20 * k));
    sDrawList->PathStroke(col, 0, w);
}

void IconPower(float x, float y, float size, ImU32 col) {
    float k = size / 24.0f;
    float w = std::max(1.0f, 2.4f * k * sScale);
    sDrawList->AddLine(PxF(x + 12 * k, y + 2 * k), PxF(x + 12 * k, y + 12 * k), col, w);
    sDrawList->PathArcTo(PxF(x + 12 * k, y + 13 * k), 9 * k * sScale, -IM_PI * 0.25f, IM_PI * 1.25f, 32);
    sDrawList->PathStroke(col, 0, w);
}

void IconPlus(float cx, float cy, float size, ImU32 col, float width) {
    float h = size * 0.5f;
    Line(cx - h, cy, cx + h, cy, col, width);
    Line(cx, cy - h, cx, cy + h, col, width);
}

void IconMinus(float cx, float cy, float size, ImU32 col, float width) {
    float h = size * 0.5f;
    Line(cx - h, cy, cx + h, cy, col, width);
}

void IconCross(float cx, float cy, float size, ImU32 col, float width) {
    float h = size * 0.5f;
    Line(cx - h, cy - h, cx + h, cy + h, col, width);
    Line(cx - h, cy + h, cx + h, cy - h, col, width);
}

void IconArrow(float cx, float cy, float size, int dir, ImU32 col) {
    float h = size * 0.5f;
    switch (dir) {
        case 0:
            Tri(cx, cy - h * 0.85f, cx + h, cy + h * 0.75f, cx - h, cy + h * 0.75f, col);
            break;
        case 1:
            Tri(cx - h, cy - h * 0.75f, cx + h, cy - h * 0.75f, cx, cy + h * 0.85f, col);
            break;
        case 2:
            Tri(cx - h * 0.85f, cy, cx + h * 0.75f, cy - h, cx + h * 0.75f, cy + h, col);
            break;
        default:
            Tri(cx - h * 0.75f, cy - h, cx + h * 0.85f, cy, cx - h * 0.75f, cy + h, col);
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------------------------------------------------
namespace Type {
const TextStyle PageTitle = { Face::Display, 21, 1.0f, 0.22f, true };
const TextStyle SectionTitle = { Face::Display, 20, 1.1f, 0.04f, false };
const TextStyle SliderValue = { Face::Display, 15, 0, 0, false };
const TextStyle DialogTitle = { Face::Display, 14, 0, 0.2f, true };
const TextStyle GroupTitle = { Face::Display, 12, 1.4f, 0.14f, true };
const TextStyle ButtonLabel = { Face::Display, 12, 1.3f, 0.1f, true };
const TextStyle DialogButton = { Face::Display, 12, 0, 0.12f, true };
const TextStyle SubHeading = { Face::Display, 11, 0, 0.14f, true };
const TextStyle RowLabel = { Face::Medium, 15, 1.25f, 0, false };
const TextStyle Sidebar = { Face::Medium, 15, 0, 0, false };
const TextStyle Control = { Face::Medium, 14, 0, 0, false };
const TextStyle Body = { Face::Body, 14, 0, 0, false };
const TextStyle Para = { Face::Body, 14, 1.45f, 0, false };
const TextStyle WarnText = { Face::Medium, 14, 1.4f, 0, false };
const TextStyle DialogBody = { Face::Body, 15, 1.4f, 0, false };
const TextStyle SearchInput = { Face::Body, 15, 0, 0, false };
const TextStyle Description = { Face::Body, 13.5f, 18.0f / 13.5f, 0, false };
const TextStyle Helper = { Face::Body, 13, 1.35f, 0, false };
const TextStyle Subtitle = { Face::Medium, 13, 0, 0, false };
const TextStyle SmallButton = { Face::Medium, 13, 0, 0, false };
const TextStyle Hint = { Face::Medium, 12, 1.3f, 0, false };
const TextStyle Note = { Face::Medium, 12, 1.35f, 0, false };
const TextStyle Count = { Face::Bold, 12, 0, 0, false };
const TextStyle OnOff = { Face::Bold, 11, 0, 0.08f, true };
const TextStyle GlyphLetter = { Face::Bold, 13, 1.0f, 0, false };
const TextStyle PromptLetter = { Face::Bold, 16, 1.0f, 0, false };
const TextStyle Readout = { Face::Mono, 15, 1.2f, 0, false };
} // namespace Type

struct BakedFont {
    Face face;
    float designSize;
    ImFont* font;
};

static std::vector<BakedFont> sFonts;
static std::vector<std::shared_ptr<Ship::Font>> sFontData;

// (ascent - descent) / unitsPerEm of each face. ImGui sizes a font by that height while CSS sizes it by
// the em, so a CSS size is multiplied by this to get the ImGui size that renders the same glyphs.
static float EmFactor(Face face) {
    switch (face) {
        case Face::Display:
            return 2568.0f / 2048.0f; // Marcellus
        case Face::Mono:
            return 1049.0f / 1000.0f; // Inconsolata
        default:
            return 1.2f; // Alegreya Sans
    }
}

void LoadFonts() {
    if (!sFonts.empty()) {
        return;
    }
    // Bake at the scale the menu will be drawn at so text is not resampled.
    float bakeScale = 1.875f;
    auto window = Ship::Context::GetInstance()->GetWindow();
    if (window != nullptr && window->GetWidth() > 0 && window->GetHeight() > 0) {
        bakeScale = std::min((float)window->GetWidth() / kDesignW, (float)window->GetHeight() / kDesignH);
    }
    bakeScale = std::clamp(bakeScale, 1.0f, 4.0f);

    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x2013, 0x2014, 0x2018, 0x201D, 0x2022, 0x2022, 0x2026,
                                      0x2026, 0x203A, 0x203A, 0x2190, 0x2193, 0x2212, 0x2212, 0 };
    struct FaceSpec {
        Face face;
        const char* path;
        std::vector<float> sizes;
    };
    const std::vector<FaceSpec> specs = {
        { Face::Display, "fonts/Marcellus-Regular.ttf", { 21, 20, 15, 14, 12, 11 } },
        { Face::Body, "fonts/AlegreyaSans-Regular.ttf", { 15, 14, 13.5f, 13 } },
        { Face::Medium, "fonts/AlegreyaSans-Medium.ttf", { 15, 14, 13, 12 } },
        { Face::Bold, "fonts/AlegreyaSans-Bold.ttf", { 16, 13, 12, 11 } },
        { Face::Mono, "fonts/Inconsolata-Regular.ttf", { 15 } },
    };

    ImGuiIO& io = ImGui::GetIO();
    for (const auto& spec : specs) {
        auto initData = std::make_shared<Ship::ResourceInitData>();
        initData->Format = RESOURCE_FORMAT_BINARY;
        initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_FONT);
        initData->ResourceVersion = 0;
        initData->Path = spec.path;
        auto data = std::static_pointer_cast<Ship::Font>(
            Ship::Context::GetInstance()->GetResourceManager()->LoadResource(spec.path, false, initData));
        if (data == nullptr || data->Data == nullptr) {
            SPDLOG_ERROR("Pause menu: could not load font {}", spec.path);
            continue;
        }
        sFontData.push_back(data);
        for (float size : spec.sizes) {
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false; // the resource owns it
            cfg.OversampleH = 2;
            cfg.OversampleV = 1;
            cfg.PixelSnapH = false;
            ImFont* font = io.Fonts->AddFontFromMemoryTTF(data->Data, (int)data->DataSize,
                                                         size * EmFactor(spec.face) * bakeScale, &cfg, ranges);
            if (font != nullptr) {
                sFonts.push_back({ spec.face, size, font });
            }
        }
    }
}

static const BakedFont* PickFont(Face face, float size) {
    const BakedFont* best = nullptr;
    float bestDiff = 1e9f;
    for (const auto& f : sFonts) {
        if (f.face != face) {
            continue;
        }
        float diff = std::fabs(f.designSize - size);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = &f;
        }
    }
    return best;
}

struct ResolvedFont {
    ImFont* font;
    float designPx; // glyph box height in design px
};

static ResolvedFont Resolve(const TextStyle& st) {
    const BakedFont* baked = PickFont(st.face, st.size);
    if (baked == nullptr) {
        // Fonts missing (asset archive without them): fall back to the default ImGui font.
        return { ImGui::GetIO().FontDefault ? ImGui::GetIO().FontDefault : ImGui::GetFont(), st.size * 1.2f };
    }
    return { baked->font, st.size * EmFactor(st.face) };
}

ImFont* ImFontFor(const TextStyle& st, float* outFontSizePx) {
    ResolvedFont rf = Resolve(st);
    if (outFontSizePx != nullptr) {
        *outFontSizePx = rf.designPx * sScale;
    }
    return rf.font;
}

float LineH(const TextStyle& st) {
    ResolvedFont rf = Resolve(st);
    return st.lineHeight > 0.0f ? st.lineHeight * st.size : rf.designPx;
}

// Advance of one code point in design px.
static float Advance(const ResolvedFont& rf, const TextStyle& st, unsigned int c) {
    return rf.font->GetCharAdvance((ImWchar)c) * (rf.designPx / rf.font->FontSize) + st.tracking * st.size;
}

static float Measure(const ResolvedFont& rf, const TextStyle& st, const char* begin, const char* end) {
    float w = 0;
    const char* p = begin;
    while (p < end) {
        unsigned int c = 0;
        int n = ImTextCharFromUtf8(&c, p, end);
        if (n <= 0) {
            break;
        }
        p += n;
        w += Advance(rf, st, c);
    }
    return w;
}

static std::string Prepare(const TextStyle& st, std::string_view text) {
    std::string s(text);
    // "##id" suffixes are ImGui ids, never shown.
    size_t hash = s.find("##");
    if (hash != std::string::npos) {
        s.erase(hash);
    }
    if (st.upper) {
        for (char& ch : s) {
            if (ch >= 'a' && ch <= 'z') {
                ch = (char)(ch - 'a' + 'A');
            }
        }
    }
    return s;
}

TextLayout Layout(const TextStyle& st, std::string_view text, float maxW, int maxLines) {
    TextLayout out;
    out.text = Prepare(st, text);
    out.lineH = LineH(st);
    ResolvedFont rf = Resolve(st);
    const char* base = out.text.c_str();
    const char* end = base + out.text.size();
    const char* p = base;

    while (p <= end) {
        // One paragraph: up to the next '\n'.
        const char* paraEnd = (const char*)memchr(p, '\n', end - p);
        if (paraEnd == nullptr) {
            paraEnd = end;
        }
        const char* lineStart = p;
        if (maxW <= 0.0f) {
            out.lines.push_back({ (int)(lineStart - base), (int)(paraEnd - base), Measure(rf, st, lineStart, paraEnd) });
        } else {
            while (true) {
                // Greedy fill by words.
                const char* q = lineStart;
                const char* lastBreak = nullptr;
                float w = 0, wAtBreak = 0;
                bool overflow = false;
                while (q < paraEnd) {
                    unsigned int c = 0;
                    int n = ImTextCharFromUtf8(&c, q, paraEnd);
                    if (n <= 0) {
                        break;
                    }
                    float adv = Advance(rf, st, c);
                    if (c == ' ') {
                        lastBreak = q;
                        wAtBreak = w;
                    }
                    if (w + adv - st.tracking * st.size > maxW && q > lineStart && c != ' ') {
                        overflow = true;
                        break;
                    }
                    w += adv;
                    q += n;
                }
                if (!overflow) {
                    out.lines.push_back({ (int)(lineStart - base), (int)(paraEnd - base), w });
                    break;
                }
                const char* lineEnd = lastBreak != nullptr && lastBreak > lineStart ? lastBreak : q;
                float lineW = lastBreak != nullptr && lastBreak > lineStart ? wAtBreak : w;
                out.lines.push_back({ (int)(lineStart - base), (int)(lineEnd - base), lineW });
                lineStart = lineEnd;
                while (lineStart < paraEnd && *lineStart == ' ') {
                    lineStart++;
                }
                if (lineStart >= paraEnd) {
                    break;
                }
            }
        }
        if (paraEnd >= end) {
            break;
        }
        p = paraEnd + 1;
    }

    if (maxLines > 0 && (int)out.lines.size() > maxLines) {
        out.lines.resize(maxLines);
        out.truncated = true;
        // Make room for the ellipsis on the last kept line.
        TextLine& last = out.lines.back();
        float ell = Advance(rf, st, 0x2026);
        float limit = maxW > 0.0f ? maxW : last.w + ell;
        while (last.end > last.begin && last.w + ell > limit) {
            // Step back one code point.
            int e = last.end - 1;
            while (e > last.begin && ((unsigned char)base[e] & 0xC0) == 0x80) {
                e--;
            }
            last.end = e;
            last.w = Measure(rf, st, base + last.begin, base + last.end);
        }
        while (last.end > last.begin && base[last.end - 1] == ' ') {
            last.end--;
        }
        last.w = Measure(rf, st, base + last.begin, base + last.end) + ell;
    }

    for (const auto& line : out.lines) {
        out.w = std::max(out.w, line.w);
    }
    out.h = out.lineH * (float)out.lines.size();
    return out;
}

float TextW(const TextStyle& st, std::string_view text) {
    std::string s = Prepare(st, text);
    ResolvedFont rf = Resolve(st);
    return Measure(rf, st, s.c_str(), s.c_str() + s.size());
}

static void RenderRun(const ResolvedFont& rf, const TextStyle& st, ImVec2 pos, ImU32 col, const char* begin,
                      const char* end, bool ellipsis) {
    float px = rf.designPx * sScale;
    pos.y = std::floor(pos.y + 0.5f);
    if (st.tracking == 0.0f) {
        pos.x = std::floor(pos.x + 0.5f);
        sDrawList->AddText(rf.font, px, pos, col, begin, end);
        if (ellipsis) {
            float w = rf.font->CalcTextSizeA(px, FLT_MAX, 0.0f, begin, end).x;
            sDrawList->AddText(rf.font, px, { pos.x + w, pos.y }, col, "\xe2\x80\xa6");
        }
        return;
    }
    const char* p = begin;
    float x = pos.x;
    float k = px / rf.font->FontSize;
    while (p < end) {
        unsigned int c = 0;
        int n = ImTextCharFromUtf8(&c, p, end);
        if (n <= 0) {
            break;
        }
        p += n;
        rf.font->RenderChar(sDrawList, px, { std::floor(x + 0.5f), pos.y }, col, (ImWchar)c);
        x += rf.font->GetCharAdvance((ImWchar)c) * k + st.tracking * st.size * sScale;
    }
    if (ellipsis) {
        rf.font->RenderChar(sDrawList, px, { std::floor(x + 0.5f), pos.y }, col, (ImWchar)0x2026);
    }
}

void DrawLayout(const TextLayout& layout, const TextStyle& st, float x, float y, ImU32 col, Align align, float boxW,
                float shadow, int firstLine, int lineCount) {
    if ((col & IM_COL32_A_MASK) == 0) {
        return;
    }
    ResolvedFont rf = Resolve(st);
    const char* base = layout.text.c_str();
    float glyphTop = (layout.lineH - rf.designPx) * 0.5f;
    int last = lineCount < 0 ? (int)layout.lines.size() : std::min((int)layout.lines.size(), firstLine + lineCount);
    float alpha = ((col >> IM_COL32_A_SHIFT) & 0xff) / 255.0f;
    for (int i = firstLine; i < last; i++) {
        const TextLine& line = layout.lines[i];
        float lx = x;
        if (align == AlignCenter) {
            lx = x + (boxW - line.w) * 0.5f;
        } else if (align == AlignRight) {
            lx = x + boxW - line.w;
        }
        float ly = y + (float)(i - firstLine) * layout.lineH + glyphTop;
        bool ell = layout.truncated && i == (int)layout.lines.size() - 1;
        ImVec2 pos = PxF(lx, ly);
        if (shadow > 0.0f) {
            ImU32 sh = IM_COL32(0, 0, 0, (int)(Tok::LoA * alpha * 255.0f));
            RenderRun(rf, st, { pos.x, pos.y + std::max(1.0f, std::floor(shadow * sScale + 0.5f)) }, sh,
                      base + line.begin, base + line.end, ell);
        }
        RenderRun(rf, st, pos, col, base + line.begin, base + line.end, ell);
    }
}

void DrawLine(const TextStyle& st, std::string_view text, float x, float cy, ImU32 col, Align align, float boxW,
              float shadow) {
    TextLayout layout = Layout(st, text);
    DrawLayout(layout, st, x, cy - layout.lineH * 0.5f, col, align, boxW, shadow, 0, 1);
}

float DrawLineEllipsis(const TextStyle& st, std::string_view text, float x, float cy, float maxW, ImU32 col,
                       float shadow) {
    TextLayout layout = Layout(st, text, 0.0f);
    if (!layout.lines.empty() && layout.lines[0].w > maxW) {
        // Re-lay out as a single truncated line.
        layout = Layout(st, text, maxW, 1);
        // A wrapped first line may be shorter than it needs to be: refill it to the limit.
        std::string s = Prepare(st, text);
        ResolvedFont rf = Resolve(st);
        float ell = Advance(rf, st, 0x2026);
        const char* base = s.c_str();
        const char* p = base;
        const char* end = base + s.size();
        float w = 0;
        while (p < end) {
            unsigned int c = 0;
            int n = ImTextCharFromUtf8(&c, p, end);
            if (n <= 0 || c == '\n') {
                break;
            }
            float adv = Advance(rf, st, c);
            if (w + adv + ell > maxW) {
                break;
            }
            w += adv;
            p += n;
        }
        layout.text = s;
        layout.lines = { { 0, (int)(p - base), w + ell } };
        layout.truncated = true;
        layout.w = w + ell;
    }
    DrawLayout(layout, st, x, cy - layout.lineH * 0.5f, col, AlignLeft, 0.0f, shadow, 0, 1);
    return layout.lines.empty() ? 0.0f : layout.lines[0].w;
}

} // namespace Pm
