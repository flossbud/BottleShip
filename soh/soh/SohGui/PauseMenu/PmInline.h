#ifndef PAUSE_MENU_INLINE_H
#define PAUSE_MENU_INLINE_H

// Pages whose content used to be a pop-out window and is now laid out as ordinary groups and rows.

#include "PmModel.h"

namespace Pm {

// Appends the groups of the inline page for "page / section", if there is one. False when that section
// has no hand-built page (its old window is then drawn on the page as it is).
bool BuildInlinePage(const std::string& page, const std::string& section, std::vector<Group>& groups);
// Number of enabled toggles on that inline page, for the sidebar count.
int CountInlinePage(const std::string& page, const std::string& section);

// Settings > Controls: controller bindings (PmControls.cpp).
void BuildControlsPage(std::vector<Group>& groups);
int CountControlsPage();
// Settings > Input Viewer: input viewer settings (PmInputViewer.cpp).
void BuildInputViewerPage(std::vector<Group>& groups);
int CountInputViewerPage();

} // namespace Pm

#endif // PAUSE_MENU_INLINE_H
