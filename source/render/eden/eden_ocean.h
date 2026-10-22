#pragma once

#include "render/eden/eden_renderer.h"

struct WeatherPreset;
struct RenderTarget;

// TD_Ocean sea of the original renderer (docs/re/ocean_spec.md):
// - noise normal map: NoiseSlice 32^3 volume -> NOISE_COMPOSITE 32^2
//   (td_noisecompose, 3 animated slices) -> NOISE_ASSEMBLE_F 256^2
//   (td_noiseassembly, 4 octaves) -> NoiseNMap 256^2 (TD_ocean_nmap_from_hmap);
// - reflection: SEAREF_N, the scene from the mirror camera (no sky, no water,
//   no blended draws) in Ocean_RTRefl 256^2 cleared to ocean_reflectionColor,
//   clipped at the water plane;
// - the TD_Ocean materials get their textures and constants every frame
//   (FUN_00961710 / FUN_00982f60).
class EdenOcean {
public:
    static constexpr uint64_t kOceanShaderSet = 0x0cea000000000002ull;
    enum eKind : uint8_t { OK_Sector = 0, OK_Area = 1, OK_Island = 2 };

    using DrawReflected = std::function<void( EdenRenderer &renderer, const EdenSceneParams &mirrorScene )>;

public:
    EdenOcean();
    ~EdenOcean();

    bool initialize( EdenRenderer &renderer );
    void destroy();
    inline bool isReady() const { return bReady; }

    // Noise normal map passes (outside render passes).
    void updateNormalMap( EdenRenderer &renderer, float deltaTime, const WeatherPreset *pWeather );
    // Reflection render (outside render passes): scene = main camera.
    void renderReflection( EdenRenderer &renderer, const EdenSceneParams &scene, const WeatherPreset *pWeather,
                           const DrawReflected &drawReflected );
    // Runtime setup of a TD_Ocean material (FUN_008be1e0 + per frame
    // constants).
    void setupMaterial( EdenRenderer &renderer, EdenMaterial &material, eKind kind, GPUTexture *pColorMap,
                        const WeatherPreset *pWeather ) const;

private:
    GPUTexture *pNoiseSlice = nullptr;
    RenderTarget *pComposite = nullptr;
    RenderTarget *pAssemble = nullptr;
    RenderTarget *pNormalMap = nullptr;
    RenderTarget *pReflection = nullptr;
    GPUBuffer *pVertexBuffer = nullptr;
    GPUBuffer *pIndexBuffer = nullptr;
    EdenMesh quad;
    EdenMaterial composeMaterial;
    EdenMaterial assembleMaterial;
    EdenMaterial normalMapMaterial;
    Matrix4x4f reflectionMatrix; // REFLMAT rows (column-vector convention)
    float phase = 0.0f;
    bool bReady = false;

    void pass( EdenRenderer &renderer, RenderTarget *pTarget, EdenMaterial &material, const EdenDrawParams &params );
};
