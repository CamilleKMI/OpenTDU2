#include "shared.h"
#include "cinematic_props.h"

#include "core/xmb.h"
#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/gs_render.h"

namespace {
std::string FileStem( const std::string &path ) {
    const size_t slash = path.find_last_of( "\\/" );
    std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    const size_t dot = file.find_last_of( '.' );
    return dot != std::string::npos ? file.substr( 0, dot ) : file;
}
} // namespace

bool CinematicProps::load( EdenSceneView &view, const Bank &bank, const BankHashcodes &hashcodes,
                           const XmbDocument &entities, const char *pCinematic, const Matrix4x4f &spotFrame ) {
    unload();
    frame = spotFrame;
    XmbValue entityRoot = entities.getRoot()["ExportType_Entity"];
    std::string cinName;
    for ( uint32_t i = 0; i < entityRoot.getArraySize( "ArrayOf_Entity__Cinematic" ); i++ ) {
        XmbValue entry = entityRoot.getArrayElement( "ArrayOf_Entity__Cinematic", i );
        if ( entry["__Name"].asString() != pCinematic ) {
            continue;
        }
        for ( uint32_t j = 0; j < entry.getArraySize( "CinFile" ); j++ ) {
            const BankEntry *pFile = hashcodes.find( entry.getArrayElement( "CinFile", j )["rsc"].asUInt64() );
            if ( pFile != nullptr && pFile->Extension == "cin" ) {
                cinName = pFile->Name;
            }
        }
    }
    const BankEntry *pCin = cinName.empty() ? nullptr : bank.findEntry( cinName.c_str(), "cin" );
    if ( pCin == nullptr || !cinematic.parse( pCin->pData, pCin->Size ) ) {
        OTDU_LOG_WARN( "CinematicProps: %s not found\n", pCinematic );
        return false;
    }
    for ( int32_t reference : cinematic.getSceneReferences() ) {
        CinematicScene scene;
        if ( reference >= 0 && reference < static_cast<int32_t>( cinematic.getReferences().size() ) ) {
            const BankEntry *pScene =
                bank.findEntry( FileStem( cinematic.getReferences()[reference].Text ).c_str(), "sce" );
            if ( pScene != nullptr ) {
                scene.parseScene( pScene->pData, pScene->Size );
            }
        }
        scenes.push_back( std::move( scene ) );
    }
    if ( scenes.empty() ) {
        return false;
    }
    // The Entity__Actor of the bank animated by an actor of the scene
    // (object entity __Hc), like the props of FirstOutro / IntroCinematic.
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    for ( uint32_t i = 0; i < entityRoot.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
        XmbValue actor = entityRoot.getArrayElement( "ArrayOf_Entity__Actor", i );
        const uint64_t hashcode = actor["__Hc"].asUInt64();
        const CinematicScene::Object *pObject = nullptr;
        for ( const CinematicScene::Object &object : scenes[0].getObjects() ) {
            if ( object.EntityHashcode == hashcode && !object.isCamera() ) {
                pObject = &object;
            }
        }
        if ( pObject == nullptr ) {
            continue;
        }
        uint64_t meshRsc = 0;
        for ( uint32_t j = 0; j < actor.getArraySize( "mesh" ); j++ ) {
            XmbValue mesh = actor.getArrayElement( "mesh", j );
            if ( mesh["rscSubType"].asInt() == 0 ) {
                meshRsc = mesh["rsc"].asUInt64();
                break;
            }
        }
        const BankEntry *pScene = meshRsc != 0 ? hashcodes.find( meshRsc, 0, 0 ) : nullptr;
        const BankEntry *pGeometry = pScene != nullptr ? bank.findEntry( pScene->Name.c_str(), "3dg" ) : nullptr;
        const BankEntry *pMaterials = pScene != nullptr ? bank.findEntry( pScene->Name.c_str(), "2dm" ) : nullptr;
        Prop prop;
        prop.Name = actor["__Name"].asString();
        prop.pObject = pObject;
        prop.Model = std::make_unique<EdenModel>();
        if ( pGeometry == nullptr ||
             !prop.Model->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, bank.getPath().c_str() ) ) {
            OTDU_LOG_WARN( "CinematicProps: %s model not loaded\n", prop.Name.c_str() );
            continue;
        }
        prop.Model->bindUVAnimations( bank );
        view.registerLightmapMaterials( *prop.Model, EdenSceneView::eLightmapScope::Exterior );
        view.registerNeonMaterials( *prop.Model );
        for ( const SceneNode &node : prop.Model->getScene().getNodes() ) {
            if ( node.Parent < 0 ) {
                prop.RootInverse = node.Local.inverse();
                break;
            }
        }
        props.push_back( std::move( prop ) );
    }
    const Vector3f origin = frame.getTranslation();
    OTDU_LOG_INFO( "CinematicProps: %s (%.2f s, %zu scenes), %zu actors at (%.1f, %.1f, %.1f)\n", pCinematic,
                   cinematic.getDuration(), scenes.size(), props.size(), origin.x, origin.y, origin.z );
    bLoaded = true;
    return true;
}

void CinematicProps::unload() {
    props.clear();
    scenes.clear();
    bLoaded = false;
    bPlaying = false;
    time = 0.0f;
}

void CinematicProps::tick( float deltaTime ) {
    if ( !bPlaying ) {
        return;
    }
    // Loop byte cine +0xb5b = 1 [CODE].
    time += deltaTime;
    const float duration = cinematic.getDuration();
    time = duration > 0.0f ? std::fmod( time, duration ) : 0.0f;
}

void CinematicProps::draw( EdenRenderer &renderer ) const {
    if ( !bPlaying ) {
        return;
    }
    const CinematicScene &scene = scenes[0];
    const float t = Min( time, scene.getDuration() );
    for ( const Prop &prop : props ) {
        if ( !prop.pObject->sampleVisibility( t, true ) ) {
            continue;
        }
        const Matrix4x4f local = Matrix4x4f::Translation( prop.pObject->samplePosition( t ) ) *
                                 EdenRotationFromQuaternion( prop.pObject->sampleRotation( t ) );
        prop.Model->draw( renderer, frame * scene.getOrigin() * local * prop.RootInverse );
    }
}
