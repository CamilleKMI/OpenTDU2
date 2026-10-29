#include "shared.h"
#include "eden_world.h"

#include <cfloat>

#include "config/gs_config.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

// Library model LODs and tree impostors of the .pmi instances
// (docs/re/lod_impostor_spec.md).
//
// [CODE] FUN_00da0440: model instance LOD table { object, lodDistance x
//   3.160494 } (squared metres).
// [CODE] FUN_00989d10: LOD i is the first with d^2 < dist2[i] x F, F =
//   SET_GLOBAL_LOD_FACTOR (f) for LOD 0..n-2, max(f, 1) for the last one;
//   beyond the last LOD the instance is not drawn (EdenWorld::loadInstances
//   culls it).
// [CODE] FUN_00989b50 (per instance, at load): if the material of the LAST
//   LOD object is TD_Impostor, the instance keeps it (+0xcc), a pointer to
//   its NbImpostorSide parameter (+0xd0), and the material gets cull mode 3,
//   alpha test on (+0x35 = 0), depth write (+0x31 = 1).
// [CODE] FUN_009969e0 (per frame): D = |pos - cam|^2 / s^2 (s = instance
//   scale, max of the three record scales: 0x9890c2), T = dist2[n-2] x f.
//   D <= T: the mesh is drawn with the blend 1 - 0.99 (D - 0.33 T) / (T -
//   0.33 T) clamped to [0, 1]; otherwise the instance is queued in an
//   impostor batch of its TD_Impostor material (FUN_00996300, 146 per batch)
//   and drawn by FUN_00996470 (at most 20000 impostors per frame, 6
//   vertices each). The per impostor vertex builder (FUN_004d9200) is [ENC]:
//   the quad below is inferred from the data (template quad of the LOD
//   object, atlas layout, view to cell mapping checked on the LOD 0 meshes).
namespace {
constexpr uint64_t kTDImpostor = 0x83078865caca336dull;
constexpr uint64_t kTDVegetation = 0x73078865caca336dull;
constexpr uint64_t kTDVegetation2 = 0x37078865c0ca336eull;
constexpr uint64_t kNbImpostorSide = 0x54539494969c949dull; // TD_Impostor sym 0 (FUN_00989b50 0x989cd9)
constexpr float kLodDistanceScale = 3.160494f;               // 0x00f4aa28
constexpr float kBlendStart = 0.33f;                         // DAT_00f76638
constexpr float kBlendSlope = 0.99f;                         // 0x00f4ae50
constexpr uint32_t kMaxImpostors = 20000;                    // FUN_00996470 iStack_8c
constexpr float kTwoPi = 6.28318530718f;

float GetLodFactor() {
    return gpConfig != nullptr ? gpConfig->getVideoSettings().GlobalLodFactor : 1.0f;
}

float RowLength( const Matrix4x4f &m, int row ) {
    return std::sqrt( m.m[row][0] * m.m[row][0] + m.m[row][1] * m.m[row][1] + m.m[row][2] * m.m[row][2] );
}
} // namespace

void EdenWorld::loadLibraryLods( EdenRenderer &renderer, const char *pTextureOwner ) {
    static const bool bNoLods = CmdLine::HasArg( "-nolods" ); // comparisons: LOD 0 up to the last distance
    const std::vector<SceneNode> &nodes = libraryModel.getScene().getNodes();
    libraryNodeLods.assign( nodes.size(), NodeLod() );
    if ( bNoLods ) {
        return;
    }
    for ( uint32_t level = 1; level <= 2; level++ ) {
        EdenModel &model = libraryLodModels[level - 1];
        model.setLodLevel( level );
        if ( !model.load( renderer, commonBank, "library", &libraryMaterials, pTextureOwner ) ) {
            OTDU_LOG_WARN( "EdenWorld: library LOD %u not loaded\n", level );
            continue;
        }
        for ( EdenMaterial &material : model.getMaterials() ) {
            if ( EdenNeons::IsNeonSetMaterial( material ) ) {
                sharedNeonMaterials.push_back( &material );
            }
        }
    }
    uint32_t numLodNodes = 0, numImpostorNodes = 0;
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        const SceneNode &node = nodes[i];
        NodeLod &lod = libraryNodeLods[i];
        lod.NumLods = static_cast<uint8_t>( Min<size_t>( node.LodDistances.size(), 3 ) );
        for ( uint32_t k = 0; k < lod.NumLods; k++ ) {
            lod.Distance2[k] = node.LodDistances[k] * kLodDistanceScale;
        }
        if ( lod.NumLods < 2 ) {
            continue;
        }
        numLodNodes++;
        EdenModel &lastModel = libraryLodModels[lod.NumLods - 2];
        const auto range = lastModel.getNodeItemRange( static_cast<int32_t>( i ) );
        if ( range.second == 0 ) {
            continue;
        }
        const EdenModel::Item &item = lastModel.getItems()[range.first];
        EdenMaterial &material = lastModel.getMaterials()[item.MaterialIndex];
        if ( material.ShaderSetGUID != kTDImpostor ) {
            continue;
        }
        // FUN_00989b50 0x989ca4..0x989ccf: cull mode 3, +0x35 = 0 (alpha
        // test), +0x31 = 1 (depth write).
        material.CullMode = 3;
        // Slot 1 (runtime dissolve texture of the handler 0x00d717e0) makes
        // S_NBTEXTURES 2 = the dissolve permutations (items 1/3/4/6 [SHADER
        // ids = HashSpec]). Their threshold (vs: (v - 1 - 0.2 (sin z + 1)) x
        // (0.4 + 0.15 (sin 2z + 1)) <= 0, ps: keep if dissolve <= threshold)
        // discards the whole quad, so the plain draw is the S_NBTEXTURES 1
        // permutation [INF]; the runtime selection is [ENC].
        if ( material.NumSlots > 1 && material.TextureHashes[1] == 1ull ) {
            material.TextureHashes[1] = 0;
            renderer.updateProgram( material );
        }
        material.bAlphaTest = true;
        material.bDepthWrite = true;
        lod.bImpostor = true;
        lod.pImpostorMaterial = &material;
        auto it = material.Parameters.find( kNbImpostorSide );
        // No parameter: 15 (FUN_009969e0 0x996bd1) [CODE]; every library
        // impostor has one (3, 7 or 15) [DATA].
        const float sides = it != material.Parameters.end() ? it->second.x : 15.0f;
        lod.NumSides = static_cast<uint32_t>( Max( sides, 1.0f ) );
        // Template quad (4 vertices in the model x = 0 plane, u = 0 .. 1 /
        // (NbImpostorSide + 1), v = 0 at the top) [DATA].
        lod.HalfWidth = Max( std::abs( item.BoundsMin.z ), std::abs( item.BoundsMax.z ) );
        lod.Top = item.BoundsMax.y;
        lod.Bottom = item.BoundsMin.y;
        lod.CellWidth = 1.0f / static_cast<float>( lod.NumSides + 1 );
        numImpostorNodes++;
        OTDU_LOG_DEBUG( "EdenWorld: impostor %s: material %s, %u sides, quad z +-%.2f y %.2f..%.2f, texture %p\n",
                        node.Name.c_str(), material.Name.c_str(), lod.NumSides, lod.HalfWidth, lod.Bottom, lod.Top,
                        static_cast<const void *>( material.pTextures[0] ) );
    }
    OTDU_LOG_INFO( "EdenWorld: library LODs: %u nodes with LOD switching, %u with an impostor (LOD 1: %zu items, "
                   "LOD 2: %zu items)\n",
                   numLodNodes, numImpostorNodes, libraryLodModels[0].getItems().size(),
                   libraryLodModels[1].getItems().size() );
    if ( pImpostorIndexBuffer == nullptr ) {
        // Template strip 0 1 2 3 = triangles (0 1 2) (2 1 3).
        std::vector<uint32_t> indices( kMaxImpostors * 6 );
        for ( uint32_t q = 0; q < kMaxImpostors; q++ ) {
            const uint32_t v = q * 4;
            const uint32_t pattern[6] = { v, v + 1, v + 2, v + 2, v + 1, v + 3 };
            memcpy( &indices[q * 6], pattern, sizeof( pattern ) );
        }
        pImpostorIndexBuffer = gpRender->getDevice().createBuffer(
            indices.size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data() );
    }
}

void EdenWorld::destroyLibraryLods() {
    for ( EdenModel &model : libraryLodModels ) {
        model.destroy();
    }
    libraryNodeLods.clear();
    impostorQueue.clear();
    impostorDraws.clear();
    numQueuedImpostors = 0;
    if ( gpRender != nullptr ) {
        gpRender->getDevice().destroyBuffer( pImpostorIndexBuffer );
    }
    pImpostorIndexBuffer = nullptr;
}

void EdenWorld::drawLibraryInstance( EdenRenderer &renderer, const PmiInstance &instance, int32_t node,
                                     const Vector3f &cameraPosition, const EdenDrawParams *pParams ) {
    {
        // Per frame counts (the world may be drawn several times per frame:
        // environment cube faces, main view).
        static uint64_t statsFrame = ~0ull;
        const uint64_t frame = gpRender->getDevice().getFrameNumber();
        if ( frame != statsFrame ) {
            static const bool bStats = getenv( "OTDU_LOD_STATS" ) != nullptr;
            if ( bStats && statsFrame != ~0ull ) {
                OTDU_LOG_INFO( "EdenWorld: frame %llu LOD draws: LOD0 %u, LOD1 %u, LOD2 %u meshes (%u faded), %u "
                               "impostors\n",
                               static_cast<unsigned long long>( statsFrame ), lodStats.Meshes[0], lodStats.Meshes[1],
                               lodStats.Meshes[2], lodStats.Faded, lodStats.Impostors );
            }
            lodStats = LodStats();
            statsFrame = frame;
        }
    }
    if ( node < 0 || node >= static_cast<int32_t>( libraryNodeLods.size() ) ||
         libraryNodeLods[node].NumLods < 2 ) {
        // No LOD switching (one LOD): LOD 0, culled by loadInstances only.
        libraryModel.drawNodeItems( renderer, node, instance.World, pParams );
        lodStats.Meshes[0]++;
        return;
    }
    const NodeLod &lod = libraryNodeLods[node];
    const uint32_t numLods = lod.NumLods;
    const float f = GetLodFactor();
    const Vector3f delta = instance.Position - cameraPosition;
    const float d2 = delta.dot( delta );
    // FUN_00989d10.
    uint32_t level = numLods - 1;
    for ( uint32_t i = 0; i < numLods; i++ ) {
        const float factor = i == numLods - 1 ? Max( f, 1.0f ) : f;
        if ( d2 < lod.Distance2[i] * factor ) {
            level = i;
            break;
        }
    }
    EdenDrawParams fadeParams;
    if ( lod.bImpostor ) {
        static const bool bForceImpostors = getenv( "OTDU_FORCE_IMPOSTORS" ) != nullptr; // calibration
        static const bool bNoImpostors = CmdLine::HasArg( "-noimpostors" );
        // FUN_009969e0: scale = max of the record scales (+0x60).
        const float scale = Max( RowLength( instance.World, 0 ), Max( RowLength( instance.World, 1 ),
                                                                       RowLength( instance.World, 2 ) ) );
        const float distance2 = d2 / Max( scale * scale, 1e-6f );
        const float threshold = lod.Distance2[numLods - 2] * f;
        if ( ( distance2 > threshold || bForceImpostors ) && !bNoImpostors ) {
            if ( numQueuedImpostors >= kMaxImpostors ) {
                return;
            }
            // [INF] cylindrical billboard (the builder FUN_004d9200 is ENC):
            // view angle theta = atan2(dz, dx) of (instance - camera) (the
            // angle FUN_009969e0 stores at +0xb0), in the model frame
            // (+ yaw: v_world = v_model RotY(yaw)); side view k = round(
            // angle N / 2 pi) mod N, u = k / (N + 1); screen right = (-sin,
            // 0, cos) of theta. Checked on the LOD 0 silhouettes (V_FIG_A,
            // V_FIG_C, V_HPIN_A: best atlas cell = view index, docs/re).
            const float theta = std::atan2( delta.z, delta.x );
            const float yaw = std::atan2( -instance.World.m[0][2], instance.World.m[0][0] );
            const uint32_t numSides = lod.NumSides;
            float cell = std::floor( ( theta + yaw ) / kTwoPi * static_cast<float>( numSides ) + 0.5f );
            int32_t k = static_cast<int32_t>( cell ) % static_cast<int32_t>( numSides );
            if ( k < 0 ) {
                k += static_cast<int32_t>( numSides );
            }
            const float widthScale = Max( RowLength( instance.World, 0 ), RowLength( instance.World, 2 ) );
            const float heightScale = RowLength( instance.World, 1 );
            const Vector3f right( -std::sin( theta ), 0.0f, std::cos( theta ) );
            const float u0 = static_cast<float>( k ) * lod.CellWidth;
            const float u1 = u0 + lod.CellWidth;
            const Vector3f &p = instance.Position;
            const float z[2] = { -lod.HalfWidth * widthScale, lod.HalfWidth * widthScale };
            const float y[2] = { lod.Top * heightScale, lod.Bottom * heightScale };
            std::vector<ImpostorVertex> &vertices = impostorQueue[lod.pImpostorMaterial];
            // Template order: (z-, top) (z-, bottom) (z+, top) (z+, bottom).
            for ( uint32_t c = 0; c < 4; c++ ) {
                const float zc = z[c >> 1];
                const float yc = y[c & 1];
                ImpostorVertex vertex;
                vertex.Position[0] = p.x + right.x * zc;
                vertex.Position[1] = p.y + yc;
                vertex.Position[2] = p.z + right.z * zc;
                vertex.Texcoord[0] = ( c >> 1 ) ? u1 : u0;
                vertex.Texcoord[1] = ( c & 1 ) ? 1.0f : 0.0f;
                vertices.push_back( vertex );
            }
            numQueuedImpostors++;
            lodStats.Impostors++;
            return;
        }
        level = Min( level, numLods - 2 );
        // Mesh to impostor blend: 1 - 0.99 (D - 0.33 T) / (0.67 T) [CODE];
        // its use (FUN_00996820 / FUN_0042c0c0 write the model block +0x10)
        // is [ENC]. TD_Vegetation has one input for it, __Lod_Dist.z: the PS
        // lerps the lit colour to the flat impostor colour (Ambient_0 +
        // Ambient_1) and the VS fades the wind out [SHADER]: z = 1 - blend
        // [INF].
        const float start = threshold * kBlendStart;
        const float blend = Clamp( 1.0f - kBlendSlope * ( distance2 - start ) / Max( threshold - start, 1e-6f ),
                                   0.0f, 1.0f );
        if ( blend < 1.0f ) {
            if ( pParams != nullptr ) {
                fadeParams = *pParams;
            }
            fadeParams.Vectors["__Lod_Dist"] = Vector4f( 0.0f, 1.0f, 1.0f - blend, 1.0f );
            fadeParams.Vectors["__AmbLight_Ambient_1"] = Vector4f( impostorAmbient.x, impostorAmbient.y,
                                                                   impostorAmbient.z, 1.0f );
            pParams = &fadeParams;
            lodStats.Faded++;
        }
    }
    if ( level > 0 && libraryLodModels[level - 1].getNodeItemRange( node ).second > 0 ) {
        libraryLodModels[level - 1].drawNodeItems( renderer, node, instance.World, pParams );
        lodStats.Meshes[level]++;
    } else {
        libraryModel.drawNodeItems( renderer, node, instance.World, pParams );
        lodStats.Meshes[0]++;
    }
}

void EdenWorld::flushImpostors( EdenRenderer &renderer ) {
    RenderDevice &device = gpRender->getDevice();
    if ( impostorFrame != device.getFrameNumber() ) {
        impostorDraws.clear();
        impostorFrame = device.getFrameNumber();
    }
    for ( auto &entry : impostorQueue ) {
        std::vector<ImpostorVertex> &vertices = entry.second;
        if ( vertices.empty() || pImpostorIndexBuffer == nullptr ) {
            continue;
        }
        const uint32_t numQuads = static_cast<uint32_t>( vertices.size() / 4 );
        const VkDeviceSize size = vertices.size() * sizeof( ImpostorVertex );
        TransientAllocation allocation = device.allocateTransient( size, 16 );
        if ( allocation.pData == nullptr ) {
            OTDU_LOG_WARN( "EdenWorld: no transient memory for %u impostors\n", numQuads );
            vertices.clear();
            continue;
        }
        memcpy( allocation.pData, vertices.data(), size );
        auto pDraw = std::make_unique<ImpostorDraw>();
        pDraw->Stream.Buffer = allocation.Buffer;
        pDraw->Stream.Size = size;
        EdenMesh &mesh = pDraw->Mesh;
        mesh.NumStreams = 1;
        mesh.pStreamBuffers[0] = &pDraw->Stream;
        mesh.StreamOffsets[0] = allocation.Offset;
        mesh.Strides[0] = sizeof( ImpostorVertex );
        RawVertexAttribute position;
        position.Usage = 0;
        position.Format = VAF_POSITION_R32G32B32_SFLOAT;
        position.Offset = offsetof( ImpostorVertex, Position );
        RawVertexAttribute texcoord;
        texcoord.Usage = 5;
        texcoord.Format = VAF_TEXCOORD_R32G32_SFLOAT;
        texcoord.Offset = offsetof( ImpostorVertex, Texcoord );
        mesh.Attributes = { position, texcoord };
        mesh.pIndexBuffer = pImpostorIndexBuffer;
        mesh.IndexCount = numQuads * 6;
        mesh.computeLayoutKey();
        // TD_Impostor colour = texture x (__AmbLight_Ambient_0 +
        // __AmbLight_Ambient_1) [SHADER]; light 1 = impostor_ambient
        // (FUN_00d769e0 -> renderer +0x45c0; FUN_00981600 puts sun ambient /
        // impostor_ambient in ambient lights 0 / 1 of the exterior light set
        // [CODE]; the world light set binding is [ENC]).
        pDraw->Params.Vectors["__AmbLight_Ambient_1"] =
            Vector4f( impostorAmbient.x, impostorAmbient.y, impostorAmbient.z, 1.0f );
        renderer.draw( mesh, *entry.first, Matrix4x4f::Identity(), &pDraw->Params );
        impostorDraws.push_back( std::move( pDraw ) );
        vertices.clear();
    }
    numQueuedImpostors = 0;
}
