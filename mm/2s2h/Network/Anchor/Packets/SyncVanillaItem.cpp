#include "2s2h/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "2s2h/BenGui/Notification.h"
#include "2s2h/Rando/Rando.h"

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * SYNC_VANILLA_ITEM
 *
 * Syncs vanilla (non-randomizer) item acquisitions in real time via Item_Give.
 * Skips pure consumables (hearts, rupees, ammo drops) so players keep independent refills
 * while sharing masks, equipment, songs, bottles, upgrades, heart pieces/containers, and dungeon items.
 */

static bool ShouldSyncVanillaItem(u8 item) {
    if (item == ITEM_NONE || item == ITEM_SHIP) {
        return false;
    }
    // Skip magic jars, invalid IDs, recovery hearts, rupees, and raw ammo refills
    if (item == ITEM_MAGIC_JAR_SMALL || item == ITEM_MAGIC_JAR_BIG) {
        return false;
    }
    if (item >= ITEM_INVALID_1 && item <= ITEM_BOMBCHUS_5) {
        return false;
    }
    return true;
}

void Anchor::SendPacket_SyncVanillaItem(u8 item) {
    if (isApplyingRemoteItem || !IsSaveLoaded() || !roomState.syncItemsAndFlags || IS_RANDO) {
        return;
    }

    if (!ShouldSyncVanillaItem(item)) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = SYNC_VANILLA_ITEM;
    payload["item"] = item;
    payload["mapIndex"] = gSaveContext.mapIndex;

    QueueOutgoingPacket(payload);
}

void Anchor::HandlePacket_SyncVanillaItem(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags || IS_RANDO) {
        return;
    }

    u8 item = payload.value("item", static_cast<u8>(ITEM_NONE));
    if (!ShouldSyncVanillaItem(item)) {
        return;
    }

    s32 prevMapIndex = gSaveContext.mapIndex;
    if (payload.contains("mapIndex")) {
        gSaveContext.mapIndex = payload["mapIndex"].get<s32>();
    }

    isApplyingRemoteItem = true;
    Item_Give(gPlayState, item);
    isApplyingRemoteItem = false;

    gSaveContext.mapIndex = prevMapIndex;
}
