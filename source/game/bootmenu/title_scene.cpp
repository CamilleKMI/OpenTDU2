#include "shared.h"
#include "title_scene.h"

#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "core/xmb.h"

#include <random>



TitleScene::TitleScene() : time( 0.0f ), bLoaded( false ) {}

TitleScene::~TitleScene() { unload(); }

bool TitleScene::load( const char *pIsland, int32_t cinematicIndex ) {
    unload();
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( pRenderer == nullptr ) {
        return false;
    }
    const std::string islandFolder = gpFile->getBankFolder() + "islands/" + pIsland + "/";
    const std::string bankPath = islandFolder + "scenarics/press_start.bnk";
    if ( !pressStartBank.load( bankPath.c_str() ) ) {
        OTDU_LOG_WARN( "TitleScene: '%s' not found\n", bankPath.c_str() );
        return false;
    }

    // FUN_009c6b20: the Entity__Cinematic whose name does not contain
    // "C_LIFE", in entity order, idx = rand() % count (FORCELOGINCINEINDEX n
    // overrides it). The last 4 digits of the name give the area / sector
    // to stream ("C_01_3332": area 3-3, sector 3-2).
    const BankEntry *pEntities = pressStartBank.findEntry( "entities.xmb" );
    XmbDocument entities;
    std::vector<std::string> names;
    std::vector<std::string> shadowConfigs;
    if ( pEntities != nullptr && entities.load( pEntities->pData, pEntities->Size ) ) {
        XmbValue root = entities.getRoot()["ExportType_Entity"];
        for ( uint32_t i = 0; i < root.getArraySize( "ArrayOf_Entity__Cinematic" ); i++ ) {
            const std::string name = root.getArrayElement( "ArrayOf_Entity__Cinematic", i )["__Name"].asString();
            if ( name.find( "C_LIFE" ) == std::string::npos ) {
                names.push_back( name );
                shadowConfigs.push_back(
                    root.getArrayElement( "ArrayOf_Entity__Cinematic", i )["DefaultShadowConfig"].asString() );
            }
        }
    }
    if ( names.empty() ) {
        OTDU_LOG_WARN( "TitleScene: no press start cinematic\n" );
        return false;
    }
    std::random_device randomDevice;
    size_t index = std::uniform_int_distribution<size_t>( 0, names.size() - 1 )( randomDevice );
    if ( cinematicIndex >= 0 && cinematicIndex < static_cast<int32_t>( names.size() ) ) {
        index = static_cast<size_t>( cinematicIndex );
    }
    cinematicName = names[index];
    this->cinematicIndex = static_cast<int32_t>( index );
    numCinematics = static_cast<int32_t>( names.size() );
    // Entity CinFile -> "press_start_ibiza_cin_M_IBIZA_0x_totem_c_0x_<digits>.cin"
    // (matched on the 4 digits of the entity name).
    const std::string digits = cinematicName.size() >= 4 ? cinematicName.substr( cinematicName.size() - 4 ) : "";
    const BankEntry *pCinematic = nullptr;
    for ( const BankEntry *pEntry : pressStartBank.findEntriesByExtension( "cin" ) ) {
        const std::string name = pEntry->Name;
        if ( name.size() >= 5 && name.compare( name.size() - 5, 5, "_" + digits ) == 0 ) {
            pCinematic = pEntry;
            break;
        }
    }
    if ( pCinematic == nullptr || !cinematic.parse( pCinematic->pData, pCinematic->Size ) ) {
        OTDU_LOG_WARN( "TitleScene: cinematic of '%s' not found\n", cinematicName.c_str() );
        return false;
    }
    // Scenes: the .sce named by the scene references (source path).
    for ( int32_t reference : cinematic.getSceneReferences() ) {
        CinematicScene scene;
        if ( reference >= 0 && reference < static_cast<int32_t>( cinematic.getReferences().size() ) ) {
            std::string path = cinematic.getReferences()[reference].Text;
            const size_t slash = path.find_last_of( "\\/" );
            std::string file = slash != std::string::npos ? path.substr( slash + 1 ) : path;
            const size_t dot = file.find_last_of( '.' );
            if ( dot != std::string::npos ) {
                file = file.substr( 0, dot );
            }
            const BankEntry *pScene = pressStartBank.findEntry( file.c_str(), "sce" );
            if ( pScene == nullptr || !scene.parseScene( pScene->pData, pScene->Size ) ) {
                OTDU_LOG_WARN( "TitleScene: scene '%s' not found\n", file.c_str() );
            }
        }
        scenes.push_back( std::move( scene ) );
    }
    if ( scenes.empty() || scenes[0].getObjects().empty() ) {
        return false;
    }
    // Streaming center: the cinematic car (the area / sector of the name).
    Vector3f origin = scenes[0].getOrigin().getTranslation();
    if ( const CinematicScene::Object *pCar = findCarObject() ) {
        origin = scenes[0].getOrigin().transformPoint( pCar->samplePosition( 0.0f ) );
    }
    OTDU_LOG_INFO( "TitleScene: cinematic '%s' %zu/%zu (%zu objects, %zu shots, %.0f s) at (%.0f, %.0f, %.0f)\n",
                   cinematicName.c_str(), index, names.size(), scenes[0].getObjects().size(),
                   cinematic.getShots().size(), cinematic.getDuration(), origin.x, origin.y, origin.z );
    const int32_t radius = CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1;
    if ( !view.initialize( pIsland, origin, radius ) ) {
        return false;
    }
    // Cinematic start (FUN_00b3ff00 -> FUN_00b3eba0, CinProps bit 4 clear):
    // the entity DefaultShadowConfig (CINE_DEFAULT for the press start
    // cinematics).
    view.setShadowConfig( EdenShadows::FindConfig( shadowConfigs[index] ) );
    OTDU_LOG_INFO( "TitleScene: shadow config '%s'\n", shadowConfigs[index].c_str() );
    view.setDrawCallback( [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        // The car is not in its own environment cube.
        if ( pass != EdenSceneView::ePass::Environment && pass != EdenSceneView::ePass::Reflection ) {
            drawCar( renderer );
        }
    } );
    {
        CinematicCarDesc desc;
        if ( !resolveCinematicCar( desc ) ) {
            desc = CinematicCarDesc();
        }
        car.load( desc );
    }
    time = static_cast<float>( CmdLine::GetArgInt( "-cinetime", 0 ) );
    bLoaded = true;
    return true;
}

void TitleScene::unload() {
    view.destroy();
    car.unload();
    pressStartBank.unload();
    scenes.clear();
    bEnvironmentAssigned = false;
    bLoaded = false;
}

void TitleScene::tick( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    time += deltaTime;
    const float duration = cinematic.getDuration();
    if ( duration > 0.0f && time >= duration ) {
        time = std::fmod( time, duration );
    }
    view.tick( deltaTime );
}

void TitleScene::updateView() {
    EdenSceneView::Camera camera;
    cinematic.getCamera( scenes, time, camera.Position, camera.Rotation, camera.Fov );
    view.setCamera( camera );
    // Message track: "weat:<weather>" then "wmod:<modifier>" on each cut.
    float weatherTime = 0.0f;
    float modifierTime = -1.0f;
    const std::string *pWeatherName = cinematic.getLastMessage( "weat:", time, &weatherTime );
    const std::string weatherName = pWeatherName != nullptr ? *pWeatherName : std::string();
    const std::string *pModifierName = cinematic.getLastMessage( "wmod:", time, &modifierTime );
    const std::string modifierName =
        ( pModifierName != nullptr && modifierTime >= weatherTime ) ? *pModifierName : std::string();
    view.setWeather( weatherName.empty()
                         ? nullptr
                         : view.getWeathers().findModified( weatherName.c_str(), modifierName.c_str() ) );
    Matrix4x4f carWorld;
    const bool bCar = computeCarWorld( carWorld );
    view.setEnvironmentCenter( carPosition, bCar );
}

void TitleScene::renderOffscreen() {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !bLoaded || pRenderer == nullptr ) {
        return;
    }
    updateView();
    // Dynamic environment cube on the car reflections once drawn.
    if ( !bEnvironmentAssigned ) {
        if ( GPUTexture *pCube = view.getEnvironmentCube() ) {
            car.assignEnvironment( *pRenderer, pCube );
            bEnvironmentAssigned = true;
        }
    }
    view.renderOffscreen();
}

void TitleScene::draw() {
    if ( bLoaded ) {
        view.draw();
    }
}

bool TitleScene::resolveCinematicCar( CinematicCarDesc &outDesc ) const {
    // .sce car object (Type 6, "CIN_CAR_01") -> entities.xmb Entity__CinCar
    // (id_car, rim_indice, paint_idx) -> DBCarPhysicsData (_car_file_name,
    // _default_rims) -> DBRims (_rsc_path, _rsc_file_name_front / rear,
    // _diameter_front / rear).
    const CinematicScene::Object *pCarObject = findCarObject();
    const std::string objectName = pCarObject != nullptr ? pCarObject->Name : std::string();
    const BankEntry *pEntities = pressStartBank.findEntry( "entities.xmb" );
    if ( objectName.empty() || pEntities == nullptr || gpDatabase == nullptr ) {
        return false;
    }
    XmbDocument entities;
    if ( !entities.load( pEntities->pData, pEntities->Size ) ) {
        return false;
    }
    XmbValue entityRoot = entities.getRoot()["ExportType_Entity"];
    int64_t carId = -1;
    for ( uint32_t i = 0; i < entityRoot.getArraySize( "ArrayOf_Entity__CinCar" ); i++ ) {
        XmbValue car = entityRoot.getArrayElement( "ArrayOf_Entity__CinCar", i );
        if ( StringCmpI( car["__Name"].asString().c_str(), objectName.c_str() ) == 0 ) {
            carId = car["id_car"].asInt( -1 );
            break;
        }
    }
    if ( carId < 0 ) {
        return false;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    uint64_t rimHashcode = 0;
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( car["_id_car"].asInt( -2 ) == carId ) {
            outDesc.FileName = car["_car_file_name"].asString();
            rimHashcode = car["_default_rims"].asUInt64();
            break;
        }
    }
    if ( outDesc.FileName.empty() ) {
        return false;
    }
    for ( uint32_t i = 0; i < root.getArraySize( "DBRims" ); i++ ) {
        XmbValue rimDesc = root.getArrayElement( "DBRims", i );
        if ( rimDesc["_hashcode"].asUInt64() == rimHashcode ) {
            const std::string path = ToLower( rimDesc["_rsc_path"].asString() );
            outDesc.RimFront = path + "/" + ToLower( rimDesc["_rsc_file_name_front"].asString() );
            outDesc.RimRear = path + "/" + ToLower( rimDesc["_rsc_file_name_rear"].asString() );
            outDesc.DiameterFront = static_cast<int32_t>( rimDesc["_diameter_front"].asInt( 18 ) );
            // Tire: rim radius + sidewall (width x aspect ratio, "245/40")
            outDesc.TireRadius = outDesc.DiameterFront * 0.0254f * 0.5f +
                                 rimDesc["_largeur_front"].asFloat( 245.0f ) *
                                     rimDesc["_hauteur_front"].asFloat( 40.0f ) * 1e-5f;
            outDesc.DiameterRear = static_cast<int32_t>( rimDesc["_diameter_rear"].asInt( 18 ) );
            break;
        }
    }
    OTDU_LOG_INFO( "TitleScene: %s = car %lld '%s', rims '%s' / '%s' (%d\" / %d\")\n", objectName.c_str(),
                   static_cast<long long>( carId ), outDesc.FileName.c_str(), outDesc.RimFront.c_str(),
                   outDesc.RimRear.c_str(), outDesc.DiameterFront, outDesc.DiameterRear );
    return true;
}

const CinematicScene::Object *TitleScene::findCarObject() const {
    // .sce actor "CIN_CAR_xx" (Entity__CinCar).
    for ( const CinematicScene &scene : scenes ) {
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            if ( object.ClassHash == CinematicScene::kClassActor && StringCmpI( object.Name.substr( 0, 7 ).c_str(),
                                                                                 "CIN_CAR" ) == 0 ) {
                return &object;
            }
        }
    }
    return nullptr;
}

bool TitleScene::computeCarWorld( Matrix4x4f &outWorld ) {
    if ( !car.isLoaded() ) {
        return false;
    }
    const CinematicScene::Object *pObject = findCarObject();
    if ( pObject == nullptr ) {
        return false;
    }
    const Vector3f position = scenes[0].getOrigin().transformPoint( pObject->samplePosition( time ) );
    const Quaternionf rotation = pObject->sampleRotation( time );
    outWorld = car.fitOnGround( position, rotation, [this]( float x, float z, float y, float &outHeight ) {
        return view.getWorld().getSurfaceHeight( x, z, y, outHeight );
    } );
    carPosition = outWorld.getTranslation();
    return true;
}

void TitleScene::drawCar( EdenRenderer &renderer ) {
    Matrix4x4f world;
    if ( computeCarWorld( world ) ) {
        car.draw( renderer, world, view.getWeather(), bEnvironmentAssigned );
    }
}
