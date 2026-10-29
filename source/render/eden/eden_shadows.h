#pragma once

#include "render/eden/eden_renderer.h"
#include "render/postfx/fullscreen_pass.h"

struct WeatherPreset;
struct RenderTarget;

// Real time sun shadows of the original renderer (docs/re/rain_shadow_spec.md
// part B): 3 PSSM slices (shadow manager FUN_00978790 / FUN_00976300),
// casters drawn into R32F maps (TD_ShadowCaster: clip z * 2 - 1, empty
// texels 1), then the full screen pass edrender_pfx_shadow (FSShadowMat[i]:
// view position from gViewPos, projection into slice i, 2x2 PCF) MIN blended
// into the R channel of the screen shadow buffer read by the scene shaders
// (gTexSamplerShadowRealTime, s15).
class EdenShadows {
public:
    // SET_SHADOW_CONFIG_SLICES / _BIAS / _MAX_CAST_DIST (Quality_Settings.cpr).
    struct Config {
        float Near = 0.8f;
        float Far = 136.0f;
        float Lambda = 0.5f;
        float Bias[4] = { 2.5f, 2.5f, 2.5f, 1.0f };
        float MaxCastDistance = 196.0f;
        uint32_t Resolution = 2048; // SET_SHADOWS_RESOLUTION (Shadows.cpr)
    };
    static constexpr uint32_t kNumSlices = 3;

    using DrawCasters = std::function<void( EdenRenderer &renderer, const EdenSceneParams &lightScene )>;

public:
    // Shadow config by name (Quality_Settings.cpr); unknown names
    // (CINE_DEFAULT, DEFAULT, AVATAR_CHOICE...): built-in defaults of
    // FUN_00973a30.
    static Config FindConfig( const std::string &name );
    inline RenderTarget *getScreenShadow() const { return pScreenShadow; }

    EdenShadows();
    ~EdenShadows();

    bool initialize( EdenRenderer &renderer, const Config &config );
    void destroy();
    // Slices / bias / cast distance (keeps the resolution).
    void setConfig( const Config &newConfig );

    // Slices + caster maps for the camera of 'scene' (outside render passes).
    void renderCasters( EdenRenderer &renderer, const EdenSceneParams &scene, const DrawCasters &drawCasters );
    // Screen shadow buffer (R: sun visibility, G / B: encoded headlight
    // light of pHeadLights = HeadLightsRT, HEADLIGHTS BLIT) from gViewPos
    // (outside render passes). Returns the buffer to bind to s15.
    GPUTexture *resolve( EdenRenderer &renderer, const EdenSceneParams &scene, GPUTexture *pViewPos,
                         const WeatherPreset *pWeather, GPUTexture *pHeadLights = nullptr );
    // Shadow_* scene constants (Shadow_SlicesDist...).
    void setupSceneConstants( EdenSceneParams &scene, const WeatherPreset *pWeather ) const;

private:
    struct Slice {
        Matrix4x4f LightViewProjection; // world -> light clip (z in [0, 1])
        float DepthRange = 1.0f;
        float TexelSize = 1.0f;         // world size of a texel
        RenderTarget *pTarget = nullptr;
    };

    Config config;
    Slice slices[kNumSlices];
    float splits[kNumSlices] = {};
    RenderTarget *pScreenShadow = nullptr;
    RenderTarget *pSliceScreens[kNumSlices] = {}; // slice passes (stencil restriction emulation)
    FullscreenPass combinePass;
    GPUBuffer *pVertexBuffer = nullptr;
    GPUBuffer *pIndexBuffer = nullptr;
    EdenMesh mesh;
    EdenMaterial passMaterials[kNumSlices];
    bool bReady = false;
};
