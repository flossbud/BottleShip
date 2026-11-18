#pragma once

// Settings > Controls > Touch Controls (Android only).
// The touch overlay and its eye button are Android views owned by MainActivity;
// these settings live in CVars so they are saved with the rest of the config.
#define CVAR_TOUCH_CONTROLS_MODE CVAR_SETTING("TouchControls.Mode")
#define CVAR_TOUCH_CONTROLS_SHOW_TOGGLE CVAR_SETTING("TouchControls.ShowToggle")

namespace AndroidTouchControls {

// Values of CVAR_TOUCH_CONTROLS_MODE. Keep in sync with MainActivity.TOUCH_CONTROLS_*.
enum Mode {
    TOUCH_CONTROLS_AUTO = 0,   // shown only while no physical gamepad is connected
    TOUCH_CONTROLS_ALWAYS = 1, // always shown
    TOUCH_CONTROLS_NEVER = 2,  // never shown, eye button hidden too
};

// Sends the current CVar values to MainActivity.setTouchControlsConfig().
// Safe to call from the game thread; does nothing on other platforms.
void Apply();

} // namespace AndroidTouchControls
