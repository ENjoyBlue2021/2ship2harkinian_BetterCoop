#include "2s2h/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include <cmath>
#include <cstdlib>

extern "C" {
#include "variables.h"
#include "functions.h"
#include "overlays/actors/ovl_En_Test4/z_en_test4.h"
extern PlayState* gPlayState;
void EnTest4_HandleEvents(struct EnTest4* actor, PlayState* play);
}

/**
 * SYNC_TIME
 *
 * Host-authoritative time & cycle state synchronization.
 * Host broadcasts time, day, time speed, and cycle flags periodically or immediately on jumps
 * (Song of Double Time, Inverted Song of Time, Song of Time, etc.).
 * Client smoothly locks its clock to the Host's clock.
 */

static u32 sTimeSyncFrameCounter = 0;
static u16 sLastSentTime = 0;
static s32 sLastSentDay = -1;
static s32 sLastSentIsNight = -1;
static s32 sLastSentTimeSpeed = 0;
static u8 sLastSentFirstCycle = 255;
static u8 sLastSentSnowhead = 255;

void Anchor::ResetHostTimeSyncTracker() {
    sTimeSyncFrameCounter = 0;
    sLastSentDay = -1;
    sLastSentIsNight = -1;
    sLastSentFirstCycle = 255;
    sLastSentSnowhead = 255;
}

void Anchor::TickHostTimeSync() {
    if (!IsSaveLoaded() || !IsHost() || !roomState.syncItemsAndFlags) {
        return;
    }

    if (gSaveContext.nextDayTime != NEXT_TIME_NONE ||
        (gPlayState != nullptr &&
         (gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || gPlayState->transitionMode != TRANS_MODE_OFF))) {
        return;
    }

    sTimeSyncFrameCounter++;

    u16 curTime = gSaveContext.save.time;
    s32 curDay = gSaveContext.save.day;
    s32 curIsNight = gSaveContext.save.isNight;
    s32 curTimeSpeed = gSaveContext.save.timeSpeedOffset;
    u8 curFirstCycle = gSaveContext.save.isFirstCycle;
    u8 curSnowhead = gSaveContext.save.snowheadCleared;

    // Detect sudden time jumps (e.g., Song of Double Time, Song of Time, Granny's story)
    int32_t rawDiff = static_cast<int32_t>(curTime) - static_cast<int32_t>(sLastSentTime);
    if (rawDiff > 32768) {
        rawDiff -= 65536;
    } else if (rawDiff < -32768) {
        rawDiff += 65536;
    }

    bool stateChanged = (curDay != sLastSentDay) || (curIsNight != sLastSentIsNight) ||
                        (curTimeSpeed != sLastSentTimeSpeed) || (curFirstCycle != sLastSentFirstCycle) ||
                        (curSnowhead != sLastSentSnowhead) || (std::abs(rawDiff) > 120);

    // Send immediately on state change / time jump, or every ~15 frames (~0.75s at 20fps game logic)
    if (stateChanged || sTimeSyncFrameCounter >= 15) {
        sTimeSyncFrameCounter = 0;
        sLastSentTime = curTime;
        sLastSentDay = curDay;
        sLastSentIsNight = curIsNight;
        sLastSentTimeSpeed = curTimeSpeed;
        sLastSentFirstCycle = curFirstCycle;
        sLastSentSnowhead = curSnowhead;
        SendPacket_SyncTime();
    }
}

void Anchor::SendPacket_SyncTime() {
    if (!IsSaveLoaded() || !IsHost()) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = SYNC_TIME;
    payload["quiet"] = true;
    payload["time"] = gSaveContext.save.time;
    payload["day"] = gSaveContext.save.day;
    payload["eventDayCount"] = gSaveContext.save.eventDayCount;
    payload["isNight"] = gSaveContext.save.isNight;
    payload["timeSpeedOffset"] = gSaveContext.save.timeSpeedOffset;
    payload["isFirstCycle"] = gSaveContext.save.isFirstCycle;
    payload["snowheadCleared"] = gSaveContext.save.snowheadCleared;
    payload["hasTatl"] = gSaveContext.save.hasTatl;

    QueueOutgoingPacket(payload);
}

void Anchor::HandlePacket_SyncTime(nlohmann::json payload) {
    if (!IsSaveLoaded() || IsHost() || !roomState.syncItemsAndFlags) {
        return;
    }

    u16 hostTime = payload.value("time", gSaveContext.save.time);
    s32 hostDay = payload.value("day", gSaveContext.save.day);
    s32 hostEventDayCount = payload.value("eventDayCount", gSaveContext.save.eventDayCount);
    s32 hostIsNight = payload.value("isNight", gSaveContext.save.isNight);
    s32 hostTimeSpeed = payload.value("timeSpeedOffset", gSaveContext.save.timeSpeedOffset);
    u8 hostFirstCycle = payload.value("isFirstCycle", gSaveContext.save.isFirstCycle);
    u8 hostSnowhead = payload.value("snowheadCleared", gSaveContext.save.snowheadCleared);
    u8 hostHasTatl = payload.value("hasTatl", gSaveContext.save.hasTatl);

    bool dayChanged = (gSaveContext.save.day != hostDay);

    gSaveContext.save.day = hostDay;
    gSaveContext.save.eventDayCount = hostEventDayCount;
    gSaveContext.save.isNight = hostIsNight;
    gSaveContext.save.timeSpeedOffset = hostTimeSpeed;
    gSaveContext.save.isFirstCycle = hostFirstCycle;
    gSaveContext.save.snowheadCleared = hostSnowhead;
    gSaveContext.save.hasTatl = hostHasTatl;

    if (dayChanged && gPlayState != nullptr) {
        Environment_NewDay(&gPlayState->envCtx);
    }

    // Circular distance on 16-bit zelda_time
    int32_t diff = static_cast<int32_t>(hostTime) - static_cast<int32_t>(gSaveContext.save.time);
    if (diff > 32768) {
        diff -= 65536;
    } else if (diff < -32768) {
        diff += 65536;
    }

    // Snap time if day changed or clock drifted by more than ~12 units
    bool snapped = dayChanged || std::abs(diff) > 12;
    if (snapped) {
        gSaveContext.save.time = hostTime;
        gSaveContext.skyboxTime = hostTime;
    } else if (std::abs(diff) > 2) {
        gSaveContext.save.time = static_cast<u16>(static_cast<int32_t>(gSaveContext.save.time) + (diff / 2));
        gSaveContext.skyboxTime = gSaveContext.save.time;
    }

    if (snapped && gPlayState != nullptr) {
        Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_SWITCH].first;
        while (actor != nullptr) {
            if (actor->id == ACTOR_EN_TEST4) {
                EnTest4* test4 = (EnTest4*)actor;
                if (test4->actionFunc == EnTest4_HandleEvents) {
                    test4->prevTime = CURRENT_TIME;
                    test4->prevBellTime = CURRENT_TIME;
                    test4->daytimeIndex = (CURRENT_TIME > CLOCK_TIME(18, 0) || CURRENT_TIME < CLOCK_TIME(6, 0))
                                              ? THREEDAY_DAYTIME_NIGHT
                                              : THREEDAY_DAYTIME_DAY;
                }
            }
            actor = actor->next;
        }
    }

    bool finalHours = (gSaveContext.save.day >= 4) ||
                      (gSaveContext.save.day == 3 && gSaveContext.save.time >= (CLOCK_TIME(0, 0) + 5) &&
                       gSaveContext.save.time < CLOCK_TIME(6, 0));
    if (!finalHours && gSaveContext.timerStates[TIMER_ID_MOON_CRASH] != TIMER_STATE_OFF) {
        gSaveContext.timerStates[TIMER_ID_MOON_CRASH] = TIMER_STATE_OFF;
    }
}
