#pragma once

#include "game/cinematic/cinematic_car.h"
#include "game/freeride/car_accessories.h"
#include "game/freeride/chase_camera.h"
#include "game/freeride/freeride_hud.h"
#include "game/gm_base.h"
#include "game/worldviewer/spot_exterior.h"
#include "game/worldviewer/weather_controls.h"
#include "physics/vehicle_collision.h"
#include "physics/vehicle_dynamics.h"
#include "render/eden/eden_scene_view.h"
#include "world/road_network.h"

#include <unordered_set>

// "FREE_DRIVE": OpenTDU2 developer / sandbox tool, not a TDU2 game mode (like
// the sandbox of OpenTDU, GMSandbox): drive any car of the database on the
// roads of any island, switch car at any time, no story, no timer.
// Built from the pieces of the story FREERIDE (left untouched) and of the
// WORLD_VIEWER:
//   - world: EdenSceneView / EdenWorld of the chosen island (areas, sectors
//     streamed around the car, island heightfield, ocean, sky), spot
//     exteriors (exterior/<spot bank>.bnk) with their <spot>_sc_*.shk
//     collision, the time of day / weather (WeatherControls, same panel as
//     the World Viewer);
//   - car: CinematicCar (vehicules/high/<_car_file_name>.bnk, interior
//     bank, DB rims (DBRims of _default_rims) and tyres, DB colours
//     DBCarsAndColors _color_id_1), VehicleDynamics with the car's
//     DBCarPhysicsData row (LoadVehicleSetup), sector .shk / -R.shk + library
//     collision, the CamerasInGame.xmb cameras of the car (_camera), the HUD
//     with the car gauges (frontend/hires/gauges/<_hud_file_name>.bnk), car
//     lights, accessories (roof, windows, blinkers: CarAccessories).
// Start: a spots.bin spot (-spot <bank name>, else the spot with the most
// sector banks installed around it), at its street side transform (record
// +0x28 / +0x38, the TESTCAR start) snapped on the nearest road surface
// (sector -R.shk triangles) [INF: the game's road network projection
// FUN_00a39630 needs the road graph, which OpenTDU2 does not load;
// roadinspection.pth holds only segment indices of that graph].
//
// Command line: -gm FREE_DRIVE -island ibiza|hawai|casino (default ibiza),
// -car <_car_file_name | _id_car | text of "brand model"> (default
// California), -color <index | _color_id_1 | colour name>, -spot <bank>,
// -carpos x,y,z [-carheading deg] (start pose, snapped on the road),
// -sectorradius n (default 1), -noexteriors, -nopanel, -listcars (logs every
// DB car with the banks it needs), the WeatherControls options (-time,
// -timespeed, -intempery, -nextintempery, -weather, -rain).
//
// Controls (no numpad needed): Up / W throttle, Down / S brake / reverse,
// Left / Right / A / D steer, Left Shift handbrake, C camera (Default_KB.xmb
// bindings + WASD), accessories 4 (menu) then 8 / 2 / 4 / 6;
// F3 or B (BACK_ON_TRACK of Default_KB.xmb): back on the road; F4 / Shift+F4: next /
// previous car; F8: next colour; F9 or Enter (CAR_LIGHT): lights off / auto / high beam;
// F5 / F6 / F7: Ibiza / Hawai / Casino; F2: panel; F1: debug UI.
class GMFreeDrive : public GameMode {
public:
    GMFreeDrive();
    ~GMFreeDrive();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawUI() override;
    void drawDebug() override;

private:
    // A DBCarPhysicsData row whose high resolution bank is installed.
    struct CarEntry {
        int32_t IdCar = -1;
        std::string FileName; // _car_file_name
        std::string HudFile;  // _hud_file_name
        std::string Brand;    // DBBrands _manufacturer_name
        std::string Label;    // brand + _car_model + _car_version
        std::string Missing;  // optional banks not installed (rims, interior, gauges)
    };
    // DBCarsAndColors row of the current car.
    struct ColorEntry {
        std::string Name;    // _color_name
        std::string ColorId; // _color_id_1 (material of <car>_color.2dm)
    };
    // spots.bin record (0x110 bytes): u64 id, char bank[8], vec3 position,
    // heading +0x20, street side position +0x28 / heading +0x38 [DATA,
    // IntroCinematic::ReadSpot].
    struct Spot {
        uint64_t Id = 0;
        std::string Bank;
        Vector3f Position;
        float Heading = 0.0f;
        Vector3f Position2;
        float Heading2 = 0.0f;
        uint32_t NumSectors = 0; // sector banks present in the 3 x 3 sectors around
        bool bHasSector = false;
        std::string ExteriorPath;
    };
    struct WheelNode {
        int32_t RimNode = -1;
        Matrix4x4f RimRest;
        Matrix4x4f TireRest;
        bool bLeft = true;
        bool bFront = true;
    };

    // World
    EdenSceneView view;
    WeatherControls weather;
    const WeatherPreset *pWeather = nullptr;
    std::unique_ptr<VehicleWorldCollision> pCollision; // one per island (library of its commonworld.bnk)
    RoadNetwork roads; // <island>.bnk road network (tunnel detection, docs/re/car_lights_spec.md 6)
    std::vector<Spot> spots;
    std::vector<std::unique_ptr<SpotExterior>> exteriors;
    std::unordered_set<std::string> exteriorCollisions; // exterior banks whose .shk were added
    std::string island;
    std::string pendingIsland;
    std::string statusText;
    bool bWorldReady = false;
    bool bFirstIsland = true;
    bool bExteriors = true;
    int32_t sectorRadius = 1;
    int32_t lastSectorKey = INT32_MIN;
    int32_t selectedSpot = -1;

    // Car
    std::vector<CarEntry> cars;
    std::vector<ColorEntry> colors;
    int32_t currentCar = -1;
    int32_t currentColor = 0;
    int32_t pendingCar = -1;
    int32_t pendingColor = -1;
    int32_t pendingSpot = -1;
    bool bPendingRespawn = false;
    CinematicCar car;
    CinematicCar::Desc carDesc;
    VehicleSetup setup;
    VehicleDynamics vehicle;
    ChaseCamera chase;
    FreeRideHud hud;
    CarAccessories accessories;
    std::vector<WheelNode> wheelNodes;
    EdenSceneView::Camera camera;
    Matrix4x4f carWorld;
    bool bCarReady = false;
    bool bEnvironmentAssigned = false;
    float lastBrakeInput = 0.0f;
    int32_t lightMode = 1;   // CinematicCar::setHeadlights: 0 off, 1 weather driven, 2 high beam
    bool bHazard = false;    // panel: both blinkers
    bool bShowPanel = true;
    bool bShowHud = true;
    char carFilter[64] = {};

    void buildCarList();
    int32_t findCar( const char *pText ) const;
    void buildColorList();
    int32_t findColor( const char *pText ) const;

    bool loadIsland( const std::string &name );
    void unloadIsland();
    void loadSpots();
    void streamWorld( const Vector3f &position, bool bForce );
    void streamExteriors( const Vector3f &position );

    bool loadCar( int32_t index );
    void unloadCar();
    void setupVehicle();
    void updateWheels();
    void applyColor( int32_t index );

    // Car on the road surface nearest to position (heading along the road,
    // the direction closest to preferredHeading), else at position on the
    // ground. bSpot: a spot start / teleport (spot heading kept near the
    // spot), else "back on the road".
    void placeCar( const Vector3f &position, float preferredHeading, bool bSpot, const Vector3f &velocity );
    bool findRoad( const Vector3f &position, float preferredHeading, float maxDistance, bool bSectorRoadsOnly,
                   bool bKeepHeadingNear, Vector3f &outPosition, float &outHeading ) const;
    bool groundHeight( const Vector3f &position, float &outHeight ) const;
    void teleportToSpot( int32_t index );
    float getCarHeading() const;
};
