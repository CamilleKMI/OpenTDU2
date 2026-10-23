#include "shared.h"
#include "eden_world.h"

#include "config/gs_config.h"

#include <cfloat>

#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"
#include "core/xmb.h"
#include "core/crypto/cpr.h"
#include "render/eden/eden_weather.h"

namespace {
constexpr uint32_t kGridQuads = EdenWorld::kHeightfieldSize - 1;
// TD_backwash (shore foam) shader set; EdenRenderer sampler heap index of
// linear + mirrored repeat (eSamplerHeapIndex SH_LinearMirror).
constexpr uint64_t kBackwashShaderSet = 0x73078865ca31234dull;
constexpr uint16_t kSamplerLinearMirror = 4;

std::string SectorName( const EdenWorld::SectorId &id ) {
    char name[64];
    snprintf( name, sizeof( name ), "sector-%d-%d-%d-%d", id.AreaX, id.AreaY, id.X, id.Y );
    return name;
}
} // namespace

EdenWorld::EdenWorld()
    : pRenderer( nullptr ),
      areaColumns( 7 ),
      areaRows( 6 ),
      pGridBuffer( nullptr ),
      pGridIndices( nullptr ),
      numGridIndices( 0 ) {}

EdenWorld::~EdenWorld() { destroy(); }

bool EdenWorld::initialize( EdenRenderer &renderer, const char *pIsland ) {
    destroy();
    pRenderer = &renderer;
    islandFolder = "Euro/Bnk/islands/" + std::string( pIsland ) + "/level/";
    if ( !loadWorldConfig( pIsland ) ) {
        // Ibiza values (its worldconfig.cpr) [DATA].
        areaColumns = 7;
        areaRows = 6;
        OTDU_LOG_WARN( "EdenWorld: %sworldconfig.cpr not readable, %d x %d areas\n", islandFolder.c_str(), areaColumns,
                       areaRows );
    }

    // Shared heightfield grid: TEXCOORD0 = (x, z) grid coordinates (the
    // TD_HeightMap vertex shader scales them by the 3 m spacing).
    std::vector<float> grid;
    grid.reserve( kHeightfieldSize * kHeightfieldSize * 2 );
    for ( uint32_t z = 0; z < kHeightfieldSize; z++ ) {
        for ( uint32_t x = 0; x < kHeightfieldSize; x++ ) {
            grid.push_back( static_cast<float>( x ) );
            grid.push_back( static_cast<float>( z ) );
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve( kGridQuads * kGridQuads * 6 );
    for ( uint32_t z = 0; z < kGridQuads; z++ ) {
        for ( uint32_t x = 0; x < kGridQuads; x++ ) {
            const uint32_t i0 = z * kHeightfieldSize + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kHeightfieldSize;
            const uint32_t i3 = i2 + 1;
            // Winding: the heightfield materials cull D3DCULL_CW.
            indices.insert( indices.end(), { i0, i2, i1, i1, i2, i3 } );
        }
    }
    RenderDevice &device = gpRender->getDevice();
    pGridBuffer = device.createBuffer( grid.size() * sizeof( float ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, grid.data() );
    pGridIndices = device.createBuffer( indices.size() * sizeof( uint32_t ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        indices.data() );
    numGridIndices = static_cast<uint32_t>( indices.size() );

    // Shared library (models instanced by the .pmi lists, detail textures).
    const std::string commonPath = islandFolder + "commonworld.bnk";
    if ( commonBank.load( commonPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( commonBank, commonPath.c_str() );
        const BankEntry *pLibrary = commonBank.findEntry( "library", "2dm" );
        if ( pLibrary != nullptr ) {
            libraryMaterials.parse( pLibrary->pData, pLibrary->Size );
        }
        // Library models (instanced by the V / O .pmi lists): a gallery
        // scene whose node hashcodes are the MOB. hashcodes.
        if ( libraryModel.load( renderer, commonBank, "library", &libraryMaterials, commonPath.c_str() ) ) {
            const std::vector<SceneNode> &nodes = libraryModel.getScene().getNodes();
            for ( int32_t i = 0; i < static_cast<int32_t>( nodes.size() ); i++ ) {
                if ( nodes[i].ObjectHashcode != 0 ) {
                    libraryNodes.emplace( nodes[i].Hashcode, i );
                }
            }
        }
    } else {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found\n", commonPath.c_str() );
    }

    // Island ocean (far sea, ocean_spec 5).
    const std::string islandPath = islandFolder + pIsland + ".bnk";
    if ( islandBank.load( islandPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( islandBank, islandPath.c_str() );
        islandOcean.load( renderer, islandBank, ( std::string( pIsland ) + "-o" ).c_str(), &libraryMaterials,
                          islandPath.c_str() );
        pIslandColorMap = gpRender->getTextureCache().find( islandPath.c_str(), ( std::string( pIsland ) + "-c" ).c_str() );
        // Island level heightfield (<island>.3dd HMAP: 16 x 16 patches, 384 m
        // spacing [DATA]); the TD_HeightMap island permutation (S_USERTYPE4
        // = 2) scales the grid by 384 m [SHADER].
        if ( loadHeightfield( islandBank, pIsland, islandPath.c_str(), islandTerrain ) ) {
            islandTerrain.Spacing = 384.0f;
            islandTerrain.Materials[0].HeightfieldGrid = 2;
            renderer.updateProgram( islandTerrain.Materials[0] );
            if ( islandTerrain.Materials[0].pProgram == nullptr ) {
                OTDU_LOG_WARN( "EdenWorld: no island permutation of TD_HeightMap for '%s'\n", pIsland );
                islandTerrain.Materials.clear();
            }
        }
        bIslandIndicesDirty = true;
    }

    // Wet ground textures.
    const std::string weatherFxPath = "Euro/Bnk/fx/weatherfx.bnk";
    if ( weatherFxBank.load( weatherFxPath.c_str() ) ) {
        TextureCache &textures = gpRender->getTextureCache();
        textures.loadBankTextures( weatherFxBank, weatherFxPath.c_str() );
        for ( uint32_t i = 0; i < 16; i++ ) {
            char name[32];
            snprintf( name, sizeof( name ), "ripple_%02u", i + 1 );
            pRipples[i] = textures.find( weatherFxPath.c_str(), name );
        }
        pPuddles = textures.find( weatherFxPath.c_str(), "puddles" );
    }

    // Actors of the A .pmi lists (road signs...): Entity__Actor (__Hc,
    // __Name) of entities.xmb, the root node of the actor .3dd is the
    // identifier of __Name.
    const std::string actorPath = "Euro/Bnk/islands/" + std::string( pIsland ) + "/scenarics/packs/worldactors.bnk";
    if ( actorBank.load( actorPath.c_str() ) ) {
        gpRender->getTextureCache().loadBankTextures( actorBank, actorPath.c_str() );
        const BankEntry *pEntities = actorBank.findEntry( "entities.xmb" );
        XmbDocument entities;
        if ( pEntities != nullptr && entities.load( pEntities->pData, pEntities->Size ) ) {
            XmbValue root = entities.getRoot()["ExportType_Entity"];
            for ( uint32_t i = 0; i < root.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
                XmbValue actor = root.getArrayElement( "ArrayOf_Entity__Actor", i );
                actorNames[actor["__Hc"].asUInt64()] = actor["__Name"].asString();
            }
        }
        for ( const BankEntry *pEntry : actorBank.findEntriesByExtension( "3dd" ) ) {
            Render3DD scene;
            if ( scene.parse( pEntry->pData, pEntry->Size, false ) && !scene.getNodes().empty() ) {
                actorScenes.emplace( scene.getNodes()[0].Hashcode, pEntry );
            }
        }
        OTDU_LOG_INFO( "EdenWorld: %zu world actors, %zu actor scenes\n", actorNames.size(), actorScenes.size() );
    } else {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found (no road signs)\n", actorPath.c_str() );
    }
    return pGridBuffer != nullptr && pGridIndices != nullptr;
}

bool EdenWorld::loadWorldConfig( const char *pIsland ) {
    // <island>/level/worldconfig.cpr: encrypted XML, <Area NbColumns="7"
    // NbRows= "6"/> (Ibiza 7 x 6, Hawai and Casino 11 x 9) [DATA].
    std::string text;
    if ( !LoadCPRText( ( islandFolder + "worldconfig.cpr" ).c_str(), text ) ) {
        return false;
    }
    auto readAttribute = [&text]( const char *pName, int32_t &outValue ) {
        size_t position = text.find( pName );
        if ( position == std::string::npos ) {
            return false;
        }
        position = text.find( '"', position );
        if ( position == std::string::npos ) {
            return false;
        }
        outValue = atoi( text.c_str() + position + 1 );
        return outValue > 0;
    };
    int32_t columns = 0, rows = 0;
    if ( !readAttribute( "NbColumns", columns ) || !readAttribute( "NbRows", rows ) ) {
        return false;
    }
    areaColumns = columns;
    areaRows = rows;
    OTDU_LOG_INFO( "EdenWorld: %s: %d x %d areas (worldconfig.cpr)\n", pIsland, areaColumns, areaRows );
    return true;
}

std::string EdenWorld::getSectorBankPath( const SectorId &id ) const {
    char path[256];
    snprintf( path, sizeof( path ), "%sarea-%d/area-%d-%d/%s.bnk", islandFolder.c_str(), id.AreaX, id.AreaX, id.AreaY,
              SectorName( id ).c_str() );
    return path;
}

std::string EdenWorld::GetSectorBankPath( const char *pIsland, const SectorId &id ) {
    char path[256];
    snprintf( path, sizeof( path ), "Euro/Bnk/islands/%s/level/area-%d/area-%d-%d/%s.bnk", pIsland, id.AreaX, id.AreaX,
              id.AreaY, SectorName( id ).c_str() );
    return path;
}

int32_t EdenWorld::getActorModel( uint64_t hashcode ) {
    auto it = actorModelIndices.find( hashcode );
    if ( it != actorModelIndices.end() ) {
        return it->second;
    }
    int32_t index = -1;
    auto nameIt = actorNames.find( hashcode );
    if ( nameIt != actorNames.end() ) {
        auto sceneIt = actorScenes.find( GetIdentifier64bit( nameIt->second.c_str() ) );
        if ( sceneIt != actorScenes.end() ) {
            const BankEntry *pScene = sceneIt->second;
            const BankEntry *pGeometry = actorBank.findEntry( pScene->Name.c_str(), "3dg" );
            const BankEntry *pMaterials = actorBank.findEntry( pScene->Name.c_str(), "2dm" );
            auto pModel = std::make_unique<EdenModel>();
            if ( pGeometry != nullptr && pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, &libraryMaterials,
                                                       actorBank.getPath().c_str() ) ) {
                actorModels.push_back( std::move( pModel ) );
                index = static_cast<int32_t>( actorModels.size() - 1 );
            }
        }
        if ( index < 0 ) {
            OTDU_LOG_WARN( "EdenWorld: actor '%s' has no model\n", nameIt->second.c_str() );
        }
    }
    actorModelIndices[hashcode] = index;
    return index;
}

void EdenWorld::loadInstances( Sector &sector, const std::string &name ) {
    // V (vegetation) and O (objects): static library models; A: actors.
    // S (guard rails, skinned with a per instance bone palette) and F
    // (empty) are not drawn yet.
    for ( const char *pSuffix : { "-v", "-o", "-a" } ) {
        const BankEntry *pEntry = sector.SectorBank.findEntry( ( name + pSuffix ).c_str(), "pmi" );
        if ( pEntry == nullptr ) {
            continue;
        }
        std::vector<PmiInstance> instances;
        if ( !ParsePmi( pEntry->pData, pEntry->Size, sector.Origin, instances ) ) {
            OTDU_LOG_WARN( "EdenWorld: failed to parse '%s'\n", pEntry->Filename.c_str() );
            continue;
        }
        for ( PmiInstance &instance : instances ) {
            int32_t model = -1;
            float distance = 2000.0f;
            if ( instance.Type == 1 ) {
                auto it = libraryNodes.find( instance.Hashcode );
                if ( it == libraryNodes.end() ) {
                    continue;
                }
                model = it->second;
                // LOD switch values (squared distances, x (16/9)^2: FUN_00da0440);
                // the instance is not drawn beyond the last one. The low LODs
                // (TD_HardwareInstance / TD_Impostor) are not loaded: LOD 0 is
                // drawn up to there.
                // FUN_00989d10 / FUN_00df6e60: d^2 < lod * 3.160494 * F, F =
                // max(SET_GLOBAL_LOD_FACTOR, 1) for the last LOD; a model
                // without LOD switching is never distance culled.
                const std::vector<float> &lods = libraryModel.getScene().getNodes()[model].LodDistances;
                const float lastFactor = Max( gpConfig != nullptr ? gpConfig->getVideoSettings().GlobalLodFactor : 1.0f, 1.0f );
                if ( !lods.empty() && lods.back() > 0.0f ) {
                    distance = std::sqrt( lods.back() * 3.160494f * lastFactor );
                } else if ( lods.size() <= 1 ) {
                    distance = FLT_MAX;
                }
                Vector3f boundsMin, boundsMax;
                if ( !libraryModel.getNodeBounds( model, boundsMin, boundsMax ) ) {
                    continue;
                }
            } else if ( instance.Type == 2 || instance.Type == 4 ) {
                model = getActorModel( instance.Hashcode );
                if ( model < 0 ) {
                    continue;
                }
                const Vector3f &p = instance.Position;
                instance.BoxMin = p - Vector3f( 3.0f, 3.0f, 3.0f );
                instance.BoxMax = p + Vector3f( 3.0f, 6.0f, 3.0f );
                distance = 300.0f;
            } else {
                continue;
            }
            sector.Instances.push_back( instance );
            sector.InstanceModels.push_back( model );
            sector.InstanceDistances.push_back( distance );
        }
    }
    OTDU_LOG_DEBUG( "EdenWorld: %s: %zu instances\n", name.c_str(), sector.Instances.size() );
}

void EdenWorld::drawInstances( EdenRenderer &renderer, const Sector &sector, const Vector3f &cameraPosition ) {
    // Frustum planes (Vulkan clip space: -w <= x, y <= w, 0 <= z <= w).
    const Matrix4x4f &vp = renderer.getViewProjection();
    Vector4f planes[6];
    for ( int i = 0; i < 6; i++ ) {
        const int axis = i / 2;
        const float sign = ( i & 1 ) ? -1.0f : 1.0f;
        float p[4];
        for ( int c = 0; c < 4; c++ ) {
            const float row = vp.m[c][axis];
            p[c] = ( axis == 2 && sign > 0.0f ) ? row : vp.m[c][3] + sign * row;
        }
        planes[i] = Vector4f( p[0], p[1], p[2], p[3] );
    }
    for ( size_t i = 0; i < sector.Instances.size(); i++ ) {
        const PmiInstance &instance = sector.Instances[i];
        const Vector3f center = ( instance.BoxMin + instance.BoxMax ) * 0.5f;
        const Vector3f extent = ( instance.BoxMax - instance.BoxMin ) * 0.5f;
        const Vector3f delta = instance.Position - cameraPosition;
        const float maxDistance = sector.InstanceDistances[i];
        const float distanceSquared = delta.dot( delta );
        if ( distanceSquared > maxDistance * maxDistance ) {
            continue;
        }
        if ( minInstanceRatio > 0.0f &&
             extent.dot( extent ) < minInstanceRatio * minInstanceRatio * distanceSquared ) {
            continue;
        }
        bool bVisible = true;
        for ( const Vector4f &plane : planes ) {
            const float d = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
            const float r = std::abs( plane.x ) * extent.x + std::abs( plane.y ) * extent.y +
                            std::abs( plane.z ) * extent.z;
            if ( d + r < 0.0f ) {
                bVisible = false;
                break;
            }
        }
        if ( !bVisible ) {
            continue;
        }
        if ( instance.Type == 1 ) {
            libraryModel.drawNodeItems( renderer, sector.InstanceModels[i], instance.World );
        } else {
            actorModels[sector.InstanceModels[i]]->draw( renderer, instance.World );
        }
    }
}

void EdenWorld::destroy() {
    if ( gpRender == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    for ( auto &pSector : sectors ) {
        device.destroyBuffer( pSector->Terrain.pBuffer );
        gpRender->getTextureCache().releaseOwner( pSector->BankPath.c_str() );
    }
    for ( auto &pArea : areas ) {
        device.destroyBuffer( pArea->Terrain.pBuffer );
        device.destroyBuffer( pArea->pIndexBuffer );
        gpRender->getTextureCache().releaseOwner( pArea->BankPath.c_str() );
    }
    sectors.clear();
    areas.clear();
    failedSectors.clear();
    libraryModel.destroy();
    libraryNodes.clear();
    actorModels.clear();
    actorModelIndices.clear();
    actorScenes.clear();
    actorNames.clear();
    if ( actorBank.isLoaded() ) {
        gpRender->getTextureCache().releaseOwner( actorBank.getPath().c_str() );
    }
    actorBank.unload();
    islandOcean.destroy();
    device.destroyBuffer( islandTerrain.pBuffer );
    device.destroyBuffer( pIslandIndexBuffer );
    islandTerrain = Heightfield();
    pIslandIndexBuffer = nullptr;
    numIslandIndices = 0;
    bIslandIndicesDirty = true;
    if ( islandBank.isLoaded() ) {
        gpRender->getTextureCache().releaseOwner( islandBank.getPath().c_str() );
    }
    islandBank.unload();
    pIslandColorMap = nullptr;
    bOceanVisibilityDirty = true;
    if ( weatherFxBank.isLoaded() ) {
        gpRender->getTextureCache().releaseOwner( weatherFxBank.getPath().c_str() );
    }
    weatherFxBank.unload();
    for ( GPUTexture *&pRipple : pRipples ) {
        pRipple = nullptr;
    }
    pPuddles = nullptr;
    device.destroyBuffer( pGridBuffer );
    device.destroyBuffer( pGridIndices );
    pGridBuffer = pGridIndices = nullptr;
    commonBank.unload();
}

EdenWorld::SectorId EdenWorld::getSectorAt( const Vector3f &position ) {
    SectorId id;
    const int32_t row = static_cast<int32_t>( std::floor( position.z / kSectorSize ) );
    const float offset = ( row & 1 ) ? kSectorSize * 0.5f : 0.0f;
    const int32_t column = static_cast<int32_t>( std::floor( ( position.x - offset ) / kSectorSize ) );
    id.AreaX = column >= 0 ? column / static_cast<int32_t>( kSectorsPerArea ) : -1;
    id.AreaY = row >= 0 ? row / static_cast<int32_t>( kSectorsPerArea ) : -1;
    id.X = column - id.AreaX * static_cast<int32_t>( kSectorsPerArea );
    id.Y = row - id.AreaY * static_cast<int32_t>( kSectorsPerArea );
    return id;
}

Vector3f EdenWorld::getSectorOrigin( const SectorId &id ) {
    const float x = ( id.AreaX * static_cast<float>( kSectorsPerArea ) + id.X ) * kSectorSize +
                    ( ( id.Y & 1 ) ? kSectorSize * 0.5f : 0.0f );
    const float z = ( id.AreaY * static_cast<float>( kSectorsPerArea ) + id.Y ) * kSectorSize;
    return Vector3f( x, 0.0f, z );
}

bool EdenWorld::isSectorLoaded( const SectorId &id ) const {
    for ( const auto &pSector : sectors ) {
        if ( pSector->Id == id ) {
            return true;
        }
    }
    return false;
}

bool EdenWorld::loadHeightfield( const Bank &bank, const char *pBaseName, const char *pTextureOwner,
                                 Heightfield &out ) {
    const BankEntry *pScene = bank.findEntry( pBaseName, "3dd" );
    const BankEntry *pGeometry = bank.findEntry( pBaseName, "3dg" );
    const BankEntry *pMaterials = bank.findEntry( pBaseName, "2dm" );
    if ( pScene == nullptr || pGeometry == nullptr || pMaterials == nullptr ) {
        return false;
    }
    Render3DD scene;
    Render3DG geometry;
    Render2DM materials;
    if ( !scene.parse( pScene->pData, pScene->Size, false ) || !geometry.parse( pGeometry->pData, pGeometry->Size, false ) ||
         !materials.parse( pMaterials->pData, pMaterials->Size ) ) {
        return false;
    }
    const GeometryPrimitive *pPrimitive = nullptr;
    uint64_t materialHash = 0;
    for ( const SceneNode &node : scene.getNodes() ) {
        const Geometry *pGeometryData = geometry.find( node.ObjectHashcode );
        if ( pGeometryData == nullptr ) {
            continue;
        }
        for ( const GeometryPrimitive &primitive : pGeometryData->Primitives ) {
            if ( primitive.NumRawVertex == kHeightfieldSize * kHeightfieldSize ) {
                pPrimitive = &primitive;
                materialHash = primitive.MaterialIndex < node.Materials.size() ? node.Materials[primitive.MaterialIndex]
                                                                               : 0;
                break;
            }
        }
        if ( pPrimitive != nullptr ) {
            break;
        }
    }
    const MaterialDesc *pDesc = materials.find( materialHash );
    if ( pDesc == nullptr && !materials.getMaterials().empty() ) {
        pDesc = &materials.getMaterials().begin()->second;
    }
    if ( pPrimitive == nullptr || pDesc == nullptr ) {
        OTDU_LOG_WARN( "EdenWorld: no heightfield in '%s'\n", pBaseName );
        return false;
    }

    // Heights (stream 0, R32)
    out.Heights.resize( kHeightfieldSize * kHeightfieldSize );
    if ( pPrimitive->RawStrides[0] == 4 ) {
        memcpy( out.Heights.data(), pPrimitive->RawStreams[0].data(), out.Heights.size() * sizeof( float ) );
    }

    std::vector<uint8_t> data;
    data.insert( data.end(), pPrimitive->RawStreams[0].begin(), pPrimitive->RawStreams[0].end() );
    const size_t stream1Offset = Align<size_t>( data.size(), 16 );
    data.resize( stream1Offset );
    data.insert( data.end(), pPrimitive->RawStreams[1].begin(), pPrimitive->RawStreams[1].end() );
    out.pBuffer = gpRender->getDevice().createBuffer( data.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, data.data() );

    EdenMesh &mesh = out.Mesh;
    mesh.NumStreams = 3;
    mesh.pStreamBuffers[0] = out.pBuffer;
    mesh.pStreamBuffers[1] = out.pBuffer;
    mesh.pStreamBuffers[2] = pGridBuffer;
    mesh.StreamOffsets[0] = 0;
    mesh.StreamOffsets[1] = stream1Offset;
    mesh.StreamOffsets[2] = 0;
    mesh.Strides[0] = pPrimitive->RawStrides[0];
    mesh.Strides[1] = pPrimitive->RawStrides[1];
    mesh.Strides[2] = 8;
    mesh.Attributes = pPrimitive->RawAttributes;
    RawVertexAttribute gridAttribute;
    gridAttribute.Usage = 5; // TEXCOORD0
    gridAttribute.UsageIndex = 0;
    gridAttribute.Stream = 2;
    gridAttribute.Format = VAF_TEXCOORD_R32G32_SFLOAT;
    gridAttribute.Offset = 0;
    mesh.Attributes.push_back( gridAttribute );
    mesh.pIndexBuffer = pGridIndices;
    mesh.FirstIndex = 0;
    mesh.IndexCount = numGridIndices;
    mesh.computeLayoutKey();

    out.Materials.resize( 1 );
    pRenderer->setupMaterial( *pDesc, out.Materials[0], pTextureOwner );
    static const bool bDebugGrid = CmdLine::HasArg( "-hmdebuggrid" );
    if ( CmdLine::HasArg( "-hmdebugps" ) ) {
        out.Materials[0].pProgram = pRenderer->getShaderSystem().getProgram(
            "TD_HeightMap", CmdLine::GetArgInt( "-hmvs", 0 ), CmdLine::GetArgInt( "-hmdebugps", 56 ) );
        return true;
    }
    if ( StringNCmpI( pBaseName, "area", 4 ) == 0 || bDebugGrid ) {
        out.Materials[0].HeightfieldGrid = 1;
        pRenderer->updateProgram( out.Materials[0] );
    }
    out.MaterialIndex = 0;
    return out.Materials[0].pProgram != nullptr;
}

bool EdenWorld::loadSector( const SectorId &id ) {
    if ( id.AreaX < 0 || id.AreaY < 0 || id.AreaX >= areaColumns || id.AreaY >= areaRows || id.X < 0 || id.Y < 0 ||
         id.X >= static_cast<int32_t>( kSectorsPerArea ) || id.Y >= static_cast<int32_t>( kSectorsPerArea ) ) {
        return false;
    }
    auto pSector = std::make_unique<Sector>();
    pSector->Id = id;
    pSector->Origin = getSectorOrigin( id );
    const std::string bankPath = getSectorBankPath( id );
    const char *path = bankPath.c_str();
    pSector->BankPath = bankPath;
    if ( !gpFile->fileExists( path ) ) {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found, sector skipped\n", path );
        return false;
    }
    if ( !pSector->SectorBank.load( path ) ) {
        OTDU_LOG_WARN( "EdenWorld: '%s' could not be loaded, skipped\n", path );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( pSector->SectorBank, path );
    const std::string name = SectorName( id );
    loadHeightfield( pSector->SectorBank, name.c_str(), path, pSector->Terrain );
    // Sector shadow map (-s.2db) on the SHADOW slot of the objects.
    GPUTexture *pShadowMap = gpRender->getTextureCache().find( path, ( name + "-s" ).c_str() );
    EdenRenderer *pEdenRenderer = pRenderer;
    auto materialSetup = [pShadowMap, pEdenRenderer]( EdenMaterial &material ) {
        if ( pShadowMap != nullptr && pEdenRenderer->assignTexture( material, "SHADOW", pShadowMap ) ) {
            pEdenRenderer->updateProgram( material );
        }
    };
    pSector->Objects.setKeepCollision( true );
    pSector->Objects.load( *pRenderer, pSector->SectorBank, ( name + "-o" ).c_str(), &libraryMaterials, path, nullptr,
                           materialSetup );
    // Shore foam (TD_backwash, material "Ecume", sector-5-0-6-7 / 5-1-6-1
    // only on Ibiza [DATA]):
    // - its texture coordinate animation: ecume_0.uva in the same bank, id
    //   0x049c2e1c84816bae = the UVA. entry of the -o.2dm UVAA section
    //   [DATA]; 8 s loop, V offset -2 -> -0.25 -> -2, V scale 3 -> 1.2 -> 3
    //   (the surf runs up the strip and back). TD_backwash vs: TC0.xy =
    //   (u, v, 1) . __UVA_0 rows 0 / 1 (dp3 with c71.xyw / c72.xyw) [SHADER].
    //   Without the binding __UVA_0 is the identity: static foam.
    // - its COLOR slot (plage.2db) addressing: 2DM slot bytes +0x18/+0x19 =
    //   1 / 2 (U / V) where every other slot has equal values [DATA]; with
    //   the D3DTADDRESS numbering (1 WRAP, 2 MIRROR, 3 CLAMP: the ocean code
    //   writes 1 for the world-tiled NoiseNMap and 3 for the projected
    //   reflection [CODE FUN_008be1e0]) V is MIRROR [INF]. The strip has v in
    //   {0, 1} and u*6 tiles along the shore (u restarts at 0 on duplicated
    //   seam vertices) [DATA sector-5-0-6-7-o.3dg], so a mirror sampler on
    //   both axes is seamless along u and gives the mirrored V of the
    //   original (wrap leaves a hard opaque / transparent seam at v' = 0, -1:
    //   plage.2db R (= output alpha) is 255 at v = 0 and 0 at v = 1 [DATA]).
    const uint32_t numUVAnimations = pSector->Objects.bindUVAnimations( pSector->SectorBank );
    for ( EdenMaterial &material : pSector->Objects.getMaterials() ) {
        if ( material.ShaderSetGUID == kBackwashShaderSet ) {
            material.SamplerIndex[0] = kSamplerLinearMirror;
        }
    }
    if ( numUVAnimations != 0 && getenv( "OTDU_OCEAN_LOG" ) != nullptr ) {
        OTDU_LOG_INFO( "EdenWorld: %s: %u UV animations\n", name.c_str(), numUVAnimations );
    }
    loadInstances( *pSector, name );
    OTDU_LOG_DEBUG( "EdenWorld: %s at (%.0f, %.0f) objects (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n", name.c_str(),
                    pSector->Origin.x, pSector->Origin.z, pSector->Objects.getBoundsMin().x,
                    pSector->Objects.getBoundsMin().y, pSector->Objects.getBoundsMin().z,
                    pSector->Objects.getBoundsMax().x, pSector->Objects.getBoundsMax().y,
                    pSector->Objects.getBoundsMax().z );
    sectors.push_back( std::move( pSector ) );
    markAreasDirty( id );
    bOceanVisibilityDirty = true;
    return true;
}

bool EdenWorld::loadArea( int32_t x, int32_t y ) {
    auto pArea = std::make_unique<Area>();
    pArea->X = x;
    pArea->Y = y;
    pArea->Origin = Vector3f( x * kSectorsPerArea * kSectorSize, 0.0f, y * kSectorsPerArea * kSectorSize );
    char path[256];
    snprintf( path, sizeof( path ), "%sarea-%d/area-%d-%d/area-%d-%d.bnk", islandFolder.c_str(), x, x, y, x, y );
    pArea->BankPath = path;
    if ( !gpFile->fileExists( path ) ) {
        return false; // open sea or data not installed (counted by loadAround)
    }
    if ( !pArea->AreaBank.load( path ) ) {
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( pArea->AreaBank, path );
    char name[64];
    snprintf( name, sizeof( name ), "area-%d-%d", x, y );
    loadHeightfield( pArea->AreaBank, name, path, pArea->Terrain );
    pArea->Terrain.Spacing = 24.0f;
    pArea->Objects.load( *pRenderer, pArea->AreaBank, ( std::string( name ) + "-o" ).c_str(), &libraryMaterials, path );
    pArea->Objects.bindUVAnimations( pArea->AreaBank );
    areas.push_back( std::move( pArea ) );
    bIslandIndicesDirty = true;
    return true;
}

void EdenWorld::markAreasDirty( const SectorId &id ) {
    // A sector of an odd row overlaps the next area by half a sector.
    for ( auto &pArea : areas ) {
        if ( pArea->Y == id.AreaY && ( pArea->X == id.AreaX || pArea->X == id.AreaX + 1 ) ) {
            pArea->bIndicesDirty = true;
        }
    }
}

uint32_t EdenWorld::unloadFar( const Vector3f &position, int32_t sectorRadius ) {
    const SectorId center = getSectorAt( position );
    const int32_t centerColumn = center.AreaX * kSectorsPerArea + center.X;
    const int32_t centerRow = center.AreaY * kSectorsPerArea + center.Y;
    uint32_t numUnloaded = 0;
    RenderDevice &device = gpRender->getDevice();
    for ( auto it = sectors.begin(); it != sectors.end(); ) {
        const SectorId &id = ( *it )->Id;
        const int32_t column = id.AreaX * kSectorsPerArea + id.X;
        const int32_t row = id.AreaY * kSectorsPerArea + id.Y;
        if ( std::abs( column - centerColumn ) <= sectorRadius && std::abs( row - centerRow ) <= sectorRadius ) {
            ++it;
            continue;
        }
        const SectorId unloadedId = id;
        device.destroyBuffer( ( *it )->Terrain.pBuffer );
        gpRender->getTextureCache().releaseOwner( ( *it )->BankPath.c_str() );
        it = sectors.erase( it );
        markAreasDirty( unloadedId );
        numUnloaded++;
    }
    if ( numUnloaded != 0 ) {
        bOceanVisibilityDirty = true;
        OTDU_LOG_INFO( "EdenWorld: %u sectors unloaded, %zu loaded\n", numUnloaded, sectors.size() );
    }
    return numUnloaded;
}

void EdenWorld::updateIslandIndices() {
    // Island quads (384 m, 16 per area) not covered by a loaded area
    // heightfield [INF: the level LOD selection is encrypted].
    bIslandIndicesDirty = false;
    gpRender->getDevice().destroyBuffer( pIslandIndexBuffer );
    pIslandIndexBuffer = nullptr;
    numIslandIndices = 0;
    const uint32_t quadsPerArea = static_cast<uint32_t>( kSectorsPerArea * kSectorSize / 384.0f );
    std::vector<uint8_t> covered( kGridQuads * kGridQuads, 0 );
    for ( const auto &pArea : areas ) {
        if ( pArea->Terrain.Materials.empty() || pArea->X < 0 || pArea->Y < 0 ) {
            continue;
        }
        for ( uint32_t z = pArea->Y * quadsPerArea; z < ( pArea->Y + 1 ) * quadsPerArea && z < kGridQuads; z++ ) {
            for ( uint32_t x = pArea->X * quadsPerArea; x < ( pArea->X + 1 ) * quadsPerArea && x < kGridQuads; x++ ) {
                covered[z * kGridQuads + x] = 1;
            }
        }
    }
    std::vector<uint32_t> indices;
    for ( uint32_t z = 0; z < kGridQuads; z++ ) {
        for ( uint32_t x = 0; x < kGridQuads; x++ ) {
            if ( covered[z * kGridQuads + x] ) {
                continue;
            }
            const uint32_t i0 = z * kHeightfieldSize + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kHeightfieldSize;
            const uint32_t i3 = i2 + 1;
            indices.insert( indices.end(), { i0, i2, i1, i1, i2, i3 } );
        }
    }
    if ( indices.empty() ) {
        return;
    }
    pIslandIndexBuffer = gpRender->getDevice().createBuffer( indices.size() * sizeof( uint32_t ),
                                                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data() );
    numIslandIndices = static_cast<uint32_t>( indices.size() );
}

void EdenWorld::loadAround( const Vector3f &position, int32_t sectorRadius ) {
    if ( areas.empty() ) {
        for ( int32_t y = 0; y < areaRows; y++ ) {
            for ( int32_t x = 0; x < areaColumns; x++ ) {
                loadArea( x, y );
            }
        }
        OTDU_LOG_INFO( "EdenWorld: %zu areas loaded (%d x %d grid, the others have no bank)\n", areas.size(),
                       areaColumns, areaRows );
    }
    const SectorId center = getSectorAt( position );
    bool bLoadedAny = false;
    const int32_t centerColumn = center.AreaX * kSectorsPerArea + center.X;
    const int32_t centerRow = center.AreaY * kSectorsPerArea + center.Y;
    for ( int32_t row = centerRow - sectorRadius; row <= centerRow + sectorRadius; row++ ) {
        for ( int32_t column = centerColumn - sectorRadius; column <= centerColumn + sectorRadius; column++ ) {
            if ( row < 0 || column < 0 ) {
                continue;
            }
            SectorId id;
            id.AreaX = column / kSectorsPerArea;
            id.AreaY = row / kSectorsPerArea;
            id.X = column % kSectorsPerArea;
            id.Y = row % kSectorsPerArea;
            const uint32_t key = static_cast<uint32_t>( row * 1024 + column );
            if ( !isSectorLoaded( id ) && failedSectors.count( key ) == 0 ) {
                if ( loadSector( id ) ) {
                    bLoadedAny = true;
                } else {
                    failedSectors.insert( key );
                }
            }
        }
    }
    if ( bLoadedAny ) {
        OTDU_LOG_INFO( "EdenWorld: %zu sectors loaded around (%.0f, %.0f)\n", sectors.size(), position.x,
                       position.z );
    }
}

void EdenWorld::updateAreaIndices( Area &area ) {
    area.bIndicesDirty = false;
    gpRender->getDevice().destroyBuffer( area.pIndexBuffer );
    area.pIndexBuffer = nullptr;
    area.NumIndices = 0;

    const float spacing = area.Terrain.Spacing;
    std::vector<uint32_t> indices;
    indices.reserve( kGridQuads * kGridQuads * 6 );
    for ( uint32_t z = 0; z < kGridQuads; z++ ) {
        for ( uint32_t x = 0; x < kGridQuads; x++ ) {
            const Vector3f center( area.Origin.x + ( x + 0.5f ) * spacing, 0.0f, area.Origin.z + ( z + 0.5f ) * spacing );
            if ( isSectorLoaded( getSectorAt( center ) ) ) {
                continue;
            }
            const uint32_t i0 = z * kHeightfieldSize + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + kHeightfieldSize;
            const uint32_t i3 = i2 + 1;
            indices.insert( indices.end(), { i0, i2, i1, i1, i2, i3 } );
        }
    }
    if ( indices.empty() ) {
        return;
    }
    area.pIndexBuffer = gpRender->getDevice().createBuffer( indices.size() * sizeof( uint32_t ),
                                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data() );
    area.NumIndices = static_cast<uint32_t>( indices.size() );
}

void EdenWorld::draw( EdenRenderer &renderer, const Vector3f &cameraPosition ) {
    static const bool bNoAreas = CmdLine::HasArg( "-noareas" );
    static const bool bNoTerrain = CmdLine::HasArg( "-noterrain" );
    static const bool bNoObjects = CmdLine::HasArg( "-noobjects" );
    static const bool bNoInstances = CmdLine::HasArg( "-noinstances" );
    if ( bOceanVisibilityDirty ) {
        updateOceanVisibility();
    }
    if ( islandOcean.isLoaded() && !bNoObjects && bOceanVisible ) {
        islandOcean.draw( renderer, Matrix4x4f::Identity() );
    }
    if ( bIslandTerrainEnabled && !islandTerrain.Materials.empty() && !bNoAreas ) {
        if ( bIslandIndicesDirty ) {
            updateIslandIndices();
        }
        if ( numIslandIndices > 0 ) {
            EdenMesh mesh = islandTerrain.Mesh;
            mesh.pIndexBuffer = pIslandIndexBuffer;
            mesh.IndexCount = numIslandIndices;
            renderer.draw( mesh, islandTerrain.Materials[0], Matrix4x4f::Identity() );
        }
    }
    for ( auto &pArea : areas ) {
        if ( bNoAreas ) {
            break;
        }
        Area &area = *pArea;
        if ( area.bIndicesDirty ) {
            updateAreaIndices( area );
        }
        if ( area.NumIndices > 0 && !area.Terrain.Materials.empty() ) {
            EdenMesh mesh = area.Terrain.Mesh;
            mesh.pIndexBuffer = area.pIndexBuffer;
            mesh.IndexCount = area.NumIndices;
            // The area permutation (S_USERTYPE4 = 1) uses a 24 m grid.
            const Matrix4x4f world = Matrix4x4f::Translation( area.Origin );
            renderer.draw( mesh, area.Terrain.Materials[0], world );
        }
    }
    for ( auto &pSector : sectors ) {
        Sector &sector = *pSector;
        const Matrix4x4f world = Matrix4x4f::Translation( sector.Origin );
        if ( !sector.Terrain.Materials.empty() && !bNoTerrain ) {
            renderer.draw( sector.Terrain.Mesh, sector.Terrain.Materials[0], world );
        }
        if ( sector.Objects.isLoaded() && !bNoObjects ) {
            sector.Objects.draw( renderer, world );
        }
        if ( !bNoObjects && !bNoInstances ) {
            drawInstances( renderer, sector, cameraPosition );
        }
    }
}

bool EdenWorld::getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const {
    bool bFound = getHeight( x, z, outHeight ) && outHeight <= maxHeight;
    for ( const auto &pSector : sectors ) {
        float height = 0.0f;
        if ( pSector->Objects.getSurfaceHeight( x - pSector->Origin.x, z - pSector->Origin.z,
                                                maxHeight - pSector->Origin.y, height ) ) {
            height += pSector->Origin.y;
            if ( !bFound || height > outHeight ) {
                outHeight = height;
                bFound = true;
            }
        }
    }
    return bFound;
}

bool EdenWorld::getHeight( float x, float z, float &outHeight ) const {
    const SectorId id = getSectorAt( Vector3f( x, 0.0f, z ) );
    for ( const auto &pSector : sectors ) {
        if ( !( pSector->Id == id ) || pSector->Terrain.Heights.empty() ) {
            continue;
        }
        const float gx = Clamp( ( x - pSector->Origin.x ) / 3.0f, 0.0f, 255.999f );
        const float gz = Clamp( ( z - pSector->Origin.z ) / 3.0f, 0.0f, 255.999f );
        const uint32_t ix = static_cast<uint32_t>( gx );
        const uint32_t iz = static_cast<uint32_t>( gz );
        const float fx = gx - ix, fz = gz - iz;
        const std::vector<float> &h = pSector->Terrain.Heights;
        const float h00 = h[iz * kHeightfieldSize + ix], h10 = h[iz * kHeightfieldSize + ix + 1];
        const float h01 = h[( iz + 1 ) * kHeightfieldSize + ix], h11 = h[( iz + 1 ) * kHeightfieldSize + ix + 1];
        outHeight = ( h00 * ( 1 - fx ) + h10 * fx ) * ( 1 - fz ) + ( h01 * ( 1 - fx ) + h11 * fx ) * fz;
        return true;
    }
    return false;
}

void EdenWorld::updateWetGround( EdenRenderer &renderer, const WeatherPreset *pWeather, GPUTexture *pEnvironmentCube,
                                 float deltaTime ) {
    const float intensity = pWeather != nullptr ? pWeather->getFloat( "rain_ground_global_intensity", 0.0f ) : 0.0f;
    const float wet = pWeather != nullptr ? pWeather->getFloat( "rain_ground_wet", 0.0f ) : 0.0f;
    const float puddleHeight = pWeather != nullptr ? pWeather->getFloat( "rain_ground_puddle_height", 0.0f ) : 0.0f;
    const float rippleIntensity =
        pWeather != nullptr ? pWeather->getFloat( "rain_ground_ripple_intensity", 0.0f ) : 0.0f;
    const float rippleScale = pWeather != nullptr ? pWeather->getFloat( "rain_ground_ripple_scale", 0.6f ) : 0.6f;
    const float rippleSpeed =
        pWeather != nullptr ? pWeather->getFloat( "rain_ground_ripple_scroll_speed", 0.0f ) : 0.0f;
    // FUN_00aa0b60: scroll (same on u and v), FUN_00aa0440: ripple frame
    // every 0.025 s.
    rippleScroll += deltaTime * rippleSpeed * 0.15f;
    if ( rippleTimer < 0.0f ) {
        rippleFrame = ( rippleFrame + 1 ) & 15;
        rippleTimer = 0.025f;
    } else {
        rippleTimer -= deltaTime;
    }
    const bool bEnable = intensity > 0.0f && pEnvironmentCube != nullptr && pPuddles != nullptr;
    static const uint64_t kRainSpecs = GetIdentifier64bit( "RainSpecs" );
    static const uint64_t kRainRippleSpecs = GetIdentifier64bit( "RainRippleSpecs" );
    for ( auto &pSector : sectors ) {
        for ( EdenMaterial &material : pSector->Objects.getMaterials() ) {
            if ( material.ShaderSetGUID != 0xc8ad93c95455291bull && material.ShaderSetGUID != 0xe8ad93c00455291bull ) {
                continue;
            }
            // FUN_00aa0780: S_USERTYPE7 materials never get the rain switch.
            if ( material.Spec[38] != 0 ) {
                continue;
            }
            if ( bEnable ) {
                material.Parameters[kRainSpecs] = Vector4f( intensity, wet, 1.0f, puddleHeight );
                material.Parameters[kRainRippleSpecs] =
                    Vector4f( rippleScroll, rippleScroll, rippleIntensity, rippleScale );
                GPUTexture *pRipple = pRipples[rippleFrame] != nullptr ? pRipples[rippleFrame] : pRipples[0];
                const bool bWasWet = material.Spec[40] == 1 && material.NumSlots >= 9;
                material.pTextures[6] = pRipple;
                material.pTextures[7] = pEnvironmentCube;
                material.pTextures[8] = pPuddles;
                if ( !bWasWet ) {
                    material.NumSlots = Max<uint32_t>( material.NumSlots, 9 );
                    material.TextureHashes[6] = GetIdentifier64bit( "RIPPLE_01" );
                    material.TextureHashes[7] = GetIdentifier64bit( "ENVMAP" );
                    material.TextureHashes[8] = GetIdentifier64bit( "PUDDLES" );
                    material.Spec[40] = 1;
                    renderer.updateProgram( material );
                    if ( material.pProgram == nullptr ) {
                        OTDU_LOG_WARN( "EdenWorld: no rain permutation for '%s'\n", material.Name.c_str() );
                        material.Spec[40] = 0;
                        for ( uint32_t i = 6; i < 9; i++ ) {
                            material.pTextures[i] = nullptr;
                            material.TextureHashes[i] = 0;
                        }
                        renderer.updateProgram( material );
                    }
                }
            } else if ( material.Spec[40] == 1 ) {
                material.Spec[40] = 0;
                for ( uint32_t i = 6; i < 9; i++ ) {
                    material.pTextures[i] = nullptr;
                    material.TextureHashes[i] = 0;
                }
                renderer.updateProgram( material );
            }
        }
    }
}

namespace {
GPUTexture *FindColorMap( const std::vector<EdenMaterial> &materials ) {
    static const uint64_t kColor = GetIdentifier64bit( "COLOR" );
    if ( materials.empty() ) {
        return nullptr;
    }
    for ( uint32_t i = 0; i < materials[0].NumSlots; i++ ) {
        if ( materials[0].SlotTypes[i] == kColor ) {
            return materials[0].pTextures[i];
        }
    }
    return nullptr;
}

bool IsOcean( const EdenMaterial &material ) { return material.ShaderSetGUID == 0x0cea000000000002ull; }
} // namespace

void EdenWorld::updateOceanVisibility() {
    // Finest level wins (ocean_spec 5.2 [INF]): the island AREA-x-y tiles of
    // areas that have their own ocean tiles and the area tiles of loaded
    // sectors are hidden. The three levels are coplanar (y = 0 [DATA]), so a
    // tile left under a finer one z-fights with it.
    // [DATA ibiza.bnk ibiza-o.3dd] the island AREA-x-y nodes are dummies
    // (no object); the mesh hangs on their child (OCEAN, OCEAO, ...), so the
    // hiding applies to every item under the AREA-x-y node, not to the node
    // itself (hiding the AREA node alone hid nothing: the island ocean was
    // always drawn under the area / sector oceans).
    bOceanVisibilityDirty = false;
    uint32_t islandHidden = 0, areaHidden = 0;
    if ( islandOcean.isLoaded() ) {
        const Render3DD &scene = islandOcean.getScene();
        const std::vector<SceneNode> &nodes = scene.getNodes();
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            islandOcean.setNodeVisible( static_cast<int32_t>( i ), bOceanVisible );
        }
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            int ax = -1, ay = -1;
            if ( sscanf( ToUpper( nodes[i].Name ).c_str(), "AREA-%d-%d", &ax, &ay ) != 2 ) {
                continue;
            }
            bool bCovered = false;
            for ( const auto &pArea : areas ) {
                if ( pArea->X != ax || pArea->Y != ay || !pArea->Objects.isLoaded() ) {
                    continue;
                }
                for ( const EdenModel::Item &item : pArea->Objects.getItems() ) {
                    bCovered |= IsOcean( pArea->Objects.getMaterials()[item.MaterialIndex] );
                }
            }
            if ( !bCovered ) {
                continue;
            }
            for ( const EdenModel::Item &item : islandOcean.getItems() ) {
                if ( scene.isDescendantOf( item.NodeIndex, static_cast<int32_t>( i ) ) ) {
                    islandOcean.setNodeVisible( item.NodeIndex, false );
                    islandHidden++;
                }
            }
        }
    }
    for ( auto &pArea : areas ) {
        Area &area = *pArea;
        if ( !area.Objects.isLoaded() ) {
            continue;
        }
        for ( const EdenModel::Item &item : area.Objects.getItems() ) {
            if ( !IsOcean( area.Objects.getMaterials()[item.MaterialIndex] ) ) {
                continue;
            }
            const Vector3f center = area.Objects.getNodeTransform( item.NodeIndex )
                                        .transformPoint( ( item.BoundsMin + item.BoundsMax ) * 0.5f );
            const Vector3f world = area.Origin + center;
            const bool bSectorLoaded = isSectorLoaded( getSectorAt( world ) );
            areaHidden += bSectorLoaded ? 1 : 0;
            area.Objects.setNodeVisible( item.NodeIndex, bOceanVisible && !bSectorLoaded );
        }
    }
    for ( auto &pSector : sectors ) {
        Sector &sector = *pSector;
        if ( !sector.Objects.isLoaded() ) {
            continue;
        }
        for ( const EdenModel::Item &item : sector.Objects.getItems() ) {
            if ( IsOcean( sector.Objects.getMaterials()[item.MaterialIndex] ) ) {
                sector.Objects.setNodeVisible( item.NodeIndex, bOceanVisible );
            }
        }
    }
    if ( getenv( "OTDU_OCEAN_LOG" ) != nullptr ) {
        OTDU_LOG_INFO( "EdenWorld: ocean: %u/%zu island items hidden, %u area tiles hidden\n", islandHidden,
                       islandOcean.getItems().size(), areaHidden );
    }
}

void EdenWorld::forEachOceanMaterial(
    const std::function<void( EdenMaterial &material, uint8_t kind, GPUTexture *pColorMap )> &callback ) {
    for ( auto &pSector : sectors ) {
        GPUTexture *pColorMap = FindColorMap( pSector->Terrain.Materials );
        for ( EdenMaterial &material : pSector->Objects.getMaterials() ) {
            if ( IsOcean( material ) ) {
                callback( material, 0, pColorMap );
            }
        }
    }
    for ( auto &pArea : areas ) {
        GPUTexture *pColorMap = FindColorMap( pArea->Terrain.Materials );
        for ( EdenMaterial &material : pArea->Objects.getMaterials() ) {
            if ( IsOcean( material ) ) {
                callback( material, 1, pColorMap );
            }
        }
    }
    for ( EdenMaterial &material : islandOcean.getMaterials() ) {
        if ( IsOcean( material ) ) {
            callback( material, 2, pIslandColorMap );
        }
    }
}
