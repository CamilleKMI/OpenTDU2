#include "shared.h"
#include "eden_ocean.h"

#include "render/eden/eden_weather.h"
#include "render/gs_render.h"

namespace {
constexpr uint64_t kComposeShaderSet = 0xfacaa9cacabdbaafull;   // td_noisecompose
constexpr uint64_t kAssembleShaderSet = 0xfacaa9caca2dbaafull;  // td_noiseassembly
constexpr uint64_t kNormalMapShaderSet = 0x0cea000000fe0001ull; // TD_ocean_nmap_from_hmap
constexpr uint32_t kNoiseSize = 32;
constexpr uint32_t kMapSize = 256;
constexpr uint32_t kReflectionSize = 256;
constexpr uint8_t kSamplerLinearWrap = 0;
constexpr uint8_t kSamplerLinearClamp = 1;

Vector4f Row( const Matrix4x4f &m, int r ) { return Vector4f( m.m[0][r], m.m[1][r], m.m[2][r], m.m[3][r] ); }

float WeatherFloat( const WeatherPreset *pWeather, const char *pName, float defaultValue ) {
    return pWeather != nullptr ? pWeather->getFloat( pName, defaultValue ) : defaultValue;
}

Vector4f WeatherVector( const WeatherPreset *pWeather, const char *pName, const Vector4f &defaultValue ) {
    return pWeather != nullptr ? pWeather->getVector4( pName, defaultValue ) : defaultValue;
}

void SetupPassMaterial( EdenRenderer &renderer, EdenMaterial &material, uint64_t guid, const char *pName,
                        uint8_t sampler ) {
    material = EdenMaterial();
    material.Name = pName;
    material.ShaderSetGUID = guid;
    material.NumSlots = 0;
    for ( uint32_t i = 0; i < 16; i++ ) {
        material.SamplerIndex[i] = sampler;
    }
    material.CullMode = 1;
    material.bDepthTest = false;
    material.bDepthWrite = false;
    renderer.updateProgram( material );
}
} // namespace

EdenOcean::EdenOcean() {}

EdenOcean::~EdenOcean() { destroy(); }

bool EdenOcean::initialize( EdenRenderer &renderer ) {
    destroy();
    RenderDevice &device = gpRender->getDevice();

    // NoiseSlice (FUN_00dd68e0): 32^3 L8 white noise (seed 0x42424242; the
    // generator is encrypted: Numerical Recipes LCG [INF]), then a 3x3
    // [1 1 1; 1 8 1; 1 1 1] / 16 filter per z slice, wrapping in x / y.
    std::vector<uint8_t> raw( kNoiseSize * kNoiseSize * kNoiseSize );
    uint32_t state = 0x42424242u;
    for ( uint8_t &value : raw ) {
        state = state * 1664525u + 1013904223u;
        const uint32_t bits = 0x3f800000u | ( state >> 9 );
        float unit = 0.0f;
        memcpy( &unit, &bits, sizeof( unit ) );
        value = static_cast<uint8_t>( ( unit - 1.0f ) * 255.0f );
    }
    std::vector<uint8_t> volume( raw.size() );
    const uint32_t mask = kNoiseSize - 1;
    for ( uint32_t z = 0; z < kNoiseSize; z++ ) {
        for ( uint32_t y = 0; y < kNoiseSize; y++ ) {
            for ( uint32_t x = 0; x < kNoiseSize; x++ ) {
                auto at = [&]( uint32_t ix, uint32_t iy ) {
                    return static_cast<uint32_t>( raw[( z * kNoiseSize + ( iy & mask ) ) * kNoiseSize + ( ix & mask )] );
                };
                const uint32_t sum = 8 * at( x, y ) + at( x - 1, y ) + at( x + 1, y ) + at( x, y - 1 ) +
                                     at( x, y + 1 ) + at( x - 1, y - 1 ) + at( x + 1, y - 1 ) + at( x - 1, y + 1 ) +
                                     at( x + 1, y + 1 );
                volume[( z * kNoiseSize + y ) * kNoiseSize + x] = static_cast<uint8_t>( sum >> 4 );
            }
        }
    }
    const void *pData = volume.data();
    const uint32_t dataSize = static_cast<uint32_t>( volume.size() );
    pNoiseSlice = device.createTexture( VK_IMAGE_VIEW_TYPE_3D, kNoiseSize, kNoiseSize, kNoiseSize, 1, 1,
                                        VK_FORMAT_R8_UNORM, &pData, &dataSize, "NoiseSlice" );
    pComposite = device.createRenderTarget( kNoiseSize, kNoiseSize, VK_FORMAT_R16_SFLOAT, false, "NOISE_COMPOSITE" );
    pAssemble = device.createRenderTarget( kMapSize, kMapSize, VK_FORMAT_R16_SFLOAT, false, "NOISE_ASSEMBLE_F" );
    pNormalMap = device.createRenderTarget( kMapSize, kMapSize, VK_FORMAT_R8G8B8A8_UNORM, false, "NoiseNMap" );
    pReflection =
        device.createRenderTarget( kReflectionSize, kReflectionSize, VK_FORMAT_R8G8B8A8_UNORM, true, "Ocean_RTRefl" );

    // Full screen quad: position (clip) + uv (D3D: v = 0 at the top).
    const float vertices[] = { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                               -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f };
    const uint32_t indices[] = { 0, 1, 2, 2, 1, 3 };
    pVertexBuffer = device.createBuffer( sizeof( vertices ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices );
    pIndexBuffer = device.createBuffer( sizeof( indices ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices );
    quad.NumStreams = 1;
    quad.pStreamBuffers[0] = pVertexBuffer;
    quad.Strides[0] = 20;
    RawVertexAttribute position;
    position.Usage = 0;
    position.Format = VAF_POSITION_R32G32B32_SFLOAT;
    quad.Attributes.push_back( position );
    RawVertexAttribute texcoord;
    texcoord.Usage = 5;
    texcoord.Format = VAF_TEXCOORD_R32G32_SFLOAT;
    texcoord.Offset = 12;
    quad.Attributes.push_back( texcoord );
    quad.pIndexBuffer = pIndexBuffer;
    quad.IndexCount = 6;
    quad.computeLayoutKey();

    // NoizComp (repeat on the volume), NoizAss (repeat: octaves uv * 2^i),
    // NoiseNMap (repeat).
    SetupPassMaterial( renderer, composeMaterial, kComposeShaderSet, "NoizComp", kSamplerLinearWrap );
    SetupPassMaterial( renderer, assembleMaterial, kAssembleShaderSet, "NoizAss", kSamplerLinearWrap );
    SetupPassMaterial( renderer, normalMapMaterial, kNormalMapShaderSet, "NoiseNMap", kSamplerLinearWrap );
    bReady = pNoiseSlice != nullptr && composeMaterial.pProgram != nullptr && assembleMaterial.pProgram != nullptr &&
             normalMapMaterial.pProgram != nullptr && pReflection != nullptr;
    OTDU_LOG_DEBUG( "EdenOcean: compose %s, assemble %s, nmap %s\n",
                    composeMaterial.pProgram ? composeMaterial.pProgram->Name.c_str() : "-",
                    assembleMaterial.pProgram ? assembleMaterial.pProgram->Name.c_str() : "-",
                    normalMapMaterial.pProgram ? normalMapMaterial.pProgram->Name.c_str() : "-" );
    return bReady;
}

void EdenOcean::destroy() {
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        device.destroyTexture( pNoiseSlice );
        for ( RenderTarget *pTarget : { pComposite, pAssemble, pNormalMap, pReflection } ) {
            device.destroyRenderTarget( pTarget );
        }
        device.destroyBuffer( pVertexBuffer );
        device.destroyBuffer( pIndexBuffer );
    }
    pNoiseSlice = nullptr;
    pComposite = pAssemble = pNormalMap = pReflection = nullptr;
    pVertexBuffer = pIndexBuffer = nullptr;
    bReady = false;
}

void EdenOcean::pass( EdenRenderer &renderer, RenderTarget *pTarget, EdenMaterial &material,
                      const EdenDrawParams &params ) {
    RenderDevice &device = gpRender->getDevice();
    device.beginRenderTarget( pTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 0.0f ) );
    EdenSceneParams scene;
    scene.View = Matrix4x4f::Identity();
    scene.Projection = Matrix4x4f::Identity();
    scene.Width = pTarget->Width;
    scene.Height = pTarget->Height;
    renderer.beginScene( scene );
    renderer.draw( quad, material, Matrix4x4f::Identity(), &params );
    renderer.endScene();
    device.endRenderTarget();
}

void EdenOcean::updateNormalMap( EdenRenderer &renderer, float deltaTime, const WeatherPreset *pWeather ) {
    if ( !bReady ) {
        return;
    }
    // FUN_00dd65e0: one slice per second, 32 s period; weights
    // sin^2(pi (k + f) / 3) / 1.5 [INF: the trig function is encrypted].
    phase += deltaTime;
    if ( phase >= 32.0f ) {
        phase -= 32.0f;
    }
    const float s0 = std::fmod( std::floor( phase ), 32.0f );
    const float s1 = std::fmod( s0 + 1.0f, 32.0f );
    const float s2 = std::fmod( s1 + 1.0f, 32.0f );
    const float f = std::fmod( phase, 1.0f );
    auto weight = [f]( float k ) {
        const float s = std::sin( kPI * ( k + f ) / 3.0f );
        return s * s / 1.5f;
    };
    {
        EdenDrawParams params;
        params.Arrays["matParam__array2"] = { Vector4f( s0 / 32.0f, s1 / 32.0f, s2 / 32.0f, 0.0f ),
                                              Vector4f( weight( 2.0f ), weight( 1.0f ), weight( 0.0f ), 0.0f ) };
        composeMaterial.pTextures[0] = pNoiseSlice;
        pass( renderer, pComposite, composeMaterial, params );
    }
    {
        EdenDrawParams params;
        assembleMaterial.pTextures[0] = &pComposite->Color;
        pass( renderer, pAssemble, assembleMaterial, params );
    }
    {
        EdenDrawParams params;
        params.VertexVectors["matParam"] = Vector4f( 1.0f / kMapSize, 1.0f / kMapSize, 0.0f, 0.0f );
        const float scaler = WeatherFloat( pWeather, "ocean_NMapScaler", 0.13f );
        params.PixelVectors["matParam"] = Vector4f( scaler, scaler, scaler, scaler );
        normalMapMaterial.pTextures[0] = &pAssemble->Color;
        pass( renderer, pNormalMap, normalMapMaterial, params );
    }
}

void EdenOcean::renderReflection( EdenRenderer &renderer, const EdenSceneParams &scene, const WeatherPreset *pWeather,
                                  const DrawReflected &drawReflected ) {
    if ( !bReady ) {
        return;
    }
    // FUN_00961ca0: mirror camera about the water plane y = h (0 on Ibiza):
    // position (x, 2h - y, z), forward mirrored, up kept.
    const float h = 0.0f;
    const Matrix4x4f cameraWorld = scene.View.inverse();
    const Vector3f position = cameraWorld.getTranslation();
    const Vector3f forward( -cameraWorld.m[2][0], -cameraWorld.m[2][1], -cameraWorld.m[2][2] );
    const Vector3f up( cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2] );
    const Vector3f mirrorPosition( position.x, 2.0f * h - position.y, position.z );
    const Vector3f mirrorForward( forward.x, -forward.y, forward.z );
    const Matrix4x4f mirrorView = Matrix4x4f::LookAt( mirrorPosition, mirrorPosition + mirrorForward, up );

    // REFLMAT = T(0.5, -0.5) * Proj * View' (rows of the column-vector
    // matrix), Proj = the main projection.
    const Matrix4x4f viewProjection = scene.Projection * mirrorView;
    const Vector4f r0 = Row( viewProjection, 0 ), r1 = Row( viewProjection, 1 ), r2 = Row( viewProjection, 2 ),
                   r3 = Row( viewProjection, 3 );
    reflectionMatrix = Matrix4x4f::Identity();
    const Vector4f rows[4] = {
        Vector4f( 0.5f * r0.x + 0.5f * r3.x, 0.5f * r0.y + 0.5f * r3.y, 0.5f * r0.z + 0.5f * r3.z, 0.5f * r0.w + 0.5f * r3.w ),
        Vector4f( -0.5f * r1.x + 0.5f * r3.x, -0.5f * r1.y + 0.5f * r3.y, -0.5f * r1.z + 0.5f * r3.z,
                  -0.5f * r1.w + 0.5f * r3.w ),
        Vector4f( 0.5f * r2.x, 0.5f * r2.y, 0.5f * r2.z, 0.5f * r2.w ), r3 };
    for ( int r = 0; r < 4; r++ ) {
        reflectionMatrix.m[0][r] = rows[r].x;
        reflectionMatrix.m[1][r] = rows[r].y;
        reflectionMatrix.m[2][r] = rows[r].z;
        reflectionMatrix.m[3][r] = rows[r].w;
    }

    // The original clips with a user clip plane (y >= h, FUN_00961bc0); the
    // same clipping with an oblique near plane: plane in view space.
    Matrix4x4f projection = scene.Projection;
    {
        const Matrix4x4f inverseTranspose = mirrorView.inverse().transposed();
        const Vector4f planeWorld( 0.0f, 1.0f, 0.0f, -h );
        Vector4f plane( 0.0f, 0.0f, 0.0f, 0.0f );
        // plane_view = inverse(View)^T * plane_world
        plane.x = inverseTranspose.m[0][0] * planeWorld.x + inverseTranspose.m[1][0] * planeWorld.y +
                  inverseTranspose.m[2][0] * planeWorld.z + inverseTranspose.m[3][0] * planeWorld.w;
        plane.y = inverseTranspose.m[0][1] * planeWorld.x + inverseTranspose.m[1][1] * planeWorld.y +
                  inverseTranspose.m[2][1] * planeWorld.z + inverseTranspose.m[3][1] * planeWorld.w;
        plane.z = inverseTranspose.m[0][2] * planeWorld.x + inverseTranspose.m[1][2] * planeWorld.y +
                  inverseTranspose.m[2][2] * planeWorld.z + inverseTranspose.m[3][2] * planeWorld.w;
        plane.w = inverseTranspose.m[0][3] * planeWorld.x + inverseTranspose.m[1][3] * planeWorld.y +
                  inverseTranspose.m[2][3] * planeWorld.z + inverseTranspose.m[3][3] * planeWorld.w;
        // Lengyel oblique near plane, clip z in [0, 1].
        const Matrix4x4f inverseProjection = projection.inverse();
        const float sx = plane.x > 0.0f ? 1.0f : ( plane.x < 0.0f ? -1.0f : 0.0f );
        const float sy = plane.y > 0.0f ? 1.0f : ( plane.y < 0.0f ? -1.0f : 0.0f );
        const Vector4f corner( sx, sy, 1.0f, 1.0f );
        Vector4f q( 0.0f, 0.0f, 0.0f, 0.0f );
        q.x = inverseProjection.m[0][0] * corner.x + inverseProjection.m[1][0] * corner.y +
              inverseProjection.m[2][0] * corner.z + inverseProjection.m[3][0] * corner.w;
        q.y = inverseProjection.m[0][1] * corner.x + inverseProjection.m[1][1] * corner.y +
              inverseProjection.m[2][1] * corner.z + inverseProjection.m[3][1] * corner.w;
        q.z = inverseProjection.m[0][2] * corner.x + inverseProjection.m[1][2] * corner.y +
              inverseProjection.m[2][2] * corner.z + inverseProjection.m[3][2] * corner.w;
        q.w = inverseProjection.m[0][3] * corner.x + inverseProjection.m[1][3] * corner.y +
              inverseProjection.m[2][3] * corner.z + inverseProjection.m[3][3] * corner.w;
        const float dot = plane.x * q.x + plane.y * q.y + plane.z * q.z + plane.w * q.w;
        if ( std::abs( dot ) > 1e-8f && plane.w < 0.0f ) {
            const float scale = 1.0f / dot;
            projection.m[0][2] = plane.x * scale;
            projection.m[1][2] = plane.y * scale;
            projection.m[2][2] = plane.z * scale;
            projection.m[3][2] = plane.w * scale;
        }
    }

    EdenSceneParams mirrorScene = scene;
    mirrorScene.View = mirrorView;
    mirrorScene.Projection = projection;
    mirrorScene.CameraPosition = mirrorPosition;
    mirrorScene.CameraDirection = mirrorForward;
    mirrorScene.Width = mirrorScene.Height = kReflectionSize;
    mirrorScene.bMirroredView = false;
    // Cleared to ocean_reflectionColor (the sky is not drawn).
    const Vector4f clear = WeatherVector( pWeather, "ocean_reflectionColor", Vector4f( 0.12f, 0.23f, 0.18f, 1.0f ) );
    RenderDevice &device = gpRender->getDevice();
    device.beginRenderTarget( pReflection, true, ColorRGBA( clear.x, clear.y, clear.z, 1.0f ) );
    renderer.setReflectionPass( true );
    renderer.beginScene( mirrorScene );
    drawReflected( renderer, mirrorScene );
    renderer.endScene();
    renderer.setReflectionPass( false );
    device.endRenderTarget();
}

void EdenOcean::setupMaterial( EdenRenderer &renderer, EdenMaterial &material, eKind kind, GPUTexture *pColorMap,
                               const WeatherPreset *pWeather ) const {
    if ( !bReady ) {
        return;
    }
    // FUN_008be1e0: kind -> S_USERTYPE1 (colour map UV per sector / area /
    // island), spec[3] = 1; opaque, depth write, CW cull.
    if ( material.Spec[32] != kind || material.Spec[3] != 1 || material.NumSlots < 4 ) {
        material.Spec[32] = kind;
        material.Spec[3] = 1;
        material.NumSlots = Max<uint32_t>( material.NumSlots, 4 );
        material.SrcBlend = 2;
        material.DstBlend = 1;
        material.bDepthWrite = true;
        material.CullMode = 2;
        material.TextureHashes[0] = GetIdentifier64bit( "OCEAN_RT" );
        material.TextureHashes[1] = GetIdentifier64bit( "NOISENMAP" );
        material.TextureHashes[2] = 0;
        material.TextureHashes[3] = GetIdentifier64bit( "COLORMAP" );
        material.SamplerIndex[0] = kSamplerLinearClamp;
        material.SamplerIndex[1] = kSamplerLinearWrap;
        material.SamplerIndex[3] = kSamplerLinearClamp;
        renderer.updateProgram( material );
    }
    material.pTextures[0] = &pReflection->Color;
    material.pTextures[1] = &pNormalMap->Color;
    material.pTextures[3] = pColorMap;

    // FUN_00961710 (weather -> constants), FUN_00982f60 (distribution).
    const Vector4f lowRes = WeatherVector( pWeather, "ocean_NMapLR", Vector4f( 0.015f, 1.0f, 0.0f, 0.0f ) );
    const Vector4f highRes = WeatherVector( pWeather, "ocean_NMapHR", Vector4f( 0.15f, 1.0f, 0.0f, 0.0f ) );
    const float specularPower = WeatherFloat( pWeather, "ocean_specularPower", 50.0f );
    const float sunIntensity = WeatherFloat( pWeather, "Skydome_sunIntensity", 0.5f );
    const Vector4f fresnel = WeatherVector( pWeather, "ocean_fresnel", Vector4f( 0.2565f, 11.087f, 0.7f, 0.0f ) );
    const Vector4f specularColor =
        WeatherVector( pWeather, "ocean_specularColorAndCutOff", Vector4f( 0.78f, 0.66f, 0.8f, 0.0f ) );
    const float attenuationBluriness = WeatherFloat( pWeather, "ocean_attenuationBluriness", 0.0f );
    const float attenuationFresnel = WeatherFloat( pWeather, "ocean_attenuationFresnel", 0.0f );
    const float reflectionBluriness = WeatherFloat( pWeather, "ocean_reflectionBluriness", 0.11f );

    material.StageConstants[0]["matParam"] = { Vector4f( lowRes.x, highRes.x, 0.0f, 0.0f ) };
    material.StageConstants[0]["reflMat__float4x4"] = { Row( reflectionMatrix, 0 ), Row( reflectionMatrix, 1 ),
                                              Row( reflectionMatrix, 2 ), Row( reflectionMatrix, 3 ) };
    material.StageConstants[0]["ShiftWorld"] = { Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) };
    material.StageConstants[0]["AreaSize"] = { Vector4f( 7.0f, 6.0f, 0.0f, 0.0f ) };
    material.StageConstants[1]["matParam__array4"] = {
        Vector4f( specularPower / Max( sunIntensity * sunIntensity, 1e-6f ), 2.0f * lowRes.y, 2.0f * highRes.y, 0.0f ),
        Vector4f( fresnel.x * fresnel.z, ( 1.0f - fresnel.x ) * fresnel.z, fresnel.y, 0.0f ),
        Vector4f( specularColor.x, specularColor.y, specularColor.z, 0.0f ),
        Vector4f( attenuationBluriness, attenuationFresnel, 0.001f, reflectionBluriness ) };
}
