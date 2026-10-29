#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_neons.h"
#include "render/eden/eden_ocean.h"
#include "render/eden/eden_planar_reflections.h"
#include "render/eden/eden_postfx.h"
#include "render/eden/eden_rain.h"
#include "render/eden/eden_shadows.h"
#include "render/eden/eden_sky.h"
#include "render/eden/eden_sun.h"
#include "render/eden/eden_weather.h"
#include "render/postfx/fullscreen_pass.h"
#include "world/eden_world.h"

// Island view rendered like the original renderer: streamed world (.pmi
// instances, wet road), sky dome, sun halo / flares, 3D clouds, dynamic
// environment cube, real time sun shadows, haze and photoshop post
// processing, lit by a (cinematic) weather. Used by the press start screen
// and the real time cinematics; the owner draws its own objects (cars,
// characters, cinematic sets) through the draw callback.
class EdenSceneView {
public:
    enum class ePass {
        Environment,   // environment cube faces (world only)
        DepthPrepass,  // Z pre-pass (FullscreenDepthTarget)
        ShadowCasters, // sun shadow slices
        Opaque,        // main scene (opaque, blended draws are deferred)
        Reflection,    // ocean reflection (mirror camera, opaque only)
        // GSReflection actor layers: the actors only (avatars, cars), seen
        // by a projector camera (see setPlanarReflectionActors).
        ProjectorActors,
    };
    // Lightmap day / night blend (NIGHTLMAP) driven by the weather.
    enum class eLightmapScope {
        Exterior, // spot exterior: TD_Building with U6 <- spot_exterior_lightmap_day_night
        Interior, // house interior: TD_Building, TD_Furnitures, d5013ed0dcacafaf <- spot_interior_lightmap_day_night
        Neon,     // neon set (FUN_00985e40): WeatherEmissive <- building_neon_strength (FUN_009834d0)
        // Registered with Exterior (FUN_00981870 / FUN_00980d30), written
        // by FUN_00980e50 (NIGHT, rain / night engineer):
        ExteriorSprite, // fcdaa96bdeadbeef sprites with OcclusionDayNight: .z <- 1 - x (at most 127)
        ExteriorLamp,   // TD_Lamp (c913741cbe17490d) halo nodes: instance colour .w <- y (at most 64)
    };
    using DrawCallback = std::function<void( EdenRenderer &renderer, ePass pass )>;

    struct Camera {
        Vector3f Position;
        Quaternionf Rotation; // Eden convention: forward = conj(q) (0, 0, 1)
        float Fov = 60.0f * kDegToRad; // vertical
        float Near = 0.8f;    // cinematic defaults (FUN_00b3eba0)
        float Far = 40000.0f;
    };

public:
    EdenSceneView();
    ~EdenSceneView();

    // Loads the world (sectors around streamCenter), the cinematic weathers,
    // clouds, sky, sun and post processing.
    bool initialize( const char *pIsland, const Vector3f &streamCenter, int32_t sectorRadius );
    void destroy();
    inline bool isReady() const { return bReady; }

    inline EdenWorld &getWorld() { return world; }
    inline WeatherLibrary &getWeathers() { return weathers; }
    inline const WeatherPreset *getWeather() const { return pWeather; }
    inline const EdenSceneParams &getSceneParams() const { return sceneParams; }
    // Dynamic environment cube (nullptr until its first faces are drawn).
    GPUTexture *getEnvironmentCube() const;

    void setWeather( const WeatherPreset *pPreset ) { pWeather = pPreset; }
    // Cloud sets of the forecast blend (GSWeather source / destination
    // clouds_cloud3dname and blend factor, FUN_00930e70 slots 0 / 1). Without
    // a call the weather's own clouds_cloud3dname is drawn alone.
    void setCloudLayers( const std::string &source, const std::string &destination, float blend );
    // Game time elapsed this frame (GSWeather +0x4f0, drives the cloud
    // rotation FUN_009301c0). Without a call: dt * 9.6 (the game clock).
    void setGameTimeDelta( float seconds ) { gameTimeDelta = seconds; }
    // RAIN (rain engineer): World Viewer "Rain" checkbox. Stub that stores
    // the flag; the rain rendering reads isRainEnabled().
    void setRainEnabled( bool bEnable ) { bRainEnabled = bEnable; }
    inline bool isRainEnabled() const { return bRainEnabled; }
    void setShadowConfig( const EdenShadows::Config &config ) { shadows.setConfig( config ); }
    void setCamera( const Camera &newCamera ) { camera = newCamera; }
    // Projected car headlight spots of the frame (HEADLIGHTS pass,
    // docs/re/car_lights_spec.md 8): one per car (FUN_00921260 headlight
    // slot, up to 26). Cleared by the caller each frame (set the list).
    struct HeadlightSpot {
        Vector3f Position;               // slot +0x330 (world)
        Vector3f AxisX, AxisY, Direction; // spot frame (Direction = slot +0x340)
        float Far = 102.0f;              // slot +0x360
        float Near = 1.4835f;            // slot +0x364 (Spot_Attenuation: far / (far - near), 1 / (far - near))
        float CosOuter = 0.0f;           // cos(slot +0x368 / 2)
        float CosInner = 1.0f;           // cos(slot +0x36c / 2)
        float TanHalfFov = 1.0f;         // tan(slot +0x368 / 2): ProjMatrix
        float DropOffNear = 0.0f;        // Spot_PositionDropOffNear.w
        float Intensity = 0.0f;          // vars.z (x Spot_Color.x)
        GPUTexture *pTexture = nullptr;  // gProjTexture (projectivespotlighttexture)
    };
    void setHeadlightSpots( const std::vector<HeadlightSpot> &spots ) { headlightSpots = spots; }
    // Center of the environment cube (the car); disabled: no cube.
    void setEnvironmentCenter( const Vector3f &center, bool bEnable );
    void setDrawCallback( DrawCallback callback ) { drawCallback = std::move( callback ); }

    // Ocean (normal map, reflection, TD_Ocean draws) on / off: closed rooms
    // rendered without the island (FIRST_WM garage).
    void setOceanEnabled( bool bEnable );
    inline bool isOceanEnabled() const { return bOceanEnabled; }

    // GSReflection (planar / refraction projective maps, see
    // EdenPlanarReflections). Per game mode switch:
    // setPlanarReflectionsEnabled( EdenPlanarReflections::IsEnabledForGameMode( 0x20 ) ).
    void setPlanarReflectionsEnabled( bool bEnable );
    // Registers the projective materials (TD_Water U4, TD_Furnitures U7) of
    // a model of a registered bank (call for each model of the bank).
    uint32_t registerProjectiveMaterials( EdenModel &model );
    // Builds the projectors of an Entity__Actor__Mesh D_REFLECT model drawn
    // at 'world' (the model must outlive the view or clearPlanarReflections
    // be called). pBankName: the bank (EPH / ECAS banks: no actor layers),
    // pActorLayer1 / 2: the entity AvatarReflectlayername1 / 2.
    uint32_t registerReflectionScene( const EdenModel &model, const Matrix4x4f &world, const char *pBankName,
                                      const char *pActorLayer1 = nullptr, const char *pActorLayer2 = nullptr );
    // Actor layers / refraction actors: the draw callback is called with
    // ePass::ProjectorActors (it must then draw the actors only). Off by
    // default.
    inline void setPlanarReflectionActors( bool bEnable ) { bProjectorActors = bEnable; }
    // mgr+0x24: actors drawn in the refraction layer (C_JACCUZZI / C_A4).
    inline void setRefractionActors( bool bEnable ) { planarReflections.setRefractionActors( bEnable ); }
    void clearPlanarReflections();
    // Forgets the GSReflection materials / projectors of one model.
    void removePlanarReflectionModel( EdenModel &model );
    inline EdenPlanarReflections &getPlanarReflections() { return planarReflections; }

    // NIGHTLMAP of the lightmapped materials of a model, overwritten every
    // frame from the weather (FUN_00980e50 / FUN_00a4ee30). Returns the
    // number of materials registered.
    uint32_t registerLightmapMaterials( EdenModel &model, eLightmapScope scope );
    // Neon materials of a model (world neon set, FUN_00985e40 [CODE]):
    // TD_NEONDECAL 63078165ca37416d and 18315cacac6cff57 (any switches),
    // fcdaa96bdeadbeef sprites with U4 (spec 0x23) set. Every frame
    // (FUN_009834d0 -> FUN_009829d0) WeatherEmissive = (x, x, x, 0) and
    // param 97909ca3a58487f1.xy = (x, 0), x = the weather
    // building_neon_strength.x (key 0x9ba1f0e6e2dce8f3). The 2DM default
    // of WeatherEmissive is 0: unregistered neons draw black (additive:
    // invisible). Same list as the lightmaps (clearLightmapMaterials).
    // Blinking neons (TD_NEONDECAL / TD_pubneon with U4 = 0, sprites with
    // U4 set) also get an AnimValue state (FUN_00985e40 blink entry, see
    // EdenNeons), updated every frame. World sector / area / .pmi neons are
    // registered by EdenWorld itself.
    uint32_t registerNeonMaterials( EdenModel &model );
    void clearLightmapMaterials() {
        lightmapMaterials.clear();
        neonBlinks.clear();
    }

    void tick( float deltaTime );
    // Neon set + AnimValue blink update (FUN_009834d0), called by renderOffscreen.
    void updateNeons();
    void renderOffscreen();
    void draw();

private:
    EdenWorld world;
    WeatherLibrary weathers;
    EdenSky sky;
    EdenSun sun;
    EdenPostFx postFx;
    EdenRain rain; // RAIN (rain engineer): falling rain, drops, splashes, rain sound
    EdenShadows shadows;
    EdenOcean ocean;
    EdenPlanarReflections planarReflections;
    std::vector<std::pair<EdenMaterial *, eLightmapScope>> lightmapMaterials;
    std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> neonBlinks;
    bool bOceanEnabled = true;
    bool bProjectorActors = false;
    Bank cloudsBank;
    EdenModel clouds;
    // Cloud objects of clouds.3dd by clouds_cloud3dname ("CLOUD_B" ->
    // CLOUD_B_0..3 subtrees + the TD_CloudMesh dome mesh of that letter).
    struct CloudSet {
        std::vector<int32_t> Roots;
        std::vector<bool> Nodes; // node in one of the subtrees
        float Angle = 0.0f;      // accumulated rotation (rad)
    };
    std::unordered_map<std::string, CloudSet> cloudSets;
    std::string cloudSource;
    std::string cloudDestination;
    float cloudBlend = 0.0f;
    bool bCloudLayers = false;
    float gameTimeDelta = -1.0f;
    bool bRainEnabled = false;
    FullscreenPass fullscreen;
    const WeatherPreset *pWeather = nullptr;
    Camera camera;
    DrawCallback drawCallback;
    EdenSceneParams sceneParams;
    RenderTarget *pSceneTarget = nullptr;
    RenderTarget *pViewPosTarget = nullptr;
    RenderTarget *pEnvironmentTarget = nullptr;
    RenderTarget *pFinalTarget = nullptr;
    RenderTarget *pHeadLightsTarget = nullptr; // HeadLightsRT (R16F)
    std::vector<HeadlightSpot> headlightSpots;
    FullscreenPass headlightPass;
    Vector3f environmentCenter;
    bool bEnvironmentEnabled = false;
    uint32_t environmentFace = 0;
    float time = 0.0f;
    float frameDelta = 0.0f;
    bool bReady = false;

private:
    void setupScene( EdenSceneParams &params );
    void ensureTargets();
    void destroyTargets();
    void renderEnvironment( const EdenSceneParams &mainScene, bool bAllFaces );
    void generateViewPos();
    GPUTexture *renderHeadlights( const EdenSceneParams &params );
    bool loadClouds( const std::string &islandFolder );
    void drawClouds( EdenRenderer &renderer );
    CloudSet *findCloudSet( const std::string &name );
    void updateClouds( float gameDelta );
    void drawObjects( EdenRenderer &renderer, ePass pass );
    void updateLightmapMaterials();
};

// Helpers shared by the cinematic users.
Matrix4x4f EdenPerspectiveFovRH( float fovY, float aspect, float zNear, float zFar );
Matrix4x4f EdenRotationFromQuaternion( const Quaternionf &q );
