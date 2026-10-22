#pragma once

#include "render/eden/eden_renderer.h"

struct WeatherPreset;

// Sky dome (td_dome.sob, "MatDome"), as set up by FUN_00c38340 and updated
// every frame by FUN_00931140 / FUN_0092fb20 / FUN_009309a0 in
// TestDrive2.exe: Hoffman & Preetham light scattering evaluated on 4
// wavelengths (0.700, 0.650, 0.570, 0.475 um) and converted to RGB by the
// SpectrumToRGB matrix (CIE weights -> XYZ -> CIE RGB).
//
//   betaR  = 1.2415e-30 / lambda^4 * Skydome_betaRayMultiplier * 0.1
//   beta'R = 7.4097e-32 / lambda^4 * Skydome_betaRayMultiplier * 0.1
//   betaM  = 3.5407e-15 K(lambda) / lambda^2 * Skydome_betaMieMultiplier * 0.01
//   beta'M = 5.6353e-16 / lambda^2 * Skydome_betaMieMultiplier * 0.01
//   g      = Skydome_HGg * 0.001
//   sun    = Preetham sun extinction (turbidity, optical mass of the sun
//            zenith angle clamped to Skydome_sunAngleAttenuation)
//            * Skydome_sunIntensity * 5 * (9.2 - 5.5 Skydome_betaMieMultiplier)
class EdenSky {
public:
    EdenSky();
    ~EdenSky();

    bool initialize( EdenRenderer &renderer );
    void destroy();
    // Scattering constants (scene wide: also read by the clouds and the
    // heightmap aerial perspective). Call before EdenRenderer::beginScene.
    static void setupAtmosphere( EdenSceneParams &scene, const WeatherPreset *pWeather );
    // Draw first (depth write off, at the far plane).
    void draw( EdenRenderer &renderer );

private:
    GPUBuffer *pVertexBuffer;
    GPUBuffer *pIndexBuffer;
    EdenMesh mesh;
    EdenMaterial material;
    bool bReady;
};
