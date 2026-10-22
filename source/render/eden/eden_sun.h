#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_renderer.h"

struct WeatherPreset;
struct RenderTarget;

// Sun flare (TestDrive2.exe FUN_00972550 / FUN_00973010, weather parameters
// sunflare_*): halo and core quads drawn with TD_SunHalo (screen space quad
// pushed to the far plane: hidden by the scene per pixel), lens flares drawn
// with TD_Flare along the sun -> screen center line, textures of
// fx/sun.bnk (Sun_A halo, Sun_I core, Flare_0x).
//
// The original measures the visible part of a marker (sunflare_markersize)
// with TD_SunVisible and an occlusion pass; here the visibility is the part
// of the depth buffer around the sun that is sky (read back one frame later).
class EdenSun {
public:
    // Direction to the sun disk drawn by td_dome (lowered, see the .cpp).
    static Vector3f GetVisualSunDirection( const Vector3f &sunDirection );

    EdenSun();
    ~EdenSun();

    bool initialize( EdenRenderer &renderer );
    void destroy();

    // Before drawing: visibility from the depth of a previous frame, then
    // queues the readback of the current one (outside render passes).
    void updateVisibility( const EdenSceneParams &scene, RenderTarget *pSceneTarget );
    // Halo / core (with the scene, after the sky), lens flares (overlay).
    void drawHalo( EdenRenderer &renderer, const EdenSceneParams &scene, const WeatherPreset *pWeather );
    void drawFlares( EdenRenderer &renderer, const EdenSceneParams &scene, const WeatherPreset *pWeather );

    inline float getVisibility() const { return visibility; }

private:
    struct Vertex {
        float Position[3];
        uint32_t Color; // B8G8R8A8
        float Normal[3];
        float Texcoord[2];
    };
    static constexpr uint32_t kMaxQuads = 16;
    static constexpr uint32_t kProbeSize = 9;

    Bank sunBank;
    EdenMaterial haloMaterial;
    EdenMaterial coreMaterial;
    std::unordered_map<std::string, EdenMaterial> flareMaterials;
    GPUBuffer *pVertexBuffers[RenderDevice::kFramesInFlight];
    GPUBuffer *pReadbackBuffers[RenderDevice::kFramesInFlight];
    bool bReadbackPending[RenderDevice::kFramesInFlight];
    GPUBuffer *pIndexBuffer;
    uint32_t numQuads;
    float visibility;
    bool bSunOnScreen;
    Vector2f sunScreen; // D3D NDC
    bool bReady;

private:
    bool projectSun( const EdenSceneParams &scene, Vector2f &outNdc ) const;
    EdenMesh beginQuads();
    void addQuad( const Vector3f &position, const Vector2f &offset, const Vector2f &halfSize, uint32_t color,
                  bool bScreenSpace );
    GPUTexture *findTexture( const char *pName ) const;
    void setupMaterial( EdenRenderer &renderer, EdenMaterial &material, uint64_t guid, const uint8_t *pSpec,
                        GPUTexture *pTexture, uint32_t numSlots );
};
