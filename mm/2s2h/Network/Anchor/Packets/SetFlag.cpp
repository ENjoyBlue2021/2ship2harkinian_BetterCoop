#include "2s2h/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/BenPort.h"

/**
 * SET_FLAG
 *
 * Fired when a flag is set in the save context
 */

void Anchor::SendPacket_SetFlag(s16 sceneId, s16 flagType, s16 flag) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = SET_FLAG;
    payload["targetTeamId"] = CVarGetString("gNetwork.Anchor.TeamId", "default");
    payload["addToQueue"] = true;
    payload["sceneId"] = sceneId;
    payload["flagType"] = flagType;
    payload["flag"] = flag;

    QueueOutgoingPacket(payload);
}

extern "C" {
#include "variables.h"
#include "functions.h"
}

void Anchor::HandlePacket_SetFlag(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    s16 sceneId = payload["sceneId"].get<s16>();
    s16 flagType = payload["flagType"].get<s16>();
    u16 flag = static_cast<u16>(payload["flag"].get<s16>());

    isApplyingRemoteFlag = true;

    if (sceneId == SCENE_MAX) {
        switch (static_cast<FlagType>(flagType)) {
            case FLAG_WEEK_EVENT_REG:
                Flags_SetWeekEventReg(flag);
                break;
            case FLAG_WEEK_EVENT_REG_HORSE_RACE:
                Flags_SetWeekEventRegHorseRace(flag);
                break;
            case FLAG_EVENT_INF:
                Flags_SetEventInf(flag);
                break;
            case FLAG_RANDO_INF:
                Flags_SetRandoInf(static_cast<RandoInf>(flag));
                break;
            default:
                break;
        }
    } else if (sceneId >= 0 && sceneId < ARRAY_COUNT(gSaveContext.cycleSceneFlags)) {
        u32 bit = 1U << (flag & 0x1F);
        bool inCurrentScene =
            (gPlayState != nullptr) &&
            (Play_GetOriginalSceneId(gPlayState->sceneId) == sceneId || gPlayState->sceneId == sceneId);

        switch (static_cast<FlagType>(flagType)) {
            case FLAG_CYCL_SCENE_CHEST:
                gSaveContext.cycleSceneFlags[sceneId].chest |= bit;
                if (inCurrentScene) {
                    gPlayState->actorCtx.sceneFlags.chest |= bit;
                }
                break;
            case FLAG_CYCL_SCENE_SWITCH: {
                u32 wordIdx = (flag & ~0x1F) >> 5;
                if (wordIdx == 0) {
                    gSaveContext.cycleSceneFlags[sceneId].switch0 |= bit;
                } else if (wordIdx == 1) {
                    gSaveContext.cycleSceneFlags[sceneId].switch1 |= bit;
                }
                if (inCurrentScene && wordIdx < ARRAY_COUNT(gPlayState->actorCtx.sceneFlags.switches)) {
                    gPlayState->actorCtx.sceneFlags.switches[wordIdx] |= bit;
                }
                break;
            }
            case FLAG_CYCL_SCENE_CLEARED_ROOM:
                gSaveContext.cycleSceneFlags[sceneId].clearedRoom |= bit;
                if (inCurrentScene) {
                    gPlayState->actorCtx.sceneFlags.clearedRoom |= bit;
                }
                break;
            case FLAG_CYCL_SCENE_COLLECTIBLE: {
                gSaveContext.cycleSceneFlags[sceneId].collectible |= bit;
                u32 wordIdx = (flag & ~0x1F) >> 5;
                if (inCurrentScene && wordIdx < ARRAY_COUNT(gPlayState->actorCtx.sceneFlags.collectible)) {
                    gPlayState->actorCtx.sceneFlags.collectible[wordIdx] |= bit;
                }
                break;
            }
            default:
                break;
        }
    }

    isApplyingRemoteFlag = false;
}
