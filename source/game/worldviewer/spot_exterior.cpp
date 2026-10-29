#include "shared.h"
#include "spot_exterior.h"

#include "audio/gs_audio.h"
#include "core/xmb.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"

SpotExterior::~SpotExterior() { unload(); }

bool SpotExterior::load( EdenSceneView &view, const std::string &bankPath ) {
    unload();
    path = bankPath;
    pView = &view;
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( pRenderer == nullptr || !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "SpotExterior: '%s' could not be loaded\n", path.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( bank, bank.getPath().c_str() );
    hashcodes.load( bank );
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "SpotExterior: '%s' has no entities.xmb\n", path.c_str() );
        return false;
    }
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    auto loadModel = [&]( uint64_t rsc ) -> std::unique_ptr<EdenModel> {
        const BankEntry *pScene = rsc != 0 ? hashcodes.find( rsc, 0, 0 ) : nullptr;
        if ( pScene == nullptr ) {
            return nullptr;
        }
        const BankEntry *pGeometry = bank.findEntry( pScene->Name.c_str(), "3dg" );
        const BankEntry *pMaterials = bank.findEntry( pScene->Name.c_str(), "2dm" );
        auto pModel = std::make_unique<EdenModel>();
        if ( pGeometry == nullptr ||
             !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, bank.getPath().c_str() ) ) {
            return nullptr;
        }
        // FUN_00d7e950 -> FUN_00d7df50: projective materials of the bank.
        view.registerProjectiveMaterials( *pModel );
        pModel->bindUVAnimations( bank );
        return pModel;
    };
    auto firstMesh = []( XmbValue entry, const char *pField ) -> uint64_t {
        for ( uint32_t j = 0; j < entry.getArraySize( pField ); j++ ) {
            XmbValue mesh = entry.getArrayElement( pField, j );
            if ( mesh["rscSubType"].asInt() == 0 ) {
                return mesh["rsc"].asUInt64();
            }
        }
        return 0;
    };
    XmbValue scenery = entities.getRoot()["ExportType_Scenery"];
    for ( uint32_t i = 0; i < scenery.getArraySize( "ArrayOf_Scenery" ); i++ ) {
        XmbValue entry = scenery.getArrayElement( "ArrayOf_Scenery", i );
        for ( uint32_t j = 0; j < entry.getArraySize( "Model" ); j++ ) {
            XmbValue model = entry.getArrayElement( "Model", j );
            if ( model["rscSubType"].asInt() == 0 ) {
                if ( auto pModel = loadModel( model["rsc"].asUInt64() ) ) {
                    sets.push_back( std::move( pModel ) );
                }
            }
        }
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( const char *pArray : { "ArrayOf_Entity__Actor__Mesh", "ArrayOf_Entity__Actor__Interior" } ) {
        const bool bMesh = strcmp( pArray, "ArrayOf_Entity__Actor__Mesh" ) == 0;
        for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
            XmbValue entry = entityExport.getArrayElement( pArray, i );
            if ( auto pModel = loadModel( firstMesh( entry, "mesh" ) ) ) {
                if ( bMesh ) {
                    // 'MESH' entity (D_REFLECT): GSReflection projectors.
                    ReflectScene scene;
                    scene.pModel = pModel.get();
                    scene.ActorLayer1 = entry["AvatarReflectlayername1"].asString();
                    scene.ActorLayer2 = entry["AvatarReflectlayername2"].asString();
                    reflectScenes.push_back( std::move( scene ) );
                }
                sets.push_back( std::move( pModel ) );
            }
        }
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
        XmbValue actor = entityExport.getArrayElement( "ArrayOf_Entity__Actor", i );
        Prop prop;
        prop.Model = loadModel( firstMesh( actor, "mesh" ) );
        if ( !prop.Model ) {
            continue;
        }
        XmbValue placement = actor["orient_pos_scale"];
        const Quaternionf rotation( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                                    placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
        const Vector3f position( placement["pos"]["X"].asFloat(), placement["pos"]["Y"].asFloat(),
                                 placement["pos"]["Z"].asFloat() );
        Matrix4x4f rootInverse;
        for ( const SceneNode &node : prop.Model->getScene().getNodes() ) {
            if ( node.Parent < 0 ) {
                rootInverse = node.Local.inverse();
                break;
            }
        }
        prop.Local = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation ) * rootInverse;
        props.push_back( std::move( prop ) );
    }
    registerLightmaps( view );
    OTDU_LOG_INFO( "SpotExterior: %s: %zu set models, %zu props\n", path.c_str(), sets.size(), props.size() );
    return true;
}

void SpotExterior::registerLightmaps( EdenSceneView &view ) {
    for ( auto &pSet : sets ) {
        view.registerLightmapMaterials( *pSet, EdenSceneView::eLightmapScope::Exterior );
        view.registerNeonMaterials( *pSet );
    }
    for ( Prop &prop : props ) {
        view.registerLightmapMaterials( *prop.Model, EdenSceneView::eLightmapScope::Exterior );
        view.registerNeonMaterials( *prop.Model );
    }
}

void SpotExterior::registerReflections( EdenSceneView &view ) {
    std::string bankName = bank.getPath();
    const size_t slash = bankName.find_last_of( "/\\" );
    if ( slash != std::string::npos ) {
        bankName = bankName.substr( slash + 1 );
    }
    for ( const ReflectScene &scene : reflectScenes ) {
        for ( const Matrix4x4f &frame : frames ) {
            view.registerReflectionScene( *scene.pModel, frame, bankName.c_str(), scene.ActorLayer1.c_str(),
                                          scene.ActorLayer2.c_str() );
        }
    }
}

uint32_t SpotExterior::resolveForeignTextures( EdenRenderer &renderer ) {
    uint32_t numResolved = 0;
    auto resolve = [&]( EdenModel &model ) {
        for ( EdenMaterial &material : model.getMaterials() ) {
            GPUTexture *pBefore[16];
            memcpy( pBefore, material.pTextures, sizeof( pBefore ) );
            if ( renderer.resolveMissingTextures( material ) == 0 ) {
                continue;
            }
            for ( uint32_t i = 0; i < 16; i++ ) {
                if ( pBefore[i] == nullptr && material.pTextures[i] != nullptr ) {
                    foreignSlots.emplace_back( &material, i );
                    numResolved++;
                }
            }
        }
    };
    for ( auto &pSet : sets ) {
        resolve( *pSet );
    }
    for ( Prop &prop : props ) {
        resolve( *prop.Model );
    }
    return numResolved;
}

void SpotExterior::clearForeignTextures() {
    for ( const auto &slot : foreignSlots ) {
        slot.first->pTextures[slot.second] = nullptr;
    }
    foreignSlots.clear();
}

void SpotExterior::unload() {
    foreignSlots.clear();
    effects.clear();
    stopSounds();
    if ( pView != nullptr ) {
        for ( auto &pSet : sets ) {
            pView->removePlanarReflectionModel( *pSet );
        }
        for ( Prop &prop : props ) {
            pView->removePlanarReflectionModel( *prop.Model );
        }
    }
    pView = nullptr;
    reflectScenes.clear();
    sets.clear();
    props.clear();
    if ( !bank.getEntries().empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
    }
    bank.unload();
}

uint32_t SpotExterior::startEffects( EdenRenderer &renderer ) {
    effects.clear();
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        return 0;
    }
    const uint32_t numEmitters = effects.load( renderer, bank, hashcodes, entities, frames );
    if ( numEmitters != 0 ) {
        OTDU_LOG_INFO( "SpotExterior: %s: %u particle emitters\n", path.c_str(), numEmitters );
        static const bool bLog = getenv( "OTDU_FX_LOG" ) != nullptr;
        for ( const auto &pEmitter : effects.getEmitters() ) {
            if ( bLog ) {
                const Vector3f p = pEmitter->getEmitter().getWorld().getTranslation();
                OTDU_LOG_INFO( "    emitter at (%.2f, %.2f, %.2f) %s%s\n", p.x, p.y, p.z,
                               pEmitter->getSystem()->Material.pProgram->Name.c_str(),
                               pEmitter->getSystem()->Material.ScreenDepthSlot >= 0 ? " soft" : "" );
            }
        }
    }
    startSounds();
    return numEmitters;
}

uint32_t SpotExterior::startSounds() {
    stopSounds();
    if ( gpAudio == nullptr || !gpAudio->isReady() ) {
        return 0;
    }
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        return 0;
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    if ( entityExport.getArraySize( "ArrayOf_Entity__Fx" ) == 0 || !sounds.load( bank ) ) {
        return 0;
    }
    // Entity__Fx started with the bank (AutoStart, Entity__Fx loader
    // FUN_00b18040: +0xe8 Fx, +0xfc AutoStart [CODE]); edFx creates the
    // edFxSound source of each " DNS" node of the .bfx [ENC]. The SND nodes
    // of the ECAS fountains have an identity matrix [DATA]: the source sits
    // at the entity position (bank frame), static (looped, LoopCount -1).
    uint32_t numVoices = 0;
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Fx" ); i++ ) {
        XmbValue entity = entityExport.getArrayElement( "ArrayOf_Entity__Fx", i );
        if ( !entity["AutoStart"].asBool( true ) ) {
            continue;
        }
        const std::string name = entity["__Name"].asString();
        const std::vector<CinematicSounds::Sound> *pSounds = sounds.findEmitter( name.c_str() );
        if ( pSounds == nullptr || pSounds->empty() ) {
            continue;
        }
        for ( const Matrix4x4f &frame : frames ) {
            const Vector3f position = frame.transformPoint( pSounds->front().Position );
            const size_t before = voices.size();
            sounds.play( *pSounds, 0.0f, false, &voices, &position );
            for ( const CinematicSounds::Sound &sound : *pSounds ) {
                OTDU_LOG_INFO( "SpotExterior: %s: %s sound '%s' client %d (%s) vol %.3f loop %d at (%.1f, %.1f, %.1f) "
                               "clip %.1f / %.1f / %.1f\n",
                               path.c_str(), name.c_str(), sound.Name.c_str(), sound.Client,
                               AudioSetup::GetBfxClientName( sound.Client ) != nullptr ? AudioSetup::GetBfxClientName( sound.Client ) : "?",
                               sound.Volume, sound.bLoop ? 1 : 0,
                               position.x, position.y, position.z, sound.ClipNear, sound.ClipMid, sound.ClipFar );
            }
            numVoices += static_cast<uint32_t>( voices.size() - before );
        }
    }
    if ( numVoices != 0 ) {
        OTDU_LOG_INFO( "SpotExterior: %s: %u sound voices\n", path.c_str(), numVoices );
    }
    return numVoices;
}

void SpotExterior::stopSounds() {
    if ( !voices.empty() ) {
        sounds.stop( voices );
    }
    voices.clear();
    sounds.unload();
}

void SpotExterior::update( float deltaTime ) { effects.update( deltaTime ); }

void SpotExterior::draw( EdenRenderer &renderer ) {
    for ( const Matrix4x4f &frame : frames ) {
        for ( const auto &pSet : sets ) {
            pSet->draw( renderer, frame );
        }
        for ( const Prop &prop : props ) {
            prop.Model->draw( renderer, frame * prop.Local );
        }
    }
    effects.draw( renderer, renderer.getSceneParams().CameraPosition );
}
