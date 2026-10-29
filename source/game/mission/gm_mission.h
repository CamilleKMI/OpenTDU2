#pragma once

#include "filesystem/bank.h"
#include "core/xmb.h"
#include "game/avatar/facefx.h"
#include "game/avatar/seated_avatar.h"
#include "game/cinematic/cinematic_audio.h"
#include "game/cinematic/cinematic_car.h"
#include "game/freeride/chase_camera.h"
#include "game/freeride/freeride_hud.h"
#include "game/gm_base.h"
#include "game/mission/cinematic_props.h"
#include "game/mission/event_reactions.h"
#include "game/mission/ige_file.h"
#include "physics/vehicle_collision.h"
#include "physics/vehicle_dynamics.h"
#include "render/eden/eden_scene_view.h"
#include "render/eden/eden_weather.h"

// "MISSION" (mode 4) entered by story step 4 "Driving Tess" (FUN_00b10da0
// case 5, docs/re/story_spec.md 4): event TESSEVEN (DBEvent[21], type 13,
// PNJ 0x293 STO_Tess_Phone), script ig2/tesseven.ig2 (IgeFile), event pack
// scenarics/packs/pack_events13.bnk (FUN_00a24960: "pack_events%02d" of the
// event type when gmth == 7).
// The mission object (mode +0x1b0790, CGameMech FUN_00a157a0, handler
// FUN_00a1abe0) [CODE]: 2 loading map (world streaming at the start) -> 0xd
// C_INTRO_E13_<pnj> (not in pack_events13: skipped) -> 4 Waiting_Start
// (passenger PNJ created FUN_00b84590(3000, ...) and seated FUN_00b9fa00) ->
// 5 racing -> finished racer (FUN_00a12bb0) -> 8 -> 9 (results; passenger
// removed); the story tick (FUN_00b10da0 case 5) adds the goal 4032, the
// C_TRUCK ambient cinematic at ECH_IA_1243I, Tess' reactions (40 s / 25 s
// timers) and, at state 9 with success (+0xf0), story step 5 (CINE kind 2).
// Command line: -storystep 4 or -gm MISSION; -startnearfinish d (test: start
// d m before the finish checkpoint); -reactiontimer s (test: first reaction
// timer instead of 40 s).
class GMMission : public GameMode {
public:
    GMMission();
    ~GMMission();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawDebug() override;

private:
    // CGameMech states used by this event (FUN_00a1abe0) [CODE].
    enum class eMechState : int32_t { LoadingMap = 2, WaitingStart = 4, Race = 5, Finished = 8, Results = 9 };

    struct EventInfo {
        std::string File = "TESSEVEN";
        int32_t Type = 13;
        std::string PnjConfig = "STO_Tess_Phone";
    } event;

    IgeFile ige;
    Bank pack;
    BankHashcodes packHashcodes;
    XmbDocument packEntities;
    CinematicSounds packSounds;
    EventReactions reactions;
    CinematicProps truck;

    EdenSceneView view;
    WeatherLibrary gameWeathers;
    const WeatherPreset *pWeather = nullptr;
    CinematicCar car;
    CinematicCar::Desc carDesc;
    VehicleSetup setup;
    VehicleDynamics vehicle;
    VehicleWorldCollision collision;
    ChaseCamera chase;
    FreeRideHud hud;
    EdenSceneView::Camera camera;
    Matrix4x4f carWorld;
    bool bEnvironmentAssigned = false;
    int32_t lastSectorKey = -1;

    EdenAvatarLibrary avatars;
    Bank driverBank;
    FaceFxLibrary faceFx;
    SeatedAvatar driver;
    SeatedAvatar passenger;
    bool bPassengerSeated = false;

    eMechState mechState = eMechState::LoadingMap;
    bool bStoryStarted = false; // story this[6]
    float storyTimer = 0.0f;    // story this[7]
    bool bSuccess = false;      // mission +0xf0
    bool bStartAllowed = false; // mission +0x65
    bool bStoryDone = false;
    Vector3f previousCarPosition;
    float raceTime = 0.0f;
    bool bLoaded = false;

    struct WheelNode {
        int32_t RimNode = -1;
        Matrix4x4f RimRest;
        Matrix4x4f TireRest;
        bool bLeft = true;
        bool bFront = true;
    };
    std::vector<WheelNode> wheelNodes;

    bool loadEvent();
    void setupVehicle();
    void updateWheels();
    void streamWorld( const Vector3f &position );
    void setMechState( eMechState state );
    void storyTick( float deltaTime );
    void creditReward();
    bool checkFinish( const Vector3f &from, const Vector3f &to ) const;
    void startPose( Vector3f &outPosition, Quaternionf &outOrientation ) const;
};
