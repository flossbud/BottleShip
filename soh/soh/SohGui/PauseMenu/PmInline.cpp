#include "PmInline.h"

namespace Pm {

bool BuildInlinePage(const std::string& page, const std::string& section, std::vector<Group>& groups) {
    if (page != "Settings") {
        return false;
    }
    if (section == "Controls") {
        BuildControlsPage(groups);
        return true;
    }
    if (section == "Input Viewer") {
        BuildInputViewerPage(groups);
        return true;
    }
    return false;
}

int CountInlinePage(const std::string& page, const std::string& section) {
    if (page != "Settings") {
        return 0;
    }
    if (section == "Controls") {
        return CountControlsPage();
    }
    if (section == "Input Viewer") {
        return CountInputViewerPage();
    }
    return 0;
}

} // namespace Pm
