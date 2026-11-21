#include "Anchor.h"
#include "2s2h/NameTag/NameTag.h"
#include "2s2h/Enhancements/FrameInterpolation/FrameInterpolation.h"
#include "2s2h/ShipUtils.h"
#include <algorithm>
#include <vector>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "z64malloc.h"
extern PlayState* gPlayState;

#include "objects/object_test3/object_test3.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/object_link_goron/object_link_goron.h"
#include "interface/parameter_static/parameter_static.h"
#include "interface/message_static/message_static.h"
extern PlayerAgeProperties sPlayerAgeProperties[PLAYER_FORM_MAX];
extern TexturePtr sPlayerEyesTextures[PLAYER_FORM_MAX][PLAYER_EYES_MAX];
extern EffectBlureInit2 D_8085D30C;
extern EffectTireMarkInit D_8085D330;
void Player_DrawGameplay(PlayState* play, Player* player, s32 lod, Gfx* cullDList,
                         OverrideLimbDrawFlex overrideLimbDraw);
void Player_Anim_PlayOnceMorph(PlayState* play, Player* player, PlayerAnimationHeader* anim);
PlayerAnimationHeader* Player_GetIdleAnim(Player* player);
}

// Hijacking player->zTargetActiveTimer (unused s32 for the dummy) to store the clientId for convenience
#define DUMMY_CLIENT_ID player->zTargetActiveTimer

static f32 sSpinChargeScales[] = { 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.25f, 0.2f, 0.15f, 0.0f };

static TexturePtr sOverheadHeartTextures[] = {
    gHeartFullTex,         gHeartQuarterTex,      gHeartQuarterTex,      gHeartQuarterTex,
    gHeartQuarterTex,      gHeartQuarterTex,      gHeartHalfTex,         gHeartHalfTex,
    gHeartHalfTex,         gHeartHalfTex,         gHeartHalfTex,         gHeartThreeQuarterTex,
    gHeartThreeQuarterTex, gHeartThreeQuarterTex, gHeartThreeQuarterTex, gHeartThreeQuarterTex,
};

static TexturePtr sOverheadHeartDDTextures[] = {
    gDefenseHeartFullTex,         gDefenseHeartQuarterTex,      gDefenseHeartQuarterTex,
    gDefenseHeartQuarterTex,      gDefenseHeartQuarterTex,      gDefenseHeartQuarterTex,
    gDefenseHeartHalfTex,         gDefenseHeartHalfTex,         gDefenseHeartHalfTex,
    gDefenseHeartHalfTex,         gDefenseHeartHalfTex,         gDefenseHeartThreeQuarterTex,
    gDefenseHeartThreeQuarterTex, gDefenseHeartThreeQuarterTex, gDefenseHeartThreeQuarterTex,
    gDefenseHeartThreeQuarterTex,
};

static DamageTable DummyPlayerDamageTable = {
    /* Deku Nut       */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_STUN),
    /* Deku Stick     */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Horse trample  */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Explosives     */ DMG_ENTRY(3, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Zora boomerang */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_STUN),
    /* Normal arrow   */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* UNK_DMG_0x06   */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Hookshot       */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_STUN),
    /* Goron punch    */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_SMALL),
    /* Sword          */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Goron pound    */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Fire arrow     */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_FIRE),
    /* Ice arrow      */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_ICE_TRAP),
    /* Light arrow    */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Goron spikes   */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Deku spin      */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Deku bubble    */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Deku launch    */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_SMALL),
    /* UNK_DMG_0x12   */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_ICE_TRAP),
    /* Zora barrier   */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Normal shield  */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* Light ray      */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* Thrown object  */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Zora punch     */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Spin attack    */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* Sword beam     */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_ELECTRIC_SHOCK),
    /* Normal Roll    */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* UNK_DMG_0x1B   */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
    /* UNK_DMG_0x1C   */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* Unblockable    */ DMG_ENTRY(0, DUMMY_PLAYER_HIT_RESPONSE_NONE),
    /* UNK_DMG_0x1E   */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_KNOCKBACK_LARGE),
    /* Powder Keg     */ DMG_ENTRY(1, DUMMY_PLAYER_HIT_RESPONSE_NORMAL),
};

// Modeled after EnTest3_Init and Player_Init
void DummyPlayer_Init(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    uint32_t clientId = Anchor::Instance->actorIndexToClientId[actor->params];
    DUMMY_CLIENT_ID = clientId;

    if (!Anchor::Instance->clients.contains(DUMMY_CLIENT_ID)) {
        Actor_Kill(actor);
        return;
    }

    AnchorClient& client = Anchor::Instance->clients[DUMMY_CLIENT_ID];

    player->actor.room = -1;
    player->csId = CS_ID_NONE;
    player->transformation = client.transformation;
    player->ageProperties = &sPlayerAgeProperties[player->transformation];
    player->heldItemAction = PLAYER_IA_NONE;
    player->heldItemId = ITEM_OCARINA_OF_TIME;

    Player_SetModelGroup(player, PLAYER_MODELGROUP_DEFAULT);
    play->playerInit(player, play, gPlayerSkeletons[player->transformation]);

    Effect_Add(play, &player->meleeWeaponEffectIndex[0], EFFECT_BLURE2, 0, 0, &D_8085D30C);
    Effect_Add(play, &player->meleeWeaponEffectIndex[1], EFFECT_BLURE2, 0, 0, &D_8085D30C);
    Effect_Add(play, &player->meleeWeaponEffectIndex[2], EFFECT_TIRE_MARK, 0, 0, &D_8085D330);

    player->maskObjectSegment = ZeldaArena_Malloc(0x3800);
    // Skipping part of play->func_18780, specifically Player_SetAction
    Player_Anim_PlayOnceMorph(play, player, Player_GetIdleAnim(player));
    player->yaw = player->actor.shape.rot.y;

    // Ensures the actor is always updating/drawing even when culled/out of distance
    actor->flags =
        ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED | ACTOR_FLAG_INSIDE_CULLING_VOLUME;
    player->cylinder.base.acFlags = AC_ON | AC_TYPE_PLAYER;
    player->cylinder.base.ocFlags2 = OC2_TYPE_1;
    player->cylinder.elem.acElemFlags = ACELEM_ON | ACELEM_HOOKABLE | ACELEM_NO_HITMARK;
    player->actor.flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
    player->cylinder.dim.radius = 30;
    player->actor.colChkInfo.damageTable = &DummyPlayerDamageTable;

    bool isGlobalRoom = (std::string("2ship-global") == CVarGetString("gNetwork.Anchor.RoomId", ""));
    if (!isGlobalRoom) {
        NameTag_RegisterForActorWithOptions(actor, client.name.c_str(), { .yOffset = 30 });
    }
}

// Update the actor with new data from the client
void DummyPlayer_Update(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    if (!Anchor::Instance->clients.contains(DUMMY_CLIENT_ID)) {
        Actor_Kill(actor);
        return;
    }

    AnchorClient& client = Anchor::Instance->clients[DUMMY_CLIENT_ID];

    if (client.sceneId != gPlayState->sceneId || !client.online || !client.isSaveLoaded) {
        actor->world.pos.x = -9999.0f;
        actor->world.pos.y = -9999.0f;
        actor->world.pos.z = -9999.0f;
        actor->shape.shadowAlpha = 0;
        return;
    }

    actor->shape.shadowAlpha = 255;
    Math_Vec3s_Copy(&actor->shape.rot, &client.posRot.rot);
    Math_Vec3f_Copy(&actor->world.pos, &client.posRot.pos);
    memcpy(&player->jointTableBuffer, &client.jointTable, 159);
    memcpy(&player->jointTableUpperBuffer, &client.upperJointTable, 159);
    player->maskObjectLoadState = 0;
    player->maskId = player->currentMask;
    player->currentMask = client.currentMask;
    player->rightHandType = client.rightHandType;
    player->leftHandType = client.leftHandType;
    player->currentShield = client.currentShield;
    player->sheathType = client.sheathType;
    player->heldItemAction = client.heldItemAction;
    player->heldItemId = client.heldItemId;
    player->itemAction = client.itemAction;
    player->stateFlags1 = client.stateFlags1;
    player->stateFlags2 = client.stateFlags2;
    player->stateFlags3 = client.stateFlags3;
    player->unk_B0C = client.unk_B0C;
    player->unk_B28 = client.unk_B28;
    player->unk_ACC = client.unk_ACC;
    player->invincibilityTimer = client.invincibilityTimer;
    player->unk_B08 = client.unk_B08;
    player->unk_B86[0] = client.unk_B86_0;
    player->unk_B86[1] = client.unk_B86_1;
    player->av1.actionVar1 = client.actionVar1;
    player->unk_ABC = client.unk_ABC;

    u8 prevMeleeState = player->meleeWeaponState;
    player->meleeWeaponState = client.meleeWeaponState;
    player->meleeWeaponAnimation = client.meleeWeaponAnimation;

    if (player->meleeWeaponState == 0 && prevMeleeState != 0) {
        EffectBlure* blure0 = (EffectBlure*)Effect_GetByIndex(player->meleeWeaponEffectIndex[0]);
        if (blure0 != nullptr) {
            EffectBlure_AddSpace(blure0);
        }
        EffectBlure* blure1 = (EffectBlure*)Effect_GetByIndex(player->meleeWeaponEffectIndex[1]);
        if (blure1 != nullptr) {
            EffectBlure_AddSpace(blure1);
        }
    }

    PlayerItemAction modelItemAction =
        (PlayerItemAction)(player->itemAction > PLAYER_IA_MINUS1 ? player->itemAction : player->heldItemAction);
    if (modelItemAction < PLAYER_IA_NONE || modelItemAction >= PLAYER_IA_MAX) {
        modelItemAction = PLAYER_IA_NONE;
    }
    Player_SetModels(player, Player_ActionToModelGroup(player, modelItemAction));

    if (Anchor::Instance->roomState.pvpMode == 0 ||
        (Anchor::Instance->roomState.pvpMode == 1 &&
         client.teamId == CVarGetString("gNetwork.Anchor.TeamId", "default"))) {
        actor->flags |= ACTOR_FLAG_LOCK_ON_DISABLED;
        return;
    }

    actor->flags &= ~ACTOR_FLAG_LOCK_ON_DISABLED;

    if (player->cylinder.base.acFlags & AC_HIT && player->invincibilityTimer == 0) {
        Anchor::Instance->SendPacket_DamagePlayer(client.clientId, player->actor.colChkInfo.damageEffect,
                                                  player->actor.colChkInfo.damage);
        player->actor.colChkInfo.damage = 0;
        if (player->actor.colChkInfo.damageEffect == DUMMY_PLAYER_HIT_RESPONSE_STUN) {
            Actor_SetColorFilter(&player->actor, 0, 0xFF, 0, 24);
        } else {
            player->invincibilityTimer = 20;
        }
    }

    Collider_UpdateCylinder(&player->actor, &player->cylinder);

    if (!(player->stateFlags2 & PLAYER_STATE2_4000)) {
        if (!(player->stateFlags1 & (PLAYER_STATE1_4 | PLAYER_STATE1_DEAD | PLAYER_STATE1_2000 | PLAYER_STATE1_4000 |
                                     PLAYER_STATE1_800000))) {
            CollisionCheck_SetOC(play, &play->colChkCtx, &player->cylinder.base);
        }

        if (!(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_4000000)) &&
            (player->invincibilityTimer <= 0)) {
            CollisionCheck_SetAC(play, &play->colChkCtx, &player->cylinder.base);

            if (player->invincibilityTimer < 0) {
                CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
            }
        }
    }

    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_10000000 | PLAYER_STATE1_20000000)) {
        player->actor.colChkInfo.mass = MASS_IMMOVABLE;
    } else {
        player->actor.colChkInfo.mass = 50;
    }

    Collider_ResetCylinderAC(play, &player->cylinder.base);
}

static void DummyPlayer_DrawSpinAttackEffects(Player* player, PlayState* play) {
    bool isCharging = (player->stateFlags1 & PLAYER_STATE1_CHARGING_SPIN_ATTACK) != 0;
    bool isSpinAttacking = (player->meleeWeaponState != 0) &&
                           (player->meleeWeaponAnimation == PLAYER_MWA_SPIN_ATTACK_1H ||
                            player->meleeWeaponAnimation == PLAYER_MWA_SPIN_ATTACK_2H ||
                            player->meleeWeaponAnimation == PLAYER_MWA_BIG_SPIN_1H ||
                            player->meleeWeaponAnimation == PLAYER_MWA_BIG_SPIN_2H);

    if (!isCharging && !isSpinAttacking) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL25_Xlu(play->state.gfxCtx);

    if (isSpinAttacking) {
        bool isGreatSpin = (player->meleeWeaponAnimation == PLAYER_MWA_BIG_SPIN_1H ||
                            player->meleeWeaponAnimation == PLAYER_MWA_BIG_SPIN_2H);
        u32 scroll = play->gameplayFrames;

        Matrix_Push();
        Matrix_Translate(player->actor.world.pos.x, player->actor.world.pos.y + 20.0f, player->actor.world.pos.z,
                         MTXMODE_NEW);
        Matrix_RotateYS(player->actor.shape.rot.y, MTXMODE_APPLY);
        Matrix_Scale(0.035f, 0.035f, 0.035f, MTXMODE_APPLY);
        MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);

        gSPSegment(POLY_XLU_DISP++, 0x08,
                   (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0xFF - ((u16)(s32)(scroll * 30) & 0xFF), 0, 64,
                                                 32, 1, 0xFF - ((u16)(s32)(scroll * 20) & 0xFF), 0, 8, 8, -30, 0, -20,
                                                 0));

        if (isGreatSpin) {
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 170, 200);
            gDPSetEnvColor(POLY_XLU_DISP++, 255, 100, 0, 128);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gGreatSpinAttackDiskDL);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gGreatSpinAttackCylinderDL);
        } else {
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 170, 255, 255, 200);
            gDPSetEnvColor(POLY_XLU_DISP++, 0, 100, 255, 128);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gSpinAttackDiskDL);
            gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gSpinAttackCylinderDL);
        }
        Matrix_Pop();
    }

    if (isCharging) {
        Matrix_Push();
        Matrix_Mult(&player->leftHandMf, MTXMODE_NEW);

        if (player->heldItemAction == PLAYER_IA_SWORD_GILDED ||
            player->heldItemAction == PLAYER_IA_SWORD_TWO_HANDED) {
            Matrix_Translate(0.0f, 220.0f, 0.0f, MTXMODE_APPLY);
            Matrix_Scale(-1.2f, -0.8f, -0.6f, MTXMODE_APPLY);
            Matrix_RotateXS(0x4000, MTXMODE_APPLY);
        } else {
            Matrix_Translate(0.0f, 220.0f, 0.0f, MTXMODE_APPLY);
            Matrix_Scale(-0.7f, -0.6f, -0.4f, MTXMODE_APPLY);
            Matrix_RotateXS(0x4000, MTXMODE_APPLY);
        }

        f32 chargeScale;
        s32 y2Scroll;
        if (player->unk_B08 >= 0.85f) {
            chargeScale = (sSpinChargeScales[play->gameplayFrames & 7] * 6.0f) + 1.0f;
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 255, 255, 170, 255);
            gDPSetEnvColor(POLY_XLU_DISP++, 255, 100, 0, 128);
            y2Scroll = 40;
        } else {
            chargeScale = (sSpinChargeScales[play->gameplayFrames & 7] * 2.0f) + 1.0f;
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0x80, 170, 255, 255, 220);
            gDPSetEnvColor(POLY_XLU_DISP++, 0, 100, 255, 128);
            y2Scroll = 20;
        }

        Matrix_Scale(1.0f, chargeScale, chargeScale, MTXMODE_APPLY);
        MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
        gSPSegment(POLY_XLU_DISP++, 0x09,
                   (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, (play->gameplayFrames * 5) & 0xFF, 0, 32, 32, 1,
                                                 (play->gameplayFrames * 20) & 0xFF,
                                                 (play->gameplayFrames * y2Scroll) & 0xFF, 8, 8, 5, 0, 20, y2Scroll));
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gSpinAttackChargingDL);
        Matrix_Pop();
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

struct OverheadRenderBuffer {
    Mtx mtx;
    Vtx vtx[4 * 22]; // Up to 20 hearts + 2 magic bar quads
};

static std::map<uint32_t, OverheadRenderBuffer> sOverheadBuffers;

static void DummyPlayer_DrawOverheadStatus(Player* player, PlayState* play, const AnchorClient& client) {
    // Only show within readable distance, matching NameTag fade behavior
    if (player->actor.xyzDistToPlayerSq > 440000.0f) {
        return;
    }

    float alpha = 1.0f;
    if (player->actor.xyzDistToPlayerSq > 360000.0f) {
        alpha = (440000.0f - player->actor.xyzDistToPlayerSq) / 80000.0f;
    }
    u8 alphaByte = (u8)(255.0f * alpha);
    if (alphaByte == 0) {
        return;
    }

    s16 healthCapacity = std::clamp<s16>(client.healthCapacity / 0x10, 1, 20);
    s16 health = std::clamp<s16>(client.health, 0, client.healthCapacity);
    s16 fullHearts = health / 0x10;
    s16 fracHeart = health % 0x10;
    if (fracHeart == 0) {
        fullHearts--;
    }

    bool hasMagic = (client.magicLevel > 0);
    s16 maxMagic = client.magicLevel * 0x30;
    s16 curMagic = std::clamp<s16>(client.magic, 0, maxMagic);

    s16 heartsPerRow = std::min<s16>(healthCapacity, 10);
    s16 numRows = (healthCapacity + 9) / 10;
    const s16 heartSize = 10;
    const s16 heartStep = 9;
    s16 totalWidth = heartsPerRow * heartStep;

    float posY = player->actor.world.pos.y;
    if (player->actor.flags & ACTOR_FLAG_ATTENTION_ENABLED) {
        posY = std::max(posY, player->actor.focus.pos.y);
    }
    // Position right above the nametag (nametag sits around +46..+64)
    posY += 66.0f;

    bool mirrorWorld = CVarGetInteger("gModes.MirroredWorld.State", 0) != 0;
    float scale = 0.50f;

    OverheadRenderBuffer& buf = sOverheadBuffers[client.clientId];
    Mtx* mtx = &buf.mtx;
    Vtx* vtx = buf.vtx;

    FrameInterpolation_RecordOpenChild(&player->actor, 10001);

    Matrix_Push();
    Matrix_Translate(player->actor.world.pos.x, posY, player->actor.world.pos.z, MTXMODE_NEW);
    Matrix_ReplaceRotation(&play->billboardMtxF);
    Matrix_Scale(scale * (mirrorWorld ? -1.0f : 1.0f), -scale, 1.0f, MTXMODE_APPLY);
    Matrix_Translate(-(float)totalWidth / 2.0f, -(float)(numRows * heartStep + (hasMagic ? 7 : 0)), 0.0f,
                     MTXMODE_APPLY);
    Matrix_ToMtx(mtx);
    Matrix_Pop();

    // Build heart quad vertices
    for (s16 i = 0; i < healthCapacity; i++) {
        s16 row = i / 10;
        s16 col = i % 10;
        Ship_CreateQuadVertexGroup(&vtx[i * 4], col * heartStep, row * heartStep, heartSize, heartSize, 0);
        // Heart textures are 16x16
        vtx[i * 4 + 1].v.tc[0] = 16 << 5;
        vtx[i * 4 + 2].v.tc[1] = 16 << 5;
        vtx[i * 4 + 3].v.tc[0] = 16 << 5;
        vtx[i * 4 + 3].v.tc[1] = 16 << 5;
    }

    // Build magic bar quad vertices right below the hearts
    if (hasMagic) {
        s16 magicY = numRows * heartStep + 2;
        s16 barMaxW = (client.magicLevel >= 2) ? 90 : 54;
        s16 barStartX = (totalWidth - barMaxW) / 2;
        s16 fillW = (maxMagic > 0) ? (s16)((curMagic * (barMaxW - 2)) / maxMagic) : 0;

        s32 bgIdx = healthCapacity * 4;
        s32 fgIdx = (healthCapacity + 1) * 4;
        Ship_CreateQuadVertexGroup(&vtx[bgIdx], barStartX, magicY, barMaxW, 6, 0);
        Ship_CreateQuadVertexGroup(&vtx[fgIdx], barStartX + 1, magicY + 1, fillW, 4, 0);
    }

    OPEN_DISPS(play->state.gfxCtx);

    POLY_XLU_DISP = Gfx_SetupDL39(POLY_XLU_DISP);
    gDPSetAlphaCompare(POLY_XLU_DISP++, G_AC_NONE);
    gSPSetGeometryMode(POLY_XLU_DISP++, G_ZBUFFER);
    gSPMatrix(POLY_XLU_DISP++, mtx, G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // Draw hearts (up to 10 per row, max 20 hearts -> max 80 vertices, load in batches of <= 10 quads = 40 vtx)
    if (client.doubleDefense > 0) {
        gDPSetCombineLERP(POLY_XLU_DISP++, ENVIRONMENT, PRIMITIVE, TEXEL0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0,
                          ENVIRONMENT, PRIMITIVE, TEXEL0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, alphaByte);
        gDPSetEnvColor(POLY_XLU_DISP++, 200, 0, 0, 255);
    } else {
        gDPSetCombineLERP(POLY_XLU_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                          PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 70, 50, alphaByte);
        gDPSetEnvColor(POLY_XLU_DISP++, 50, 40, 60, 255);
    }

    TexturePtr lastTex = nullptr;
    for (s16 i = 0; i < healthCapacity; i++) {
        if (i % 10 == 0) {
            s16 batchCount = std::min<s16>(healthCapacity - i, 10) * 4;
            gSPVertex(POLY_XLU_DISP++, (uintptr_t)&vtx[i * 4], batchCount, 0);
        }

        TexturePtr heartTex;
        if (client.doubleDefense > 0) {
            if (i < fullHearts) {
                heartTex = gDefenseHeartFullTex;
            } else if (i == fullHearts) {
                heartTex = sOverheadHeartDDTextures[fracHeart];
            } else {
                heartTex = gDefenseHeartEmptyTex;
            }
        } else {
            if (i < fullHearts) {
                heartTex = gHeartFullTex;
            } else if (i == fullHearts) {
                heartTex = sOverheadHeartTextures[fracHeart];
            } else {
                heartTex = gHeartEmptyTex;
            }
        }

        if (heartTex != lastTex) {
            lastTex = heartTex;
            gDPLoadTextureBlock(POLY_XLU_DISP++, heartTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
        }

        s16 vStart = (i % 10) * 4;
        gSP1Quadrangle(POLY_XLU_DISP++, vStart, vStart + 2, vStart + 3, vStart + 1, 0);
    }

    // Draw magic bar below hearts using solid primitive color combine
    if (hasMagic) {
        gDPPipeSync(POLY_XLU_DISP++);
        gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                          PRIMITIVE);
        gSPVertex(POLY_XLU_DISP++, (uintptr_t)&vtx[healthCapacity * 4], 8, 0);

        // Dark border background
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 20, 20, 30, (u8)(200.0f * alpha));
        gSP1Quadrangle(POLY_XLU_DISP++, 0, 2, 3, 1, 0);

        // Magic fill (Chateau Romani blue or standard green)
        if (curMagic > 0) {
            if (client.chateauActive) {
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 128, 180, 255, alphaByte);
            } else {
                gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 0, 210, 40, alphaByte);
            }
            gSP1Quadrangle(POLY_XLU_DISP++, 4, 6, 7, 5, 0);
        }
    }

    gSPPopMatrix(POLY_XLU_DISP++, G_MTX_MODELVIEW);

    CLOSE_DISPS(play->state.gfxCtx);

    FrameInterpolation_RecordCloseChild();
}

void DummyPlayer_Draw(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;

    if (!Anchor::Instance->clients.contains(DUMMY_CLIENT_ID)) {
        Actor_Kill(actor);
        return;
    }

    AnchorClient& client = Anchor::Instance->clients[DUMMY_CLIENT_ID];

    if (client.sceneId != gPlayState->sceneId || !client.online || !client.isSaveLoaded) {
        return;
    }

    // Draw Goron curled rolling ball when partner is rolling in Goron form
    if ((player->transformation == PLAYER_FORM_GORON) && (player->stateFlags3 & PLAYER_STATE3_1000)) {
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL25_Opa(play->state.gfxCtx);
        Gfx_SetupDL25_Xlu(play->state.gfxCtx);

        f32 spB8 = player->unk_ABC + 1.0f;
        f32 spB4 = 1.0f - (player->unk_ABC * 0.5f);

        Matrix_Push();
        Matrix_Translate(player->actor.world.pos.x,
                         player->actor.world.pos.y + (1200.0f * player->actor.scale.y * spB8),
                         player->actor.world.pos.z, MTXMODE_NEW);

        if (player->unk_B86[0] != 0) {
            Matrix_RotateYS(player->unk_B28, MTXMODE_APPLY);
            Matrix_RotateXS(player->unk_B86[0], MTXMODE_APPLY);
            Matrix_RotateYS(-player->unk_B28, MTXMODE_APPLY);
        }

        Matrix_RotateYS(player->actor.shape.rot.y, MTXMODE_APPLY);
        Matrix_RotateZS(player->actor.shape.rot.z, MTXMODE_APPLY);
        Matrix_Scale(player->actor.scale.x * spB4 * 1.15f, player->actor.scale.y * spB8 * 1.15f,
                     CLAMP_MIN(spB8, spB4) * player->actor.scale.z * 1.15f, MTXMODE_APPLY);
        Matrix_RotateXS(player->actor.shape.rot.x, MTXMODE_APPLY);
        Scene_SetRenderModeXlu(play, 0, 1);
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
        MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gLinkGoronCurledDL);

        if (player->unk_B86[1] != 0) {
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)object_link_goron_DL_00C540);
        }
        Matrix_Pop();
        CLOSE_DISPS(play->state.gfxCtx);
    } else {
        Player_DrawGameplay(play, player, 1, gCullBackDList, Player_OverrideLimbDrawGameplayDefault);
    }

    DummyPlayer_DrawSpinAttackEffects(player, play);
    DummyPlayer_DrawOverheadStatus(player, play, client);
}

void DummyPlayer_Destroy(Actor* actor, PlayState* play) {
    Player* player = (Player*)actor;
    Effect_Destroy(play, player->meleeWeaponEffectIndex[0]);
    Effect_Destroy(play, player->meleeWeaponEffectIndex[1]);
    Effect_Destroy(play, player->meleeWeaponEffectIndex[2]);
}
