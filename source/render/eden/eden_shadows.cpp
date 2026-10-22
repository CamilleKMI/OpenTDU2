#include "shared.h"
#include "eden_shadows.h"
#include "system/cmd_line.h"

#include "render/shaders/builtin/shadowcombine_frag.h"

#include "render/eden/eden_weather.h"
#include "render/gs_render.h"

namespace {
constexpr uint64_t kShadowPassShaderSet = 0xe674a83899eaeb37ull; // edrender_pfx_shadow
constexpr float kLightDistance = 900.0f;                         // shadow manager +0x464
constexpr uint8_t kSamplerPointClamp = 3;

Vector4f Row( const Matrix4x4f &m, int r ) { return Vector4f( m.m[0][r], m.m[1][r], m.m[2][r], m.m[3][r] ); }
} // namespace

namespace {
struct ShadowConfigEntry {
    const char *pName;
    EdenShadows::Config Config;
};

EdenShadows::Config MakeShadowConfig( float n, float f, float lambda, float b0, float b1, float b2, float b3,
                                      float maxCast ) {
    EdenShadows::Config config;
    config.Near = n;
    config.Far = f;
    config.Lambda = lambda;
    config.Bias[0] = b0;
    config.Bias[1] = b1;
    config.Bias[2] = b2;
    config.Bias[3] = b3;
    config.MaxCastDistance = maxCast;
    return config;
}

// Quality_Settings.cpr (same values at every quality level); CINE_DEFAULT is
// an alias of DEFAULT and AVATAR_CHOICE is never set: both use the built-in
// defaults of FUN_00973a30.
const ShadowConfigEntry kShadowConfigs[] = {
    { "LOGIN", MakeShadowConfig( 0.8f, 136.0f, 0.5f, 2.5f, 2.5f, 2.5f, 1.0f, 196.0f ) },
    { "PG", MakeShadowConfig( 1.6f, 90.0f, 0.5f, 2.0f, 2.0f, 2.0f, 2.0f, 150.0f ) },
    { "PM", MakeShadowConfig( 1.0f, 20.0f, 0.5f, 0.3f, 0.5f, 0.7f, 1.0f, 50.0f ) },
    { "PA", MakeShadowConfig( 1.1f, 20.0f, 1.0f, 0.4f, 1.0f, 0.5f, 1.0f, 40.0f ) },
    { "PR", MakeShadowConfig( 0.8f, 20.0f, 1.0f, 0.4f, 1.0f, 1.0f, 1.0f, 30.0f ) },
};
const EdenShadows::Config kDefaultShadowConfig = MakeShadowConfig( 0.8f, 32.0f, 0.5f, 0.7f, 1.5f, 1.5f, 1.0f, 150.0f );
} // namespace

EdenShadows::Config EdenShadows::FindConfig( const std::string &name ) {
    for ( const ShadowConfigEntry &entry : kShadowConfigs ) {
        if ( name == entry.pName ) {
            return entry.Config;
        }
    }
    return kDefaultShadowConfig;
}

EdenShadows::EdenShadows() {}

EdenShadows::~EdenShadows() { destroy(); }

bool EdenShadows::initialize( EdenRenderer &renderer, const Config &newConfig ) {
    destroy();
    config = newConfig;
    RenderDevice &device = gpRender->getDevice();
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        // SHADOW_NEAR / MID / FAR: R32F + depth (FUN_00c32380 / FUN_00c32880).
        slices[i].pTarget =
            device.createRenderTarget( config.Resolution, config.Resolution, VK_FORMAT_R32_SFLOAT, true, "ShadowSlice" );
    }
    const float vertices[] = { -1.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f, 3.0f, -1.0f, 0.0f };
    const uint32_t indices[] = { 0, 1, 2 };
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

    // FSShadowMat[i] (FUN_00977e80): spec[36 + i] = 1, spec[40] = 3,
    // blend ONE / ONE / MIN, color write R only, no depth.
    bReady = true;
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        EdenMaterial &material = passMaterials[i];
        material = EdenMaterial();
        material.Name = "FSShadowMat";
        material.ShaderSetGUID = kShadowPassShaderSet;
        material.Spec[36 + i] = 1;
        material.Spec[40] = 3;
        material.NumSlots = 0;
        for ( uint32_t slot = 0; slot < 16; slot++ ) {
            material.SamplerIndex[slot] = kSamplerPointClamp;
        }
        material.SrcBlend = 2;
        material.DstBlend = 2;
        material.BlendOp = 4;
        material.ColorWriteMask = 1;
        material.CullMode = 1;
        material.bDepthTest = false;
        material.bDepthWrite = false;
        renderer.updateProgram( material );
        bReady = bReady && material.pProgram != nullptr && slices[i].pTarget != nullptr;
        OTDU_LOG_DEBUG( "EdenShadows: slice %u pass %s\n", i,
                        material.pProgram != nullptr ? material.pProgram->Name.c_str() : "-" );
    }
    return bReady;
}

void EdenShadows::setConfig( const Config &newConfig ) {
    const uint32_t resolution = config.Resolution;
    config = newConfig;
    config.Resolution = resolution;
}

void EdenShadows::destroy() {
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        for ( Slice &slice : slices ) {
            device.destroyRenderTarget( slice.pTarget );
            slice.pTarget = nullptr;
        }
        device.destroyRenderTarget( pScreenShadow );
        for ( RenderTarget *&pTarget : pSliceScreens ) {
            device.destroyRenderTarget( pTarget );
            pTarget = nullptr;
        }
        combinePass.destroy();
        device.destroyBuffer( pVertexBuffer );
        device.destroyBuffer( pIndexBuffer );
    }
    pScreenShadow = nullptr;
    pVertexBuffer = pIndexBuffer = nullptr;
    bReady = false;
}

void EdenShadows::setupSceneConstants( EdenSceneParams &scene, const WeatherPreset *pWeather ) const {
    // Shadow_SlicesDist.w: weather sun_shadow_opacity (manager +0x431 set).
    const float opacity = pWeather != nullptr ? pWeather->getFloat( "sun_shadow_opacity", 1.0f ) : 1.0f;
    scene.Vectors["Shadow_SlicesDist"] = Vector4f( splits[0], splits[1], splits[2], opacity );
    const float texel = 1.0f / static_cast<float>( config.Resolution );
    scene.Vectors["Shadow_RTSizes01"] = Vector4f( texel, texel, texel, texel );
    scene.Vectors["Shadow_RTSizes23"] = Vector4f( texel, texel, texel, texel );
    scene.Vectors["Shadow_ExtraParams"] =
        Vector4f( -1.0f / scene.Projection.m[0][0], -1.0f / scene.Projection.m[1][1], 0.0f, 0.0f );
}

void EdenShadows::renderCasters( EdenRenderer &renderer, const EdenSceneParams &scene,
                                 const DrawCasters &drawCasters ) {
    if ( !bReady ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    // Split distances (FUN_00973890).
    const float n = config.Near;
    const float f = config.Far;
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        const float k = static_cast<float>( i + 1 ) / kNumSlices;
        splits[i] = config.Lambda * n * std::pow( f / n, k ) + ( 1.0f - config.Lambda ) * ( n + ( f - n ) * k );
    }
    const Matrix4x4f cameraWorld = scene.View.inverse();
    const Vector3f right( cameraWorld.m[0][0], cameraWorld.m[0][1], cameraWorld.m[0][2] );
    const Vector3f up( cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2] );
    const Vector3f forward( -cameraWorld.m[2][0], -cameraWorld.m[2][1], -cameraWorld.m[2][2] );
    const Vector3f position = cameraWorld.getTranslation();
    const float tanX = 1.0f / scene.Projection.m[0][0];
    const float tanY = 1.0f / scene.Projection.m[1][1];
    const float cameraNear = scene.Projection.m[3][2] / scene.Projection.m[2][2];
    const Vector3f light = scene.SunDirection.normalized();

    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        Slice &slice = slices[i];
        // Slice frustum (FUN_00976300): camera near or the previous split.
        const float sliceNear = i == 0 ? cameraNear : splits[i - 1];
        const float sliceFar = splits[i];
        Vector3f corners[8];
        Vector3f center( 0.0f, 0.0f, 0.0f );
        for ( uint32_t c = 0; c < 8; c++ ) {
            const float d = ( c & 4 ) ? sliceFar : sliceNear;
            const float sx = ( c & 1 ) ? 1.0f : -1.0f;
            const float sy = ( c & 2 ) ? 1.0f : -1.0f;
            corners[c] = position + forward * d + right * ( sx * tanX * d ) + up * ( sy * tanY * d );
            center = center + corners[c] * 0.125f;
        }
        // Light camera 900 m back along the sun direction, up (0, 1, 0).
        const Vector3f eye = center - light * kLightDistance;
        const Vector3f lightUp = std::abs( light.y ) > 0.9999f ? Vector3f( 1.0f, 0.0f, 0.0f ) : Vector3f( 0.0f, 1.0f, 0.0f );
        const Matrix4x4f view = Matrix4x4f::LookAt( eye, center, lightUp );
        Vector3f boundsMin( 1e30f, 1e30f, 1e30f );
        Vector3f boundsMax( -1e30f, -1e30f, -1e30f );
        for ( const Vector3f &corner : corners ) {
            const Vector3f v = view.transformPoint( corner );
            boundsMin = Vector3f( Min( boundsMin.x, v.x ), Min( boundsMin.y, v.y ), Min( boundsMin.z, v.z ) );
            boundsMax = Vector3f( Max( boundsMax.x, v.x ), Max( boundsMax.y, v.y ), Max( boundsMax.z, v.z ) );
        }
        // Casters up to MAX_CAST_DIST toward the sun [INF: the original
        // intersects the range with the caster bounds].
        const float depthNear = Max( 1.0f, -boundsMax.z - config.MaxCastDistance );
        const float depthFar = -boundsMin.z + 1.0f;
        const float width = boundsMax.x - boundsMin.x;
        const float height = boundsMax.y - boundsMin.y;
        Matrix4x4f projection = Matrix4x4f::Identity();
        projection.m[0][0] = 2.0f / width;
        projection.m[1][1] = 2.0f / height;
        projection.m[2][2] = -1.0f / ( depthFar - depthNear );
        projection.m[3][0] = -( boundsMin.x + boundsMax.x ) / width;
        projection.m[3][1] = -( boundsMin.y + boundsMax.y ) / height;
        projection.m[3][2] = -depthNear / ( depthFar - depthNear );
        slice.LightViewProjection = projection * view;
        slice.DepthRange = depthFar - depthNear;
        slice.TexelSize = Max( width, height ) / static_cast<float>( config.Resolution );

        EdenSceneParams lightScene = scene;
        lightScene.View = view;
        lightScene.Projection = projection;
        lightScene.CameraPosition = eye;
        lightScene.CameraDirection = light;
        lightScene.Width = lightScene.Height = config.Resolution;
        lightScene.bMirroredView = false;
        device.beginRenderTarget( slice.pTarget, true, ColorRGBA( 1.0f, 1.0f, 1.0f, 1.0f ) );
        renderer.setShadowCasterMode( true );
        renderer.beginScene( lightScene );
        drawCasters( renderer, lightScene );
        renderer.endScene();
        renderer.setShadowCasterMode( false );
        device.endRenderTarget();
    }
}

GPUTexture *EdenShadows::resolve( EdenRenderer &renderer, const EdenSceneParams &scene, GPUTexture *pViewPos,
                                  const WeatherPreset *pWeather ) {
    if ( !bReady || pViewPos == nullptr ) {
        return nullptr;
    }
    RenderDevice &device = gpRender->getDevice();
    if ( pScreenShadow == nullptr || pScreenShadow->Width != scene.Width || pScreenShadow->Height != scene.Height ) {
        device.destroyRenderTarget( pScreenShadow );
        pScreenShadow =
            device.createRenderTarget( scene.Width, scene.Height, VK_FORMAT_R8G8B8A8_UNORM, false, "ShadowRealTime" );
        for ( RenderTarget *&pTarget : pSliceScreens ) {
            device.destroyRenderTarget( pTarget );
            pTarget = device.createRenderTarget( scene.Width, scene.Height, VK_FORMAT_R8G8B8A8_UNORM, false,
                                                 "ShadowSliceScreen" );
        }
    }
    EdenSceneParams passScene = scene;
    setupSceneConstants( passScene, pWeather );
    renderer.beginScene( passScene );
    const Matrix4x4f inverseView = scene.View.inverse();
    EdenDrawParams params;
    std::vector<Vector4f> viewToLight( 4, Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
    std::vector<Vector4f> uvMatrix( 8, Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) );
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        // View -> light clip; uv = (0.5 x + 0.5, 0.5 - 0.5 y) (Vulkan
        // samples at texel centers: no D3D9 half texel).
        const Matrix4x4f viewToClip = slices[i].LightViewProjection * inverseView;
        Vector4f depthRow = Row( viewToClip, 2 );
        // SET_SHADOW_CONFIG_BIAS, in texels of the slice [INF: the original
        // adds it to the w of the light row].
        depthRow.w -= config.Bias[i] * slices[i].TexelSize / slices[i].DepthRange;
        viewToLight[i] = depthRow;
        const Vector4f row0 = Row( viewToClip, 0 );
        const Vector4f row1 = Row( viewToClip, 1 );
        const Vector4f row3 = Row( viewToClip, 3 );
        uvMatrix[2 * i] = Vector4f( 0.5f * row0.x + 0.5f * row3.x, 0.5f * row0.y + 0.5f * row3.y,
                                    0.5f * row0.z + 0.5f * row3.z, 0.5f * row0.w + 0.5f * row3.w );
        uvMatrix[2 * i + 1] = Vector4f( -0.5f * row1.x + 0.5f * row3.x, -0.5f * row1.y + 0.5f * row3.y,
                                        -0.5f * row1.z + 0.5f * row3.z, -0.5f * row1.w + 0.5f * row3.w );
    }
    params.Arrays["ViewToLight__array4"] = viewToLight;
    params.Arrays["UVMatrix__array8"] = uvMatrix;
    params.Vectors["SlicePhaseScale"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    params.Vectors["SliceUvOffset"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    params.Vectors["FadeAlphaFactor"] = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    params.Vectors["vars"] =
        Vector4f( 0.0f, 0.0f, static_cast<float>( scene.Width ), static_cast<float>( scene.Height ) );
    // Each slice pass (MIN blend, cleared to 1) in its own buffer; the
    // original restricts them with the stencil (FSShadowMatStencil, geometry
    // not resolved): emulated by the combine pass, which keeps for every
    // pixel the slice of its view depth (none beyond the last split).
    for ( uint32_t i = 0; i < kNumSlices; i++ ) {
        device.beginRenderTarget( pSliceScreens[i], true, ColorRGBA( 1.0f, 0.0f, 0.0f, 1.0f ) );
        renderer.beginScene( passScene );
        EdenMaterial &material = passMaterials[i];
        material.pTextures[0] = pViewPos;
        material.pTextures[1 + i] = &slices[i].pTarget->Color;
        renderer.draw( mesh, material, Matrix4x4f::Identity(), &params );
        renderer.endScene();
        device.endRenderTarget();
    }
    device.beginRenderTarget( pScreenShadow, true, ColorRGBA( 1.0f, 0.0f, 0.0f, 1.0f ) );
    GPUTexture *textures[4] = { pViewPos, &pSliceScreens[0]->Color, &pSliceScreens[1]->Color,
                                &pSliceScreens[2]->Color };
    static const float debugSlice = static_cast<float>( CmdLine::GetArgInt( "-shadowslice", 0 ) );
    const float combineParams[4] = { splits[0], splits[1], splits[2], debugSlice };
    combinePass.draw( device, kSPIRV_shadowcombine_frag, sizeof( kSPIRV_shadowcombine_frag ), textures, 4,
                      combineParams, sizeof( combineParams ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Point );
    device.endRenderTarget();
    return &pScreenShadow->Color;
}
