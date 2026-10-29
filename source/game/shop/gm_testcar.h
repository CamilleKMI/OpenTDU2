#pragma once

#include "filesystem/bank.h"
#include "game/avatar/seated_avatar.h"
#include "game/cinematic/cinematic_car.h"
#include "game/freeride/chase_camera.h"
#include "game/freeride/freeride_hud.h"
#include "game/gm_base.h"
#include "physics/vehicle_collision.h"
#include "physics/vehicle_dynamics.h"
#include "render/eden/eden_scene_view.h"
#include "render/eden/eden_weather.h"

// "TESTCAR" (mode 7): the test drive of a dealer car (docs/re/story_spec.md
// 5.2.5). Entered by the shop FSCommand tstk (FUN_009f3b30, the request in
// GetTestDriveRequest()); mode vtable 0x00f1bc50 / +0x90 0x00f1bc9c [CODE]:
//   - init FUN_00a90cf0: the test car becomes the current car
//     (FUN_00ab9be0(DAT_00fd92a0, colour, rims, interior)), FUN_00a945f0
//     (driving mode objects);
//   - activate FUN_00a90990: ambient 'bird' / 'papr' objects on, FUN_00a94320,
//     FUN_00a906a0 (passenger FUN_00b84590(3000, 0xfa4713bc_ffa1845b,
//     DAT_00fdb450 = the shop PNJ config) seated by FUN_00b9fa00),
//     FUN_00960280(6, 1): the car put at the spot (an entity 'CARP'
//     "PosCarParking" of the spot bank, else FUN_00a39630: the spot position
//     snapped on the road network), start time +0x1b07f0 = -1, the driver
//     seated (FUN_00baec60 / FUN_00bbb810), HUD /:MinCd / /:SecCd / /:CentCd
//     bindings and the clip /HUD/time_cd (FUN_00a12300(1)), /:IsTest true;
//   - tick FUN_00a90850: first tick start = now, end = now + DAT_00f77694
//     (120 s); remaining = end - now (>= 0) -> FUN_00a11120 -> the HUD
//     digits; paused (DAT_011fe90e): end += dt; now > end -> FUN_00a90300;
//   - FSCommands FUN_00a90570: "quit" -> FUN_00a90300; "sbdd" -> /:cur_gm
//     "test", /:IsRestart false, /:PauseQuit = DB 0x207;
//   - end FUN_00a90300: the spot of DAT_01025784 entered again
//     (FUN_00b30030(1, 0) -> the spot's mode, SHOP), else mode 3 SHOP
//     (DAT_00f77698 == -1);
//   - deactivate FUN_00a90730: /:IsTest false, passenger removed
//     (FUN_00b9f950 / FUN_00b84530); exit FUN_00a90c70: the current car
//     restored (FUN_00ab9b70) only at story steps 11 / -1.
// OpenTDU2: FREERIDE driving (VehicleDynamics with the DBCarPhysicsData row of
// the car, chase / cockpit cameras, HUD + GAUGES), the player avatar at the
// driver seat and the passenger at the passenger seat (SeatedAvatar).
// Command line: -gm TESTCAR (test: -testcar id, default 371) ; -testtime s
// (test: duration instead of 120 s).
class GMTestCar : public GameMode {
public:
    GMTestCar();
    ~GMTestCar();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;

private:
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
    SeatedAvatar driver;
    SeatedAvatar passenger;
    bool bPassengerSeated = false;

    float startTime = -1.0f; // +0x1b07f0
    float endTime = 0.0f;    // +0x1b07f4
    float clock = 0.0f;
    float duration = 120.0f;
    bool bEnded = false;
    bool bLoaded = false;

    struct WheelNode {
        int32_t RimNode = -1;
        Matrix4x4f RimRest;
        Matrix4x4f TireRest;
        bool bLeft = true;
        bool bFront = true;
    };
    std::vector<WheelNode> wheelNodes;

    void setupVehicle();
    void updateWheels();
    void streamWorld( const Vector3f &position );
    void endTestDrive( const char *pReason );
};
