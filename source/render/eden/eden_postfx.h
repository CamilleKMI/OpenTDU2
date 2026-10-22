#pragma once

#include "render/eden/eden_renderer.h"

struct WeatherPreset;
struct RenderTarget;

// Post processing of the original renderer with its edrender_pfx_* shaders
// (TestDrive2.exe, see docs/re/postfx_spec.md):
//
// - Haze (FUN_009309a0 / FUN_0092f1d0, edrender_pfx_haze item
//   30fdd1c5b25935d1, ps_3): atmospheric scattering + fog over the opaque
//   scene from the screen depth buffer (gViewPos B/A). The scene materials
//   have their S_FOG switch cleared by the material handlers: the fog comes
//   from this pass. Blend ONE, SRCALPHA: dst = haze.rgb + dst * haze.a.
// - Photoshop (FUN_008b0aa0): scene -> quarter size (edrender_pfx_sampling),
//   5 taps separable Gaussian (edrender_pfx_blur, sigma 1), 256x256 LUT
//   (edrender_pfx_lut_creation: color balance, soft light, overlay, bloom
//   ramp, gamma) and LUT application (edrender_pfx_apply_lut):
//   out.c = LUT( scene.c, luma( lerp( scene, blur, 0.6 ) ) ).
//
// The D3D9 half pixel offsets of the vertex shaders ("vars".xy) are dropped:
// Vulkan samples at pixel centers.
class EdenPostFx {
public:
    EdenPostFx();
    ~EdenPostFx();

    bool initialize( EdenRenderer &renderer );
    void destroy();

    // In the scene pass, before the blended draws.
    void drawHaze( EdenRenderer &renderer, GPUTexture *pViewPos, const WeatherPreset *pWeather );
    // Outside render passes: pSource (scene) -> pDestination.
    void applyPhotoshop( EdenRenderer &renderer, RenderTarget *pSource, RenderTarget *pDestination,
                         const WeatherPreset *pWeather );

private:
    GPUBuffer *pVertexBuffer;
    GPUBuffer *pIndexBuffer;
    EdenMesh mesh;
    EdenMaterial hazeMaterial;
    EdenMaterial samplingMaterial;
    EdenMaterial blurMaterial;
    EdenMaterial lutMaterial;
    EdenMaterial applyMaterial;
    RenderTarget *pQuarter;
    RenderTarget *pQuarterBlur;
    RenderTarget *pBlurTemp;
    RenderTarget *pLut;
    bool bReady;

private:
    void setupMaterial( EdenRenderer &renderer, EdenMaterial &material, uint64_t guid,
                        std::initializer_list<std::pair<uint32_t, uint8_t>> spec, uint32_t numTextures );
    void ensureTargets( uint32_t width, uint32_t height );
    void destroyTargets();
    void pass( EdenRenderer &renderer, RenderTarget *pTarget, EdenMaterial &material,
               std::initializer_list<GPUTexture *> textures, const EdenDrawParams &params );
};
