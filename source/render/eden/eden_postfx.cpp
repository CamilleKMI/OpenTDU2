#include "shared.h"
#include "eden_postfx.h"

#include "render/eden/eden_weather.h"
#include "render/gs_render.h"

namespace {
constexpr uint64_t kHazeShaderSet = 0x6526003ca93e193bull;      // edrender_pfx_haze
constexpr uint64_t kSamplingShaderSet = 0xfffffffffffff003ull;  // edrender_pfx_sampling
constexpr uint64_t kBlurShaderSet = 0xfffffffffffff001ull;      // edrender_pfx_blur
constexpr uint64_t kLutCreateShaderSet = 0xffdcfdcffffff003ull; // edrender_pfx_lut_creation
constexpr uint64_t kApplyLutShaderSet = 0xfffffdcffdcff003ull;  // edrender_pfx_apply_lut
constexpr uint32_t kLutSize = 256;                              // 0x100 >> "Lut Downscale" (0)
constexpr uint8_t kSamplerLinearClamp = 1;

// Gaussian weights of FUN_0089e0a0 (method 1: 5 taps, normalized by
// w0 + 2 (w1 + w2)): w(x) = exp( x^2 / (-2 sigma^2) ).
void GaussianWeights( float sigma, float outWeights[3] ) {
    float sum = 0.0f;
    for ( int32_t x = 0; x < 3; x++ ) {
        outWeights[x] = std::exp( static_cast<float>( x * x ) / ( -2.0f * sigma * sigma ) );
        sum += x == 0 ? outWeights[x] : 2.0f * outWeights[x];
    }
    for ( int32_t x = 0; x < 3; x++ ) {
        outWeights[x] /= sum;
    }
}
} // namespace

EdenPostFx::EdenPostFx()
    : pVertexBuffer( nullptr ), pIndexBuffer( nullptr ), pQuarter( nullptr ), pQuarterBlur( nullptr ),
      pBlurTemp( nullptr ), pLut( nullptr ), bReady( false ) {}

EdenPostFx::~EdenPostFx() { destroy(); }

void EdenPostFx::setupMaterial( EdenRenderer &renderer, EdenMaterial &material, uint64_t guid,
                                std::initializer_list<std::pair<uint32_t, uint8_t>> spec, uint32_t numTextures ) {
    material.ShaderSetGUID = guid;
    memset( material.Spec, 0, sizeof( material.Spec ) );
    for ( const auto &entry : spec ) {
        material.Spec[entry.first] = entry.second;
    }
    material.NumSlots = numTextures;
    for ( uint32_t i = 0; i < 16; i++ ) {
        material.TextureHashes[i] = i < numTextures ? 0x20000ull + i : 0;
        material.SamplerIndex[i] = kSamplerLinearClamp;
    }
    material.SrcBlend = 2;
    material.DstBlend = 1;
    material.CullMode = 1;
    material.bDepthTest = false;
    material.bDepthWrite = false;
    renderer.updateProgram( material );
}

bool EdenPostFx::initialize( EdenRenderer &renderer ) {
    destroy();
    // Full screen triangle (positions in D3D clip space, used as is by the
    // PFX vertex shaders).
    const float vertices[] = { -1.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f, 3.0f, -1.0f, 0.0f };
    const uint32_t indices[] = { 0, 1, 2 };
    RenderDevice &device = gpRender->getDevice();
    pVertexBuffer = device.createBuffer( sizeof( vertices ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices );
    pIndexBuffer = device.createBuffer( sizeof( indices ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices );
    mesh.NumStreams = 1;
    mesh.pStreamBuffers[0] = pVertexBuffer;
    mesh.Strides[0] = 12;
    RawVertexAttribute position;
    position.Usage = 0;
    position.Format = VAF_POSITION_R32G32B32_SFLOAT;
    mesh.Attributes.push_back( position );
    mesh.pIndexBuffer = pIndexBuffer;
    mesh.IndexCount = 3;
    mesh.computeLayoutKey();

    // PFX_Haze #1 (per pixel scattering): S_FOG, S_USERTYPE1 = 3.
    setupMaterial( renderer, hazeMaterial, kHazeShaderSet, { { 11, 1 }, { 32, 3 } }, 1 );
    hazeMaterial.Name = "PFX_Haze";
    hazeMaterial.SrcBlend = 2; // ONE
    hazeMaterial.DstBlend = 5; // SRCALPHA
    hazeMaterial.SamplerIndex[0] = 3; // point clamp: encoded depth
    // PFX_Sampling (4 taps downsample)
    setupMaterial( renderer, samplingMaterial, kSamplingShaderSet, {}, 1 );
    samplingMaterial.Name = "PFX_Sampling";
    // PFX_Blur, method 1 (5 taps Gaussian): material index 2 (S_USERTYPE1).
    setupMaterial( renderer, blurMaterial, kBlurShaderSet, { { 32, 2 } }, 1 );
    blurMaterial.Name = "PFX_Blur";
    // PFX_LutCreate
    setupMaterial( renderer, lutMaterial, kLutCreateShaderSet, {}, 0 );
    lutMaterial.Name = "PFX_LutCreate";
    // PFX_ApplyLut (spec[0] = 4 textures: sA, sB, sLut, sD)
    setupMaterial( renderer, applyMaterial, kApplyLutShaderSet, { { 0, 4 } }, 4 );
    applyMaterial.Name = "PFX_ApplyLut";

    bReady = hazeMaterial.pProgram != nullptr && samplingMaterial.pProgram != nullptr &&
             blurMaterial.pProgram != nullptr && lutMaterial.pProgram != nullptr && applyMaterial.pProgram != nullptr;
    OTDU_LOG_DEBUG( "EdenPostFx: haze %s, sampling %s, blur %s, lut %s, apply %s\n",
                    hazeMaterial.pProgram ? hazeMaterial.pProgram->Name.c_str() : "-",
                    samplingMaterial.pProgram ? samplingMaterial.pProgram->Name.c_str() : "-",
                    blurMaterial.pProgram ? blurMaterial.pProgram->Name.c_str() : "-",
                    lutMaterial.pProgram ? lutMaterial.pProgram->Name.c_str() : "-",
                    applyMaterial.pProgram ? applyMaterial.pProgram->Name.c_str() : "-" );
    return bReady;
}

void EdenPostFx::destroyTargets() {
    if ( gpRender == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    for ( RenderTarget **ppTarget : { &pQuarter, &pQuarterBlur, &pBlurTemp, &pLut } ) {
        device.destroyRenderTarget( *ppTarget );
        *ppTarget = nullptr;
    }
}

void EdenPostFx::destroy() {
    destroyTargets();
    if ( gpRender != nullptr ) {
        gpRender->getDevice().destroyBuffer( pVertexBuffer );
        gpRender->getDevice().destroyBuffer( pIndexBuffer );
    }
    pVertexBuffer = pIndexBuffer = nullptr;
    bReady = false;
}

void EdenPostFx::ensureTargets( uint32_t width, uint32_t height ) {
    const uint32_t quarterWidth = Max( width >> 2, 1u );
    const uint32_t quarterHeight = Max( height >> 2, 1u );
    if ( pQuarter != nullptr && pQuarter->Width == quarterWidth && pQuarter->Height == quarterHeight ) {
        return;
    }
    destroyTargets();
    RenderDevice &device = gpRender->getDevice();
    pQuarter = device.createRenderTarget( quarterWidth, quarterHeight, VK_FORMAT_UNDEFINED, false, "PfxTmp" );
    pQuarterBlur = device.createRenderTarget( quarterWidth, quarterHeight, VK_FORMAT_UNDEFINED, false, "PfxTmp" );
    pBlurTemp = device.createRenderTarget( quarterWidth, quarterHeight, VK_FORMAT_UNDEFINED, false, "PfxTmp" );
    // A8R8G8B8 LUT (8 bits per channel, quantized like the original).
    pLut = device.createRenderTarget( kLutSize, kLutSize, VK_FORMAT_R8G8B8A8_UNORM, false, "PfxLut" );
}

void EdenPostFx::pass( EdenRenderer &renderer, RenderTarget *pTarget, EdenMaterial &material,
                       std::initializer_list<GPUTexture *> textures, const EdenDrawParams &params ) {
    RenderDevice &device = gpRender->getDevice();
    uint32_t slot = 0;
    for ( GPUTexture *pTexture : textures ) {
        material.pTextures[slot++] = pTexture;
    }
    device.beginRenderTarget( pTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    EdenSceneParams scene;
    scene.Width = pTarget->Width;
    scene.Height = pTarget->Height;
    renderer.beginScene( scene );
    renderer.draw( mesh, material, Matrix4x4f::Identity(), &params );
    renderer.endScene();
    device.endRenderTarget();
}

void EdenPostFx::drawHaze( EdenRenderer &renderer, GPUTexture *pViewPos, const WeatherPreset *pWeather ) {
    if ( !bReady || pViewPos == nullptr ) {
        return;
    }
    hazeMaterial.pTextures[0] = pViewPos;
    EdenDrawParams params;
    const float haze = pWeather != nullptr ? pWeather->getFloat( "Skydome_haze", 0.0f ) : 0.0f;
    params.Vectors["Haze"] = Vector4f( haze, haze, haze, haze );
    // VS vars (c61): screen (1/W, 1/H, 1, 1) for the screen materials
    // (FUN_009309a0).
    params.Vectors["vars"] = Vector4f( 1.0f / Max( pViewPos->Width, 1u ), 1.0f / Max( pViewPos->Height, 1u ), 1.0f, 1.0f );
    // Full screen, after the scene draws (not sorted with them).
    params.bDeferBlended = false;
    renderer.draw( mesh, hazeMaterial, Matrix4x4f::Identity(), &params );
}

void EdenPostFx::applyPhotoshop( EdenRenderer &renderer, RenderTarget *pSource, RenderTarget *pDestination,
                                 const WeatherPreset *pWeather ) {
    if ( !bReady || pSource == nullptr || pDestination == nullptr ) {
        return;
    }
    ensureTargets( pSource->Width, pSource->Height );
    auto param = [pWeather]( const char *pName, float defaultValue ) {
        return pWeather != nullptr ? pWeather->getFloat( pName, defaultValue ) : defaultValue;
    };
    auto param3 = [pWeather]( const char *pName ) {
        return pWeather != nullptr ? pWeather->getVector3( pName, Vector3f() ) : Vector3f();
    };

    // 1. Downsample (FUN_0089e730): taps at +-0.25 destination texel.
    {
        EdenDrawParams params;
        params.Vectors["vars"] =
            Vector4f( 0.0f, 0.0f, 0.25f / pQuarter->Width, 0.25f / pQuarter->Height );
        pass( renderer, pQuarter, samplingMaterial, { &pSource->Color }, params );
    }
    // 2. Separable Gaussian, sigma 1 (FUN_0089eb70): horizontal then vertical.
    {
        float weights[3];
        GaussianWeights( 1.0f, weights );
        EdenDrawParams params;
        params.Vectors["coeff_0123"] = Vector4f( weights[0], weights[1], weights[2], 0.0f );
        params.Vectors["coeff_4"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
        params.Vectors["vars"] = Vector4f( 0.0f, 0.0f, 1.0f / pQuarter->Width, 0.0f );
        pass( renderer, pBlurTemp, blurMaterial, { &pQuarter->Color }, params );
        params.Vectors["vars"] = Vector4f( 0.0f, 0.0f, 0.0f, 1.0f / pQuarterBlur->Height );
        pass( renderer, pQuarterBlur, blurMaterial, { &pBlurTemp->Color }, params );
    }
    // 3. LUT (x: channel value, y: luminance). Weather -> photoshop
    //    (FUN_00d769e0); gamma 0.8 (brightness option at its default).
    {
        EdenDrawParams params;
        params.Vectors["params"] = Vector4f( param( "photoshop_softlight_opacity", 0.55f ),
                                             param( "photoshop_softlight_brightness", 1.25f ),
                                             param( "photoshop_overlay_opacity", 0.3f ), 0.0f );
        params.Arrays["balance__array3"] = { Vector4f( param3( "photoshop_color_balance_shadows" ), 0.0f ),
                                             Vector4f( param3( "photoshop_color_balance_midtones" ), 0.0f ),
                                             Vector4f( param3( "photoshop_color_balance_hightones" ), 0.0f ) };
        const double black = param( "photoshop_bloom_black_level", 0.5f );
        const double white = param( "photoshop_bloom_white_level", 1.0f );
        const double range = white - black;
        params.Vectors["bloom_params"] =
            Vector4f( static_cast<float>( black ), static_cast<float>( range != 0.0 ? 1.0 / range : 0.0 ),
                      param( "photoshop_bloom_intensity", 0.25f ), 0.0f );
        params.Vectors["Gamma"] = Vector4f( 0.8f, 0.8f, 0.8f, 0.8f );
        pass( renderer, pLut, lutMaterial, {}, params );
    }
    // 4. Apply: sA scene, sB blurred quarter, sLut, sD depth (crappy DoF only).
    {
        EdenDrawParams params;
        params.Vectors["vars"] = Vector4f( 0.0f, 0.0f, 0.6f, 0.0f ); // "Blurred Lumincance Factor"
        pass( renderer, pDestination, applyMaterial, { &pSource->Color, &pQuarterBlur->Color, &pLut->Color },
              params );
    }
}
