#include "2s2h/Network/Anchor/Anchor.h"
#include "2s2h/BenGui/Notification.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * HEAL_PLAYER
 *
 * Allows any connected player to send a one-time full heart refill to another player.
 */

void Anchor::SendPacket_HealPlayer(uint32_t targetClientId) {
    nlohmann::json payload;
    payload["type"] = HEAL_PLAYER;
    payload["targetClientId"] = targetClientId;

    QueueOutgoingPacket(payload);
}

void Anchor::HandlePacket_HealPlayer(nlohmann::json payload) {
    if (!IsSaveLoaded()) {
        return;
    }

    Health_ChangeBy(gPlayState, gSaveContext.save.saveInfo.playerData.healthCapacity);

    std::string senderName = "Partner";
    if (payload.contains("clientId") && payload["clientId"].is_number_unsigned()) {
        uint32_t senderId = payload["clientId"].get<uint32_t>();
        auto it = clients.find(senderId);
        if (it != clients.end() && !it->second.name.empty()) {
            senderName = it->second.name;
        }
    }

    Notification::Emit({
        .prefix = senderName,
        .message = "healed you!",
    });
}
