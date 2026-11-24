#include "2s2h/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/BenPort.h"

#include <cstring>

extern "C" {
#include "variables.h"
#include "functions.h"
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

namespace {
static u32 sLastQuestItems = 0;
static u16 sLastOwlFlags = 0;
static u8 sLastWeekEventReg[ARRAY_COUNT(gSaveContext.save.saveInfo.weekEventReg)] = {};
static u8 sLastEventInf[ARRAY_COUNT(gSaveContext.eventInf)] = {};
static bool sProgressInitialized = false;
} // namespace

void Anchor::ResetProgressSyncTracker() {
    sProgressInitialized = false;
}

void Anchor::SnapshotProgressState() {
    if (!IsSaveLoaded()) {
        sProgressInitialized = false;
        return;
    }
    sLastQuestItems = gSaveContext.save.saveInfo.inventory.questItems;
    sLastOwlFlags = gSaveContext.save.saveInfo.playerData.owlActivationFlags;
    std::memcpy(sLastWeekEventReg, gSaveContext.save.saveInfo.weekEventReg, sizeof(sLastWeekEventReg));
    std::memcpy(sLastEventInf, gSaveContext.eventInf, sizeof(sLastEventInf));
    sProgressInitialized = true;
}

void Anchor::TickProgressSync() {
    if (!isConnected || !IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        sProgressInitialized = false;
        return;
    }

    if (!sProgressInitialized) {
        SnapshotProgressState();
        return;
    }

    u32 curQuest = gSaveContext.save.saveInfo.inventory.questItems;
    u32 newQuestBits = curQuest & ~sLastQuestItems;

    u16 curOwl = gSaveContext.save.saveInfo.playerData.owlActivationFlags;
    u16 newOwlBits = curOwl & ~sLastOwlFlags;

    nlohmann::json weekRegDiffs = nlohmann::json::array();
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.save.saveInfo.weekEventReg); i++) {
        u8 cur = gSaveContext.save.saveInfo.weekEventReg[i];
        u8 prev = sLastWeekEventReg[i];
        if (cur != prev) {
            weekRegDiffs.push_back({ i, static_cast<u8>(cur & ~prev), static_cast<u8>(prev & ~cur) });
            sLastWeekEventReg[i] = cur;
        }
    }

    nlohmann::json eventInfDiffs = nlohmann::json::array();
    for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.eventInf); i++) {
        u8 cur = gSaveContext.eventInf[i];
        u8 prev = sLastEventInf[i];
        if (cur != prev) {
            eventInfDiffs.push_back({ i, static_cast<u8>(cur & ~prev), static_cast<u8>(prev & ~cur) });
            sLastEventInf[i] = cur;
        }
    }

    sLastQuestItems = curQuest;
    sLastOwlFlags = curOwl;

    if (newQuestBits == 0 && newOwlBits == 0 && weekRegDiffs.empty() && eventInfDiffs.empty()) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = SYNC_PROGRESS;
    payload["targetTeamId"] = CVarGetString("gNetwork.Anchor.TeamId", "default");
    payload["addToQueue"] = true;
    if (newQuestBits != 0) {
        payload["questBits"] = newQuestBits;
    }
    if (newOwlBits != 0) {
        payload["owlBits"] = newOwlBits;
    }
    if (!weekRegDiffs.empty()) {
        payload["weekReg"] = weekRegDiffs;
    }
    if (!eventInfDiffs.empty()) {
        payload["eventInf"] = eventInfDiffs;
    }

    QueueOutgoingPacket(payload);
}

void Anchor::HandlePacket_SyncProgress(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    u32 questBits = payload.value("questBits", static_cast<u32>(0));
    if (questBits != 0) {
        u32 newlyAdded = questBits & ~gSaveContext.save.saveInfo.inventory.questItems;
        gSaveContext.save.saveInfo.inventory.questItems |= questBits;
        if (newlyAdded != 0) {
            Audio_PlaySfx(NA_SE_SY_GET_ITEM);
        }
    }

    u16 owlBits = payload.value("owlBits", static_cast<u16>(0));
    if (owlBits != 0) {
        gSaveContext.save.saveInfo.playerData.owlActivationFlags |= owlBits;
    }

    if (payload.contains("weekReg") && payload["weekReg"].is_array()) {
        for (const auto& entry : payload["weekReg"]) {
            if (entry.is_array() && entry.size() >= 3) {
                size_t idx = entry[0].get<size_t>();
                u8 setMask = entry[1].get<u8>();
                u8 clearMask = entry[2].get<u8>();
                if (idx < ARRAY_COUNT(gSaveContext.save.saveInfo.weekEventReg)) {
                    gSaveContext.save.saveInfo.weekEventReg[idx] =
                        (gSaveContext.save.saveInfo.weekEventReg[idx] | setMask) & ~clearMask;
                }
            }
        }
    }

    if (payload.contains("eventInf") && payload["eventInf"].is_array()) {
        for (const auto& entry : payload["eventInf"]) {
            if (entry.is_array() && entry.size() >= 3) {
                size_t idx = entry[0].get<size_t>();
                u8 setMask = entry[1].get<u8>();
                u8 clearMask = entry[2].get<u8>();
                if (idx < ARRAY_COUNT(gSaveContext.eventInf)) {
                    gSaveContext.eventInf[idx] = (gSaveContext.eventInf[idx] | setMask) & ~clearMask;
                }
            }
        }
    }

    SnapshotProgressState();
}
