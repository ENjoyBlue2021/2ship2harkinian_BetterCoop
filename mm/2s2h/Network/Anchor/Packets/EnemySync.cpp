#include "2s2h/Network/Anchor/Anchor.h"
#include "2s2h/Network/Anchor/JsonConversions.hpp"
#include "2s2h/ObjectExtension/ActorListIndex.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * ENEMY_UPDATE / ENEMY_DAMAGE / ENEMY_KILL
 *
 * Host-authoritative enemy synchronization and shared co-op combat:
 * - Host periodically broadcasts active enemies (ACTORCAT_ENEMY) in the current scene/room.
 * - Client smoothly interpolates/syncs enemy positions, rotations, and health to match the Host.
 * - Both Host and Client can attack and damage enemies: whenever an enemy takes damage or is killed
 *   locally, ENEMY_DAMAGE / ENEMY_KILL is sent to the partner so the hit flash, health reduction,
 *   collider AC hit reaction, and death occur on both screens.
 * - Enemies on the Host also consider the closer of the Host Player or partner DummyPlayer for AI targeting.
 */

namespace {

struct SyncedEnemyState {
    s16 sceneId = SCENE_MAX;
    s8 room = -1;
    s16 actorId = 0;
    s16 params = 0;
    s16 listIndex = -1;
    Vec3f homePos = { 0.0f, 0.0f, 0.0f };
    Vec3f targetPos = { 0.0f, 0.0f, 0.0f };
    Vec3s targetShapeRot = { 0, 0, 0 };
    Vec3s targetWorldRot = { 0, 0, 0 };
    u8 health = 0;
    u32 lastUpdatedFrame = 0;
    u32 lastSpawnAttemptFrame = 0;
    bool dead = false;
    u8 zeroHealthFrames = 0;
};

struct LocalEnemyTracker {
    u8 lastHealth = 0;
    u8 lastColorFilterTimer = 0;
    bool initialized = false;
};

static std::map<std::string, SyncedEnemyState> sSyncedEnemies;
static std::map<const Actor*, LocalEnemyTracker> sLocalEnemyTrackers;
static std::map<std::string, Actor*> sKeyToActor;
static std::map<const Actor*, std::string> sActorToKey;
static std::set<std::string> sSuppressSendDamageForKey;
static s16 sLastEnemySyncSceneId = SCENE_MAX;
static u32 sEnemyUpdateTickCounter = 0;

// Dummy collider and element used to stimulate an enemy's AC collider when lethal damage arrives remotely
static Collider sDummyAtCollider = {};
static ColliderElement sDummyAtElement = {};
static bool sDummyColliderInitialized = false;

static void EnsureDummyAtCollider(PlayState* play) {
    if (!sDummyColliderInitialized) {
        sDummyAtCollider.actor = (play != nullptr) ? &GET_PLAYER(play)->actor : nullptr;
        sDummyAtCollider.atFlags = AT_ON | AT_TYPE_PLAYER;
        sDummyAtCollider.acFlags = AC_NONE;
        sDummyAtCollider.ocFlags1 = OC1_NONE;
        sDummyAtCollider.ocFlags2 = OC2_TYPE_1;
        sDummyAtCollider.shape = COLSHAPE_CYLINDER;

        sDummyAtElement.atDmgInfo.dmgFlags = 0x00000100; // Normal sword slash flag
        sDummyAtElement.atDmgInfo.effect = 0;
        sDummyAtElement.atDmgInfo.damage = 2;
        sDummyAtElement.atElemFlags = ATELEM_ON;
        sDummyColliderInitialized = true;
    }
    if (play != nullptr && GET_PLAYER(play) != nullptr) {
        sDummyAtCollider.actor = &GET_PLAYER(play)->actor;
    }
}

static bool IsSyncableEnemy(const Actor* actor) {
    if (actor == nullptr || actor->update == nullptr) {
        return false;
    }
    if (actor->category != ACTORCAT_ENEMY && actor->category != ACTORCAT_BOSS) {
        return false;
    }
    return true;
}

static std::string GetEnemySyncKey(const Actor* actor) {
    int16_t listIdx = GetActorListIndex(actor);
    if (listIdx >= 0) {
        return "L:" + std::to_string(actor->id) + ":" + std::to_string( (int)actor->room ) + ":" +
               std::to_string(listIdx);
    }

    int hx = (int)std::round(actor->home.pos.x / 10.0f);
    int hy = (int)std::round(actor->home.pos.y / 10.0f);
    int hz = (int)std::round(actor->home.pos.z / 10.0f);
    return "D:" + std::to_string(actor->id) + ":" + std::to_string((int)actor->room) + ":" +
           std::to_string(actor->params) + ":" + std::to_string(hx) + ":" + std::to_string(hy) + ":" +
           std::to_string(hz);
}

static bool IsLiveEnemyActor(PlayState* play, Actor* target) {
    if (play == nullptr || target == nullptr) {
        return false;
    }
    s32 categories[2] = { ACTORCAT_ENEMY, ACTORCAT_BOSS };
    for (s32 cat : categories) {
        Actor* actor = play->actorCtx.actorLists[cat].first;
        while (actor != nullptr) {
            if (actor == target) {
                return actor->update != nullptr;
            }
            actor = actor->next;
        }
    }
    return false;
}

static std::string GetOrBindEnemyKey(const Actor* actor) {
    auto it = sActorToKey.find(actor);
    if (it != sActorToKey.end()) {
        return it->second;
    }
    return GetEnemySyncKey(actor);
}

static Actor* FindMatchingEnemyActor(PlayState* play, const std::string& key, s16 actorId, s8 room, s16 listIndex,
                                     const Vec3f& homePos, const Vec3f& worldPos) {
    if (play == nullptr) {
        return nullptr;
    }

    auto boundIt = sKeyToActor.find(key);
    if (boundIt != sKeyToActor.end()) {
        if (IsLiveEnemyActor(play, boundIt->second) && boundIt->second->id == actorId) {
            return boundIt->second;
        }
        sActorToKey.erase(boundIt->second);
        sKeyToActor.erase(boundIt);
    }

    Actor* bestMatch = nullptr;
    f32 bestDistSq = 1e18f;

    s32 categories[2] = { ACTORCAT_ENEMY, ACTORCAT_BOSS };
    for (s32 cat : categories) {
        Actor* actor = play->actorCtx.actorLists[cat].first;
        while (actor != nullptr) {
            if (actor->update != nullptr && actor->id == actorId) {
                std::string candidateKey = GetOrBindEnemyKey(actor);
                if (candidateKey == key) {
                    return actor;
                }

                // Fallback fuzzy match by actorId + room + closest position
                if (room == -1 || actor->room == -1 || actor->room == room) {
                    if (listIndex >= 0 && GetActorListIndex(actor) == listIndex) {
                        return actor;
                    }
                    f32 dx = actor->world.pos.x - worldPos.x;
                    f32 dy = actor->world.pos.y - worldPos.y;
                    f32 dz = actor->world.pos.z - worldPos.z;
                    f32 distSq = (dx * dx) + (dy * dy) + (dz * dz);
                    if (distSq < bestDistSq && distSq < (400.0f * 400.0f)) {
                        bestDistSq = distSq;
                        bestMatch = actor;
                    }
                }
            }
            actor = actor->next;
        }
    }

    return bestMatch;
}

static void StimulateEnemyAcColliders(PlayState* play, Actor* enemy, u32 dmgFlags, u8 damage) {
    if (play == nullptr || enemy == nullptr) {
        return;
    }

    EnsureDummyAtCollider(play);
    sDummyAtElement.atDmgInfo.dmgFlags = (dmgFlags != 0) ? dmgFlags : 0x00000100;
    sDummyAtElement.atDmgInfo.damage = std::max<u8>(damage, 1);

    CollisionCheckContext* colCtx = &play->colChkCtx;
    for (s32 i = 0; i < colCtx->colACCount; i++) {
        Collider* col = colCtx->colAC[i];
        if (col == nullptr || col->actor != enemy) {
            continue;
        }

        col->acFlags |= AC_HIT;
        col->ac = sDummyAtCollider.actor;

        switch (col->shape) {
            case COLSHAPE_JNTSPH: {
                ColliderJntSph* jntSph = (ColliderJntSph*)col;
                for (s32 j = 0; j < jntSph->count; j++) {
                    jntSph->elements[j].base.acElemFlags |= ACELEM_HIT;
                    jntSph->elements[j].base.acHitElem = &sDummyAtElement;
                }
                break;
            }
            case COLSHAPE_CYLINDER: {
                ColliderCylinder* cyl = (ColliderCylinder*)col;
                cyl->elem.acElemFlags |= ACELEM_HIT;
                cyl->elem.acHitElem = &sDummyAtElement;
                break;
            }
            case COLSHAPE_TRIS: {
                ColliderTris* tris = (ColliderTris*)col;
                for (s32 j = 0; j < tris->count; j++) {
                    tris->elements[j].base.acElemFlags |= ACELEM_HIT;
                    tris->elements[j].base.acHitElem = &sDummyAtElement;
                }
                break;
            }
            case COLSHAPE_QUAD: {
                ColliderQuad* quad = (ColliderQuad*)col;
                quad->elem.acElemFlags |= ACELEM_HIT;
                quad->elem.acHitElem = &sDummyAtElement;
                break;
            }
            case COLSHAPE_SPHERE: {
                ColliderSphere* sph = (ColliderSphere*)col;
                sph->elem.acElemFlags |= ACELEM_HIT;
                sph->elem.acHitElem = &sDummyAtElement;
                break;
            }
            default:
                break;
        }
    }
}

// On the Host, let enemies target whichever player (Host Player or Client DummyPlayer) is closer
static void UpdateEnemyTargetNearestCoopPlayer(Actor* enemy, PlayState* play) {
    if (Anchor::Instance == nullptr || !Anchor::Instance->isConnected || !Anchor::Instance->IsHost()) {
        return;
    }

    Player* localPlayer = GET_PLAYER(play);
    if (localPlayer == nullptr) {
        return;
    }

    f32 closestDistSq = enemy->xyzDistToPlayerSq;

    for (auto& [clientId, client] : Anchor::Instance->clients) {
        if (client.self || !client.online || !client.isSaveLoaded || client.sceneId != play->sceneId ||
            client.player == nullptr) {
            continue;
        }

        Actor* dummyActor = &client.player->actor;
        if (dummyActor->update == nullptr || dummyActor->world.pos.y < -9000.0f) {
            continue;
        }

        f32 xzDist = Actor_WorldDistXZToActor(enemy, dummyActor);
        f32 yDiff = Actor_HeightDiff(enemy, dummyActor);
        f32 distSq = SQ(xzDist) + SQ(yDiff);

        if (distSq < closestDistSq) {
            closestDistSq = distSq;
            enemy->xzDistToPlayer = xzDist;
            enemy->playerHeightRel = yDiff;
            enemy->xyzDistToPlayerSq = distSq;
            enemy->yawTowardsPlayer = Actor_WorldYawTowardActor(enemy, dummyActor);
        }
    }
}

} // namespace

void Anchor::ResetEnemySyncState() {
    sSyncedEnemies.clear();
    sLocalEnemyTrackers.clear();
    sKeyToActor.clear();
    sActorToKey.clear();
    sSuppressSendDamageForKey.clear();
    sEnemyUpdateTickCounter = 0;
}

void Anchor::OnEnemyActorUpdate(Actor* actor) {
    if (!isConnected || !IsSaveLoaded() || gPlayState == nullptr || !IsSyncableEnemy(actor)) {
        return;
    }

    // Check if any partner is in the same scene
    bool partnerInScene = false;
    for (auto& [clientId, client] : clients) {
        if (!client.self && client.online && client.isSaveLoaded && client.sceneId == gPlayState->sceneId) {
            partnerInScene = true;
            break;
        }
    }
    if (!partnerInScene) {
        return;
    }

    std::string key = GetOrBindEnemyKey(actor);
    auto& tracker = sLocalEnemyTrackers[actor];
    u8 curHealth = (u8)std::max<s16>(0, actor->colChkInfo.health);

    if (!tracker.initialized) {
        tracker.initialized = true;
        tracker.lastHealth = curHealth;
        tracker.lastColorFilterTimer = actor->colorFilterTimer;
    } else {
        // Detect if local player just damaged this enemy on this frame
        bool healthDropped = (curHealth < tracker.lastHealth);
        bool freshHitFlash = (actor->colorFilterTimer > tracker.lastColorFilterTimer) && (actor->colorFilterTimer >= 4);

        if ((healthDropped || freshHitFlash) && !sSuppressSendDamageForKey.contains(key)) {
            u8 damageDealt = healthDropped ? (tracker.lastHealth - curHealth) : (u8)actor->colChkInfo.damage;
            SendPacket_EnemyDamage(actor, curHealth, damageDealt, actor->colChkInfo.damageEffect,
                                   actor->colorFilterParams, actor->colorFilterTimer);
        }

        tracker.lastHealth = curHealth;
        tracker.lastColorFilterTimer = actor->colorFilterTimer;
    }

    sSuppressSendDamageForKey.erase(key);

    if (IsHost()) {
        if (curHealth > 0) {
            auto deadIt = sSyncedEnemies.find(key);
            if (deadIt != sSyncedEnemies.end()) {
                deadIt->second.dead = false;
            }
        }
        // On Host: update enemy AI distance/yaw toward the nearest co-op player for next frame
        UpdateEnemyTargetNearestCoopPlayer(actor, gPlayState);
        return;
    }

    // On Client: apply Host-authoritative position, rotation, and health
    auto it = sSyncedEnemies.find(key);
    if (it == sSyncedEnemies.end()) {
        return;
    }

    SyncedEnemyState& synced = it->second;
    if (synced.sceneId != gPlayState->sceneId) {
        return;
    }

    if (synced.dead) {
        Vec3f effPos = actor->world.pos;
        effPos.y += 20.0f;
        Vec3f effVel = { 0.0f, 0.0f, 0.0f };
        Vec3f effAccel = { 0.0f, 0.0f, 0.0f };
        Color_RGBA8 primColor = { 255, 255, 255, 255 };
        Color_RGBA8 envColor = { 0, 0, 255, 255 };
        EffectSsDeadDb_Spawn(gPlayState, &effPos, &effVel, &effAccel, &primColor, &envColor, 100, 0, 9);
        Actor_Kill(actor);
        return;
    }

    // Only sync if host sent an update recently (within ~45 frames)
    if ((gPlayState->gameplayFrames - synced.lastUpdatedFrame) > 45) {
        return;
    }

    f32 dx = synced.targetPos.x - actor->world.pos.x;
    f32 dy = synced.targetPos.y - actor->world.pos.y;
    f32 dz = synced.targetPos.z - actor->world.pos.z;
    f32 distSq = (dx * dx) + (dy * dy) + (dz * dz);

    if (distSq > (180.0f * 180.0f)) {
        // Snap if far apart
        actor->world.pos = synced.targetPos;
    } else if (distSq > 1.0f) {
        // Smooth blend toward host position
        actor->world.pos.x += dx * 0.55f;
        actor->world.pos.y += dy * 0.55f;
        actor->world.pos.z += dz * 0.55f;
    }

    Math_ScaledStepToS(&actor->shape.rot.x, synced.targetShapeRot.x, 0x2000);
    Math_ScaledStepToS(&actor->shape.rot.y, synced.targetShapeRot.y, 0x2000);
    Math_ScaledStepToS(&actor->shape.rot.z, synced.targetShapeRot.z, 0x2000);
    Math_ScaledStepToS(&actor->world.rot.x, synced.targetWorldRot.x, 0x2000);
    Math_ScaledStepToS(&actor->world.rot.y, synced.targetWorldRot.y, 0x2000);
    Math_ScaledStepToS(&actor->world.rot.z, synced.targetWorldRot.z, 0x2000);

    if (synced.health < actor->colChkInfo.health) {
        actor->colChkInfo.health = synced.health;
        tracker.lastHealth = synced.health;
    }

    // If enemy has 0 health from Host for more than 25 frames and hasn't finished dying, clean it up
    if (actor->colChkInfo.health == 0 && synced.health == 0) {
        synced.zeroHealthFrames++;
        if (synced.zeroHealthFrames == 1) {
            StimulateEnemyAcColliders(gPlayState, actor, 0x00000100, 10);
        } else if (synced.zeroHealthFrames > 35) {
            Actor_Kill(actor);
        }
    } else {
        synced.zeroHealthFrames = 0;
    }
}

void Anchor::OnEnemyActorKill(Actor* actor) {
    sLocalEnemyTrackers.erase(actor);

    std::string key = GetOrBindEnemyKey(actor);
    auto a2k = sActorToKey.find(actor);
    if (a2k != sActorToKey.end()) {
        auto k2a = sKeyToActor.find(a2k->second);
        if (k2a != sKeyToActor.end() && k2a->second == actor) {
            sKeyToActor.erase(k2a);
        }
        sActorToKey.erase(a2k);
    }

    if (!isConnected || !IsSaveLoaded() || gPlayState == nullptr || !IsSyncableEnemy(actor)) {
        return;
    }

    auto it = sSyncedEnemies.find(key);
    if (it != sSyncedEnemies.end() && it->second.dead) {
        return;
    }

    SendPacket_EnemyKill(actor);
}

void Anchor::TickEnemySync() {
    if (!isConnected || !IsSaveLoaded() || gPlayState == nullptr) {
        return;
    }

    if (gPlayState->sceneId != sLastEnemySyncSceneId) {
        sLastEnemySyncSceneId = gPlayState->sceneId;
        ResetEnemySyncState();
    }

    if (!IsHost()) {
        return;
    }

    sEnemyUpdateTickCounter++;
    // Broadcast enemy positions every 2 frames when a partner is in the same scene
    if (sEnemyUpdateTickCounter >= 2) {
        sEnemyUpdateTickCounter = 0;
        SendPacket_EnemyUpdate();
    }
}

void Anchor::SendPacket_EnemyUpdate() {
    if (!IsSaveLoaded() || !IsHost() || gPlayState == nullptr) {
        return;
    }

    bool partnerInScene = false;
    for (auto& [clientId, client] : clients) {
        if (!client.self && client.online && client.isSaveLoaded && client.sceneId == gPlayState->sceneId) {
            partnerInScene = true;
            break;
        }
    }
    if (!partnerInScene) {
        return;
    }

    nlohmann::json enemiesJson = nlohmann::json::array();

    s32 categories[2] = { ACTORCAT_ENEMY, ACTORCAT_BOSS };
    for (s32 cat : categories) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].first;
        while (actor != nullptr) {
            if (actor->update != nullptr) {
                nlohmann::json e;
                e["k"] = GetOrBindEnemyKey(actor);
                e["id"] = actor->id;
                e["pr"] = actor->params;
                e["rm"] = actor->room;
                e["li"] = GetActorListIndex(actor);
                e["h"] = actor->home.pos;
                e["p"] = actor->world.pos;
                e["sr"] = actor->shape.rot;
                e["wr"] = actor->world.rot;
                e["hp"] = (u8)std::max<s16>(0, actor->colChkInfo.health);
                enemiesJson.push_back(std::move(e));
            }
            actor = actor->next;
        }
    }

    if (enemiesJson.empty()) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = ENEMY_UPDATE;
    payload["quiet"] = true;
    payload["sceneId"] = gPlayState->sceneId;
    payload["room"] = gPlayState->roomCtx.curRoom.num;
    payload["enemies"] = std::move(enemiesJson);

    for (auto& [clientId, client] : clients) {
        if (!client.self && client.online && client.isSaveLoaded && client.sceneId == gPlayState->sceneId) {
            payload["targetClientId"] = clientId;
            QueueOutgoingPacket(payload);
        }
    }
}

void Anchor::HandlePacket_EnemyUpdate(nlohmann::json payload) {
    if (!IsSaveLoaded() || IsHost() || gPlayState == nullptr) {
        return;
    }

    s16 sceneId = payload.value("sceneId", (s16)SCENE_MAX);
    if (sceneId != gPlayState->sceneId || !payload.contains("enemies") || !payload["enemies"].is_array()) {
        return;
    }

    for (const auto& e : payload["enemies"]) {
        std::string key = e.value("k", std::string(""));
        if (key.empty()) {
            continue;
        }

        SyncedEnemyState& state = sSyncedEnemies[key];
        state.sceneId = sceneId;
        state.actorId = e.value("id", (s16)0);
        state.params = e.value("pr", (s16)0);
        state.room = e.value("rm", (s8)-1);
        state.listIndex = e.value("li", (s16)-1);
        if (e.contains("h")) {
            state.homePos = e["h"].get<Vec3f>();
        }
        if (e.contains("p")) {
            state.targetPos = e["p"].get<Vec3f>();
        }
        if (e.contains("sr")) {
            state.targetShapeRot = e["sr"].get<Vec3s>();
        }
        if (e.contains("wr")) {
            state.targetWorldRot = e["wr"].get<Vec3s>();
        }
        state.health = e.value("hp", (u8)0);
        state.lastUpdatedFrame = gPlayState->gameplayFrames;

        if (state.health > 0) {
            state.dead = false;
            state.zeroHealthFrames = 0;

            bool roomMatches = (state.room == -1 || state.room == gPlayState->roomCtx.curRoom.num ||
                                state.room == gPlayState->roomCtx.prevRoom.num);
            if (roomMatches) {
                Actor* match = FindMatchingEnemyActor(gPlayState, key, state.actorId, state.room, state.listIndex,
                                                      state.homePos, state.targetPos);
                if (match != nullptr) {
                    sKeyToActor[key] = match;
                    sActorToKey[match] = key;
                } else if (state.lastSpawnAttemptFrame == 0 ||
                           (gPlayState->gameplayFrames - state.lastSpawnAttemptFrame) >= 30) {
                    state.lastSpawnAttemptFrame = gPlayState->gameplayFrames;

                    if (state.actorId >= 0 && state.actorId < ACTOR_ID_MAX &&
                        gActorOverlayTable[state.actorId].profile != nullptr) {
                        s16 objId = gActorOverlayTable[state.actorId].profile->objectId;
                        if (objId == GAMEPLAY_KEEP || Object_GetSlot(&gPlayState->objectCtx, objId) >= 0) {
                            Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, state.actorId,
                                                         state.targetPos.x, state.targetPos.y, state.targetPos.z,
                                                         state.targetShapeRot.x, state.targetShapeRot.y,
                                                         state.targetShapeRot.z, state.params);
                            if (spawned != nullptr) {
                                spawned->home.pos = state.homePos;
                                spawned->room = state.room;
                                spawned->colChkInfo.health = state.health;
                                sKeyToActor[key] = spawned;
                                sActorToKey[spawned] = key;

                                auto& tracker = sLocalEnemyTrackers[spawned];
                                tracker.initialized = true;
                                tracker.lastHealth = state.health;
                                tracker.lastColorFilterTimer = spawned->colorFilterTimer;
                            }
                        }
                    }
                }
            }
        }
    }
}

void Anchor::SendPacket_EnemyDamage(Actor* actor, u8 newHealth, u8 damage, u8 damageEffect, u16 colorFilterParams,
                                    u8 colorFilterTimer) {
    if (!IsSaveLoaded() || gPlayState == nullptr || actor == nullptr) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = ENEMY_DAMAGE;
    payload["quiet"] = true;
    payload["sceneId"] = gPlayState->sceneId;
    payload["k"] = GetOrBindEnemyKey(actor);
    payload["id"] = actor->id;
    payload["rm"] = actor->room;
    payload["li"] = GetActorListIndex(actor);
    payload["home"] = actor->home.pos;
    payload["pos"] = actor->world.pos;
    payload["hp"] = newHealth;
    payload["dmg"] = damage;
    payload["eff"] = damageEffect;
    payload["cfp"] = colorFilterParams;
    payload["cft"] = colorFilterTimer;

    for (auto& [clientId, client] : clients) {
        if (!client.self && client.online && client.isSaveLoaded && client.sceneId == gPlayState->sceneId) {
            payload["targetClientId"] = clientId;
            QueueOutgoingPacket(payload);
        }
    }
}

void Anchor::HandlePacket_EnemyDamage(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == nullptr) {
        return;
    }

    s16 sceneId = payload.value("sceneId", (s16)SCENE_MAX);
    if (sceneId != gPlayState->sceneId) {
        return;
    }

    std::string key = payload.value("k", std::string(""));
    s16 actorId = payload.value("id", (s16)0);
    s8 room = payload.value("rm", (s8)-1);
    s16 listIndex = payload.value("li", (s16)-1);
    Vec3f homePos = payload.value("home", Vec3f{ 0.0f, 0.0f, 0.0f });
    Vec3f worldPos = payload.value("pos", Vec3f{ 0.0f, 0.0f, 0.0f });
    u8 newHealth = payload.value("hp", (u8)0);
    u8 damage = payload.value("dmg", (u8)1);
    u8 damageEffect = payload.value("eff", (u8)0);
    u16 colorFilterParams = payload.value("cfp", (u16)0x4000);
    u8 colorFilterTimer = payload.value("cft", (u8)8);

    if (!key.empty()) {
        sSyncedEnemies[key].health = newHealth;
        sSuppressSendDamageForKey.insert(key);
    }

    Actor* actor = FindMatchingEnemyActor(gPlayState, key, actorId, room, listIndex, homePos, worldPos);
    if (actor == nullptr) {
        return;
    }

    actor->colChkInfo.health = newHealth;
    actor->colChkInfo.damage = damage;
    actor->colChkInfo.damageEffect = damageEffect;
    actor->colorFilterParams = (colorFilterParams != 0) ? colorFilterParams : 0x4000;
    actor->colorFilterTimer = std::max<u8>(colorFilterTimer, 8);

    auto& tracker = sLocalEnemyTrackers[actor];
    tracker.initialized = true;
    tracker.lastHealth = newHealth;
    tracker.lastColorFilterTimer = actor->colorFilterTimer;

    Actor_PlaySfx(actor, NA_SE_IT_SWORD_STRIKE);

    // Stimulate the enemy's AC collider so its native state machine reacts to the hit (knockback, stun, or death!)
    StimulateEnemyAcColliders(gPlayState, actor, 0x00000100, damage);
}

void Anchor::SendPacket_EnemyKill(Actor* actor) {
    if (!IsSaveLoaded() || gPlayState == nullptr || actor == nullptr) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = ENEMY_KILL;
    payload["quiet"] = true;
    payload["sceneId"] = gPlayState->sceneId;
    payload["k"] = GetOrBindEnemyKey(actor);
    payload["id"] = actor->id;
    payload["rm"] = actor->room;
    payload["li"] = GetActorListIndex(actor);
    payload["home"] = actor->home.pos;
    payload["pos"] = actor->world.pos;

    for (auto& [clientId, client] : clients) {
        if (!client.self && client.online && client.isSaveLoaded && client.sceneId == gPlayState->sceneId) {
            payload["targetClientId"] = clientId;
            QueueOutgoingPacket(payload);
        }
    }
}

void Anchor::HandlePacket_EnemyKill(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == nullptr) {
        return;
    }

    s16 sceneId = payload.value("sceneId", (s16)SCENE_MAX);
    if (sceneId != gPlayState->sceneId) {
        return;
    }

    std::string key = payload.value("k", std::string(""));
    s16 actorId = payload.value("id", (s16)0);
    s8 room = payload.value("rm", (s8)-1);
    s16 listIndex = payload.value("li", (s16)-1);
    Vec3f homePos = payload.value("home", Vec3f{ 0.0f, 0.0f, 0.0f });
    Vec3f worldPos = payload.value("pos", Vec3f{ 0.0f, 0.0f, 0.0f });

    if (!key.empty()) {
        sSyncedEnemies[key].sceneId = sceneId;
        sSyncedEnemies[key].health = 0;
        sSyncedEnemies[key].dead = true;
    }

    Actor* actor = FindMatchingEnemyActor(gPlayState, key, actorId, room, listIndex, homePos, worldPos);
    if (actor != nullptr) {
        actor->colChkInfo.health = 0;
        StimulateEnemyAcColliders(gPlayState, actor, 0x00000100, 99);
    }
}
