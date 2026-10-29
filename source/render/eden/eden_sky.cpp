#include "shared.h"
#include "eden_sky.h"

#include "render/eden/eden_weather.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"

namespace {
constexpr uint64_t kDomeShaderSet = 0x2428b45eec949165ull; // td_dome.sob
constexpr uint64_t kMoonShaderSet = 0x2428b45eec949166ull; // td_moon.sob (MatMoon, FUN_00c384f0)
constexpr double kMoonDistance = 100000.0;                  // 0x00f4ac08
constexpr double kMoonSizeFactor = 10.0;                    // 0x00f49820
constexpr uint32_t kRings = 32;
constexpr uint32_t kSegments = 64;

// 0x00edd4d8 (micrometers)
constexpr float kWavelengths[4] = { 0.700f, 0.650f, 0.570f, 0.475f };
// 0x00f4b508, 0x00f4b500, 0x00f4b4f8, 0x00f4a6a8
constexpr double kMieK[4] = { 0.6871119737625122, 0.6850000023841858, 0.6790000200271606, 0.6700000166893005 };
constexpr double kRayleigh = 1.2415033427170064e-30;      // 0x00f4b520
constexpr double kRayleighAngular = 7.409676918656853e-32; // 0x00f4b518
constexpr double kMieAngular = 5.635251964567736e-16;     // 0x00f4b510
constexpr double kMie = 3.540733244794536e-15;            // 0x00f4b4f0

// Spectral (4 wavelengths) -> CIE XYZ weights (0x00edd4f8)
constexpr float kSpectrumToXYZ[3][4] = {
    { 0.01136f, 0.2835f, 0.7621f, 0.1421f },
    { 0.0041f, 0.107f, 0.952f, 0.1126f },
    { 0.0f, 0.0f, 0.0021f, 1.0419f },
};
// CIE XYZ -> RGB (0x00f4b678, rows: R, G, B)
constexpr float kXYZToRGB[3][3] = {
    { 2.3706744f, -0.9000405f, -0.4706338f },
    { -0.5138850f, 1.4253036f, 0.0885814f },
    { 0.0052982f, -0.0146949f, 1.0093968f },
};
} // namespace

EdenSky::EdenSky() : pVertexBuffer( nullptr ), pIndexBuffer( nullptr ), bReady( false ) {}

EdenSky::~EdenSky() { destroy(); }

bool EdenSky::initialize( EdenRenderer &renderer ) {
    destroy();
    // Unit sphere (the original builds a 30x30 cube sphere, only the
    // directions matter: the vertex shader projects them at the far plane).
    std::vector<float> vertices;
    for ( uint32_t ring = 0; ring <= kRings; ring++ ) {
        const float phi = kPI * ring / kRings; // 0: zenith
        for ( uint32_t segment = 0; segment <= kSegments; segment++ ) {
            const float theta = 2.0f * kPI * segment / kSegments;
            vertices.push_back( std::sin( phi ) * std::cos( theta ) );
            vertices.push_back( std::cos( phi ) );
            vertices.push_back( std::sin( phi ) * std::sin( theta ) );
        }
    }
    std::vector<uint32_t> indices;
    for ( uint32_t ring = 0; ring < kRings; ring++ ) {
        for ( uint32_t segment = 0; segment < kSegments; segment++ ) {
            const uint32_t i0 = ring * ( kSegments + 1 ) + segment;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kSegments + 1;
            const uint32_t i3 = i2 + 1;
            indices.insert( indices.end(), { i0, i1, i2, i1, i3, i2 } );
        }
    }
    RenderDevice &device = gpRender->getDevice();
    pVertexBuffer = device.createBuffer( vertices.size() * sizeof( float ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         vertices.data() );
    pIndexBuffer = device.createBuffer( indices.size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        indices.data() );
    mesh.NumStreams = 1;
    mesh.pStreamBuffers[0] = pVertexBuffer;
    mesh.Strides[0] = 12;
    RawVertexAttribute position;
    position.Usage = 0;
    position.Format = VAF_POSITION_R32G32B32_SFLOAT;
    mesh.Attributes.push_back( position );
    mesh.pIndexBuffer = pIndexBuffer;
    mesh.IndexCount = static_cast<uint32_t>( indices.size() );
    mesh.computeLayoutKey();

    // "MatDome" without stars (S_USERTYPE1 = 0, no STARS texture).
    material.Name = "MatDome";
    material.ShaderSetGUID = kDomeShaderSet;
    material.CullMode = 1;
    material.bDepthTest = true;
    material.bDepthWrite = false;
    renderer.updateProgram( material );
    bReady = material.pProgram != nullptr && pVertexBuffer != nullptr && pIndexBuffer != nullptr;

    // MatMoon (FUN_00c384f0): 4 vertices (-1,-1) (-1,1) (1,1) (1,-1) with
    // uv = (x, y) in [0, 1] corners, indices 0 1 3 2 (strip); SRCALPHA /
    // INVSRCALPHA, texture "MOON" (fx/weatherfx.bnk).
    const std::string weatherFxPath = gpFile->getBankFolder() + "fx/weatherfx.bnk";
    if ( weatherFxBank.load( weatherFxPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( weatherFxBank, weatherFxBank.getPath().c_str() );
        GPUTexture *pMoon = gpRender->getTextureCache().find( weatherFxBank.getPath().c_str(), "moon" );
        const float moonVertices[4][5] = {
            { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f },
            { -1.0f, 1.0f, 0.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f, 0.0f, 1.0f, 1.0f },
            { 1.0f, -1.0f, 0.0f, 1.0f, 0.0f },
        };
        const uint32_t moonIndices[6] = { 0, 1, 3, 3, 1, 2 };
        pMoonVertexBuffer =
            device.createBuffer( sizeof( moonVertices ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, moonVertices );
        pMoonIndexBuffer = device.createBuffer( sizeof( moonIndices ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, moonIndices );
        moonMesh.NumStreams = 1;
        moonMesh.pStreamBuffers[0] = pMoonVertexBuffer;
        moonMesh.Strides[0] = 20;
        RawVertexAttribute attribute;
        attribute.Usage = 0;
        attribute.Format = VAF_POSITION_R32G32B32_SFLOAT;
        attribute.Offset = 0;
        moonMesh.Attributes.push_back( attribute );
        attribute.Usage = 5;
        attribute.Format = VAF_TEXCOORD_R32G32_SFLOAT;
        attribute.Offset = 12;
        moonMesh.Attributes.push_back( attribute );
        moonMesh.pIndexBuffer = pMoonIndexBuffer;
        moonMesh.IndexCount = 6;
        moonMesh.computeLayoutKey();
        moonMaterial.Name = "MatMoon";
        moonMaterial.ShaderSetGUID = kMoonShaderSet;
        moonMaterial.NumSlots = 1;
        moonMaterial.pTextures[0] = pMoon;
        moonMaterial.TextureHashes[0] = pMoon != nullptr ? GetIdentifier64bit( "MOON" ) : 0;
        moonMaterial.SrcBlend = 5; // +0x38
        moonMaterial.DstBlend = 6; // +0x39
        moonMaterial.CullMode = 1;
        moonMaterial.bDepthTest = true;
        moonMaterial.bDepthWrite = false;
        moonMaterial.Bucket = 0x14; // +0x20
        renderer.updateProgram( moonMaterial );
        bMoonReady = pMoon != nullptr && moonMaterial.pProgram != nullptr;
        OTDU_LOG_DEBUG( "EdenSky: moon %s, program %s\n", pMoon != nullptr ? "found" : "missing",
                        moonMaterial.pProgram != nullptr ? moonMaterial.pProgram->Name.c_str() : "(none)" );
    }
    return bReady;
}

void EdenSky::destroy() {
    if ( gpRender != nullptr ) {
        gpRender->getDevice().destroyBuffer( pVertexBuffer );
        gpRender->getDevice().destroyBuffer( pIndexBuffer );
        gpRender->getDevice().destroyBuffer( pMoonVertexBuffer );
        gpRender->getDevice().destroyBuffer( pMoonIndexBuffer );
        if ( !weatherFxBank.getEntries().empty() ) {
            gpRender->getTextureCache().releaseOwner( weatherFxBank.getPath().c_str() );
        }
    }
    weatherFxBank.unload();
    pVertexBuffer = pIndexBuffer = nullptr;
    pMoonVertexBuffer = pMoonIndexBuffer = nullptr;
    moonMesh = EdenMesh();
    bReady = false;
    bMoonReady = false;
}

void EdenSky::setupAtmosphere( EdenSceneParams &scene, const WeatherPreset *pWeather ) {
    auto param = [pWeather]( const char *pName, float defaultValue ) {
        return pWeather != nullptr ? pWeather->getFloat( pName, defaultValue ) : defaultValue;
    };
    const float rayMultiplier = param( "Skydome_betaRayMultiplier", 2.0f ) * 0.1f;
    const float betaMieMultiplier = param( "Skydome_betaMieMultiplier", 0.03f );
    const float mieMultiplier = betaMieMultiplier * 0.01f;
    const float g = param( "Skydome_HGg", 999.0f ) * 0.001f;
    const float saturation = param( "Skydome_saturation", 1.0f );
    const float turbidity = param( "Skydome_turbitity", 2.0f );
    const float sunIntensity = param( "Skydome_sunIntensity", 0.4f );
    const float sunAngleAttenuation = param( "Skydome_sunAngleAttenuation", 90.0f );

    float betaR[4], betaDashR[4], betaM[4], betaDashM[4], betaSum[4], inverseSum[4];
    for ( uint32_t i = 0; i < 4; i++ ) {
        const double inverseWavelength = 1.0e6 / kWavelengths[i];
        const double inverse2 = inverseWavelength * inverseWavelength;
        const double inverse4 = inverse2 * inverse2;
        betaR[i] = static_cast<float>( inverse4 * kRayleigh * rayMultiplier );
        betaDashR[i] = static_cast<float>( inverse4 * kRayleighAngular * rayMultiplier );
        betaM[i] = static_cast<float>( inverse2 * kMieK[i] * kMie * mieMultiplier );
        betaDashM[i] = static_cast<float>( inverse2 * kMieAngular * mieMultiplier );
        betaSum[i] = betaR[i] + betaM[i];
        inverseSum[i] = 1.0f / betaSum[i];
    }

    // Sun color (FUN_0092fb20)
    const Vector3f toSun = -scene.SunDirection.normalized();
    const float zenithAngle = std::acos( Clamp( toSun.y, -1.0f, 1.0f ) );
    float theta = Min( zenithAngle, sunAngleAttenuation * kPI / 180.0f );
    theta = Max( theta, 0.0f );
    const double beta = 0.046083658933639526 * turbidity - 0.04586026072502136;
    const double opticalMass =
        1.0 / ( std::cos( theta ) + 0.15 * std::pow( 93.885 - theta * 180.0 / kPI, -1.253 ) );
    float intensity = static_cast<float>( sunIntensity * 5.0 * ( 9.2 - betaMieMultiplier * 5.5 ) );
    if ( zenithAngle > kPI * 0.5f ) {
        intensity *= std::exp( ( kPI * 0.5f - zenithAngle ) * 20.0f );
    }
    float sunColor[4];
    for ( uint32_t i = 0; i < 4; i++ ) {
        const double rayleigh = std::exp( -opticalMass * 0.008735 * std::pow( kWavelengths[i], -4.08 ) );
        const double aerosol = std::exp( -opticalMass * beta * std::pow( kWavelengths[i], -1.3 ) );
        sunColor[i] = static_cast<float>( intensity * rayleigh * aerosol );
    }

    // SpectrumToRGB: rows = RGB, columns = wavelengths.
    Matrix4x4f spectrumToRGB = Matrix4x4f::Identity();
    for ( uint32_t row = 0; row < 4; row++ ) {
        for ( uint32_t column = 0; column < 4; column++ ) {
            float value = 0.0f;
            if ( row < 3 ) {
                for ( uint32_t k = 0; k < 3; k++ ) {
                    value += kXYZToRGB[row][k] * kSpectrumToXYZ[k][column];
                }
            }
            spectrumToRGB.m[row][column] = value;
        }
    }

    std::unordered_map<std::string, Vector4f> &vectors = scene.Vectors;
    vectors["BetaRPlusBetaM"] = Vector4f( betaSum[0], betaSum[1], betaSum[2], betaSum[3] );
    vectors["HGg_Saturation"] = Vector4f( 1.0f - g * g, 1.0f + g * g, 2.0f * g, saturation );
    vectors["BetaDashR"] = Vector4f( betaDashR[0], betaDashR[1], betaDashR[2], betaDashR[3] );
    vectors["BetaDashM"] = Vector4f( betaDashM[0], betaDashM[1], betaDashM[2], betaDashM[3] );
    vectors["OneOverBetaRPlusBetaM"] = Vector4f( inverseSum[0], inverseSum[1], inverseSum[2], inverseSum[3] );
    vectors["Multipliers"] = Vector4f( 1.0f, 0.0138f, 0.0113f, 0.008f );
    vectors["SunColorAndIntensity"] = Vector4f( sunColor[0], sunColor[1], sunColor[2], sunColor[3] );
    vectors["GroundColor"] = Vector4f( 0.7f, 0.6f, 0.5f, 0.0f ); // FUN_00930d70
    vectors["StarsIntensity"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    vectors["StarsFade"] = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    scene.Matrices["SpectrumToRGB__float4x4"] = spectrumToRGB;
}

void EdenSky::draw( EdenRenderer &renderer ) {
    if ( bReady ) {
        renderer.draw( mesh, material, Matrix4x4f::Identity() );
    }
}

void EdenSky::drawMoon( EdenRenderer &renderer, const EdenSceneParams &scene, const WeatherPreset *pWeather ) {
    if ( !bMoonReady || pWeather == nullptr ) {
        return;
    }
    // FUN_0092ed80: PosMoon = (-sundirection * 100000, Skydome_moonPercent),
    // SizeMoon = (Skydome_moonSize * 10, moon object +0x1400..+0x1408 [not
    // traced; unused by the textured pixel shader]).
    const float percent = pWeather->getFloat( "Skydome_moonPercent", 0.0f );
    if ( percent <= 0.0f ) {
        return;
    }
    const Vector3f sun = scene.SunDirection;
    const float distance = static_cast<float>( kMoonDistance );
    EdenDrawParams params;
    params.Vectors["PosMoon"] = Vector4f( -sun.x * distance, -sun.y * distance, -sun.z * distance, percent );
    params.Vectors["SizeMoon"] =
        Vector4f( pWeather->getFloat( "Skydome_moonSize", 0.0f ) * static_cast<float>( kMoonSizeFactor ), 0.0f, 0.0f, 0.0f );
    renderer.draw( moonMesh, moonMaterial, Matrix4x4f::Identity(), &params );
}
