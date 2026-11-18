#ifdef ENABLE_REMOTE_CONTROL
#include "SohMenu.h"
#include <soh/Notification/Notification.h>
#include <soh/Network/Network.h>
#include "SohGui.hpp"
#include <soh/Network/Sail/Sail.h>
#include <soh/Network/CrowdControl/CrowdControl.h>

namespace SohGui {

extern std::shared_ptr<SohMenu> mSohMenu;
using namespace UIWidgets;

void SohMenu::AddMenuNetwork() {
    // Add Network Menu
    AddMenuEntry("Network", CVAR_SETTING("Menu.NetworkSidebarSection"));

    // Sail
    WidgetPath path = { "Network", "Sail", SECTION_COLUMN_1 };
    AddSidebarEntry("Network", path.sidebarName, 3);

    AddWidget(path,
              "Sail is a networking protocol designed to facilitate remote "
              "control of the Ship of Harkinian client. It is intended to "
              "be utilized alongside a Sail server, for which we provide a "
              "few straightforward implementations on our GitHub. The current "
              "implementations available allow integration with Twitch chat "
              "and SAMMI Bot, feel free to contribute your own!\n"
              "\n"
              "Click this button to copy the link to the Sail Github "
              "page to your clipboard.",
              WIDGET_TEXT);
    AddWidget(path, "Copy Link##Sail", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            ImGui::SetClipboardText("https://github.com/HarbourMasters/sail");
            Notification::Emit({
                .message = "Copied to clipboard",
            });
        })
        .Options(ButtonOptions().Tooltip("https://github.com/HarbourMasters/sail"));
    AddWidget(path, "Host & Port##Sail", WIDGET_CVAR_HOST_PORT)
        .HostPort(CVAR_REMOTE_SAIL("Host"), "127.0.0.1", CVAR_REMOTE_SAIL("Port"), 43384)
        .PreFunc([](WidgetInfo& info) { info.options->disabled = Sail::Instance->isEnabled; });
    AddWidget(path, "Enable##Sail", WIDGET_BUTTON)
        .PreFunc([](WidgetInfo& info) {
            std::string host = CVarGetString(CVAR_REMOTE_SAIL("Host"), "127.0.0.1");
            uint16_t port = CVarGetInteger(CVAR_REMOTE_SAIL("Port"), 43384);
            info.options->disabled = !(!SohUtils::IsStringEmpty(host) && port > 1024 && port < 65535);
            if (Sail::Instance->isEnabled) {
                info.name = "Disable##Sail";
            } else {
                info.name = "Enable##Sail";
            }
        })
        .Callback([](WidgetInfo& info) {
            if (Sail::Instance->isEnabled) {
                CVarClear(CVAR_REMOTE_SAIL("Enabled"));
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
                Sail::Instance->Disable();
            } else {
                CVarSetInteger(CVAR_REMOTE_SAIL("Enabled"), 1);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
                Sail::Instance->Enable();
            }
        });
    AddWidget(path, "Connecting...##Sail", WIDGET_TEXT).PreFunc([](WidgetInfo& info) {
        info.isHidden = !Sail::Instance->isEnabled;
        if (Sail::Instance->isConnected) {
            info.name = "Connected##Sail";
        } else {
            info.name = "Connecting...##Sail";
        }
    });

    path.sidebarName = "Crowd Control";
    AddSidebarEntry("Network", path.sidebarName, 3);
    path.column = SECTION_COLUMN_1;

    AddWidget(path,
              "Crowd Control is a platform that allows viewers to interact "
              "with a streamer's game in real time.\n"
              "\n"
              "Click this button to copy the link to the Crowd Control "
              "website to your clipboard.",
              WIDGET_TEXT);
    AddWidget(path, "Copy Link##CrowdControl", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) {
            ImGui::SetClipboardText("https://crowdcontrol.live");
            Notification::Emit({
                .message = "Copied to clipboard",
            });
        })
        .Options(ButtonOptions().Tooltip("https://crowdcontrol.live"));
    AddWidget(path, "Host & Port##CrowdControl", WIDGET_CVAR_HOST_PORT)
        .HostPort(CVAR_REMOTE_CROWD_CONTROL("Host"), "127.0.0.1", CVAR_REMOTE_CROWD_CONTROL("Port"), 43384)
        .PreFunc([](WidgetInfo& info) { info.options->disabled = CrowdControl::Instance->isEnabled; });
    AddWidget(path, "Enable##CrowdControl", WIDGET_BUTTON)
        .PreFunc([](WidgetInfo& info) {
            std::string host = CVarGetString(CVAR_REMOTE_CROWD_CONTROL("Host"), "127.0.0.1");
            uint16_t port = CVarGetInteger(CVAR_REMOTE_CROWD_CONTROL("Port"), 43384);
            info.options->disabled = !(!SohUtils::IsStringEmpty(host) && port > 1024 && port < 65535);
            if (CrowdControl::Instance->isEnabled) {
                info.name = "Disable##CrowdControl";
            } else {
                info.name = "Enable##CrowdControl";
            }
        })
        .Callback([](WidgetInfo& info) {
            if (CrowdControl::Instance->isEnabled) {
                CVarClear(CVAR_REMOTE_CROWD_CONTROL("Enabled"));
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
                CrowdControl::Instance->Disable();
            } else {
                CVarSetInteger(CVAR_REMOTE_CROWD_CONTROL("Enabled"), 1);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
                CrowdControl::Instance->Enable();
            }
        });
    AddWidget(path, "Connecting...##CrowdControl", WIDGET_TEXT).PreFunc([](WidgetInfo& info) {
        info.isHidden = !CrowdControl::Instance->isEnabled;
        if (CrowdControl::Instance->isConnected) {
            info.name = "Connected##CrowdControl";
        } else {
            info.name = "Connecting...##CrowdControl";
        }
    });
}

} // namespace SohGui
#endif