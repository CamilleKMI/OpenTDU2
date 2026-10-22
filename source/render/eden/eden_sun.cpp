#include "shared.h"
#include "eden_sun.h"

#include "filesystem/gs_file.h"
#include "render/eden/eden_weather.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"

namespace {
constexpr uint64_t kSunHaloShaderSet = 0xfacaa96babcbdbaaull; // TD_SunHalo
constexpr uint64_t kFlareShaderSet = 0x333788666a373333ull;   // TD_Flare
// TD_SunHalo item f4a7cf260ad9d328: S_NBTEXTURES = 4 (base/next textures
// of the two weathers being blended).
constexpr uint8_t kHaloSpec[EdenShaderSystem::kNumSpecs] = { 4 };
// TD_Flare item 379015535fbf351c: texture x vertex color (S_PRELIGHT).
constexpr uint8_t kFlareSpec[EdenShaderSystem::kNumSpecs] = { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1 };

uint32_t PackColor( const Vector4f &color ) {
    auto channel = []( float value ) { return static_cast<uint32_t>( Clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f ); };
    // D3DCOLOR (B8G8R8A8 in memory)
    return channel( color.z ) | ( channel( color.y ) << 8 ) | ( channel( color.x ) << 16 ) | ( channel( color.w ) << 24 );
}

Vector4f TransformClip( const Matrix4x4f &m, const Vector3f &p ) {
    return Vector4f( m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0],
                     m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1],
                     m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2],
                     m.m[0][3] * p.x + m.m[1][3] * p.y + m.m[2][3] * p.z + m.m[3][3] );
}
} // namespace

EdenSun::EdenSun()
    : pVertexBuffers {}, pReadbackBuffers {}, bReadbackPending {}, pIndexBuffer( nullptr ), numQuads( 0 ),
      visibility( 0.0f ), bSunOnScreen( false ), bReady( false ) {}

EdenSun::~EdenSun() { destroy(); }

GPUTexture *EdenSun::findTexture( const char *pName ) const {
    return gpRender->getTextureCache().find( sunBank.getPath().c_str(), pName );
}

void EdenSun::setupMaterial( EdenRenderer &renderer, EdenMaterial &material, uint64_t guid, const uint8_t *pSpec,
                             GPUTexture *pTexture, uint32_t numSlots ) {
    material.ShaderSetGUID = guid;
    memcpy( material.Spec, pSpec, sizeof( material.Spec ) );
    material.NumSlots = numSlots;
    for ( uint32_t i = 0; i < numSlots; i++ ) {
        material.pTextures[i] = pTexture;
        material.TextureHashes[i] = pTexture != nullptr ? 0x10000ull + i : 0;
    }
    // Additive (D3DBLEND_SRCALPHA, D3DBLEND_ONE), no depth writes.
    material.SrcBlend = 5;
    material.DstBlend = 2;
    material.CullMode = 1;
    material.bDepthWrite = false;
    material.Bucket = 0x2f;
    renderer.updateProgram( material );
}

bool EdenSun::initialize( EdenRenderer &renderer ) {
    destroy();
    const std::string path = gpFile->getBankFolder() + "fx/sun.bnk";
    if ( !sunBank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "EdenSun: '%s' not found\n", path.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( sunBank, sunBank.getPath().c_str() );

    setupMaterial( renderer, haloMaterial, kSunHaloShaderSet, kHaloSpec, findTexture( "sun_a" ), 4 );
    haloMaterial.Name = "SunHalo0";
    haloMaterial.bDepthTest = true; // pushed to the far plane: behind the scene
    setupMaterial( renderer, coreMaterial, kSunHaloShaderSet, kHaloSpec, findTexture( "sun_i" ), 4 );
    coreMaterial.Name = "SunFlare";
    coreMaterial.bDepthTest = true;
    for ( const BankEntry *pEntry : sunBank.findEntriesByExtension( "2db" ) ) {
        if ( ToLower( pEntry->Name ).rfind( "flare_", 0 ) != 0 ) {
            continue;
        }
        EdenMaterial &material = flareMaterials[ToLower( pEntry->Name )];
        setupMaterial( renderer, material, kFlareShaderSet, kFlareSpec, findTexture( pEntry->Name.c_str() ), 1 );
        material.Name = pEntry->Name;
        material.bDepthTest = false;
    }

    RenderDevice &device = gpRender->getDevice();
    for ( uint32_t i = 0; i < RenderDevice::kFramesInFlight; i++ ) {
        pVertexBuffers[i] = device.createBuffer( kMaxQuads * 4 * sizeof( Vertex ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                 nullptr );
        pReadbackBuffers[i] =
            device.createBuffer( kProbeSize * kProbeSize * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr );
        bReadbackPending[i] = false;
    }
    std::vector<uint32_t> indices;
    for ( uint32_t quad = 0; quad < kMaxQuads; quad++ ) {
        const uint32_t base = quad * 4;
        indices.insert( indices.end(), { base, base + 1, base + 2, base + 2, base + 1, base + 3 } );
    }
    pIndexBuffer = device.createBuffer( indices.size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        indices.data() );
    bReady = haloMaterial.pProgram != nullptr && pIndexBuffer != nullptr;
    OTDU_LOG_DEBUG( "EdenSun: halo %s, flare %s, %zu flare materials\n",
                    haloMaterial.pProgram ? haloMaterial.pProgram->Name.c_str() : "(none)",
                    flareMaterials.empty() || flareMaterials.begin()->second.pProgram == nullptr
                        ? "(none)"
                        : flareMaterials.begin()->second.pProgram->Name.c_str(),
                    flareMaterials.size() );
    return bReady;
}

void EdenSun::destroy() {
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        for ( uint32_t i = 0; i < RenderDevice::kFramesInFlight; i++ ) {
            device.destroyBuffer( pVertexBuffers[i] );
            device.destroyBuffer( pReadbackBuffers[i] );
            pVertexBuffers[i] = pReadbackBuffers[i] = nullptr;
        }
        device.destroyBuffer( pIndexBuffer );
        if ( !sunBank.getEntries().empty() ) {
            gpRender->getTextureCache().releaseOwner( sunBank.getPath().c_str() );
        }
    }
    pIndexBuffer = nullptr;
    sunBank.unload();
    flareMaterials.clear();
    bReady = false;
}

Vector3f EdenSun::GetVisualSunDirection( const Vector3f &sunDirection ) {
    // td_dome vs (all permutations): the dome draws the sun disk lowered by
    // a = 2 pi frac(-(1 + L.y) 0.0858386 + 0.5) - pi (L = __DirLight_Dir_0,
    // the direction the light travels) around the horizontal axis (same
    // azimuth). The halo and the flares are placed on that visible disk
    // [INF: the CPU placement code was not found; matches the in game
    // screenshots].
    const Vector3f light = sunDirection.normalized();
    const Vector3f toSun = -light;
    const float horizontal = std::sqrt( toSun.x * toSun.x + toSun.z * toSun.z );
    if ( horizontal < 1e-5f ) {
        return toSun;
    }
    float phase = -( 1.0f + light.y ) * 0.08583862334489822f + 0.5f;
    phase -= std::floor( phase );
    const float angle = phase * 6.2831854820251465f - 3.1415927410125732f;
    const float elevation = std::asin( Clamp( toSun.y, -1.0f, 1.0f ) ) + angle;
    return Vector3f( toSun.x / horizontal * std::cos( elevation ), std::sin( elevation ),
                     toSun.z / horizontal * std::cos( elevation ) );
}

bool EdenSun::projectSun( const EdenSceneParams &scene, Vector2f &outNdc ) const {
    const Vector3f toSun = GetVisualSunDirection( scene.SunDirection );
    const Vector3f viewDirection = scene.View.transformVector( toSun );
    if ( viewDirection.z >= -1e-3f ) { // Right handed view space: in front is -Z
        return false;
    }
    const Vector4f clip = TransformClip( scene.Projection, viewDirection * 1000.0f );
    if ( clip.w <= 0.0f ) {
        return false;
    }
    outNdc = Vector2f( clip.x / clip.w, clip.y / clip.w );
    return true;
}

void EdenSun::updateVisibility( const EdenSceneParams &scene, RenderTarget *pSceneTarget ) {
    if ( !bReady || pSceneTarget == nullptr || !pSceneTarget->bInitialized ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    const uint32_t slot = device.getFrameSlot();
    // Result of the readback queued kFramesInFlight frames ago (its fence
    // has been waited by beginFrame).
    if ( bReadbackPending[slot] && pReadbackBuffers[slot]->pMapped != nullptr ) {
        const float *pDepth = static_cast<const float *>( pReadbackBuffers[slot]->pMapped );
        uint32_t numSky = 0;
        for ( uint32_t i = 0; i < kProbeSize * kProbeSize; i++ ) {
            numSky += pDepth[i] >= 0.999999f ? 1u : 0u;
        }
        visibility = static_cast<float>( numSky ) / ( kProbeSize * kProbeSize );
    }
    bReadbackPending[slot] = false;
    numQuads = 0; // The vertex buffer of this frame slot is free again

    bSunOnScreen = projectSun( scene, sunScreen ) && std::abs( sunScreen.x ) < 1.2f && std::abs( sunScreen.y ) < 1.2f;
    if ( !bSunOnScreen ) {
        visibility = 0.0f;
        return;
    }
    const int32_t pixelX = static_cast<int32_t>( ( sunScreen.x * 0.5f + 0.5f ) * pSceneTarget->Width );
    const int32_t pixelY = static_cast<int32_t>( ( 0.5f - sunScreen.y * 0.5f ) * pSceneTarget->Height );
    if ( pixelX < 0 || pixelY < 0 || pixelX >= static_cast<int32_t>( pSceneTarget->Width ) ||
         pixelY >= static_cast<int32_t>( pSceneTarget->Height ) ) {
        visibility = 0.0f;
        return;
    }
    bReadbackPending[slot] =
        device.copyRenderTargetDepth( pSceneTarget, pixelX - static_cast<int32_t>( kProbeSize / 2 ),
                                      pixelY - static_cast<int32_t>( kProbeSize / 2 ), kProbeSize, kProbeSize,
                                      pReadbackBuffers[slot] );
    if ( !bReadbackPending[slot] ) {
        visibility = 1.0f;
    }
}

EdenMesh EdenSun::beginQuads() {
    EdenMesh mesh;
    mesh.NumStreams = 1;
    mesh.pStreamBuffers[0] = pVertexBuffers[gpRender->getDevice().getFrameSlot()];
    mesh.Strides[0] = sizeof( Vertex );
    RawVertexAttribute attribute;
    attribute.Usage = 0;
    attribute.Format = VAF_POSITION_R32G32B32_SFLOAT;
    attribute.Offset = offsetof( Vertex, Position );
    mesh.Attributes.push_back( attribute );
    attribute.Usage = 10;
    attribute.Format = VAF_COLOR_B8G8R8A8_UNORM;
    attribute.Offset = offsetof( Vertex, Color );
    mesh.Attributes.push_back( attribute );
    attribute.Usage = 3;
    attribute.Format = VAF_NORMAL_R32G32B32_SFLOAT;
    attribute.Offset = offsetof( Vertex, Normal );
    mesh.Attributes.push_back( attribute );
    attribute.Usage = 5;
    attribute.Format = VAF_TEXCOORD_R32G32_SFLOAT;
    attribute.Offset = offsetof( Vertex, Texcoord );
    mesh.Attributes.push_back( attribute );
    mesh.pIndexBuffer = pIndexBuffer;
    mesh.computeLayoutKey();
    return mesh;
}

void EdenSun::addQuad( const Vector3f &position, const Vector2f &offset, const Vector2f &halfSize, uint32_t color,
                       bool bScreenSpace ) {
    GPUBuffer *pBuffer = pVertexBuffers[gpRender->getDevice().getFrameSlot()];
    if ( numQuads >= kMaxQuads || pBuffer == nullptr || pBuffer->pMapped == nullptr ) {
        return;
    }
    Vertex *pVertices = static_cast<Vertex *>( pBuffer->pMapped ) + numQuads * 4;
    static constexpr float kCorners[4][2] = { { -1.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };
    for ( uint32_t i = 0; i < 4; i++ ) {
        Vertex &vertex = pVertices[i];
        const float dx = kCorners[i][0] * halfSize.x;
        const float dy = kCorners[i][1] * halfSize.y;
        if ( bScreenSpace ) {
            // TD_SunHalo: x / z, y / z = screen position (D3D NDC)
            vertex.Position[0] = position.x + offset.x + dx;
            vertex.Position[1] = position.y + offset.y + dy;
            vertex.Position[2] = 1.0f;
            vertex.Normal[0] = vertex.Normal[1] = vertex.Normal[2] = 0.0f;
        } else {
            // TD_Flare: view space position + offset (normal.xy)
            vertex.Position[0] = position.x;
            vertex.Position[1] = position.y;
            vertex.Position[2] = position.z;
            vertex.Normal[0] = offset.x + dx;
            vertex.Normal[1] = offset.y + dy;
            vertex.Normal[2] = 0.0f;
        }
        vertex.Color = color;
        vertex.Texcoord[0] = kCorners[i][0] * 0.5f + 0.5f;
        vertex.Texcoord[1] = 0.5f - kCorners[i][1] * 0.5f;
    }
    numQuads++;
}

void EdenSun::drawHalo( EdenRenderer &renderer, const EdenSceneParams &scene, const WeatherPreset *pWeather ) {
    if ( !bReady || !bSunOnScreen || visibility <= 0.0f || pWeather == nullptr ) {
        return;
    }
    const float aspect = static_cast<float>( scene.Width ) / Max( 1.0f, static_cast<float>( scene.Height ) );
    EdenMesh mesh = beginQuads();
    const Vector3f center( sunScreen.x, sunScreen.y, 1.0f );
    const float haloSize = pWeather->getFloat( "sunflare_halo_size", 0.4f );
    const float coreSize = pWeather->getFloat( "sunflare_core_size", 0.8f );
    const Vector4f haloColor = pWeather->getVector4( "sunflare_halo_color", Vector4f( 1, 1, 1, 1 ) );
    const Vector4f coreColor = pWeather->getVector4( "sunflare_core_color", Vector4f( 1, 1, 1, 1 ) );
    const uint32_t firstQuad = numQuads;
    addQuad( center, Vector2f(), Vector2f( haloSize / aspect, haloSize ), PackColor( haloColor ), true );
    addQuad( center, Vector2f(), Vector2f( coreSize * 0.25f / aspect, coreSize * 0.25f ), PackColor( coreColor ), true );

    // MatParam0: x visibility (alpha, the quad shrinks when occluded), y / z
    // weights of the base / next weather texture. MatParam1: x blend towards
    // the second texture pair, y / z its weights.
    EdenDrawParams params;
    params.Vectors["MatParam0"] = Vector4f( visibility, 1.0f, 0.0f, 0.0f );
    params.Vectors["MatParam1"] = Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    const Vector3f emissive = pWeather->getVector3( "sunflare_core_emissive", Vector3f( 1, 1, 1 ) );
    params.Vectors["__Material_Emissive"] = Vector4f( emissive, 1.0f );
    mesh.FirstIndex = firstQuad * 6;
    mesh.IndexCount = 6;
    renderer.draw( mesh, haloMaterial, Matrix4x4f::Identity(), &params );
    mesh.FirstIndex = ( firstQuad + 1 ) * 6;
    renderer.draw( mesh, coreMaterial, Matrix4x4f::Identity(), &params );
}

void EdenSun::drawFlares( EdenRenderer &renderer, const EdenSceneParams &scene, const WeatherPreset *pWeather ) {
    if ( !bReady || !bSunOnScreen || visibility <= 0.0f || pWeather == nullptr ) {
        return;
    }
    const int32_t count = static_cast<int32_t>( pWeather->getFloat( "sunflare_lensflare_count", 0.0f ) );
    Vector4f color = pWeather->getVector4( "sunflare_lensflare_color", Vector4f( 1, 1, 1, 1 ) );
    color.w *= visibility;
    // Quads in view space at 1 unit, screen offsets converted with the
    // projection scale (world matrix = inverse view: __World_View = identity).
    const float scaleX = 1.0f / scene.Projection.m[0][0];
    const float scaleY = 1.0f / scene.Projection.m[1][1];
    const Matrix4x4f inverseView = scene.View.inverse();
    EdenMesh mesh = beginQuads();
    struct Draw {
        const EdenMaterial *pMaterial;
        uint32_t Quad;
    };
    std::vector<Draw> draws;
    for ( int32_t i = 1; i <= count && i <= 8; i++ ) {
        const std::string prefix = "sunflare_lensflare" + std::to_string( i );
        const std::string materialName = ToLower( pWeather->getString( ( prefix + "_material" ).c_str(), "" ) );
        auto it = flareMaterials.find( materialName );
        if ( it == flareMaterials.end() || it->second.pProgram == nullptr ) {
            continue;
        }
        const float position = pWeather->getFloat( ( prefix + "_position" ).c_str(), 1.0f );
        const Vector4f size = pWeather->getVector4( ( prefix + "_size" ).c_str(), Vector4f( 0.1f, 0.1f, 0, 0 ) );
        // Along the sun -> screen center line (1: on the sun).
        const Vector2f ndc = sunScreen * position;
        // Sizes in screen height units (square sprites stay square).
        const Vector2f halfSize( size.x * scaleY, size.y * scaleY );
        draws.push_back( { &it->second, numQuads } );
        addQuad( Vector3f( ndc.x * scaleX, ndc.y * scaleY, -1.0f ), Vector2f(), halfSize, PackColor( color ), false );
    }
    for ( const Draw &draw : draws ) {
        mesh.FirstIndex = draw.Quad * 6;
        mesh.IndexCount = 6;
        renderer.draw( mesh, *draw.pMaterial, inverseView );
    }
}
