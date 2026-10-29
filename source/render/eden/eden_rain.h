#pragma once

#include "audio/gs_audio.h"
#include "render/2dm.h"
#include "render/eden/eden_renderer.h"
#include "render/eden/eden_weather.h"

class EdenWorld;

// Falling rain of the weather FX manager (TestDrive2.exe, docs/re/rain_spec.md).
// The weather FX root (FUN_00aa7200, objects +0xdb0..+0xdc8) gets the rain
// values of the weather every frame (FUN_00aa6ce0 -> FUN_00aa61a0):
//
// - WFX_Rain (+0xdb0, ctor FUN_00de2a70, vtable 0x00f1afe0): 9 "rain blocks"
//   (0x9b0 bytes each) of 75 m x 75 m x 75 m on a 3 x 3 grid around the
//   camera, each 4 vertical crossing quads textured with RAIN_FAR
//   (fx/weatherfx.bnk) scrolling down, material "RainGrp" TD_Rain
//   (e1b3c51172e345ff). Enabled when rain_alpha > 0.001; vertex colour
//   (200, 200, 200, (int)rain_alpha), tiling rain_tiling, scroll speed
//   rain_scroll_speed (FUN_00de24f0, FUN_00dbe930, FUN_00de1f40,
//   FUN_00de19f0, FUN_00dbe5d0).
// - WFX_Drop (+0xdb8, ctor FUN_00aa5940): 300 drops (1.5 m x 0.06 m streaks,
//   texture DROP, material "RainDrop" TD_Gouraud 73078865ca37336d) falling
//   at 3.5 * 9.81 m/s in a 40 m box wrapped around a point 12 m ahead of the
//   player car (FUN_00aa4eb0, FUN_00aa4c90). Alpha (int)rain_alpha, enabled
//   when rain_alpha > 1e-4.
// - WFX_Splash (+0xdb4, ctor FUN_00aa4930): up to rain_splash_max (500
//   slots) ground splashes (texture SPLASH, 3 x 3 atlas of 9 frames,
//   material "RainSplash" TD_Gouraud) in 15 zones of 8.33 m on a 3 x 5 grid
//   ahead of the player car (FUN_00aa3d80, FUN_00aa3960, FUN_00aa3b10).
//   Alpha (int)rain_splash_alpha, enabled when rain_splash_max > 0.
//
// The World Viewer has no car: the camera frame stands in for the player
// car frame (drops, splashes) [INF]. The car speed factor DAT_0117ac84
// (rain streak slant, scroll boost) is written by encrypted code only; it is
// 0 here [INF].
//
// Rain sound: islands/<island>/sound/ambience/ambiences.xmb Rain[0]
// (RainResource: Curve, Name "Rain_Mid", Path, Volume, Pitch), looped on the
// AmbienceRain client (13); the volume law of the ambience manager is not
// readable (FUN_00cc13c0 loads the resource, the update is not decoded):
// volume = Volume * Curve(rain_alpha) [INF], see rain_spec.md.
class EdenRain {
public:
    // Camera frame of the frame (world space, Eden convention).
    struct Frame {
        Vector3f Position;
        Vector3f Right;
        Vector3f Up;
        Vector3f Forward;
    };

    EdenRain() = default;
    ~EdenRain();
    EdenRain( const EdenRain & ) = delete;
    EdenRain &operator=( const EdenRain & ) = delete;

    // islandFolder: "<bank folder>islands/<island>/". Uses the weatherfx.bnk
    // textures loaded by EdenWorld (owner "Euro/Bnk/fx/weatherfx.bnk").
    bool initialize( EdenRenderer &renderer, const std::string &islandFolder );
    void destroy();

    // Rain parameters of the frame. bEnabled = World Viewer "Rain" checkbox:
    // off -> no falling rain (the wet ground keeps the weather values); on ->
    // the weather's rain_* / thunder_* values when it rains (rain_alpha > 0),
    // else those of the Rainny intempery (game_weathers.xmb file block 3) at
    // the same time of day. Returns the weather to use for the rain FX and
    // the wet ground (pWeather itself when nothing is overridden).
    const WeatherPreset *resolveWeather( const WeatherPreset *pWeather, bool bEnabled );

    // Simulation (once per frame).
    void update( float deltaTime, const Frame &frame, const EdenWorld &world, const WeatherPreset *pWeather );
    // Deferred blended draws (main view only).
    void draw( EdenRenderer &renderer, const Vector3f &cameraPosition );
    void stopSound();

    inline bool isActive() const { return bActive; }

private:
    struct Vertex {
        float Position[3];
        uint32_t Color; // B8G8R8A8 (D3DCOLOR)
        float Texcoord[2];
    };

    // WFX_Rain block (0x9b0 bytes): +0x8a0 centre, +0x8d0 4 quads {A, B, uv
    // offset}, +0x990 LCG seed (0x3039), +0x998 scroll.
    struct RainBlock {
        Vector3f Center;
        float Base = 0.0f; // +0x8a4: ground height or camera y - 37.5
        Vector3f A[4];
        Vector3f B[4];
        float U[4] = {};
        float V[4] = {};
        uint32_t Seed = 0x3039;
        float Scroll = 0.0f;
        bool bValid = false;
    };

    // WFX_Drop drop (0x60 bytes).
    struct Drop {
        Vector3f Local;     // +0x00 (x, y, z) in the 40 m box
        Vector3f Previous;  // +0x10 relative position of the previous frame
        Vector3f Top;       // +0x20 world
        Vector3f Bottom;    // +0x30 world
        int32_t Cell[3] = {};
        bool bAlive = false;
        bool bPlaced = false;
    };

    // WFX_Splash slot (0x20 bytes).
    struct Splash {
        Vector3f Position;
        float Life = 0.0f;
        float Size = 0.0f;
        int32_t Frame = 0;
        bool bAlive = false;
    };

    struct Zone {
        Vector3f Local;    // car frame (FUN_00aa3960)
        Vector3f Position; // world, one zone refreshed per frame
        bool bValid = false;
    };

    EdenMaterial rainMaterial;
    EdenMaterial dropMaterial;
    EdenMaterial splashMaterial;
    bool bMaterials = false;
    GPUBuffer *pBlockIndices = nullptr;
    GPUBuffer *pQuadIndices = nullptr;
    EdenMesh blockMeshes[9];
    EdenMesh dropMesh;
    EdenMesh splashMesh;
    GPUBuffer streams[11];

    // Forced rain (checkbox on, dry weather): Rainny intempery of the island
    // game_weathers.xmb at the time of the drawn weather.
    WeatherLibrary gameWeathers;
    WeatherSystem rainSystem;
    WeatherPreset resolved;

    // WFX_Rain
    RainBlock blocks[9];
    bool bGrid = false;      // +0x59a0
    float rainAlpha = 0.0f;  // +0x59b8 (int)
    float rainTiling = 3.0f; // +0x59b4
    float rainScroll = 2.0f; // +0x59a8
    bool bRainOn = false;    // +0x201
    Vector3f gridOrigin;

    // WFX_Drop
    Drop drops[300];
    uint32_t dropSeed = 0x3039; // +0x7b40
    float dropTimer = 0.0f;     // +0xab8
    int32_t dropAlive = 0;      // +0xab4
    int32_t dropAlpha = 200;    // +0x7b6c
    bool bDropsOn = false;
    Vector3f dropRight;

    // WFX_Splash
    Splash splashes[500];
    Zone zones[15];
    uint32_t zoneIndex = 0;      // +0x4d30
    uint32_t splashSeed = 0x3039; // +0x4960
    float splashTimer = 0.0f;    // +0xad4
    int32_t splashAlive = 0;     // +0xad0
    int32_t splashMax = 250;     // +0x4d4c
    int32_t splashAlpha = 255;   // +0x4d54
    bool bSplashOn = false;
    Vector3f splashRight;
    Vector3f splashUp;

    // Sound
    std::shared_ptr<AudioClip> rainClip;
    float soundVolume = 1.0f;
    float soundPitch = 1.0f;
    std::vector<std::pair<float, float>> soundCurve; // (x, y)
    GSAudio::VoiceHandle voice = GSAudio::kInvalidVoice;

    bool bActive = false;
    bool bInitialized = false;

    bool setupMaterial( EdenRenderer &renderer, EdenMaterial &material, const char *pName, uint64_t guid,
                        const uint8_t *pSpec, const char *pTexture, uint8_t address, uint8_t alphaTest );
    float random( uint32_t &seed ) const;
    void layGrid( const Frame &frame, const EdenWorld &world );
    void recenterGrid( int32_t block );
    void initBlock( RainBlock &block, const Vector3f &center );
    void updateBlocks( float deltaTime, const Frame &frame, const EdenWorld &world );
    void updateDrops( float deltaTime, const Frame &frame, const EdenWorld &world );
    void updateSplashes( float deltaTime, const Frame &frame, const EdenWorld &world );
    void updateSound();
    void setParameters( const WeatherPreset *pWeather );
    bool loadSound( const std::string &islandFolder );
};
