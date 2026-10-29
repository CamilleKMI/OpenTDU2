#include "shared.h"
#include "gm_testcar.h"

#include "audio/gs_audio.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/avatarchoice/intro_cinematic.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

namespace {
// DAT_00edf610: test drive duration (FUN_009f3b30 / FUN_008d9a10) [CODE + DATA].
constexpr float kTestDriveDuration = 120.0f;
// DB text of /:PauseQuit (FUN_00a90570 'sbdd': FUN_0099aa80(..., 0x207)) [CODE].
constexpr int32_t kLocPauseQuit = 0x207;

Matrix4x4f WithoutTranslation( Matrix4x4f m ) {
    m.m[3][0] = m.m[3][1] = m.m[3][2] = 0.0f;
    return m;
}

int32_t SectorKey( const Vector3f &position ) {
    const int32_t row = static_cast<int32_t>( std::floor( position.z / 768.0f ) );
    const float offset = ( row & 1 ) ? 384.0f : 0.0f;
    const int32_t column = static_cast<int32_t>( std::floor( ( position.x - offset ) / 768.0f ) );
    return row * 1024 + column;
}
} // namespace

GMTestCar::GMTestCar() : GameMode( eGameMode::GM_TestCar ) {}

GMTestCar::~GMTestCar() { exit(); }

bool GMTestCar::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    TestDriveRequest &request = GetTestDriveRequest();
    if ( !request.bActive ) {
        // -gm TESTCAR test entry: a dealer car of ESTOL_B_1275I.
        request = TestDriveRequest();
        request.bActive = true;
        request.IdCar = CmdLine::GetArgInt( "-testcar", 371 );
        request.Spot = "ESTOL_B_1275I";
        request.PassengerConfig = "SPT_Jude";
    }
    duration = CmdLine::HasArg( "-testtime" ) ? static_cast<float>( CmdLine::GetArgInt( "-testtime", 120 ) ) : request.Duration;
    if ( gpAudio != nullptr ) {
        // audiosetup.xmb game mode TESTCAR [DATA].
        gpAudio->setGameMode( "TESTCAR" );
    }
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.AvatarConfig.empty() ) {
        const char *pAvatar = CmdLine::GetArgValue( "-avatar" );
        profile.AvatarConfig = GetStoryAvatarConfig( pAvatar != nullptr ? pAvatar : "AVT_Male_Intro" );
    }
    // FUN_00a90cf0: FUN_00ab9be0(DAT_00fd92a0, colour, rims, interior): the test
    // car is the current car [CODE]; FUN_00a90c70 restores it only at story
    // steps 11 / -1, so at step 7 it stays the current (loaned) car [CODE].
    if ( profile.StoryStep != 11 && profile.StoryStep != -1 ) {
        profile.LoanCar = request.IdCar;
        profile.CurrentGarageSlot = -1;
    }
    if ( !CinematicCar::FindDatabaseDesc( request.IdCar, request.RimsHashcode, carDesc ) ) {
        OTDU_LOG_WARN( "TESTCAR: car %d not in the database\n", request.IdCar );
        return false;
    }

    // Start pose: FUN_00960280(6, 1) [CODE]: no 'CARP' entity
    // "PosCarParking" in estol_b.bnk [DATA] -> FUN_00a39630: the spot
    // position snapped on the road network (FUN_00cfb280 / FUN_00cfab80 on
    // DAT_0119b420) [CODE]; the road network is not loaded by OpenTDU2: the
    // spot's second transform (rt+0x50 / rt+0x60, the street side of the
    // dealer) [INF].
    Vector3f startPosition( 12298.7f, 6.01f, 16374.6f );
    float startHeading = -3.046f;
    IntroCinematic::SpotRecord spot;
    if ( IntroCinematic::ReadSpot( request.Spot.c_str(), spot ) ) {
        startPosition = spot.Position2;
        startHeading = spot.Heading2;
    }
    const Quaternionf startOrientation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), startHeading );

    const int32_t radius = CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1;
    if ( !view.initialize( "ibiza", startPosition, radius ) ) {
        return false;
    }
    gameWeathers.load( ( gpFile->getBankFolder() + "islands/ibiza/fx/game_weathers.xmb" ).c_str() );
    const char *pWeatherName = CmdLine::GetArgValue( "-weather" );
    pWeather = gameWeathers.find( pWeatherName != nullptr ? pWeatherName : "14" ); // clock [ENC] -> "14" [INF]
    if ( pWeather == nullptr && !gameWeathers.getPresets().empty() ) {
        pWeather = &gameWeathers.getPresets().front();
    }
    view.setWeather( pWeather );
    view.setShadowConfig( EdenShadows::FindConfig( "FREERIDE" ) );

    car.load( carDesc );
    if ( !request.ColorId.empty() ) {
        car.applyColor( request.ColorId );
    }
    collision.setHeightFunction(
        [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    collision.loadLibrary( view.getWorld().getCommonBank() );
    collision.streamAround( startPosition, 1 );
    setupVehicle();
    chase.load( setup.CameraId != 0 ? setup.CameraId : setup.Db.CarId );
    std::string hudFile = carDesc.FileName;
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBCarPhysicsData", i );
            if ( StringCmpI( row["_car_file_name"].asString().c_str(), carDesc.FileName.c_str() ) == 0 ) {
                hudFile = row["_hud_file_name"].asString();
                break;
            }
        }
    }
    // GamePC.cpr flash movies of mode 7 (HUD, GAUGES as FREERIDE) [DATA, INF list].
    hud.load( hudFile );
    hud.setVisible( true );
    hud.setTestDrive( true );
    hud.setCountdown( duration );

    // FUN_00a906a0: the passenger (state 3000) from DAT_00fdb450; the driver
    // seated (FUN_00baec60 / FUN_00bbb810) [CODE].
    avatars.load();
    const std::string driverPath = gpFile->getBankFolder() + "scenarics/avatars/avatarsdriver.bnk";
    if ( driverBank.load( driverPath.c_str() ) ) {
        if ( !driver.load( avatars, profile.AvatarConfig.c_str(), SeatedAvatar::eSeat::Driver, driverBank ) ) {
            OTDU_LOG_WARN( "TESTCAR: driver avatar '%s' not loaded\n", profile.AvatarConfig.c_str() );
        }
        bPassengerSeated = passenger.load( avatars, request.PassengerConfig.c_str(), SeatedAvatar::eSeat::Passenger, driverBank );
        if ( !bPassengerSeated ) {
            OTDU_LOG_WARN( "TESTCAR: passenger '%s' not loaded\n", request.PassengerConfig.c_str() );
        }
    }

    view.setDrawCallback( [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        if ( pass == EdenSceneView::ePass::Environment || pass == EdenSceneView::ePass::Reflection ) {
            return;
        }
        if ( car.isLoaded() ) {
            car.draw( renderer, carWorld, view.getWeather(), bEnvironmentAssigned );
        }
        driver.draw( renderer, chase.getMode() != 23 );
        if ( bPassengerSeated ) {
            passenger.draw( renderer );
        }
    } );

    vehicle.reset( startPosition + Vector3f( 0.0f, 0.3f, 0.0f ), startOrientation, Vector3f() );
    carWorld = vehicle.getWorldMatrix();
    chase.reset( startPosition, Matrix4x4f::FromQuaternion( startOrientation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
    camera = chase.update( 0.0f, carWorld, 0.0f,
                           [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    // FUN_00a90990: start time +0x1b07f0 = -1 (DAT_00ede404): set at the first tick.
    startTime = -1.0f;
    clock = 0.0f;
    bLoaded = true;
    OTDU_LOG_INFO( "TESTCAR: car %d '%s' (colour %d '%s', rims %d) at %s (%.2f, %.2f, %.2f) heading %.3f, %.0f s, "
                   "driver %s, passenger %s\n",
                   request.IdCar, carDesc.FileName.c_str(), request.Color, request.ColorId.c_str(), request.Rims,
                   request.Spot.c_str(), startPosition.x, startPosition.y, startPosition.z, startHeading, duration,
                   profile.AvatarConfig.c_str(), request.PassengerConfig.c_str() );
    return true;
}

void GMTestCar::setupVehicle() {
    // Same rig as GMFreeRide / GMMission (D_RIM_xx wheel nodes), the
    // DBCarPhysicsData row of the car (LoadVehicleSetup).
    LoadVehicleSetup( carDesc.FileName, setup );
    std::vector<VehicleDynamics::WheelDesc> wheels;
    wheelNodes.clear();
    if ( car.isLoaded() ) {
        const EdenModel &model = car.getModel();
        const std::vector<SceneNode> &nodes = model.getScene().getNodes();
        for ( size_t i = 0; i + 1 < nodes.size(); i++ ) {
            if ( nodes[i].Name.rfind( "D_RIM_", 0 ) != 0 ) {
                continue;
            }
            WheelNode node;
            node.RimNode = static_cast<int32_t>( i );
            node.RimRest = model.getNodeTransform( node.RimNode );
            node.TireRest = model.getNodeTransform( node.RimNode + 1 );
            const Vector3f hub = node.RimRest.getTranslation();
            node.bFront = hub.z > 0.0f;
            node.bLeft = hub.x > 0.0f;
            wheelNodes.push_back( node );
            VehicleDynamics::WheelDesc wheel;
            wheel.HubLocal = hub;
            wheel.bFront = node.bFront;
            wheel.bLeft = node.bLeft;
            wheel.Radius = carDesc.TireRadius;
            wheels.push_back( wheel );
        }
    }
    if ( wheels.empty() ) {
        const float halfBase = 1.35f;
        for ( int32_t i = 0; i < 4; i++ ) {
            VehicleDynamics::WheelDesc wheel;
            wheel.bFront = i < 2;
            wheel.bLeft = ( i & 1 ) == 0;
            const float track = wheel.bFront ? setup.Db.TrackFront : setup.Db.TrackRear;
            wheel.HubLocal = Vector3f( ( wheel.bLeft ? 0.5f : -0.5f ) * track, carDesc.TireRadius, wheel.bFront ? halfBase : -halfBase );
            wheel.Radius = carDesc.TireRadius;
            wheels.push_back( wheel );
        }
    }
    vehicle.setup( setup, wheels );
    OTDU_LOG_INFO( "TESTCAR: vehicle '%s' car id %d, %zu wheels, mass %.0f kg\n", carDesc.FileName.c_str(), setup.Db.CarId,
                   wheels.size(), setup.Db.Mass );
}

void GMTestCar::updateWheels() {
    if ( !car.isLoaded() ) {
        return;
    }
    EdenModel &model = car.getModel();
    const std::vector<VehicleDynamics::WheelState> &states = vehicle.getWheels();
    for ( size_t i = 0; i < wheelNodes.size() && i < states.size(); i++ ) {
        const WheelNode &node = wheelNodes[i];
        const Matrix4x4f motion =
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0, 1, 0 ), node.bFront ? vehicle.getSteerAngle() : 0.0f ) ) *
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 1, 0, 0 ), states[i].SpinAngle ) );
        const Vector3f lift( 0.0f, vehicle.getWheelOffset( i ), 0.0f );
        model.setNodeTransform( node.RimNode, Matrix4x4f::Translation( node.RimRest.getTranslation() + lift ) * motion *
                                                  WithoutTranslation( node.RimRest ) );
        model.setNodeTransform( node.RimNode + 1, Matrix4x4f::Translation( node.TireRest.getTranslation() + lift ) * motion *
                                                      WithoutTranslation( node.TireRest ) );
    }
}

void GMTestCar::streamWorld( const Vector3f &position ) {
    const int32_t key = SectorKey( position );
    if ( key == lastSectorKey ) {
        return;
    }
    lastSectorKey = key;
    view.getWorld().loadAround( position, CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1 );
    collision.streamAround( position, 1 );
}

void GMTestCar::endTestDrive( const char *pReason ) {
    // FUN_00a90300 [CODE]: the dealer spot (DAT_01025784) entered again by
    // FUN_00b30030(1, 0) (its mode: SHOP); DAT_00f77698 -1 -> mode 3.
    if ( bEnded ) {
        return;
    }
    bEnded = true;
    hud.setTestDrive( false );
    const Vector3f p = vehicle.getPosition();
    OTDU_LOG_INFO( "TESTCAR: end of the test drive (%s) after %.1f s at (%.1f, %.1f, %.1f), %.1f km -> SHOP %s\n", pReason,
                   clock, p.x, p.y, p.z, vehicle.getOdometer() / 1000.0f, GetTestDriveRequest().Spot.c_str() );
    pGameInstance->setNextGameMode( eGameMode::GM_Shop );
}

void GMTestCar::tick( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    clock += deltaTime;
    // FUN_00a90850: the countdown [CODE].
    if ( startTime < 0.0f ) {
        startTime = clock;
        endTime = clock + duration;
    }
    // Pause menu "quit" (FUN_00a90570 'quit' -> FUN_00a90300) [CODE]; the
    // pause key opening it is [INF]: pause = quit the test drive.
    if ( gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        endTestDrive( "quit" );
    }
    const float remaining = std::max( endTime - clock, 0.0f );
    hud.setCountdown( remaining );
    if ( endTime < clock ) {
        endTestDrive( "time out" );
    }

    VehicleDynamics::Input input;
    if ( !bEnded ) {
        input.Throttle = gpInput->getAnalog( eInputAction::IA_CarAccel );
        input.Brake = gpInput->getAnalog( eInputAction::IA_CarBrake );
        input.Steering = gpInput->getSteering();
        input.bHandBrake = gpInput->isDown( eInputAction::IA_CarHandBrake );
        if ( gpInput->wasPressed( eInputAction::IA_CarCamera ) ) {
            chase.nextMode();
        }
    } else {
        input.Brake = 1.0f;
    }
    vehicle.step( deltaTime, input, collision );
    carWorld = vehicle.getWorldMatrix();
    updateWheels();
    streamWorld( carWorld.getTranslation() );
    camera = chase.update( deltaTime, carWorld, vehicle.getSpeedKmh(),
                           [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    static const char *pCarCam = CmdLine::GetArgValue( "-carcam" );
    if ( pCarCam != nullptr ) {
        Vector3f offset( 1.5f, 1.4f, 1.5f );
        sscanf( pCarCam, "%f,%f,%f", &offset.x, &offset.y, &offset.z );
        camera.Position = carWorld.transformPoint( offset );
        camera.Rotation = ChaseCamera::LookRotation( camera.Position, carWorld.transformPoint( Vector3f( 0.0f, 0.8f, -0.3f ) ) );
        camera.Near = 0.05f;
    }
    driver.update( car, carWorld );
    if ( bPassengerSeated ) {
        passenger.update( car, carWorld );
    }

    FreeRideHud::State hudState;
    hudState.SpeedKmh = vehicle.getSpeedKmh();
    hudState.Rpm = vehicle.getRpm();
    hudState.RedLine = setup.Db.RedLine;
    hudState.RpmMax = setup.Db.RPMMax;
    hudState.Gear = vehicle.getGear();
    hudState.CameraMode = chase.getMode();
    hudState.OdometerKm = vehicle.getOdometer() / 1000.0f;
    const Vector3f carPosition = carWorld.getTranslation();
    const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    const Vector3f flat = Vector3f( forward.x, 0.0f, forward.z ).normalized();
    hudState.CarX = carPosition.x;
    hudState.CarZ = carPosition.z;
    hudState.HeadingX = flat.x;
    hudState.HeadingZ = flat.z;
    hud.tick( deltaTime, hudState );

    view.setCamera( camera );
    view.setWeather( pWeather );
    view.setEnvironmentCenter( carPosition, car.isLoaded() );
    driver.computeLighting( pWeather, view.getSceneParams() );
    passenger.computeLighting( pWeather, view.getSceneParams() );
    if ( gpAudio != nullptr ) {
        const Vector3f cameraForward = EdenRotationFromQuaternion( camera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        gpAudio->setListener( camera.Position, cameraForward, Vector3f() );
    }
    view.tick( deltaTime );
    static const bool bLogDrive = CmdLine::HasArg( "-logdrive" );
    if ( bLogDrive && ( pGameInstance->getFrameIndex() % 10 ) == 0 ) {
        OTDU_LOG_INFO( "TESTCAR pos (%.2f %.2f %.2f) %.1f km/h gear %d, remaining %.2f s\n", carPosition.x, carPosition.y,
                       carPosition.z, vehicle.getSpeedKmh(), vehicle.getGear(), remaining );
    }
}

void GMTestCar::drawOffscreen() {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( !bLoaded || pRenderer == nullptr || bNo3D ) {
        return;
    }
    if ( !bEnvironmentAssigned && car.isLoaded() ) {
        if ( GPUTexture *pCube = view.getEnvironmentCube() ) {
            car.assignEnvironment( *pRenderer, pCube );
            bEnvironmentAssigned = true;
        }
    }
    view.renderOffscreen();
}

void GMTestCar::draw() {
    if ( !bLoaded ) {
        return;
    }
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( !bNo3D ) {
        view.draw();
    }
    hud.draw( gpRender->get2DRenderer(), &collision );
}

void GMTestCar::exit() {
    if ( !bLoaded ) {
        return;
    }
    // FUN_00a90730: /:IsTest false, passenger removed [CODE].
    hud.setTestDrive( false );
    hud.unload();
    driver.unload();
    passenger.unload();
    avatars.unload();
    car.unload();
    view.destroy();
    collision.clear();
    driverBank.unload();
    bLoaded = false;
}
