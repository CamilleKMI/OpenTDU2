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
// SH_LinearUV + 3 * clamp + clamp (2DM / D3DTADDRESS 3 on u and v).
constexpr uint8_t kSamplerLinearClampUV = 5 + 3 * 2 + 2;
// TD_Ground, TD_Ground_HM, TD_Road shader sets (ShaderSet guids, FUN_00982af0).
constexpr uint64_t kGroundShaderSet = 0xb0f1234b0f123473ull;
constexpr uint64_t kGroundHMShaderSet = 0xa0a0a1a1a2a2a3a3ull;
constexpr uint64_t kRoadShaderSet = 0xc8ad93c95455291bull;
const uint64_t kShadowSlotType = GetIdentifier64bit( "SHADOW" );
const uint64_t kSectorOffset = GetIdentifier64bit( "SECTROFF" );

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
    // Water plane: DAT_00fe5028 == 1 (level "Hawai", FUN_0098e450) -> -1.8,
    // else 0 (FUN_00963010 [CODE]).
    waterHeight = StringCmpI( pIsland, "hawai" ) == 0 ? -1.8f : 0.0f;
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
            // Neon set of the library materials (the .pmi instances share
            // them; FUN_00988d10 registers each instance model, the
            // WeatherEmissive value is the same for all).
            for ( EdenMaterial &material : libraryModel.getMaterials() ) {
                if ( EdenNeons::IsNeonSetMaterial( material ) ) {
                    sharedNeonMaterials.push_back( &material );
                }
            }
            // LOD 1 / 2 objects and tree impostors (eden_world_lod.cpp).
            loadLibraryLods( renderer, commonPath.c_str() );
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
            // AREASIZE = (16 / NbColumns, 16 / NbRows) (FUN_0098cc90 called by
            // FUN_0098e450 with levelObj+0xf98/+0xf9c = the worldconfig.cpr
            // area grid, FUN_0098e060 [CODE]). The island permutation VS:
            // colour UV = grid * AreaSize / 256 [SHADER], i.e. the colour map
            // spans the area grid (16 quads of 384 m per area).
            islandTerrain.Materials[0].StageConstants[0]["AreaSize"] = {
                Vector4f( 16.0f / static_cast<float>( areaColumns ), 16.0f / static_cast<float>( areaRows ), 0.0f,
                          0.0f ) };
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
    loadEffectsBank( pIsland );
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
                for ( EdenMaterial &material : pModel->getMaterials() ) {
                    if ( EdenNeons::IsNeonSetMaterial( material ) ) {
                        sharedNeonMaterials.push_back( &material );
                    }
                }
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
    // V (vegetation) and O (objects): static library models; A: actors;
    // S: skinned library models (guard rails SF_GLISS / SF_GLISO, MOB type 3,
    // payload = one skin matrix per skeleton bone, see pmi_spec.md 1.5).
    // F (ambient effects) is read by loadEffects.
    static const bool bNoRails = CmdLine::HasArg( "-norails" ); // comparisons: -S.pmi not loaded
    static const bool bGroundLog = getenv( "OTDU_GROUND_LOG" ) != nullptr;
    uint32_t groundCounts[4] = {};
    for ( const char *pSuffix : { "-v", "-o", "-a", "-s" } ) {
        if ( bNoRails && pSuffix[1] == 's' ) {
            continue;
        }
        // FUN_00988d10 list kinds: V 0, O 1, S 2 (the A actors: 3).
        const uint8_t listKind = pSuffix[1] == 'v' ? 0 : pSuffix[1] == 'o' ? 1 : pSuffix[1] == 's' ? 2 : 3;
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
            } else if ( instance.Type == 3 ) {
                // Guard rail: library node (one LOD, switch value 0). The S
                // list pass FUN_00981f40 culls per cell with a maxDist2 written
                // by FUN_004dd1c0 [ENC]: drawn over the loaded sectors [INF].
                // The record AABB holds the skinned mesh [DATA]; FUN_00988d10
                // sets a 25 m bounding radius (DAT_00edd53c) [CODE].
                auto it = libraryNodes.find( instance.Hashcode );
                if ( it == libraryNodes.end() || instance.pPayload == nullptr || instance.PayloadSize < 64 ) {
                    continue;
                }
                model = it->second;
                distance = FLT_MAX;
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
            // FUN_00985e40 for the instance model (FUN_00988d10 0x98947b,
            // FUN_00960c30 for the actors): one AnimValue state per blinking
            // material of the model, seeded per instance.
            if ( sector.InstanceBlinkStart.empty() ) {
                sector.InstanceBlinkStart.push_back( 0 );
            }
            const uint32_t instanceIndex = static_cast<uint32_t>( sector.Instances.size() );
            const uint32_t sectorKey = static_cast<uint32_t>( ( ( sector.Id.AreaX * 8 + sector.Id.X ) << 20 ) ^
                                                              ( ( sector.Id.AreaY * 8 + sector.Id.Y ) << 12 ) );
            for ( const EdenMaterial *pMaterial : getInstanceBlinkMaterials( instance.Type == 3 ? 1 : instance.Type, model ) ) {
                EdenNeons::Blink blink;
                blink.initialize( sectorKey ^ ( instanceIndex * 0x9e3779b1u ) ^
                                  static_cast<uint32_t>( sector.InstanceBlinks.size() ) );
                sector.InstanceBlinks.emplace_back( pMaterial, blink );
            }
            static const bool bNeonLog = getenv( "OTDU_NEON_LOG" ) != nullptr;
            if ( bNeonLog && sector.InstanceBlinks.size() != sector.InstanceBlinkStart.back() ) {
                OTDU_LOG_INFO( "EdenWorld: %s instance %u %016llx (%.1f %.1f %.1f): %u blinking neon materials (%s)\n",
                               name.c_str(), instanceIndex, static_cast<unsigned long long>( instance.Hashcode ),
                               instance.Position.x, instance.Position.y, instance.Position.z,
                               static_cast<uint32_t>( sector.InstanceBlinks.size() - sector.InstanceBlinkStart.back() ),
                               sector.InstanceBlinks.back().first->Name.c_str() );
                OTDU_LOG_INFO( "    axes x (%.2f %.2f %.2f) z (%.2f %.2f %.2f) box (%.1f %.1f %.1f)-(%.1f %.1f %.1f)\n",
                               instance.World.m[0][0], instance.World.m[0][1], instance.World.m[0][2],
                               instance.World.m[2][0], instance.World.m[2][1], instance.World.m[2][2],
                               instance.BoxMin.x, instance.BoxMin.y, instance.BoxMin.z, instance.BoxMax.x,
                               instance.BoxMax.y, instance.BoxMax.z );
            }
            sector.InstanceBlinkStart.push_back( static_cast<uint32_t>( sector.InstanceBlinks.size() ) );
            sector.Instances.push_back( instance );
            sector.InstanceModels.push_back( model );
            sector.InstanceDistances.push_back( distance );
            sector.InstanceLists.push_back( listKind );
            if ( bGroundLog && ( instance.Type == 1 || instance.Type == 3 ) && libraryNodeHasGround( model ) ) {
                groundCounts[listKind]++;
            }
        }
    }
    if ( bGroundLog ) {
        OTDU_LOG_INFO( "EdenWorld: %s: instances with a TD_Ground material: V %u, O %u, S %u\n", name.c_str(),
                       groundCounts[0], groundCounts[1], groundCounts[2] );
    }
    OTDU_LOG_DEBUG( "EdenWorld: %s: %zu instances\n", name.c_str(), sector.Instances.size() );
    if ( !sector.InstanceBlinks.empty() ) {
        OTDU_LOG_DEBUG( "EdenWorld: %s: %zu blinking instance neons\n", name.c_str(), sector.InstanceBlinks.size() );
    }
}

bool EdenWorld::libraryNodeHasGround( int32_t node ) {
    auto it = libraryNodeGround.find( node );
    if ( it != libraryNodeGround.end() ) {
        return it->second;
    }
    bool bGround = false;
    EdenModel *pModels[3] = { &libraryModel, &libraryLodModels[0], &libraryLodModels[1] };
    for ( EdenModel *pModel : pModels ) {
        const auto range = pModel->getNodeItemRange( node );
        for ( uint32_t i = range.first; i < range.first + range.second && !bGround; i++ ) {
            bGround = pModel->getMaterials()[pModel->getItems()[i].MaterialIndex].ShaderSetGUID == kGroundShaderSet;
        }
    }
    libraryNodeGround.emplace( node, bGround );
    return bGround;
}

const std::vector<const EdenMaterial *> &EdenWorld::getInstanceBlinkMaterials( uint8_t type, int32_t model ) {
    auto &cache = type == 1 ? libraryNodeBlinks : actorModelBlinks;
    auto it = cache.find( model );
    if ( it != cache.end() ) {
        return it->second;
    }
    std::vector<const EdenMaterial *> &list = cache[model];
    auto add = [&list]( const EdenMaterial &material ) {
        if ( EdenNeons::IsBlinkMaterial( material ) &&
             std::find( list.begin(), list.end(), &material ) == list.end() ) {
            list.push_back( &material );
        }
    };
    if ( type == 1 ) {
        // Materials drawn by the instanced node [INF: the model instance
        // material list of the original is not decoded further].
        const auto range = libraryModel.getNodeItemRange( model );
        for ( uint32_t i = range.first; i < range.first + range.second; i++ ) {
            add( libraryModel.getMaterials()[libraryModel.getItems()[i].MaterialIndex] );
        }
    } else if ( model >= 0 && model < static_cast<int32_t>( actorModels.size() ) ) {
        for ( const EdenMaterial &material : actorModels[model]->getMaterials() ) {
            add( material );
        }
    }
    return list;
}

void EdenWorld::RegisterNeons( EdenModel &model, uint32_t key, std::vector<EdenMaterial *> &outSet,
                               std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> &outBlinks ) {
    if ( CmdLine::HasArg( "-noworldneons" ) ) {
        return;
    }
    for ( EdenMaterial &material : model.getMaterials() ) {
        if ( EdenNeons::IsNeonSetMaterial( material ) ) {
            outSet.push_back( &material );
        }
        if ( EdenNeons::IsBlinkMaterial( material ) ) {
            EdenNeons::Blink blink;
            blink.initialize( key ^ static_cast<uint32_t>( outBlinks.size() * 0x9e3779b1u ) );
            material.Parameters[EdenNeons::kAnimValue] = blink.Value;
            outBlinks.emplace_back( &material, blink );
        }
    }
}

void EdenWorld::updateNeons( const WeatherPreset *pWeather, float deltaTime ) {
    // impostor_ambient (FUN_00d769e0 -> renderer +0x45c0 [CODE]): ambient
    // light 1 of the impostors (eden_world_lod.cpp).
    if ( pWeather != nullptr ) {
        impostorAmbient = pWeather->getVector3( "impostor_ambient", Vector3f( 0.0f, 0.0f, 0.0f ) );
    }
    static const bool bDisabled = CmdLine::HasArg( "-noworldneons" ); // comparisons: no world neon registration
    if ( bDisabled ) {
        return;
    }
    // FUN_009834d0: neon set (FUN_009829d0) then the AnimValue entries
    // (FUN_009821a0).
    float strength = 0.0f;
    if ( EdenNeons::GetStrength( pWeather, strength ) ) {
        for ( EdenMaterial *pMaterial : sharedNeonMaterials ) {
            EdenNeons::ApplyStrength( *pMaterial, strength );
        }
        for ( auto &pSector : sectors ) {
            for ( EdenMaterial *pMaterial : pSector->NeonMaterials ) {
                EdenNeons::ApplyStrength( *pMaterial, strength );
            }
        }
        for ( auto &pArea : areas ) {
            for ( EdenMaterial *pMaterial : pArea->NeonMaterials ) {
                EdenNeons::ApplyStrength( *pMaterial, strength );
            }
        }
    }
    for ( auto &pSector : sectors ) {
        for ( auto &entry : pSector->NeonBlinks ) {
            entry.second.update( deltaTime );
            entry.first->Parameters[EdenNeons::kAnimValue] = entry.second.Value;
        }
        for ( auto &entry : pSector->InstanceBlinks ) {
            entry.second.update( deltaTime );
        }
    }
    for ( auto &pArea : areas ) {
        for ( auto &entry : pArea->NeonBlinks ) {
            entry.second.update( deltaTime );
            entry.first->Parameters[EdenNeons::kAnimValue] = entry.second.Value;
        }
    }
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
        // Per instance AnimValue (parameter block of (material, instance)).
        static const bool bNoNeons = CmdLine::HasArg( "-noworldneons" );
        const EdenDrawParams *pParams = nullptr;
        EdenDrawParams params;
        if ( !bNoNeons && i + 1 < sector.InstanceBlinkStart.size() &&
             sector.InstanceBlinkStart[i] != sector.InstanceBlinkStart[i + 1] ) {
            params.bDeferBlended = true;
            for ( uint32_t b = sector.InstanceBlinkStart[i]; b < sector.InstanceBlinkStart[i + 1]; b++ ) {
                params.MaterialVectors.push_back(
                    { sector.InstanceBlinks[b].first, EdenNeons::kAnimValue, sector.InstanceBlinks[b].second.Value } );
            }
            pParams = &params;
        }
        // TD_Ground instances: per sector sectrOff and SHADOW = -s map (O / S).
        const uint8_t listKind = sector.InstanceLists[i];
        if ( ( instance.Type == 1 || instance.Type == 3 ) && listKind < 3 &&
             libraryNodeHasGround( sector.InstanceModels[i] ) ) {
            if ( pParams != nullptr ) {
                const EdenDrawParams &ground = sector.GroundInstanceParams[listKind];
                params.Vectors = ground.Vectors;
                params.TextureOverrides = ground.TextureOverrides;
            } else {
                pParams = &sector.GroundInstanceParams[listKind];
            }
        }
        if ( instance.Type == 3 ) {
            // Skin matrices: D3D row major float4x4 per bone, v_world =
            // v . Bone[i] . World (TD_Ground skinned vs: __Skin_Matrix c148
            // then __World_View_Proj c60 [SHADER]); same storage as World.
            Matrix4x4f bones[32];
            const uint32_t numBones = Min<uint32_t>( instance.PayloadSize / 64u, 32u );
            for ( uint32_t b = 0; b < numBones; b++ ) {
                memcpy( bones[b].m, instance.pPayload + b * 64u, 64u );
            }
            libraryModel.drawNodeItemsSkinned( renderer, sector.InstanceModels[i], instance.World, bones, numBones,
                                               pParams );
        } else if ( instance.Type == 1 ) {
            drawLibraryInstance( renderer, instance, sector.InstanceModels[i], cameraPosition, pParams );
        } else {
            actorModels[sector.InstanceModels[i]]->draw( renderer, instance.World, pParams );
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
    fxSources.clear();
    fxLibrary.clear();
    fxEntities = XmbDocument();
    fxHashcodes = BankHashcodes();
    if ( fxBank.isLoaded() ) {
        gpRender->getTextureCache().releaseOwner( fxBank.getPath().c_str() );
    }
    fxBank.unload();
    destroyLibraryLods();
    libraryModel.destroy();
    libraryNodes.clear();
    actorModels.clear();
    sharedNeonMaterials.clear();
    libraryNodeBlinks.clear();
    actorModelBlinks.clear();
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
    // FUN_00982af0 (end of the sector setup FUN_00986760) [CODE]: for the
    // sector materials (+0x1a7c) of TD_Road (c8ad93c95455291b: every SHADOW
    // slot) and TD_Ground / TD_Ground_HM (slot 2): texture = the -s map
    // (sector resource +0x674), addressing 3 / 3 (CLAMP), and for the two
    // TD_Ground families sectrOff = (sector x, 0, sector z, 0) (+0x30 / +0x34,
    // minus the origin shift DAT_01367a10 that OpenTDU2 does not use). The
    // TD_Ground vs samples the -s map at (world.xz - sectrOff.xz) / 768: with
    // the 2DM value (0, 0, 0, 768) the lookup was off by half a sector on the
    // odd sector rows (x origin + 384 m).
    GPUTexture *pShadowMap = gpRender->getTextureCache().find( path, ( name + "-s" ).c_str() );
    pSector->pShadowMap = pShadowMap;
    EdenRenderer *pEdenRenderer = pRenderer;
    const Vector3f sectorOrigin = pSector->Origin;
    auto materialSetup = [pShadowMap, pEdenRenderer, sectorOrigin]( EdenMaterial &material ) {
        if ( pShadowMap == nullptr ) {
            return;
        }
        const bool bGround = material.ShaderSetGUID == kGroundShaderSet || material.ShaderSetGUID == kGroundHMShaderSet;
        if ( !bGround && material.ShaderSetGUID != kRoadShaderSet ) {
            if ( pEdenRenderer->assignTexture( material, "SHADOW", pShadowMap ) ) {
                pEdenRenderer->updateProgram( material );
            }
            return;
        }
        // TD_Ground / TD_Ground_HM: slot 2 whatever its 2DM type (+0x150; it is
        // SHADOW in the library and sector-5-0-6-7-o TD_Ground materials but
        // 5067tunnel_mesh, whose slot 2 is DETAIL1 [DATA]).
        bool bAssigned = false;
        for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
            if ( bGround ? i == 2 : material.SlotTypes[i] == kShadowSlotType ) {
                material.pTextures[i] = pShadowMap;
                material.TextureHashes[i] = GetIdentifier64bit( pShadowMap->Name.c_str() );
                material.SamplerIndex[i] = kSamplerLinearClampUV;
                bAssigned = true;
            }
        }
        if ( bGround ) {
            material.Parameters[kSectorOffset] = Vector4f( sectorOrigin.x, 0.0f, sectorOrigin.z, 0.0f );
        }
        if ( bAssigned ) {
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
    // FUN_00986e90: streamed world object loaded -> FUN_00985e40 for its
    // model instance (+0xf4) [CODE]; the sector -o scene is taken as one
    // model instance [INF].
    RegisterNeons( pSector->Objects, static_cast<uint32_t>( GetIdentifier64bit( name.c_str() ) ),
                   pSector->NeonMaterials, pSector->NeonBlinks );
    // TD_Ground parameters of the O / S list instances (night_lighting_spec
    // 2.1). FUN_00982af0 fills the override record (list +0x7670 =
    // { TD_Ground guid, 3, slot { texture +0x24 = the -s map, addressing
    // +0x2c / +0x2d = 2 / 2 } }, same as FUN_00987e80) of the lists +0xa010
    // (kind 1, O) and +0x11750 (kind 2, S) and of the fourth list +0x18e90;
    // FUN_00987ec0 / FUN_009884c0 point every library model instance with a
    // TD_Ground material at the record of its list (+0xb8, flags |= 0x10000)
    // and write the instance sectrOff = (sector x, 0, sector z, 768.0
    // DAT_00f4aa88) [CODE]. The record of the V list (+0x28d0 + 0x7670) is not
    // written by readable code [unknown]; how the submit (0x4dfa00 / 0x4dfa80)
    // applies the record is [ENC]: the SHADOW slot of the TD_Ground materials
    // (runtime slot, TD_Ground handler 0x00d712c0) takes its texture [INF].
    for ( uint32_t list = 0; list < 3; list++ ) {
        EdenDrawParams &params = pSector->GroundInstanceParams[list];
        params.bDeferBlended = true;
        params.Vectors["sectrOff"] = Vector4f( pSector->Origin.x, 0.0f, pSector->Origin.z, 768.0f );
        if ( list != 0 && pShadowMap != nullptr ) {
            EdenDrawParams::TextureOverride entry;
            entry.ShaderSetGUID = kGroundShaderSet;
            entry.SlotType = kShadowSlotType;
            entry.pTexture = pShadowMap;
            entry.Sampler = kSamplerLinearMirror;
            params.TextureOverrides.push_back( entry );
        }
    }
    loadInstances( *pSector, name );
    loadEffects( *pSector, name );
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
    if ( getenv( "OTDU_GROUND_LOG" ) != nullptr ) {
        uint32_t numGround = 0;
        for ( const EdenMaterial &material : pArea->Objects.getMaterials() ) {
            if ( material.ShaderSetGUID == kGroundShaderSet || material.ShaderSetGUID == kGroundHMShaderSet ) {
                OTDU_LOG_INFO( "EdenWorld: %s: TD_Ground material '%s'\n", name, material.Name.c_str() );
                numGround++;
            }
        }
        OTDU_LOG_INFO( "EdenWorld: %s: %u TD_Ground area object materials\n", name, numGround );
    }
    RegisterNeons( pArea->Objects, static_cast<uint32_t>( GetIdentifier64bit( name ) ), pArea->NeonMaterials,
                   pArea->NeonBlinks );
    // Sector cell groups of the area objects (FUN_00992890 [CODE]): for each
    // of the 8 x 8 sectors the node "Sector-%d-%d-%d-%d" (x, y, sx, sy) is
    // looked up in the area -o scene and stored in a per sector table
    // (FUN_00992820); on odd rows the first cell also looks up the node of
    // sector (x - 1, y, 7, sy), whose staggered cell overlaps this area by
    // half a sector. [DATA] area-x-y-o.3dd: BLOCK -> SECTOR-x-y-sx-sy (dummy)
    // -> OCEAN tile (the 768 m cell) and the far objects of that sector
    // (e.g. casino area-0-0: CITY under SECTOR-0-0-3-1).
    if ( pArea->Objects.isLoaded() ) {
        const Render3DD &scene = pArea->Objects.getScene();
        std::vector<std::pair<int32_t, SectorId>> groups;
        for ( int32_t sy = 0; sy < static_cast<int32_t>( kSectorsPerArea ); sy++ ) {
            for ( int32_t sx = 0; sx < static_cast<int32_t>( kSectorsPerArea ); sx++ ) {
                if ( sx == 0 && ( sy & 1 ) != 0 ) {
                    const SectorId previous = { x - 1, y, static_cast<int32_t>( kSectorsPerArea ) - 1, sy };
                    const int32_t node = scene.findNode( SectorName( previous ).c_str() );
                    if ( node >= 0 ) {
                        groups.emplace_back( node, previous );
                    }
                }
                const SectorId id = { x, y, sx, sy };
                const int32_t node = scene.findNode( SectorName( id ).c_str() );
                if ( node >= 0 ) {
                    groups.emplace_back( node, id );
                }
            }
        }
        for ( const EdenModel::Item &item : pArea->Objects.getItems() ) {
            SectorId sector = { INT32_MIN, 0, 0, 0 };
            for ( const auto &group : groups ) {
                if ( scene.isDescendantOf( item.NodeIndex, group.first ) ) {
                    sector = group.second;
                    break;
                }
            }
            pArea->ItemSectors.push_back( sector );
        }
        if ( getenv( "OTDU_OCEAN_LOG" ) != nullptr ) {
            OTDU_LOG_INFO( "EdenWorld: %s: %zu items, %zu sector groups\n", name, pArea->Objects.getItems().size(),
                           groups.size() );
        }
    }
    areas.push_back( std::move( pArea ) );
    bIslandIndicesDirty = true;
    bOceanVisibilityDirty = true;
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
    static const bool bNoAreaObjects = CmdLine::HasArg( "-noareaobjects" );
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
    // Area objects (area-local, -o: the 768 m ocean tiles of the sector cells
    // and the far objects), per sector group (updateOceanVisibility). Without
    // them the island AREA-x-y ocean tile (hidden under a loaded area) left a
    // hole down to the island heightfield (-100 m off the coast [DATA]) or
    // the sky dome ground colour: the "beige band" on the horizon.
    for ( auto &pArea : areas ) {
        if ( bNoObjects || bNoAreaObjects ) {
            break;
        }
        if ( pArea->Objects.isLoaded() ) {
            pArea->Objects.draw( renderer, Matrix4x4f::Translation( pArea->Origin ) );
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
    flushImpostors( renderer );
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
    // Area objects: the items of a "Sector-x-y-sx-sy" group (its ocean tile
    // and far objects) are hidden while that sector is loaded (finest level
    // wins [INF: the per sector table of FUN_00992820 is filled in readable
    // code, its use per frame is not]). Items outside any group fall back to
    // the sector under their centre.
    for ( auto &pArea : areas ) {
        Area &area = *pArea;
        if ( !area.Objects.isLoaded() ) {
            continue;
        }
        const std::vector<EdenModel::Item> &items = area.Objects.getItems();
        for ( size_t i = 0; i < items.size(); i++ ) {
            const EdenModel::Item &item = items[i];
            SectorId sector = i < area.ItemSectors.size() ? area.ItemSectors[i] : SectorId{ INT32_MIN, 0, 0, 0 };
            if ( sector.AreaX == INT32_MIN ) {
                const Vector3f center = area.Objects.getNodeTransform( item.NodeIndex )
                                            .transformPoint( ( item.BoundsMin + item.BoundsMax ) * 0.5f );
                sector = getSectorAt( area.Origin + center );
            }
            const bool bSectorLoaded = isSectorLoaded( sector );
            const bool bOcean = IsOcean( area.Objects.getMaterials()[item.MaterialIndex] );
            areaHidden += ( bOcean && bSectorLoaded ) ? 1 : 0;
            area.Objects.setNodeVisible( item.NodeIndex, !bSectorLoaded && ( bOceanVisible || !bOcean ) );
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
    if ( getenv( "OTDU_OCEAN_DUMP" ) != nullptr && islandOcean.isLoaded() ) {
        const std::vector<SceneNode> &nodes = islandOcean.getScene().getNodes();
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            OTDU_LOG_INFO( "ocean node %zu '%s' parent %d obj %016llx\n", i, nodes[i].Name.c_str(), nodes[i].Parent,
                           static_cast<unsigned long long>( nodes[i].ObjectHashcode ) );
        }
        for ( const EdenModel::Item &item : islandOcean.getItems() ) {
            const Vector3f a = islandOcean.getNodeTransform( item.NodeIndex ).transformPoint( item.BoundsMin );
            const Vector3f b = islandOcean.getNodeTransform( item.NodeIndex ).transformPoint( item.BoundsMax );
            OTDU_LOG_INFO( "ocean item node %d (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n", item.NodeIndex, a.x, a.y, a.z, b.x,
                           b.y, b.z );
        }
        for ( auto &pArea : areas ) {
            const std::vector<SceneNode> &areaNodes = pArea->Objects.getScene().getNodes();
            for ( size_t i = 0; i < areaNodes.size(); i++ ) {
                OTDU_LOG_INFO( "area %d-%d node %zu '%s' parent %d obj %016llx\n", pArea->X, pArea->Y, i,
                               areaNodes[i].Name.c_str(), areaNodes[i].Parent,
                               static_cast<unsigned long long>( areaNodes[i].ObjectHashcode ) );
            }
            for ( const EdenModel::Item &item : pArea->Objects.getItems() ) {
                const Vector3f a = pArea->Objects.getNodeTransform( item.NodeIndex ).transformPoint( item.BoundsMin );
                const Vector3f b = pArea->Objects.getNodeTransform( item.NodeIndex ).transformPoint( item.BoundsMax );
                const EdenMaterial &material = pArea->Objects.getMaterials()[item.MaterialIndex];
                OTDU_LOG_INFO( "area %d-%d item node %d mat '%s' %016llx (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n", pArea->X,
                               pArea->Y, item.NodeIndex, material.Name.c_str(),
                               static_cast<unsigned long long>( material.ShaderSetGUID ), a.x, a.y, a.z, b.x, b.y, b.z );
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
            // AREASIZE (kind 2 only) = the worldconfig.cpr area grid as floats
            // (levelObj+0xf98/+0xf9c, FUN_008be1e0 / FUN_0098e060 [CODE]):
            // vs_2 colour UV = v0.xz / (6144 AreaSize) [SHADER].
            material.StageConstants[0]["AreaSize"] = {
                Vector4f( static_cast<float>( areaColumns ), static_cast<float>( areaRows ), 0.0f, 0.0f ) };
        }
    }
}

void EdenWorld::loadEffectsBank( const char *pIsland ) {
    // FUN_00c39050: the Particles service ("Particles", vtable 0x00ef6fd0)
    // loads the island bank "vfx_env" of the "FX" folder into its bank object
    // (mgr+0xd6c0) [CODE].
    const std::string path = "Euro/Bnk/islands/" + std::string( pIsland ) + "/fx/vfx_env.bnk";
    if ( !gpFile->fileExists( path.c_str() ) || !fxBank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "EdenWorld: '%s' not found (no ambient effects)\n", path.c_str() );
        return;
    }
    gpRender->getTextureCache().loadBankTextures( fxBank, path.c_str() );
    fxHashcodes.load( fxBank );
    const BankEntry *pEntities = fxBank.findEntry( "entities.xmb" );
    if ( pEntities == nullptr || !fxEntities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "EdenWorld: '%s' has no entities.xmb\n", path.c_str() );
        return;
    }
    // Fx__Root__World { __Hc; __Name; SourceData; TriggerDistance } (loader
    // FUN_00b17930: base FUN_00b17820 -> +0x10 SourceData, TriggerDistance ->
    // +0x20, default 1000.0 = DAT_00eddb38 [CODE]).
    XmbValue world = fxEntities.getRoot()["ExportType_FxWorld"];
    for ( uint32_t i = 0; i < world.getArraySize( "ArrayOf_Fx__Root__World" ); i++ ) {
        XmbValue entry = world.getArrayElement( "ArrayOf_Fx__Root__World", i );
        EffectSource &source = fxSources[entry["SourceData"].asUInt64()];
        source.Name = entry["__Name"].asString();
        source.TriggerDistance = entry["TriggerDistance"].asFloat( 1000.0f );
        OTDU_LOG_DEBUG( "EdenWorld: ambient effect %s (source %016llx), trigger distance %.0f\n", source.Name.c_str(),
                        static_cast<unsigned long long>( entry["SourceData"].asUInt64() ), source.TriggerDistance );
    }
    OTDU_LOG_INFO( "EdenWorld: %s: %zu ambient effects (Fx__Root__World)\n", path.c_str(), fxSources.size() );
}

void EdenWorld::loadEffects( Sector &sector, const std::string &name ) {
    const BankEntry *pEntry = sector.SectorBank.findEntry( ( name + "-f" ).c_str(), "pmi" );
    if ( pEntry == nullptr ) {
        return;
    }
    // FUN_00c38d40: same record decoding as the other lists (FUN_00988250 +
    // sector origin x / z), any MOB type, at most 30 (0x1e) records.
    std::vector<PmiInstance> instances;
    if ( !ParsePmi( pEntry->pData, pEntry->Size, sector.Origin, instances ) ) {
        OTDU_LOG_WARN( "EdenWorld: failed to parse '%s'\n", pEntry->Filename.c_str() );
        return;
    }
    for ( const PmiInstance &instance : instances ) {
        if ( sector.Effects.size() >= 30 ) {
            break;
        }
        Sector::Effect effect;
        effect.SourceHash = instance.Hashcode;
        effect.World = instance.World;
        // +0x60 = TriggerDistance^2 of the Fx__Root__World whose SourceData
        // is the record hash (FUN_00b34a40), 0 when there is none (never
        // started) [CODE].
        auto it = fxSources.find( instance.Hashcode );
        effect.TriggerDistanceSq = it != fxSources.end() ? it->second.TriggerDistance * it->second.TriggerDistance : 0.0f;
        OTDU_LOG_INFO( "EdenWorld: %s: effect %s (%016llx) at (%.1f, %.1f, %.1f)\n", name.c_str(),
                       it != fxSources.end() ? it->second.Name.c_str() : "(unknown source)",
                       static_cast<unsigned long long>( instance.Hashcode ), instance.Position.x, instance.Position.y,
                       instance.Position.z );
        sector.Effects.push_back( std::move( effect ) );
    }
}

void EdenWorld::setEffectsEnabled( bool bEnable ) {
    bEffectsEnabled = bEnable;
    if ( !bEnable ) {
        for ( auto &pSector : sectors ) {
            for ( Sector::Effect &effect : pSector->Effects ) {
                effect.Instances.clear();
            }
        }
    }
}

void EdenWorld::updateEffects( const Vector3f &cameraPosition, float deltaTime ) {
    if ( !bEffectsEnabled || pRenderer == nullptr ) {
        return;
    }
    // FUN_00c38fb0 -> FUN_00c38b10 per loaded sector [CODE]: d^2 from the
    // camera position (DAT_011958f0..f8) to the entry translation.
    for ( auto &pSector : sectors ) {
        for ( size_t i = 0; i < pSector->Effects.size(); i++ ) {
            Sector::Effect &effect = pSector->Effects[i];
            const Vector3f delta = effect.World.getTranslation() - cameraPosition;
            const float distanceSq = delta.dot( delta );
            if ( effect.TriggerDistanceSq < distanceSq ) {
                if ( !effect.Instances.empty() ) {
                    OTDU_LOG_DEBUG( "EdenWorld: effect %016llx stopped (%.0f m)\n",
                                    static_cast<unsigned long long>( effect.SourceHash ), std::sqrt( distanceSq ) );
                }
                effect.Instances.clear();
                continue;
            }
            if ( effect.Instances.empty() ) {
                // FUN_00a9bb30: creates the fx of the source (edFx, [ENC]).
                auto it = fxSources.find( effect.SourceHash );
                if ( it == fxSources.end() ) {
                    continue;
                }
                EffectSource &source = it->second;
                if ( !source.bResolved ) {
                    source.bResolved = true;
                    source.Systems =
                        fxLibrary.getSourceSystems( *pRenderer, fxBank, fxHashcodes, fxEntities, effect.SourceHash );
                }
                // The .bfx particle nodes of vfx_env have an identity
                // transform [DATA]: world = entry matrix * .prt emitter.
                for ( size_t s = 0; s < source.Systems.size(); s++ ) {
                    auto pInstance = std::make_unique<EdenParticleInstance>();
                    const uint32_t seed = static_cast<uint32_t>( effect.SourceHash ) ^
                                          static_cast<uint32_t>( ( i + 1 ) * 0x9e3779b9u ) ^ static_cast<uint32_t>( s );
                    pInstance->initialize( source.Systems[s], effect.World, seed );
                    effect.Instances.push_back( std::move( pInstance ) );
                }
                const Vector3f p = effect.World.getTranslation();
                OTDU_LOG_INFO( "EdenWorld: effect %s started at (%.1f, %.1f, %.1f), %.0f m from the camera, %zu "
                               "emitters\n",
                               source.Name.c_str(), p.x, p.y, p.z, std::sqrt( distanceSq ), effect.Instances.size() );
            }
            for ( auto &pInstance : effect.Instances ) {
                pInstance->update( deltaTime );
            }
        }
    }
}

void EdenWorld::drawEffects( EdenRenderer &renderer, const Vector3f &cameraPosition ) {
    if ( !bEffectsEnabled ) {
        return;
    }
    for ( auto &pSector : sectors ) {
        for ( Sector::Effect &effect : pSector->Effects ) {
            for ( auto &pInstance : effect.Instances ) {
                pInstance->draw( renderer, cameraPosition );
            }
        }
    }
}

uint32_t EdenWorld::getNumEffects() const {
    uint32_t count = 0;
    for ( const auto &pSector : sectors ) {
        count += static_cast<uint32_t>( pSector->Effects.size() );
    }
    return count;
}

uint32_t EdenWorld::getNumRunningEffects() const {
    uint32_t count = 0;
    for ( const auto &pSector : sectors ) {
        for ( const Sector::Effect &effect : pSector->Effects ) {
            count += effect.Instances.empty() ? 0u : 1u;
        }
    }
    return count;
}
