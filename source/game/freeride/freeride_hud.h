#pragma once

#include "filesystem/bank.h"
#include "flash/flash_definitions.h"
#include "flash/flash_player.h"

class Renderer2D;
class VehicleWorldCollision;

// FREERIDE overlay (docs: /tmp/claude-0/spec_freeride.md 4).
//   - HUD.vmf (frontend/hires/hud.bnk, GamePC.cpr AddFlashMovie "HUD" 1):
//     the root `default` frame hides every HUD clip; the game shows the
//     root clip HUD and its minimap frame in FREERIDE [INF: the visibility
//     calls are encrypted].
//   - GENERAL.vmf pop_goal: /:GoalTxt = loc 5038 "Enjoy!", goto "open"
//     (FUN_00d61790, 30 s auto close; FUN_00d61700 close_popup).
//   - GAUGES.vmf (frontend/hires/gauges.bnk) + the car gauge bank
//     frontend/hires/gauges/<_hud_file_name>.bnk (hud.ini Style / AddHud,
//     ext_rpm / int_* dial textures, needle.2db), fed like FUN_00965810 /
//     FUN_00967f10 (docs: /tmp/claude-0/spec_freeride.md 4b).
//   - Minimap content: drawn by the engine with GamePC.cpr [HudMap] colours
//     ([ENC] drawing, road geometry [INF]): here the road surfaces of the
//     sector -R.shk files, heading up.
//   - Zone badge: DBCruisingZones i1 / i2 -> loc 3336 / 3337 ("Ibiza Area
//     1 / 2"); the zone lookup is [ENC] (spec 4) [INF: zone 2 east of area 4].
class FreeRideHud {
public:
    struct State {
        float SpeedKmh = 0.0f;
        float Rpm = 0.0f;
        float RedLine = 8000.0f;
        float RpmMax = 8500.0f;
        int32_t Gear = 0; // -1 R, 0 N
        float OdometerKm = 0.0f;
        float CarX = 0.0f, CarZ = 0.0f;
        float HeadingX = 0.0f, HeadingZ = 1.0f; // car forward on the ground
        int32_t CameraMode = 20; // CamerasInGame mode (camera +0x2e8)
    };

public:
    // hudFileName: DBCarPhysicsData _hud_file_name (gauge bank).
    bool load( const std::string &hudFileName );
    void unload();

    void showGoal( const std::string &text );
    void closeGoal();
    void tick( float deltaTime, const State &state );
    void draw( Renderer2D &renderer, const VehicleWorldCollision *pCollision );
    void setVisible( bool bVisible ) { bShown = bVisible; }

private:
    FlashPlayer hud;
    FlashPlayer general;
    State state;
    float goalTimer = -1.0f;
    bool bShown = false;
    std::string zoneShort;
    std::string zoneName;
    int32_t zoneIndex = 0;

    void dumpClips( const FlashPlayer::Clip &clip, const std::string &path, int32_t depth );
    // GAUGES.vmf and the car gauge bank.
    struct NeedleTable { // AddHud: piecewise linear value -> angle (FUN_00968fe0)
        std::vector<float> Values, Angles;
        float map( float value ) const;
    };
    FlashPlayer gauges;
    FlashDefinitions gaugeDefinitions; // shape geometry of the needle / dial frame
    Bank gaugeBank;
    std::string gaugeStyle;
    std::unordered_map<std::string, NeedleTable> needleTables;
    GPUTexture *pDialTexture = nullptr;   // ext_rpm.2db
    GPUTexture *pNeedleTexture = nullptr; // needle.2db
    float needleAngle = 0.0f;
    bool loadGaugeBank( const std::string &hudFileName );
    void updateGauges();
    void drawGaugeTextures( Renderer2D &renderer, bool bDial );
    void beginGaugeStage( Renderer2D &renderer ) const;
    bool clipMatrix( const std::string &path, FlashMatrix &outMatrix, uint16_t &outCharacter );
    void drawGauge( Renderer2D &renderer );
    void drawMinimap( Renderer2D &renderer, const VehicleWorldCollision *pCollision );
    void drawZone( Renderer2D &renderer );
};
