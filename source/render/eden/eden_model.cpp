#include "shared.h"
#include "eden_model.h"

#include "filesystem/bank.h"
#include "render/gs_render.h"

EdenModel::EdenModel() : pVertexBuffer( nullptr ), pIndexBuffer( nullptr ) {}

EdenModel::~EdenModel() { destroy(); }

void EdenModel::destroy() {
    if ( gpRender != nullptr ) {
        gpRender->getDevice().destroyBuffer( pVertexBuffer );
        gpRender->getDevice().destroyBuffer( pIndexBuffer );
    }
    pVertexBuffer = nullptr;
    pIndexBuffer = nullptr;
    items.clear();
    materials.clear();
    collisionVertices.clear();
    collisionIndices.clear();
    nodeTransforms.clear();
    nodeVisible.clear();
    nodeItems.clear();
}

bool EdenModel::load( EdenRenderer &renderer, const Bank &bank, const char *pBaseName,
                      const Render2DM *pFallbackMaterials, const char *pTextureOwner, const NodeFilter &filter,
                      const MaterialSetup &materialSetup ) {
    const BankEntry *pScene = nullptr;
    const BankEntry *pGeometry = nullptr;
    const BankEntry *pMaterials = nullptr;
    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( pBaseName != nullptr && StringCmpI( entry.Name.c_str(), pBaseName ) != 0 ) {
            continue;
        }
        if ( entry.Extension == "3dd" && pScene == nullptr ) {
            pScene = &entry;
        } else if ( entry.Extension == "3dg" && pGeometry == nullptr ) {
            pGeometry = &entry;
        } else if ( entry.Extension == "2dm" && pMaterials == nullptr ) {
            pMaterials = &entry;
        }
    }
    if ( pScene == nullptr || pGeometry == nullptr ) {
        return false;
    }
    return load( renderer, *pScene, *pGeometry, pMaterials, pFallbackMaterials, pTextureOwner, filter, materialSetup );
}

bool EdenModel::load( EdenRenderer &renderer, const BankEntry &sceneEntry, const BankEntry &geometryEntry,
                      const BankEntry *pMaterialEntry, const Render2DM *pFallbackMaterials, const char *pTextureOwner,
                      const NodeFilter &filter, const MaterialSetup &materialSetup ) {
    destroy();
    Render3DG geometry;
    if ( !scene.parse( sceneEntry.pData, sceneEntry.Size, false ) ||
         !geometry.parse( geometryEntry.pData, geometryEntry.Size, false ) ) {
        OTDU_LOG_WARN( "EdenModel: failed to parse '%s'\n", sceneEntry.Filename.c_str() );
        return false;
    }
    if ( pMaterialEntry != nullptr ) {
        localMaterials.parse( pMaterialEntry->pData, pMaterialEntry->Size );
    }

    const std::vector<SceneNode> &nodes = scene.getNodes();
    nodeTransforms.clear();
    for ( const SceneNode &node : nodes ) {
        nodeTransforms.push_back( node.World );
    }
    nodeVisible.assign( nodes.size(), true );

    std::unordered_map<uint64_t, uint32_t> materialIndices;
    auto getMaterial = [&]( uint64_t hashcode ) -> int32_t {
        auto it = materialIndices.find( hashcode );
        if ( it != materialIndices.end() ) {
            return static_cast<int32_t>( it->second );
        }
        const MaterialDesc *pDesc = localMaterials.find( hashcode );
        if ( pDesc == nullptr && pFallbackMaterials != nullptr ) {
            pDesc = pFallbackMaterials->find( hashcode );
        }
        if ( pDesc == nullptr ) {
            materialIndices[hashcode] = ~0u;
            return -1;
        }
        EdenMaterial material;
        renderer.setupMaterial( *pDesc, material, pTextureOwner );
        if ( materialSetup ) {
            materialSetup( material );
        }
        materials.push_back( std::move( material ) );
        const uint32_t index = static_cast<uint32_t>( materials.size() - 1 );
        materialIndices[hashcode] = index;
        return static_cast<int32_t>( index );
    };

    std::vector<uint8_t> vertexData;
    std::vector<uint32_t> indexData;
    boundsMin = Vector3f( 1e9f, 1e9f, 1e9f );
    boundsMax = Vector3f( -1e9f, -1e9f, -1e9f );
    for ( int32_t nodeIndex = 0; nodeIndex < static_cast<int32_t>( nodes.size() ); nodeIndex++ ) {
        const SceneNode &node = nodes[nodeIndex];
        if ( node.ObjectHashcode == 0 || ( filter && !filter( scene, nodeIndex ) ) ) {
            continue;
        }
        const Geometry *pGeometry = geometry.find( node.ObjectHashcode );
        if ( pGeometry == nullptr ) {
            continue;
        }
        for ( const GeometryPrimitive &primitive : pGeometry->Primitives ) {
            if ( primitive.NumRawVertex == 0 || primitive.Indices.empty() ) {
                continue;
            }
            const uint64_t materialHash =
                primitive.MaterialIndex < node.Materials.size() ? node.Materials[primitive.MaterialIndex] : 0;
            const int32_t materialIndex = getMaterial( materialHash );
            if ( materialIndex < 0 || materials[materialIndex].pProgram == nullptr ) {
                continue;
            }

            Item item;
            item.NodeIndex = nodeIndex;
            item.BonePalette = primitive.BonePalette;
            item.MaterialIndex = static_cast<uint32_t>( materialIndex );
            EdenMesh &mesh = item.Mesh;
            mesh.NumStreams = 0;
            for ( uint32_t stream = 0; stream < 2; stream++ ) {
                if ( primitive.RawStrides[stream] == 0 ) {
                    continue;
                }
                const size_t offset = Align<size_t>( vertexData.size(), 16 );
                vertexData.resize( offset );
                vertexData.insert( vertexData.end(), primitive.RawStreams[stream].begin(),
                                   primitive.RawStreams[stream].end() );
                mesh.StreamOffsets[stream] = offset;
                mesh.Strides[stream] = primitive.RawStrides[stream];
                mesh.NumStreams = stream + 1;
            }
            mesh.Attributes = primitive.RawAttributes;
            mesh.FirstIndex = static_cast<uint32_t>( indexData.size() );
            mesh.IndexCount = static_cast<uint32_t>( primitive.Indices.size() );
            mesh.VertexOffset = 0;
            indexData.insert( indexData.end(), primitive.Indices.begin(), primitive.Indices.end() );
            mesh.computeLayoutKey();
            if ( bKeepCollision ) {
                const uint32_t base = static_cast<uint32_t>( collisionVertices.size() );
                for ( const MeshVertex &vertex : primitive.Vertices ) {
                    collisionVertices.push_back( node.World.transformPoint(
                        Vector3f( vertex.Position[0], vertex.Position[1], vertex.Position[2] ) ) );
                }
                for ( uint32_t index : primitive.Indices ) {
                    collisionIndices.push_back( base + index );
                }
            }

            item.BoundsMin = Vector3f( 1e9f, 1e9f, 1e9f );
            item.BoundsMax = Vector3f( -1e9f, -1e9f, -1e9f );
            for ( const MeshVertex &vertex : primitive.Vertices ) {
                const Vector3f p( vertex.Position[0], vertex.Position[1], vertex.Position[2] );
                item.BoundsMin = Vector3f( Min( item.BoundsMin.x, p.x ), Min( item.BoundsMin.y, p.y ),
                                           Min( item.BoundsMin.z, p.z ) );
                item.BoundsMax = Vector3f( Max( item.BoundsMax.x, p.x ), Max( item.BoundsMax.y, p.y ),
                                           Max( item.BoundsMax.z, p.z ) );
                const Vector3f w = node.World.transformPoint( p );
                boundsMin = Vector3f( Min( boundsMin.x, w.x ), Min( boundsMin.y, w.y ), Min( boundsMin.z, w.z ) );
                boundsMax = Vector3f( Max( boundsMax.x, w.x ), Max( boundsMax.y, w.y ), Max( boundsMax.z, w.z ) );
            }
            items.push_back( std::move( item ) );
        }
    }
    if ( items.empty() ) {
        OTDU_LOG_DEBUG( "EdenModel '%s': nothing to draw\n", sceneEntry.Filename.c_str() );
        return false;
    }

    nodeItems.assign( nodes.size(), { 0u, 0u } );
    for ( uint32_t i = 0; i < items.size(); i++ ) {
        auto &range = nodeItems[items[i].NodeIndex];
        if ( range.second == 0 ) {
            range.first = i;
        }
        range.second = i + 1 - range.first;
    }

    RenderDevice &device = gpRender->getDevice();
    pVertexBuffer = device.createBuffer( vertexData.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexData.data() );
    pIndexBuffer = device.createBuffer( indexData.size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        indexData.data() );
    for ( Item &item : items ) {
        for ( uint32_t stream = 0; stream < item.Mesh.NumStreams; stream++ ) {
            item.Mesh.pStreamBuffers[stream] = pVertexBuffer;
        }
        item.Mesh.pIndexBuffer = pIndexBuffer;
    }
    OTDU_LOG_DEBUG( "EdenModel '%s': %zu items, %zu materials, %zu KB vertices\n", sceneEntry.Filename.c_str(),
                    items.size(), materials.size(), vertexData.size() / 1024 );
    return pVertexBuffer != nullptr && pIndexBuffer != nullptr;
}

void EdenModel::draw( EdenRenderer &renderer, const Matrix4x4f &world, const EdenDrawParams *pParams ) const {
    for ( const Item &item : items ) {
        if ( !nodeVisible[item.NodeIndex] ) {
            continue;
        }
        renderer.draw( item.Mesh, materials[item.MaterialIndex], world * nodeTransforms[item.NodeIndex], pParams );
    }
}

bool EdenModel::getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const {
    bool bFound = false;
    for ( size_t i = 0; i + 2 < collisionIndices.size(); i += 3 ) {
        const Vector3f &a = collisionVertices[collisionIndices[i]];
        const Vector3f &b = collisionVertices[collisionIndices[i + 1]];
        const Vector3f &c = collisionVertices[collisionIndices[i + 2]];
        if ( x < Min( a.x, Min( b.x, c.x ) ) || x > Max( a.x, Max( b.x, c.x ) ) || z < Min( a.z, Min( b.z, c.z ) ) ||
             z > Max( a.z, Max( b.z, c.z ) ) ) {
            continue;
        }
        // Barycentric coordinates in the XZ plane.
        const float denominator = ( b.z - c.z ) * ( a.x - c.x ) + ( c.x - b.x ) * ( a.z - c.z );
        if ( std::abs( denominator ) < 1e-8f ) {
            continue;
        }
        const float u = ( ( b.z - c.z ) * ( x - c.x ) + ( c.x - b.x ) * ( z - c.z ) ) / denominator;
        const float v = ( ( c.z - a.z ) * ( x - c.x ) + ( a.x - c.x ) * ( z - c.z ) ) / denominator;
        const float w = 1.0f - u - v;
        if ( u < 0.0f || v < 0.0f || w < 0.0f ) {
            continue;
        }
        const float y = u * a.y + v * b.y + w * c.y;
        if ( y <= maxHeight && ( !bFound || y > outHeight ) ) {
            outHeight = y;
            bFound = true;
        }
    }
    return bFound;
}

void EdenModel::drawNode( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world ) const {
    for ( const Item &item : items ) {
        if ( item.NodeIndex == nodeIndex ) {
            renderer.draw( item.Mesh, materials[item.MaterialIndex], world * nodeTransforms[item.NodeIndex] );
        }
    }
}

void EdenModel::drawNodeItems( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world,
                               const EdenDrawParams *pParams ) const {
    if ( nodeIndex < 0 || nodeIndex >= static_cast<int32_t>( nodeItems.size() ) ) {
        return;
    }
    const auto &range = nodeItems[nodeIndex];
    for ( uint32_t i = range.first; i < range.first + range.second; i++ ) {
        renderer.draw( items[i].Mesh, materials[items[i].MaterialIndex], world, pParams );
    }
}

bool EdenModel::getNodeBounds( int32_t nodeIndex, Vector3f &outMin, Vector3f &outMax ) const {
    if ( nodeIndex < 0 || nodeIndex >= static_cast<int32_t>( nodeItems.size() ) || nodeItems[nodeIndex].second == 0 ) {
        return false;
    }
    const auto &range = nodeItems[nodeIndex];
    outMin = Vector3f( 1e9f, 1e9f, 1e9f );
    outMax = Vector3f( -1e9f, -1e9f, -1e9f );
    for ( uint32_t i = range.first; i < range.first + range.second; i++ ) {
        const Item &item = items[i];
        outMin = Vector3f( Min( outMin.x, item.BoundsMin.x ), Min( outMin.y, item.BoundsMin.y ),
                           Min( outMin.z, item.BoundsMin.z ) );
        outMax = Vector3f( Max( outMax.x, item.BoundsMax.x ), Max( outMax.y, item.BoundsMax.y ),
                           Max( outMax.z, item.BoundsMax.z ) );
    }
    return true;
}

void EdenModel::drawSkinned( EdenRenderer &renderer, const Matrix4x4f &world,
                             const std::vector<Matrix4x4f> &skinMatrices, const EdenAvatarLighting *pLighting ) const {
    EdenDrawParams params;
    params.bDeferBlended = true;
    params.pAvatarLighting = pLighting;
    EdenDrawParams rigidParams;
    rigidParams.bDeferBlended = true;
    rigidParams.pAvatarLighting = pLighting;
    Matrix4x4f palette[32];
    for ( const Item &item : items ) {
        if ( !nodeVisible[item.NodeIndex] ) {
            continue;
        }
        if ( item.BonePalette.empty() ) {
            renderer.draw( item.Mesh, materials[item.MaterialIndex], world * nodeTransforms[item.NodeIndex],
                           pLighting != nullptr ? &rigidParams : nullptr );
            continue;
        }
        const uint32_t count = Min<uint32_t>( static_cast<uint32_t>( item.BonePalette.size() ), 32u );
        for ( uint32_t i = 0; i < count; i++ ) {
            const uint32_t bone = item.BonePalette[i];
            palette[i] = bone < skinMatrices.size() ? skinMatrices[bone] : Matrix4x4f::Identity();
        }
        params.pSkinMatrices = palette;
        params.NumSkinMatrices = count;
        renderer.draw( item.Mesh, materials[item.MaterialIndex], world, &params );
    }
}

uint32_t EdenModel::bindUVAnimations( const Bank &bank ) {
    const std::vector<uint64_t> &ids = localMaterials.getUVAnimationIds();
    if ( ids.empty() ) {
        return 0;
    }
    uint32_t numBound = 0;
    for ( const BankEntry *pEntry : bank.findEntriesByExtension( "uva" ) ) {
        auto pAnimation = std::make_shared<EdenUVAnimation>();
        if ( !pAnimation->parse( pEntry->pData, pEntry->Size ) ||
             std::find( ids.begin(), ids.end(), pAnimation->Id ) == ids.end() ) {
            continue;
        }
        uint32_t slot = 0;
        std::vector<std::string> candidates;
        if ( !EdenUVAnimation::ParseName( pEntry->Name, slot, candidates ) || slot >= 4 ) {
            continue;
        }
        bool bFound = false;
        for ( const std::string &candidate : candidates ) {
            const uint64_t hashcode = GetIdentifier64bit( candidate.c_str() );
            for ( EdenMaterial &material : materials ) {
                if ( material.Hashcode == hashcode ) {
                    material.UVAnimations[slot] = pAnimation;
                    bFound = true;
                }
            }
            if ( bFound ) {
                break;
            }
        }
        if ( bFound ) {
            numBound++;
            OTDU_LOG_DEBUG( "EdenModel: UV animation '%s' (%.1f s) -> __UVA_%u\n", pEntry->Name.c_str(),
                            pAnimation->Duration, slot );
        }
    }
    return numBound;
}
