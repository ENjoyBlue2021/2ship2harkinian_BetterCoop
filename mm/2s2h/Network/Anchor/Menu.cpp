#include "Anchor.h"
#include <libultraship/libultraship.h>
#include "2s2h/BenGui/UIWidgets.hpp"
#include "ShipUtils.h"
#include "2s2h/BenGui/BenMenu.h"
#include "2s2h/BenGui/BenGui.hpp"
#include "2s2h/BenGui/Notification.h"
#include "2s2h/Network/Sail/Sail.h"
#include "2s2h/Rando/Spoiler/Spoiler.h"
#include "2s2h/Rando/MiscBehavior/MiscBehavior.h"

namespace BenGui {
extern std::shared_ptr<BenMenu> mBenMenu;
extern std::shared_ptr<AnchorRoomWindow> mAnchorRoomWindow;
} // namespace BenGui
using namespace UIWidgets;

static const char* pvpModes[3] = { "Off", "On", "On + Friendly Fire" };
static std::vector<const char*> teleportModes = { "None", "Team Only", "All" };
static std::vector<const char*> showLocationsModes = { "None", "Team Only", "All" };

static const char* anchorConnectionModes[3] = { "Host LAN", "Join LAN", "Online" };

void AnchorMainMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    int mode = CVarGetInteger("gNetwork.Anchor.Mode", 0);
    if (mode < 0 || mode > 2) {
        mode = 0;
    }
    std::string host = CVarGetString("gNetwork.Anchor.Host", "anchor.hm64.org");
    std::string lanHostIp = CVarGetString("gNetwork.Anchor.LanHostIp", "192.168.1.100");
    uint16_t port = CVarGetInteger("gNetwork.Anchor.Port", 43383);
    std::string anchorTeamId = CVarGetString("gNetwork.Anchor.TeamId", "default");
    std::string anchorRoomId = CVarGetString("gNetwork.Anchor.RoomId", mode == 2 ? "" : "lan");
    std::string anchorName = CVarGetString("gNetwork.Anchor.Name", "");

    bool isPortValid = (port > 1024 && port < 65535);
    bool isFormValid = false;
    if (mode == 0) {
        isFormValid = isPortValid && !IsStringEmpty(anchorName);
    } else if (mode == 1) {
        isFormValid = !IsStringEmpty(lanHostIp) && isPortValid && !IsStringEmpty(anchorName);
    } else {
        isFormValid = !IsStringEmpty(host) && isPortValid && !IsStringEmpty(anchorRoomId) && !IsStringEmpty(anchorName);
    }

    ImGui::SeparatorText("Connection Settings");

    ImGui::BeginDisabled(anchor->isEnabled);
    ImGui::Text("Connection Mode");
    for (int i = 0; i < 3; i++) {
        if (i > 0) {
            ImGui::SameLine();
        }
        bool selected = (mode == i);
        if (selected) {
            UIWidgets::PushStyleButton(THEME_COLOR);
        }
        std::string modeBtnLabel = std::string(anchorConnectionModes[i]) + "##ModeBtn";
        if (ImGui::Button(modeBtnLabel.c_str(),
                          ImVec2((ImGui::GetContentRegionAvail().x - (ImGui::GetStyle().ItemSpacing.x * (2 - i))) /
                                     (3 - i),
                                 0))) {
            CVarSetInteger("gNetwork.Anchor.Mode", i);
            if (i != 2 && IsStringEmpty(CVarGetString("gNetwork.Anchor.RoomId", ""))) {
                CVarSetString("gNetwork.Anchor.RoomId", "lan");
            }
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        if (selected) {
            UIWidgets::PopStyleButton();
        }
    }

    if (mode == 0) {
        static std::string cachedLanIp = LanHostServer::GetLocalLanIp();
        ImGui::Text("Your LAN IP: %s", cachedLanIp.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            cachedLanIp = LanHostServer::GetLocalLanIp();
        }
        ImGui::Text("Host Port");
        UIWidgets::PushStyleInput(THEME_COLOR);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputScalar("##Port", ImGuiDataType_U16, &port)) {
            CVarSetInteger("gNetwork.Anchor.Port", port);
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        UIWidgets::PopStyleInput();
    } else if (mode == 1) {
        ImGui::Text("Host LAN IP & Port");
        if (UIWidgets::InputString("##LanHostIp", &lanHostIp,
                                   UIWidgets::InputOptions()
                                       .Size(ImGui::GetContentRegionAvail() -
                                             ImVec2((ImGui::GetFontSize() * 5 + ImGui::GetStyle().ItemSpacing.x), 0))
                                       .Color(THEME_COLOR))) {
            CVarSetString("gNetwork.Anchor.LanHostIp", lanHostIp.c_str());
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }

        ImGui::SameLine();
        UIWidgets::PushStyleInput(THEME_COLOR);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
        if (ImGui::InputScalar("##Port", ImGuiDataType_U16, &port)) {
            CVarSetInteger("gNetwork.Anchor.Port", port);
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        UIWidgets::PopStyleInput();
    } else {
        ImGui::Text("Host & Port");
        if (UIWidgets::InputString("##Host", &host,
                                   UIWidgets::InputOptions()
                                       .Size(ImGui::GetContentRegionAvail() -
                                             ImVec2((ImGui::GetFontSize() * 5 + ImGui::GetStyle().ItemSpacing.x), 0))
                                       .Color(THEME_COLOR))) {
            CVarSetString("gNetwork.Anchor.Host", host.c_str());
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }

        ImGui::SameLine();
        UIWidgets::PushStyleInput(THEME_COLOR);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
        if (ImGui::InputScalar("##Port", ImGuiDataType_U16, &port)) {
            CVarSetInteger("gNetwork.Anchor.Port", port);
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        UIWidgets::PopStyleInput();
    }

    ImGui::Text("Player Name");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##Name", &anchorName, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString("gNetwork.Anchor.Name", anchorName.c_str());
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    if (mode == 2) {
        ImGui::Text("Room ID");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (UIWidgets::InputString("##RoomId", &anchorRoomId,
                                   UIWidgets::InputOptions().IsSecret(anchor->isEnabled).Color(THEME_COLOR))) {
            CVarSetString("gNetwork.Anchor.RoomId", anchorRoomId.c_str());
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        ImGui::Text("Team ID (Items Shared)");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (UIWidgets::InputString("##TeamId", &anchorTeamId, UIWidgets::InputOptions().Color(THEME_COLOR))) {
            CVarSetString("gNetwork.Anchor.TeamId", anchorTeamId.c_str());
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        ImGui::Spacing();

        if (UIWidgets::Button("Restore Defaults", UIWidgets::ButtonOptions()
                                                      .Size(ImVec2(ImGui::GetContentRegionAvail().x / 2, 0))
                                                      .Color(UIWidgets::Colors::Red))) {
            CVarSetString("gNetwork.Anchor.Host", "anchor.hm64.org");
            CVarSetInteger("gNetwork.Anchor.Port", 43383);
            CVarSetString("gNetwork.Anchor.TeamId", "default");
            CVarSetString("gNetwork.Anchor.RoomId", "");
            CVarSetString("gNetwork.Anchor.Name", "");
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }

        ImGui::SameLine();

        if (UIWidgets::Button("Global Room", UIWidgets::ButtonOptions()
                                                 .Color(UIWidgets::Colors::Blue)
                                                 .Tooltip("Always-online public room so you don't have to experience "
                                                          "Termina alone. PVP and syncing are disabled."))) {
            CVarSetString("gNetwork.Anchor.Host", "anchor.hm64.org");
            CVarSetInteger("gNetwork.Anchor.Port", 43383);
            CVarSetString("gNetwork.Anchor.TeamId", "default");
            CVarSetString("gNetwork.Anchor.RoomId", "2ship-global");
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }

    ImGui::EndDisabled();

    ImGui::Spacing();

    ImGui::BeginDisabled(!isFormValid);
    const char* buttonLabel = anchor->isEnabled
                                  ? (mode == 0 ? "Stop Hosting" : "Disconnect")
                                  : (mode == 0 ? "Host LAN" : (mode == 1 ? "Join LAN" : "Connect"));
    UIWidgets::PushStyleButton(anchor->isEnabled ? UIWidgets::ColorValues.at(UIWidgets::Colors::Red)
                                                 : UIWidgets::ColorValues.at(UIWidgets::Colors::Green));
    std::string connectBtnId = std::string(buttonLabel) + "##ConnectBtn";
    if (ImGui::Button(connectBtnId.c_str(), ImVec2(-1.0f, 0.0f))) {
        if (anchor->isEnabled) {
            CVarClear("gNetwork.Anchor.Enabled");
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->Disable();
        } else {
            if (mode != 2) {
                CVarSetString("gNetwork.Anchor.RoomId", "lan");
                CVarSetString("gNetwork.Anchor.TeamId", "default");
            }
            CVarSetInteger("gNetwork.Anchor.Enabled", 1);
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->Enable();
        }
    }
    UIWidgets::PopStyleButton();
    ImGui::EndDisabled();
    ImGui::Spacing();

    if (!anchor->isEnabled) {
        return;
    }

    if (!anchor->isConnected) {
        ImGui::Text("Connecting...");
        if (mode == 0 && !anchor->lanServer.GetLastError().empty()) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", anchor->lanServer.GetLastError().c_str());
        }
        return;
    }

    ImGui::SeparatorText("Current Session");
    if (anchor->lanServer.IsRunning()) {
        ImGui::Text("%s Hosting LAN on %s:%u", ICON_FA_CHECK, LanHostServer::GetLocalLanIp().c_str(),
                    anchor->lanServer.GetPort());
    } else {
        ImGui::Text("%s Connected (%s)", ICON_FA_CHECK, anchor->IsHost() ? "Host" : "Client");
    }

    UIWidgets::PushStyleButton(THEME_COLOR);
    if (anchor->IsHost()) {
        if (ImGui::Button("Push State to Client")) {
            anchor->SendPacket_UpdateTeamState(CVarGetString("gNetwork.Anchor.TeamId", "default"));
            anchor->SendPacket_SyncTime();
        }
        UIWidgets::Tooltip("Overwrite the client's save state and time with yours");
    } else {
        if (ImGui::Button("Sync Full State from Host")) {
            anchor->SendPacket_RequestTeamState();
        }
        UIWidgets::Tooltip("Pull the Host's current save state, items, flags, and time");
    }
    UIWidgets::PopStyleButton();

    ImGui::SameLine();

    UIWidgets::WindowButton("Toggle Anchor Room Window", "gWindows.AnchorRoom", BenGui::mAnchorRoomWindow);
    if (!BenGui::mAnchorRoomWindow->IsVisible()) {
        BenGui::mAnchorRoomWindow->DrawElement();
    }

    // Quick Teleport controls
    bool hasRemotePlayer = false;
    for (auto& [clientId, client] : anchor->clients) {
        if (client.self || !client.online) {
            continue;
        }
        if (!hasRemotePlayer) {
            ImGui::SeparatorText("Quick Teleport");
            hasRemotePlayer = true;
        }
        if (anchor->CanTeleportTo(clientId)) {
            ImGui::PushID(static_cast<int>(clientId));
            std::string tpLabel = "Teleport " + anchorName + " -> " + client.name;
            if (ImGui::Button(tpLabel.c_str())) {
                anchor->SendPacket_RequestTeleport(clientId);
            }
            ImGui::SameLine();
            std::string summonLabel = "Teleport " + client.name + " -> " + anchorName;
            if (ImGui::Button(summonLabel.c_str())) {
                anchor->SendPacket_SummonPlayer(clientId);
            }
            ImGui::PopID();
        }
    }
}

void AnchorAdminMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;
    bool isGlobalRoom = (std::string("2ship-global") == CVarGetString("gNetwork.Anchor.RoomId", ""));

    if (!anchor->isEnabled || !anchor->isConnected || anchor->roomState.ownerClientId != anchor->ownClientId ||
        isGlobalRoom) {
        return;
    }

    ImGui::SeparatorText("Room Settings (Admin Only)");

    UIWidgets::PushStyleButton(THEME_COLOR);
    if (ImGui::Button("Clear All Team State")) {
        std::set<std::string> teams;
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            teams.insert(client.teamId);
        }
        for (auto& team : teams) {
            anchor->SendPacket_ClearTeamState(team);
        }
    }
    UIWidgets::PopStyleButton();

    if (UIWidgets::CVarCombobox("PvP Mode:", "gNetwork.Anchor.RoomSettings.PvpMode", pvpModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(2)
                                    .LabelPosition(UIWidgets::LabelPosition::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCombobox("Show Locations For:", "gNetwork.Anchor.RoomSettings.ShowLocationsMode",
                                showLocationsModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPosition::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCombobox("Allow Teleporting To:", "gNetwork.Anchor.RoomSettings.TeleportMode", teleportModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPosition::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Sync Items & Flags", "gNetwork.Anchor.RoomSettings.SyncItemsAndFlags",
                                UIWidgets::CheckboxOptions().DefaultValue(true).Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
}

void AnchorInstructionsMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    ImGui::TextWrapped(
        "Important Notes:\n- All players involved should start at the file select screen with Anchor disabled.\n- "
        "Archipelago and Anchor can be used together, but make sure you disable item syncing in Anchor!");

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    ImGui::SeparatorText("Co-op Usage Instructions");

    ImGui::TextWrapped("1. The host player should set up a room by entering a Room ID and enable Anchor. (If just one "
                       "team, leave the team as 'default')");

    ImGui::TextWrapped("2. Configure the room settings as desired (PvP, teleporting, etc).");

    ImGui::TextWrapped("3. If multiple teams, other team leaders should enter the same Room ID and their associated "
                       "Team IDs and enable Anchor.");

    ImGui::TextWrapped("4. Host/Team leaders should then create a randomizer seed like normal, and load into the game. "
                       "This can match across teams but doesn't have to.");

    ImGui::TextWrapped("4. Once the Host/Team leaders are in-game, other players can enter the same Room ID and "
                       "associated Team IDs and enable Anchor.");

    ImGui::TextWrapped("5. Other players should then each create a new randomizer file (the configuration does not "
                       "matter, it will be overridden by the team leader's settings), and load into the game.");

    ImGui::TextWrapped("6. All players should now be connected and able to see each other in-game, occasionally you "
                       "may need to leave and re-enter South clock town if a player does not appear.");
}

void RegisterAnchorMenu() {
    // Add Network Menu
    BenGui::mBenMenu->AddMenuEntry("Network", "gSettings.Menu.NetworkSidebarSection");

    BenGui::mBenMenu->AddSidebarEntry("Network", "Anchor", 2);
    WidgetPath path = { "Network", "Anchor", SECTION_COLUMN_1 };
    BenGui::mBenMenu->AddWidget(path, "AnchorMainMenu", WIDGET_CUSTOM).CustomFunction(AnchorMainMenu);
    path.column = SECTION_COLUMN_2;
    BenGui::mBenMenu->AddWidget(path, "AnchorAdminMenu", WIDGET_CUSTOM).CustomFunction(AnchorAdminMenu);
    BenGui::mBenMenu->AddWidget(path, "AnchorInstructionsMenu", WIDGET_CUSTOM).CustomFunction(AnchorInstructionsMenu);
}

static RegisterMenuInitFunc menuInitFunc(RegisterAnchorMenu);
