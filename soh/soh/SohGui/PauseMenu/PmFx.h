#ifndef PAUSE_MENU_FX_H
#define PAUSE_MENU_FX_H

// The two effects of the pause-style menu that ImGui's draw lists cannot express on their own:
//
//  - Backdrop: the frozen game frame, blurred. A draw callback copies whatever is on screen when the
//    menu starts drawing (the game, already presented by the renderer) into a small texture and blurs it.
//  - Page turn: a page is drawn into an offscreen texture, then shown as a wall of a rotating box.
//
// Both work through ImDrawList callbacks, so they run inside ImGui's own render pass, in order with the
// menu's draw commands. They need the OpenGL backend; on any other backend Available() is false and the
// menu falls back to a plain dim and an instant page change.

#include <imgui.h>

namespace Pm {
namespace Fx {

bool Available();

// Queues the capture + blur of the current framebuffer contents. `sigmaPx` is the blur radius (Gaussian
// sigma) in screen pixels. After this call the draw list is back in ImGui's normal render state.
void QueueBackdrop(ImDrawList* drawList, float sigmaPx);
// The blurred frame. Stored bottom-up: draw it with V flipped.
ImTextureID BackdropTexture();

constexpr int kPageSlots = 2;
// Redirects the following draw commands into page texture `slot`, cleared to transparent.
void BeginPageCapture(ImDrawList* drawList, int slot);
// Ends the redirect. The following draw commands go to the screen again, blended as premultiplied alpha
// (which is what the page textures hold) until EndPageCompose.
void EndPageCapture(ImDrawList* drawList);
void EndPageCompose(ImDrawList* drawList);
// Stored bottom-up and premultiplied.
ImTextureID PageTexture(int slot);

// Releases the GL objects. Safe to call when nothing was created.
void Shutdown();

} // namespace Fx
} // namespace Pm

#endif // PAUSE_MENU_FX_H
