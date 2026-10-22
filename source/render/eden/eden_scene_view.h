#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_ocean.h"
#include "render/eden/eden_planar_reflections.h"
#include "render/eden/eden_postfx.h"
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
    void setShadowConfig( const EdenShadows::Config &config ) { shadows.setConfig( config ); }
    void setCamera( const Camera &newCamera ) { camera = newCamera; }
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
    inline EdenPlanarReflections &getPlanarReflections() { return planarReflections; }

    // NIGHTLMAP of the lightmapped materials of a model, overwritten every
    // frame from the weather (FUN_00980e50 / FUN_00a4ee30). Returns the
    // number of materials registered.
    uint32_t registerLightmapMaterials( EdenModel &model, eLightmapScope scope );
    void clearLightmapMaterials() { lightmapMaterials.clear(); }

    void tick( float deltaTime );
    void renderOffscreen();
    void draw();

private:
    EdenWorld world;
    WeatherLibrary weathers;
    EdenSky sky;
    EdenSun sun;
    EdenPostFx postFx;
    EdenShadows shadows;
    EdenOcean ocean;
    EdenPlanarReflections planarReflections;
    std::vector<std::pair<EdenMaterial *, eLightmapScope>> lightmapMaterials;
    bool bOceanEnabled = true;
    bool bProjectorActors = false;
    Bank cloudsBank;
    EdenModel clouds;
    std::string cloudsSuffix;
    FullscreenPass fullscreen;
    const WeatherPreset *pWeather = nullptr;
    Camera camera;
    DrawCallback drawCallback;
    EdenSceneParams sceneParams;
    RenderTarget *pSceneTarget = nullptr;
    RenderTarget *pViewPosTarget = nullptr;
    RenderTarget *pEnvironmentTarget = nullptr;
    RenderTarget *pFinalTarget = nullptr;
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
    bool loadClouds( const std::string &islandFolder );
    void drawClouds( EdenRenderer &renderer );
    void drawObjects( EdenRenderer &renderer, ePass pass );
    void updateLightmapMaterials();
};

// Helpers shared by the cinematic users.
Matrix4x4f EdenPerspectiveFovRH( float fovY, float aspect, float zNear, float zFar );
Matrix4x4f EdenRotationFromQuaternion( const Quaternionf &q );
