#include "Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/BenPort.h"
#include "2s2h/BenGui/UIWidgets.hpp"
#include "2s2h/NameTag/NameTag.h"
#include "2s2h/ShipUtils.h"
#include "2s2h/Rando/Spoiler/Spoiler.h"
#include "2s2h/Rando/MiscBehavior/MiscBehavior.h"

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

// MARK: - Overrides

void Anchor::Enable() {
    int mode = CVarGetInteger("gNetwork.Anchor.Mode", 0);
    int port = CVarGetInteger("gNetwork.Anchor.Port", 43383);
    if (port <= 0 || port > 65535) {
        port = 43383;
    }

    if (mode == 0) {
        // Host LAN: start embedded LAN server and connect locally as Host (clientId 1)
        if (!lanServer.Start(static_cast<uint16_t>(port))) {
            SPDLOG_ERROR("[Anchor] Failed to start LAN Host Server on port {}: {}", port, lanServer.GetLastError());
            return;
        }
        Network::Enable("127.0.0.1", static_cast<uint16_t>(port));
        ownClientId = 1;
        roomState.ownerClientId = 1;
    } else if (mode == 1) {
        // Join LAN
        lanServer.Stop();
        Network::Enable(CVarGetString("gNetwork.Anchor.LanHostIp", "192.168.1.100"), static_cast<uint16_t>(port));
        ownClientId = CVarGetInteger("gNetwork.Anchor.LastClientId", 0);
        roomState.ownerClientId = 0;
    } else {
        // Online Anchor
        lanServer.Stop();
        Network::Enable(CVarGetString("gNetwork.Anchor.Host", "anchor.proxysaw.dev"), static_cast<uint16_t>(port));
        ownClientId = CVarGetInteger("gNetwork.Anchor.LastClientId", 0);
        roomState.ownerClientId = 0;
    }
    ResetHostTimeSyncTracker();
    ResetDungeonItemSyncTracker();
}

void Anchor::Disable() {
    Network::Disable();
    lanServer.Stop();
    ResetHostTimeSyncTracker();
    ResetDungeonItemSyncTracker();

    clients.clear();
    RefreshClientActors();
}

void Anchor::OnConnected() {
    SendPacket_Handshake();
    RegisterHooks();

    if (IsSaveLoaded()) {
        SendPacket_RequestTeamState();
    }
}

void Anchor::OnDisconnected() {
    RegisterHooks();
}

void Anchor::QueueOutgoingPacket(nlohmann::json payload) {
    if (!isConnected) {
        return;
    }

    payload["clientId"] = ownClientId;

    Network::QueueOutgoingPacket(payload);
}

void Anchor::ProcessIncomingPacket(nlohmann::json payload) {
    // If it doesn't contain a type, it's not a valid payload
    if (!payload.contains("type")) {
        return;
    }

    std::string packetType = payload["type"].get<std::string>();

    // Allow all packets between local Linux builds and GitHub CI Windows builds even if build strings differ

    // packetType here is a string so we can't use a switch statement
    if (packetType == ALL_CLIENT_STATE)
        HandlePacket_AllClientState(payload);
    else if (packetType == DAMAGE_PLAYER)
        HandlePacket_DamagePlayer(payload);
    else if (packetType == DISABLE_ANCHOR)
        HandlePacket_DisableAnchor(payload);
    else if (packetType == ENEMY_DAMAGE)
        HandlePacket_EnemyDamage(payload);
    else if (packetType == ENEMY_KILL)
        HandlePacket_EnemyKill(payload);
    else if (packetType == ENEMY_UPDATE)
        HandlePacket_EnemyUpdate(payload);
    else if (packetType == GAME_COMPLETE)
        HandlePacket_GameComplete(payload);
    else if (packetType == GIVE_ITEM)
        HandlePacket_GiveItem(payload);
    else if (packetType == PLAYER_SFX)
        HandlePacket_PlayerSfx(payload);
    else if (packetType == PLAYER_UPDATE)
        HandlePacket_PlayerUpdate(payload);
    else if (packetType == UPDATE_TEAM_STATE)
        HandlePacket_UpdateTeamState(payload);
    else if (packetType == REQUEST_TEAM_STATE)
        HandlePacket_RequestTeamState(payload);
    else if (packetType == REQUEST_TELEPORT)
        HandlePacket_RequestTeleport(payload);
    else if (packetType == SERVER_MESSAGE)
        HandlePacket_ServerMessage(payload);
    else if (packetType == SET_CHECK_STATUS)
        HandlePacket_SetCheckStatus(payload);
    else if (packetType == SET_FLAG)
        HandlePacket_SetFlag(payload);
    else if (packetType == SYNC_TIME)
        HandlePacket_SyncTime(payload);
    else if (packetType == SYNC_VANILLA_ITEM)
        HandlePacket_SyncVanillaItem(payload);
    else if (packetType == TELEPORT_TO)
        HandlePacket_TeleportTo(payload);
    else if (packetType == UNSET_FLAG)
        HandlePacket_UnsetFlag(payload);
    else if (packetType == UPDATE_CLIENT_STATE)
        HandlePacket_UpdateClientState(payload);
    else if (packetType == UPDATE_ROOM_STATE)
        HandlePacket_UpdateRoomState(payload);
    else if (packetType == UPDATE_DUNGEON_ITEMS)
        HandlePacket_UpdateDungeonItems(payload);
}

static bool justReset = false;

void Anchor::RegisterHooks() {
    COND_HOOK(OnSceneSpawnActors, isConnected, [&]() {
        ResetEnemySyncState();
        SendPacket_UpdateClientState();
        justReset = false;
        shouldRefreshActors = true;
    });

    COND_ID_HOOK(ShouldActorInit, ACTOR_PLAYER, isConnected, [&](void* actorRef, bool* should) {
        Actor* actor = (Actor*)actorRef;

        if (refreshingActors) {
            // By the time we get here, the actor was already added to the ACTORCAT_PLAYER list, so we need to move it
            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, ACTORCAT_NPC);
            actor->id = ACTOR_ITEM_INBOX;
            actor->category = ACTORCAT_NPC;
            actor->init = DummyPlayer_Init;
            actor->update = DummyPlayer_Update;
            actor->draw = DummyPlayer_Draw;
            actor->destroy = DummyPlayer_Destroy;
        }
    });

    COND_ID_HOOK(OnActorUpdate, ACTOR_PLAYER, isConnected, [&](Actor* actor) {
        SendPacket_PlayerUpdate();
        TickEnemySync();

        if (shouldRefreshActors) {
            shouldRefreshActors = false;
            RefreshClientActors();
        }
    });

    COND_HOOK(OnActorUpdate, isConnected, [&](Actor* actor) { OnEnemyActorUpdate(actor); });

    COND_HOOK(OnActorKill, isConnected, [&](Actor* actor) { OnEnemyActorKill(actor); });

    COND_HOOK(OnPlayerSfx, isConnected, [&](u16 sfxId) { SendPacket_PlayerSfx(sfxId); });

    COND_HOOK(OnSaveLoad, isConnected, [&](s16 fileNum) { SendPacket_RequestTeamState(); });

    COND_HOOK(OnConsoleLogoUpdate, isConnected, [&]() {
        if (!justReset) {
            SendPacket_UpdateClientState();
            justReset = true;
        }
    });

    COND_HOOK(AfterOwlSave, isConnected, [&]() {
        if (gPlayState != NULL) {
            SendPacket_UpdateTeamState(CVarGetString("gNetwork.Anchor.TeamId", "default"));
        }
    });

    COND_HOOK(AfterEndOfCycleSave, isConnected, [&]() {
        if (gPlayState != NULL) {
            SendPacket_UpdateTeamState(CVarGetString("gNetwork.Anchor.TeamId", "default"));
        }
    });

    COND_HOOK(OnItemGive, isConnected, [&](u8 item) {
        if (!isApplyingRemoteItem && IsSaveLoaded()) {
            SendPacket_SyncVanillaItem(item);
        }
    });

    COND_VB_SHOULD(VB_GIVE_ITEM_FROM_ITEM00, isConnected, {
        if (*should) {
            EnItem00* item00 = va_arg(args, EnItem00*);
            if (item00 != nullptr &&
                (item00->actor.params == ITEM00_FLEXIBLE || item00->actor.params == ITEM00_BIG_FAIRY)) {
                Anchor::Instance->SendPacket_SyncFairyHeal();
            }
        }
    });

    COND_HOOK(OnFlagSet, isConnected, [&](FlagType flagType, u32 flag) {
        if (!isApplyingRemoteFlag && IsSaveLoaded()) {
            SendPacket_SetFlag(SCENE_MAX, static_cast<s16>(flagType), static_cast<s16>(flag));
        }
    });

    COND_HOOK(OnFlagUnset, isConnected, [&](FlagType flagType, u32 flag) {
        if (!isApplyingRemoteFlag && IsSaveLoaded()) {
            SendPacket_UnsetFlag(SCENE_MAX, static_cast<s16>(flagType), static_cast<s16>(flag));
        }
    });

    COND_HOOK(OnSceneFlagSet, isConnected, [&](s16 sceneId, FlagType flagType, u32 flag) {
        if (!isApplyingRemoteFlag && IsSaveLoaded()) {
            SendPacket_SetFlag(sceneId, static_cast<s16>(flagType), static_cast<s16>(flag));
        }
    });

    COND_HOOK(OnSceneFlagUnset, isConnected, [&](s16 sceneId, FlagType flagType, u32 flag) {
        if (!isApplyingRemoteFlag && IsSaveLoaded()) {
            SendPacket_UnsetFlag(sceneId, static_cast<s16>(flagType), static_cast<s16>(flag));
        }
    });

    COND_HOOK(OnGameStateMainStart, isConnected, [&]() {
        TickHostTimeSync();
        TickDungeonItemSync();

        std::queue<nlohmann::json> packetQueue;
        Anchor::Instance->SwapIncomingPacketQueue(packetQueue);

        if (!queuedPacketsFromTeamState.empty() && IsSaveLoaded()) {
            // Put them in the main queue to be processed
            while (!queuedPacketsFromTeamState.empty()) {
                packetQueue.push(std::move(queuedPacketsFromTeamState.front()));
                queuedPacketsFromTeamState.pop();
            }
        }

        while (!packetQueue.empty()) {
            nlohmann::json payload = packetQueue.front();
            packetQueue.pop();
            try {
                Anchor::Instance->ProcessIncomingPacket(payload);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("[Anchor] Exception while processing incoming packet {}", e.what());
                SPDLOG_ERROR("[Anchor] Packet: {}", payload.dump());
            }
        }
    });
}

// MARK: - Misc/Helpers

// Kills all existing anchor actors and respawns them with the new client data
void Anchor::RefreshClientActors() {
    if (!IsSaveLoaded()) {
        return;
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].first;

    while (actor != NULL) {
        if (actor->id == ACTOR_ITEM_INBOX && actor->update == DummyPlayer_Update) {
            NameTag_RemoveAllForActor(actor);
            Actor_Kill(actor);
        }
        actor = actor->next;
    }

    actorIndexToClientId.clear();
    refreshingActors = true;
    for (auto& [clientId, client] : clients) {
        if (!client.online || client.self) {
            continue;
        }

        actorIndexToClientId.push_back(clientId);
        // We are using a hook `ShouldActorInit` to override the init/update/draw/destroy functions of the Player we
        // spawn We quickly store a mapping of "index" to clientId, then within the init function we use this to get the
        // clientId and store it on player->zTargetActiveTimer (unused s32 for the dummy) for convenience
        auto dummy = Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, client.posRot.pos.x,
                                 client.posRot.pos.y, client.posRot.pos.z, client.posRot.rot.x, client.posRot.rot.y,
                                 client.posRot.rot.z, actorIndexToClientId.size() - 1);
        client.player = (Player*)dummy;
    }
    refreshingActors = false;
}

bool Anchor::IsSaveLoaded() {
    if (gPlayState == nullptr) {
        return false;
    }

    if (GET_PLAYER(gPlayState) == nullptr) {
        return false;
    }

    if (gSaveContext.fileNum < 0 || gSaveContext.fileNum > 2) {
        return false;
    }

    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }

    return true;
}

bool Anchor::IsHost() const {
    return lanServer.IsRunning() || (ownClientId != 0 && ownClientId == roomState.ownerClientId);
}
