#ifndef MENU_H
#define MENU_H

#include <libultraship/libultraship.h>
#include "graphic/Fast3D/gfx_rendering_api.h"
#include "MenuTypes.h"

namespace Ship {
uint32_t GetVectorIndexOf(std::vector<std::string>& vector, std::string value);

// Holds the menu's data: pages ("menu entries"), their sidebar sections and the widgets in them.
// Drawing is done by the pause-style menu (SohGui/PauseMenu), which SohMenu feeds from this data.
class Menu : public GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    Menu(const std::string& cVar, const std::string& name, uint8_t searchSidebarIndex_ = 0,
         UIWidgets::Colors menuThemeIndex_ = UIWidgets::Colors::LightBlue);

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
    void Draw() override;
    void UpdateWindowBackendObjects();
    // Colour used by the windows that are still drawn with UIWidgets (the former pop-outs).
    UIWidgets::Colors GetMenuThemeColor();

    void AddMenuEntry(std::string entryName, const char* entryCvar);
    std::unordered_map<uint32_t, disabledInfo>& GetDisabledMap();

  protected:
    ImVec2 mOriginalSize;
    std::string mName;
    uint32_t mWindowFlags;
    std::unordered_map<std::string, MainMenuEntry> menuEntries;
    std::vector<std::string> menuOrder;
    UIWidgets::Colors defaultThemeIndex;
    std::shared_ptr<std::vector<Ship::WindowBackend>> availableWindowBackends;
    std::unordered_map<Ship::WindowBackend, const char*> availableWindowBackendsMap;
    Ship::WindowBackend configWindowBackend;

    std::unordered_map<uint32_t, disabledInfo> disabledMap;
    std::vector<disabledInfo> disabledVector;
};
} // namespace Ship

#endif // MENU_H
