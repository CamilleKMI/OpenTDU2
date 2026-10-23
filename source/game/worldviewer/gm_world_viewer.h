#pragma once

#include "game/gm_base.h"
#include "game/worldviewer/spot_exterior.h"
#include "render/eden/eden_scene_view.h"
#include "render/eden/eden_weather.h"

// "WORLD_VIEWER": OpenTDU2 developer tool, not a TDU2 game mode (like the
// world viewer of OpenTDU's sandbox, GMSandbox + CameraMouse). A free-fly
// camera over one island streamed with EdenWorld (areas, sectors, .pmi
// instances, ocean, sky, weather, shadows), no car, no HUD, no Flash.
//
// Command line: -gm WORLD_VIEWER -island ibiza|hawai|casino (default ibiza),
// -spot <spots.bin bank name> start spot (default: the first spot whose sector
// bank is present), -campos x,y,z / -camyaw deg / -campitch deg,
// -sectorradius n (default 1), -weather <game_weathers name> (default "14"),
// -flyspeed m/s (default 50), -noexteriors (no spot exteriors).
//
// Controls (OpenTDU CameraMouse: look while a mouse button is held, keys
// move along the view):
//   right mouse button held (or left button outside the panel): look
//   (relative mouse mode, the cursor is hidden while looking);
//   Tab: toggle a captured mouse (look without holding a button);
//   W / S (or Up / Down): forward / backward along the view direction;
//   A / D (or Left / Right): strafe; E / Space / Page Up: up;
//   Q / Ctrl / Page Down: down; Shift: speed x2; mouse wheel: base speed
//   x1.1 / /1.1 (OpenTDU DAT_00f4174c / DAT_00f417e4); F2: hide the panel;
//   F5 / F6 / F7: switch to Ibiza / Hawai / Casino (also in the panel).
class GMWorldViewer : public GameMode {
public:
    GMWorldViewer();
    ~GMWorldViewer();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawUI() override;

    static constexpr const char *kIslands[] = { "ibiza", "hawai", "casino" };

private:
    // spots.bin record (".SPT" / "SPTA", 0x110 bytes: u64 id, char bank[8],
    // vec3 position, f32, f32 heading) [DATA, same reader as FirstOutro].
    struct Spot {
        uint64_t Id = 0;
        std::string Bank;
        Vector3f Position;
        float Heading = 0.0f;
        bool bHasSector = false; // sector bank of the position present
        uint32_t NumSectors = 0; // sector banks present in the 3 x 3 sectors around
        std::string ExteriorPath; // exterior/<bank>.bnk when present
    };

    EdenSceneView view;
    WeatherLibrary gameWeathers;
    const WeatherPreset *pWeather = nullptr;
    std::vector<Spot> spots;
    std::vector<std::unique_ptr<SpotExterior>> exteriors;
    bool bExteriors = true;
    bool bGroundClamp = true; // camera kept 1.5 m above the loaded sector heightfields
    std::string island;
    std::string pendingIsland;
    std::string statusText;
    bool bWorldReady = false;

    // Free-fly camera
    Vector3f position;
    float yaw = 0.0f;   // forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch)
    float pitch = 0.0f;
    float baseSpeed = 50.0f; // m/s [INF: developer tool]
    float currentSpeed = 0.0f;
    float lookSensitivity = 0.0025f; // rad / pixel [INF]
    float fovDegrees = 60.0f;
    bool bLooking = false;
    bool bCaptured = false;
    bool bRelativeMouse = false;
    bool bShowPanel = true;
    int32_t sectorRadius = 1;
    int32_t lastSectorKey = INT32_MIN;
    int32_t selectedSpot = -1;
    int32_t selectedWeather = 0;

    bool loadIsland( const std::string &name );
    void unloadIsland();
    void loadSpots();
    void teleport( const Vector3f &target, float heading, float height );
    void updateCamera( float deltaTime );
    void streamWorld( bool bForce );
    void streamExteriors();
    void setRelativeMouse( bool bEnable );
    Vector3f getForward() const;
};
