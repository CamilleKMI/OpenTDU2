#pragma once

#include "game/cinematic/cinematic_car.h"
#include "game/freeride/car_accessories.h"
#include "game/freeride/chase_camera.h"
#include "game/freeride/fade_freeride.h"
#include "game/freeride/first_outro.h"
#include "game/freeride/freeride_hud.h"
#include "game/gm_base.h"
#include "physics/vehicle_collision.h"
#include "physics/vehicle_dynamics.h"
#include "render/eden/eden_scene_view.h"
#include "render/eden/eden_weather.h"
#include "world/road_network.h"

// "FREERIDE" (mode 1) entered by story step 2 "First Freeride"
// (FUN_00b10da0 case 3, docs: /tmp/claude-0/spec_freeride.md 1):
//   1. the spot cinematic C_FIRST_OUTRO of the EPH_L3DI house (the
//      California leaves the garage, 14.31 s), requested with FUN_00b99c20
//      and played by FUN_00a7cc70;
//   2. at its end (listener event 3) the game camera comes back
//      (FUN_00ca18b0), "/:IsWalkmode" false, pop_goal loc 5038 "Enjoy!",
//      TUTO_ENJOY, roof opened (FUN_00c3fcc0), timer 90 s;
//   3. when the timer runs out: C_FADE_FREERIDE (fade_freeride.bnk, 15 s,
//      white fade, Tess knocks) -> at its end story step 3 "Meeting Tess"
//      (game mode CINE, C_MEETING_TESS, game/cine/gm_cine.h).
// The player drives the current car (387 California) with VehicleDynamics
// on the streamed island (sector .shk / -R.shk collision), chase camera
// from CamerasInGame.xmb, HUD (FreeRideHud).
// Command line: -gm FREERIDE starts here; -skipcine starts at the end of
// C_FIRST_OUTRO; -weather <game_weathers name> (default "14" [INF]).
class GMFreeRide : public GameMode {
public:
    GMFreeRide();
    ~GMFreeRide();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawDebug() override;

private:
    enum class eState { Outro, Drive, Fade };

    // Story step 8 "Tuto FROG" (FUN_00b10da0 case 9) entered from the used
    // car dealer (GMShop): hand-off stub (docs/re/story_spec.md 5.3).
    bool bStep8Stub = false;
    std::string step8Text;

    EdenSceneView view;
    WeatherLibrary gameWeathers;
    const WeatherPreset *pWeather = nullptr;
    FirstOutro outro;
    FadeFreeride fade;
    bool bCineRequested = false;
    CinematicCar car;
    CinematicCar::Desc carDesc;
    VehicleSetup setup;
    VehicleDynamics vehicle;
    VehicleWorldCollision collision;
    RoadNetwork roads; // ibiza.bnk road network (tunnel detection)
    ChaseCamera chase;
    FreeRideHud hud;
    CarAccessories accessories;
    EdenSceneView::Camera camera;
    eState state = eState::Outro;
    float stepTimer = 0.0f;       // story step timer (this[7], 90 s)
    float fadeTime = 0.0f;
    float lastBrakeInput = 0.0f; // brake pedal of the last vehicle step (car brake lights)
    bool bEnvironmentAssigned = false;
    bool bLoaded = false;
    Matrix4x4f carWorld;
    int32_t lastSectorKey = -1;
    std::string shadowConfig;

    // Wheel nodes of the car model (D_RIM_xx and the tire node after it).
    struct WheelNode {
        int32_t RimNode = -1;
        Matrix4x4f RimRest;
        Matrix4x4f TireRest;
        bool bLeft = true;
        bool bFront = true;
    };
    std::vector<WheelNode> wheelNodes;

    void setupVehicle();
    std::string hudFileName() const;
    void startDriving();
    void updateWheels();
    void streamWorld( const Vector3f &position );
    void setShadowConfig( const std::string &name );
    Matrix4x4f outroCarWorld();
    bool surfaceHeight( float x, float z, float y, float &outHeight ) const;
};
