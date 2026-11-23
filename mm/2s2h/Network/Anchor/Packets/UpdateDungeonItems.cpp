#include "2s2h/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/BenPort.h"

#include <cstring>

extern "C" {
#include "variables.h"
}

/**
 * UPDATE_DUNGEON_ITEMS
 *
 * Syncs dungeon items (map, compass, boss key), small keys (both gained and spent on locked doors),
 * and stray fairies across all dungeon indices in real time.
 */

namespace {
constexpr size_t kDungeonCount = ARRAY_COUNT(gSaveContext.save.saveInfo.inventory.dungeonItems);
static u8 sLastDungeonItems[kDungeonCount] = {};
static s8 sLastDungeonKeys[kDungeonCount] = {};
static s8 sLastStrayFairies[kDungeonCount] = {};
static bool sDungeonTrackerInitialized = false;
} // namespace

void Anchor::ResetDungeonItemSyncTracker() {
    sDungeonTrackerInitialized = false;
}

void Anchor::SnapshotDungeonItems() {
    if (!IsSaveLoaded()) {
        sDungeonTrackerInitialized = false;
        return;
    }
    std::memcpy(sLastDungeonItems, gSaveContext.save.saveInfo.inventory.dungeonItems, sizeof(sLastDungeonItems));
    std::memcpy(sLastDungeonKeys, gSaveContext.save.saveInfo.inventory.dungeonKeys, sizeof(sLastDungeonKeys));
    std::memcpy(sLastStrayFairies, gSaveContext.save.saveInfo.inventory.strayFairies, sizeof(sLastStrayFairies));
    sDungeonTrackerInitialized = true;
}

void Anchor::TickDungeonItemSync() {
    if (!isConnected || !IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        sDungeonTrackerInitialized = false;
        return;
    }

    if (!sDungeonTrackerInitialized) {
        SnapshotDungeonItems();
        return;
    }

    for (u16 i = 0; i < kDungeonCount; i++) {
        u8 curItems = gSaveContext.save.saveInfo.inventory.dungeonItems[i];
        s8 curKeys = gSaveContext.save.saveInfo.inventory.dungeonKeys[i];
        s8 curFairies = gSaveContext.save.saveInfo.inventory.strayFairies[i];

        if (curItems != sLastDungeonItems[i] || curKeys != sLastDungeonKeys[i] ||
            curFairies != sLastStrayFairies[i]) {
            sLastDungeonItems[i] = curItems;
            sLastDungeonKeys[i] = curKeys;
            sLastStrayFairies[i] = curFairies;
            SendPacket_UpdateDungeonItems(i);
        }
    }
}

void Anchor::SendPacket_UpdateDungeonItems(u16 mapIndex) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags || mapIndex >= kDungeonCount) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = UPDATE_DUNGEON_ITEMS;
    payload["targetTeamId"] = CVarGetString("gNetwork.Anchor.TeamId", "default");
    payload["addToQueue"] = true;
    payload["mapIndex"] = mapIndex;
    payload["dungeonItems"] = gSaveContext.save.saveInfo.inventory.dungeonItems[mapIndex];
    payload["dungeonKeys"] = gSaveContext.save.saveInfo.inventory.dungeonKeys[mapIndex];
    payload["strayFairies"] = gSaveContext.save.saveInfo.inventory.strayFairies[mapIndex];

    QueueOutgoingPacket(payload);
}

void Anchor::SendPacket_UpdateDungeonItems() {
    SendPacket_UpdateDungeonItems(static_cast<u16>(gSaveContext.mapIndex));
}

void Anchor::HandlePacket_UpdateDungeonItems(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    u16 mapIndex = payload["mapIndex"].get<u16>();
    if (mapIndex >= kDungeonCount) {
        return;
    }

    u8 items = payload["dungeonItems"].get<u8>();
    s8 keys = payload["dungeonKeys"].get<s8>();
    s8 fairies = payload["strayFairies"].get<s8>();

    gSaveContext.save.saveInfo.inventory.dungeonItems[mapIndex] = items;
    gSaveContext.save.saveInfo.inventory.dungeonKeys[mapIndex] = keys;
    gSaveContext.save.saveInfo.inventory.strayFairies[mapIndex] = fairies;

    if (sDungeonTrackerInitialized) {
        sLastDungeonItems[mapIndex] = items;
        sLastDungeonKeys[mapIndex] = keys;
        sLastStrayFairies[mapIndex] = fairies;
    }
}
