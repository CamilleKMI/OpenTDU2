#include "shared.h"
#include "first_outro.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

namespace {
constexpr const char *kSpotBank = "islands/ibiza/exterior/eph_l3di.bnk";
constexpr const char *kHouseSpot = "EPH_L3DI_5067I"; // id 0x49447c83_957885a4 (FUN_00b31490 in step 2)
constexpr const char *kCinematic = "C_FIRST_OUTRO";

Quaternionf Conjugate( const Quaternionf &q ) { return Quaternionf( -q.x, -q.y, -q.z, q.w ); }

std::string FileStem( const std::string &path ) {
    const size_t slash = path.find_last_of( "\\/" );
    std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    const size_t dot = file.find_last_of( '.' );
    return dot != std::string::npos ? file.substr( 0, dot ) : file;
}

void ReadPlacement( XmbValue placement, Vector3f &position, Quaternionf &rotation ) {
    rotation = Quaternionf( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                            placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
    position = Vector3f( placement["pos"]["X"].asFloat(), placement["pos"]["Y"].asFloat(), placement["pos"]["Z"].asFloat() );
}
} // namespace

bool FirstOutro::findHouseSpot( const char *pSpot, Vector3f &outPosition, float &outHeading ) const {
    // islands/ibiza/level/spots.bin: ".SPT", "SPTA" { u32 size } then 0x110
    // byte records { u64 id; char bank[8]; vec3 position; f32; f32 heading;
    // ... } (same reader as IntroCinematic, intro_spec 12.1).
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( ( gpFile->getBankFolder() + "islands/ibiza/level/spots.bin" ).c_str(), data ) ||
         data.size() < 0x20 || memcmp( data.data() + 0x10, "SPTA", 4 ) != 0 ) {
        return false;
    }
    uint32_t size = 0;
    memcpy( &size, data.data() + 0x14, 4 );
    const uint64_t id = GetIdentifier64bit( pSpot );
    for ( size_t offset = 0x20; offset + 0x110 <= data.size() && offset < 0x20 + size; offset += 0x110 ) {
        uint64_t recordId = 0;
        memcpy( &recordId, data.data() + offset, 8 );
        if ( recordId == id ) {
            memcpy( &outPosition, data.data() + offset + 0x10, 12 );
            memcpy( &outHeading, data.data() + offset + 0x20, 4 );
            return true;
        }
    }
    return false;
}

bool FirstOutro::locate() {
    float heading = 0.0f;
    if ( !findHouseSpot( kHouseSpot, housePosition, heading ) ) {
        OTDU_LOG_WARN( "FirstOutro: spot %s not found in spots.bin\n", kHouseSpot );
        return false;
    }
    sceneRotation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), heading );
    sceneOrigin = Matrix4x4f::Translation( housePosition ) * Matrix4x4f::FromQuaternion( sceneRotation );
    OTDU_LOG_INFO( "FirstOutro: house %s at (%.3f, %.3f, %.3f) heading %.4f\n", kHouseSpot, housePosition.x,
                   housePosition.y, housePosition.z, heading );
    return true;
}

bool FirstOutro::load( EdenSceneView &view ) {
    unload();
    if ( !locate() ) {
        return false;
    }
    const std::string bankPath = gpFile->getBankFolder() + kSpotBank;
    if ( !bank.load( bankPath.c_str() ) ) {
        OTDU_LOG_WARN( "FirstOutro: '%s' not found\n", bankPath.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( bank, bank.getPath().c_str() );
    hashcodes.load( bank );
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        return false;
    }
    // Entity__Cinematic C_FIRST_OUTRO: CinFile[0] .cin, CinFile[1] .sce.
    XmbValue entityRoot = entities.getRoot()["ExportType_Entity"];
    std::string cinName, sceName;
    for ( uint32_t i = 0; i < entityRoot.getArraySize( "ArrayOf_Entity__Cinematic" ); i++ ) {
        XmbValue entry = entityRoot.getArrayElement( "ArrayOf_Entity__Cinematic", i );
        if ( entry["__Name"].asString() != kCinematic ) {
            continue;
        }
        for ( uint32_t j = 0; j < entry.getArraySize( "CinFile" ); j++ ) {
            XmbValue file = entry.getArrayElement( "CinFile", j );
            const BankEntry *pFile = hashcodes.find( file["rsc"].asUInt64() );
            if ( pFile != nullptr ) {
                ( pFile->Extension == "cin" ? cinName : sceName ) = pFile->Name;
            }
        }
        OTDU_LOG_INFO( "FirstOutro: %s CinProps %lld CinEnd %lld: '%s' / '%s'\n", kCinematic,
                       static_cast<long long>( entry.getArraySize( "CinProps" ) > 0
                                                   ? entry.getArrayElement( "CinProps", 0 ).asInt()
                                                   : 0 ),
                       static_cast<long long>( entry["CinEnd"].asInt() ), cinName.c_str(), sceName.c_str() );
    }
    const BankEntry *pCin = bank.findEntry( cinName.empty() ? "eph_l3di_sc_M_FIRST_OUTRO" : cinName.c_str(), "cin" );
    if ( pCin == nullptr || !cinematic.parse( pCin->pData, pCin->Size ) ) {
        OTDU_LOG_WARN( "FirstOutro: %s .cin not found\n", kCinematic );
        return false;
    }
    for ( int32_t reference : cinematic.getSceneReferences() ) {
        CinematicScene scene;
        if ( reference >= 0 && reference < static_cast<int32_t>( cinematic.getReferences().size() ) ) {
            const BankEntry *pScene =
                bank.findEntry( FileStem( cinematic.getReferences()[reference].Text ).c_str(), "sce" );
            if ( pScene == nullptr || !scene.parseScene( pScene->pData, pScene->Size ) ) {
                OTDU_LOG_WARN( "FirstOutro: scene of %s not found\n", kCinematic );
            }
        }
        scenes.push_back( std::move( scene ) );
    }
    if ( scenes.empty() ) {
        return false;
    }
    sounds.load( bank );
    sounds.setSceneOrigin( sceneOrigin );
    loadModels( view, entities );
    bLoaded = true;
    return true;
}

void FirstOutro::loadModels( EdenSceneView &view, const XmbDocument &entities ) {
    // Same rules as the house models of IntroCinematic::loadBankModels
    // (spec_pool_interior 6): Scenery and Entity__Actor__Mesh in the bank
    // frame, Entity__Actor at entityTRS * rootLocal^-1; the interior LODs
    // (Entity__Actor__Interior) are kept: no interior is loaded here.
    // FREERIDE (mode 1) has no GSReflection (table 0x00f817e0): D_REFLECT is
    // drawn without projectors.
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    auto loadModel = [&]( uint64_t rsc ) -> std::unique_ptr<EdenModel> {
        const BankEntry *pScene = hashcodes.find( rsc, 0, 0 );
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
        pModel->bindUVAnimations( bank );
        view.registerLightmapMaterials( *pModel, EdenSceneView::eLightmapScope::Exterior );
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
        for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
            if ( auto pModel = loadModel( firstMesh( entityExport.getArrayElement( pArray, i ), "mesh" ) ) ) {
                sets.push_back( std::move( pModel ) );
            }
        }
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
        XmbValue actor = entityExport.getArrayElement( "ArrayOf_Entity__Actor", i );
        Prop prop;
        prop.Name = actor["__Name"].asString();
        // CF_Key: the realtor's key of C_REMOVAL (hidden outside it [INF]).
        if ( prop.Name == "CF_Key" ) {
            continue;
        }
        prop.Model = loadModel( firstMesh( actor, "mesh" ) );
        if ( !prop.Model ) {
            continue;
        }
        prop.EntityHashcode = actor["__Hc"].asUInt64();
        Vector3f position;
        Quaternionf rotation;
        ReadPlacement( actor["orient_pos_scale"], position, rotation );
        prop.Local = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation );
        for ( const SceneNode &node : prop.Model->getScene().getNodes() ) {
            if ( node.Parent < 0 ) {
                prop.RootInverse = node.Local.inverse();
                break;
            }
        }
        props.push_back( std::move( prop ) );
    }
    OTDU_LOG_INFO( "FirstOutro: %zu set models, %zu props\n", sets.size(), props.size() );
}

void FirstOutro::unload() {
    sounds.stop( voices );
    sounds.unload();
    sets.clear();
    props.clear();
    scenes.clear();
    if ( !bank.getEntries().empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
    }
    bank.unload();
    bPlaying = false;
    bLoaded = false;
    time = 0.0f;
}

void FirstOutro::start( float startTime ) {
    time = startTime;
    bPlaying = bLoaded;
}

void FirstOutro::tick( float deltaTime ) {
    if ( !bPlaying ) {
        return;
    }
    const float previous = time;
    time += deltaTime;
    // Fx track: FxM_FIRST_OUTRO (m_first_outro_CINEMATIC.wav) at 0.
    if ( !CmdLine::HasArg( "-noaudio" ) ) {
        sounds.update( cinematic, previous, time, &voices );
    }
    // CinEnd 3: the end pose is kept; listener event 3 -> FREERIDE resumes.
    if ( time >= cinematic.getDuration() ) {
        time = cinematic.getDuration();
        bPlaying = false;
    }
}

bool FirstOutro::sceneTime( float atTime, const CinematicScene *&outScene, float &outSceneTime ) const {
    const Cinematic::Shot *pShot = cinematic.getShot( atTime );
    if ( pShot == nullptr || pShot->Scene >= scenes.size() ) {
        return false;
    }
    outScene = &scenes[pShot->Scene];
    outSceneTime = Min( pShot->getSceneTime( atTime ), outScene->getDuration() );
    return true;
}

bool FirstOutro::getCamera( EdenSceneView::Camera &outCamera ) const {
    Vector3f position;
    Quaternionf rotation;
    float fov = outCamera.Fov;
    if ( !cinematic.getCamera( scenes, Min( time, cinematic.getDuration() - 1e-3f ), position, rotation, fov ) ) {
        return false;
    }
    outCamera.Position = sceneOrigin.transformPoint( position );
    outCamera.Rotation = rotation * Conjugate( sceneRotation );
    outCamera.Fov = fov;
    outCamera.Near = 0.8f; // Znear -1 -> 0.8 (FUN_00b3eba0)
    outCamera.Far = 40000.0f;
    return true;
}

bool FirstOutro::getCarPose( float atTime, Vector3f &outPosition, Quaternionf &outOrientation,
                             Vector3f &outVelocity ) const {
    const CinematicScene *pScene = nullptr;
    float t = 0.0f;
    if ( !sceneTime( Min( atTime, cinematic.getDuration() - 1e-3f ), pScene, t ) ) {
        return false;
    }
    const CinematicScene::Object *pCar = pScene->findObject( "PLAYER_CAR" );
    if ( pCar == nullptr ) {
        return false;
    }
    const Vector3f local = pScene->getOrigin().transformPoint( pCar->samplePosition( t ) );
    outPosition = sceneOrigin.transformPoint( local );
    // Eden heading q: car frame R(conj q); island frame R_y R(conj q).
    outOrientation = sceneRotation * Conjugate( pCar->sampleRotation( t ) );
    const float h = 0.2f;
    const Vector3f before = pScene->getOrigin().transformPoint( pCar->samplePosition( Max( t - h, 0.0f ) ) );
    outVelocity = sceneOrigin.transformVector( local - before ) * ( 1.0f / h );
    return true;
}

std::string FirstOutro::getShadowConfig() const {
    const std::string *pName = cinematic.getLastMessage( "shad:", time );
    return pName != nullptr ? *pName : std::string( "CINE_DEFAULT" );
}

void FirstOutro::draw( EdenRenderer &renderer, float ) const {
    for ( const auto &pSet : sets ) {
        pSet->draw( renderer, sceneOrigin );
    }
    // Door_garage: .sce actor of C_FIRST_OUTRO (roll-up), else its entity
    // placement.
    const CinematicScene *pScene = nullptr;
    float t = 0.0f;
    const bool bScene = sceneTime( Min( time, cinematic.getDuration() - 1e-3f ), pScene, t );
    for ( const Prop &prop : props ) {
        Matrix4x4f local = prop.Local;
        if ( bScene ) {
            for ( const CinematicScene::Object &object : pScene->getObjects() ) {
                if ( object.EntityHashcode == prop.EntityHashcode && object.findTrack( CinematicScene::kTrackPosition ) ) {
                    local = Matrix4x4f::Translation( object.samplePosition( t ) ) *
                            EdenRotationFromQuaternion( object.sampleRotation( t ) );
                    break;
                }
            }
        }
        prop.Model->draw( renderer, sceneOrigin * local * prop.RootInverse );
    }
}
