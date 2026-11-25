#include "2s2h/Network/Anchor/Anchor.h"
#include "2s2h/Network/Anchor/JsonConversions.hpp"
#include "2s2h/ObjectExtension/ActorListIndex.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include <map>
#include <set>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "overlays/actors/ovl_Obj_Syokudai/z_obj_syokudai.h"
extern PlayState* gPlayState;
}

/**
 * TORCH_LIT
 *
 * Syncs individual torch lighting (ObjSyokudai) between co-op partners in the same scene,
 * including timed group torches whose switch flag only sets once all torches in the group are lit.
 */

namespace {
static std::map<const Actor*, s16> sTorchLastSnuff;
static std::set<const Actor*> sTorchRemoteLit;
} // namespace

void Anchor::ResetTorchSyncState() {
    sTorchLastSnuff.clear();
    sTorchRemoteLit.clear();
}

void Anchor::OnTorchActorUpdate(Actor* actor) {
    if (!isConnected || !IsSaveLoaded() || !roomState.syncItemsAndFlags || gPlayState == nullptr || actor == nullptr ||
        actor->id != ACTOR_OBJ_SYOKUDAI) {
        return;
    }

    ObjSyokudai* torch = (ObjSyokudai*)actor;
    auto it = sTorchLastSnuff.find(actor);
    if (it == sTorchLastSnuff.end()) {
        sTorchLastSnuff[actor] = torch->snuffTimer;
        return;
    }

    s16 prev = it->second;
    s16 cur = torch->snuffTimer;
    it->second = cur;

    if (prev == OBJ_SYOKUDAI_SNUFF_OUT && cur != OBJ_SYOKUDAI_SNUFF_OUT) {
        if (sTorchRemoteLit.erase(actor) > 0) {
            return;
        }
        SendPacket_TorchLit(actor);
    }
}

void Anchor::SendPacket_TorchLit(Actor* actor) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags || gPlayState == nullptr || actor == nullptr) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = TORCH_LIT;
    payload["quiet"] = true;
    payload["sceneId"] = gPlayState->sceneId;
    payload["rm"] = actor->room;
    payload["li"] = GetActorListIndex(actor);
    payload["pos"] = actor->world.pos;

    for (auto& [clientId, client] : clients) {
        if (!client.self && client.online && client.isSaveLoaded && client.sceneId == gPlayState->sceneId) {
            payload["targetClientId"] = clientId;
            QueueOutgoingPacket(payload);
        }
    }
}

void Anchor::HandlePacket_TorchLit(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags || gPlayState == nullptr) {
        return;
    }

    s16 sceneId = payload.value("sceneId", static_cast<s16>(SCENE_MAX));
    if (sceneId != gPlayState->sceneId) {
        return;
    }

    Vec3f worldPos = payload.value("pos", Vec3f{ 0.0f, 0.0f, 0.0f });

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_PROP].first;
    while (actor != nullptr) {
        if (actor->update != nullptr && actor->id == ACTOR_OBJ_SYOKUDAI) {
            f32 dx = actor->world.pos.x - worldPos.x;
            f32 dy = actor->world.pos.y - worldPos.y;
            f32 dz = actor->world.pos.z - worldPos.z;
            f32 distSq = (dx * dx) + (dy * dy) + (dz * dz);
            if (distSq < (20.0f * 20.0f)) {
                ObjSyokudai* torch = (ObjSyokudai*)actor;
                if (torch->snuffTimer == OBJ_SYOKUDAI_SNUFF_OUT &&
                    OBJ_SYOKUDAI_GET_TYPE(actor) != OBJ_SYOKUDAI_TYPE_SWITCH_CAUSES_FLAME) {
                    torch->remoteLightPending = true;
                    sTorchRemoteLit.insert(actor);
                }
                break;
            }
        }
        actor = actor->next;
    }
}
