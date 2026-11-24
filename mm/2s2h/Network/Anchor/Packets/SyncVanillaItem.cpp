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
 * Syncs ALL vanilla (non-randomizer) item acquisitions in real time via Item_Give,
 * including enemy/world drops (rupees, hearts, magic jars, arrows, bombs, sticks, nuts, fairies),
 * heart pieces/containers, dungeon keys/maps/compasses/boss keys, skulltula tokens, masks,
 * equipment, songs, bottles, and upgrades.
 */

static bool ShouldSyncVanillaItem(u8 item) {
    if (item == ITEM_NONE || item == ITEM_SHIP) {
        return false;
    }
    if (item >= ITEM_INVALID_1 && item <= ITEM_INVALID_7) {
        return false;
    }
    return true;
}

static bool IsSilentOrSelfFeedbackConsumable(u8 item) {
    return item == ITEM_RECOVERY_HEART || item == ITEM_MAGIC_JAR_SMALL || item == ITEM_MAGIC_JAR_BIG ||
           (item >= ITEM_RUPEE_GREEN && item <= ITEM_RUPEE_HUGE);
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
    payload["sceneId"] = (gPlayState != nullptr) ? gPlayState->sceneId : static_cast<s16>(SCENE_MAX);

    QueueOutgoingPacket(payload);
}

void Anchor::SendPacket_SyncFairyHeal() {
    if (isApplyingRemoteItem || !IsSaveLoaded() || !roomState.syncItemsAndFlags || IS_RANDO) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = SYNC_VANILLA_ITEM;
    payload["fairyHeal"] = true;

    QueueOutgoingPacket(payload);
}

void Anchor::HandlePacket_SyncVanillaItem(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags || IS_RANDO) {
        return;
    }

    if (payload.value("fairyHeal", false)) {
        if (gPlayState != nullptr) {
            Health_ChangeBy(gPlayState, 0x70);
        }
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
    if (item == ITEM_SKULL_TOKEN && payload.contains("sceneId")) {
        s16 senderSceneId = payload["sceneId"].get<s16>();
        SET_QUEST_ITEM(QUEST_QUIVER);
        Inventory_IncrementSkullTokenCount(senderSceneId);
    } else {
        Item_Give(gPlayState, item);
    }
    isApplyingRemoteItem = false;

    gSaveContext.mapIndex = prevMapIndex;
    SnapshotDungeonItems();
    SnapshotProgressState();

    if (!IsSilentOrSelfFeedbackConsumable(item)) {
        Audio_PlaySfx(NA_SE_SY_GET_ITEM);
    }
}
