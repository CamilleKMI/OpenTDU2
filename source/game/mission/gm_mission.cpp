#include "shared.h"
#include "gm_mission.h"

#include "audio/gs_audio.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/avatarchoice/intro_cinematic.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "physics/havok_collision.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"

namespace {
// Story step 4 timers (FUN_00b10da0 case 5) [CODE]: DAT_00f90bc8 40.0 (set in
// state 4), double DAT_00f4a208 25.0 (family 5 while above), DAT_00edd53c
// 25.0 (period of family 6).
constexpr float kFirstTimer = 40.0f;
constexpr float kFamily5Above = 25.0f;
constexpr float kReactionPeriod = 25.0f;
// The car collision callback FUN_00a15530 -> FUN_00a154d0: FUN_00a15430(0, 5)
// at story step 4 (impact01..05) [CODE]; the impact strength that raises
// the Havok callback is not known [INF: 2 m/s of chassis approach speed].
constexpr float kImpactSpeed = 2.0f;
// ECH_IA_1243I: FUN_00b31490(0xa87b7777, 0x315f4149) in the story tick [CODE].
// FUN_00abb560: event type (+0x40) -> short name of the "E%02d_%s" handlers
// and the C_INTRO / C_WIN / C_LOOSE_E%02d_%s cinematics (strings 0xf19744..)
// [CODE].
const char *EventShortName( int32_t type ) {
    switch ( type ) {
    case 1: return "ADR";
    case 2: return "TIM";
    case 3: return "SHA";
    case 4: return "FIL";
    case 5: return "CHA";
    case 6: return "SPE";
    case 7: return "DRI";
    case 8:
    case 14:
    case 15: return "CON";
    case 9: return "CTI";
    case 10: return "CTR";
    case 12: return "JAN";
    case 13: return "TESS";
    default: return "";
    }
}
constexpr const char *kTruckSpot = "ECH_IA_1243I";

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

// Heading quaternion (car frame +z forward) towards a flat direction:
// R_y(atan2(x, z)) (0, 0, 1) = direction.
Quaternionf HeadingTowards( const Vector3f &direction ) {
    return Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), std::atan2( direction.x, direction.z ) );
}
} // namespace

GMMission::GMMission() : GameMode( eGameMode::GM_Mission ) {}

GMMission::~GMMission() { exit(); }

bool GMMission::loadEvent() {
    // DBEvent row of the story event (FUN_00abba10: profile event list by
    // _eventfile; the row of TESSEVEN is DBEvent[21]) [CODE + DATA]:
    // _eventtype 13, _pnj1 0x293 = DBPNJ STO_Tess_Phone (Tess with the
    // W_PN_G_Phone_TESS accessory in the glasses slot).
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBEvent" ); i++ ) {
            XmbValue row = root.getArrayElement( "DBEvent", i );
            if ( row["_eventfile"].asString() != event.File ) {
                continue;
            }
            event.Type = static_cast<int32_t>( row["_eventtype"].asInt() );
            const uint64_t pnj = row["_pnj1"].asUInt64();
            for ( uint32_t j = 0; j < root.getArraySize( "DBPNJ" ); j++ ) {
                XmbValue pnjRow = root.getArrayElement( "DBPNJ", j );
                if ( pnjRow["_hashcode"].asUInt64() == pnj ) {
                    event.PnjConfig = pnjRow["_configid"].asString();
                    break;
                }
            }
            OTDU_LOG_INFO( "MISSION: DBEvent %s type %d pnj 0x%llx (%s)\n", event.File.c_str(), event.Type,
                           static_cast<unsigned long long>( pnj ), event.PnjConfig.c_str() );
            break;
        }
    }
    const std::string igePath = gpFile->getBankFolder() + "ig2/" + ToLower( event.File ) + ".ig2";
    if ( !ige.load( igePath.c_str() ) ) {
        OTDU_LOG_WARN( "MISSION: '%s' not readable\n", igePath.c_str() );
        return false;
    }
    OTDU_LOG_INFO( "MISSION: %s version 0x%x, %zu parameters (gmth %lld, gmpm 0x%llx, leme %lld), %u entities, %zu "
                   "checkpoints\n",
                   igePath.c_str(), ige.getVersion(), ige.getParams().size(),
                   static_cast<long long>( ige.getInt( IgeFile::MakeTag( "gmth" ) ) ),
                   static_cast<long long>( ige.getInt( IgeFile::MakeTag( "gmpm" ) ) ),
                   static_cast<long long>( ige.getInt( IgeFile::MakeTag( "leme" ) ) ), ige.getNumEntities(),
                   ige.getCheckpoints().size() );
    for ( const IgeFile::Checkpoint &cp : ige.getCheckpoints() ) {
        OTDU_LOG_INFO( "MISSION: checkpoint id %u (%u %u %u) at (%.2f, %.2f, %.2f) forward (%.3f, %.3f, %.3f) width %.1f\n",
                       cp.Id, cp.Fields[0], cp.Fields[1], cp.Fields[2], cp.Position.x, cp.Position.y, cp.Position.z,
                       cp.Forward.x, cp.Forward.y, cp.Forward.z, cp.Width );
    }
    // FUN_00a24960: gmth 7 -> "pack_events%02d" of the event type, loaded from
    // "Scenarics\\Packs" into mode +0x1b53a0 (the mission bank) [CODE].
    char packName[32];
    snprintf( packName, sizeof( packName ), "pack_events%02d", event.Type );
    const std::string packPath = gpFile->getBankFolder() + "scenarics/packs/" + packName + ".bnk";
    if ( !pack.load( packPath.c_str() ) ) {
        OTDU_LOG_WARN( "MISSION: '%s' not found\n", packPath.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( pack, pack.getPath().c_str() );
    packHashcodes.load( pack );
    const BankEntry *pEntities = pack.findEntry( "entities.xmb" );
    if ( pEntities == nullptr || !packEntities.load( pEntities->pData, pEntities->Size ) ) {
        return false;
    }
    packSounds.load( pack );
    // FUN_00a15430: handler "E%02d_%s" (event type, FUN_00abb560 name):
    // E13_TESS, present in pack_events13 [CODE + DATA].
    char handler[32];
    snprintf( handler, sizeof( handler ), "E%02d_%s", event.Type, EventShortName( event.Type ) );
    reactions.load( pack, packHashcodes, packEntities, handler );
    return true;
}

void GMMission::startPose( Vector3f &outPosition, Quaternionf &outOrientation ) const {
    // State 2 (Loading_Map) waits for the world streaming at the start
    // position (+0x1b5220) [CODE]; the car is put at the first checkpoint of
    // the script (the start gate: id 0x2cb at the hotel) [INF: the
    // placement call is not in the functions read], heading along its
    // forward row.
    const std::vector<IgeFile::Checkpoint> &cps = ige.getCheckpoints();
    if ( cps.empty() ) {
        outPosition = Vector3f( 11073.13f, 3.588f, 17203.56f ); // E_HOTELA_1266I second transform
        outOrientation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), -0.8548f );
        return;
    }
    const IgeFile::Checkpoint &start = cps.front();
    outPosition = start.Position;
    outOrientation = HeadingTowards( Vector3f( start.Forward.x, 0.0f, start.Forward.z ) );
    if ( CmdLine::HasArg( "-startnearfinish" ) && cps.size() > 1 ) {
        // Test shortcut: d m before the finish gate, facing it.
        const IgeFile::Checkpoint &finish = cps.back();
        const float distance = static_cast<float>( CmdLine::GetArgInt( "-startnearfinish", 40 ) );
        const Vector3f along = Vector3f( finish.Forward.x, 0.0f, finish.Forward.z ).normalized();
        outPosition = finish.Position - along * distance;
        outOrientation = HeadingTowards( along );
    }
}

bool GMMission::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    // Story step 4 enter (FUN_00b10da0 case 5): profile+0x58 = 4, car 0x183
    // current, mission = FUN_00abba10(profile events, "TESSEVEN") selected
    // (FUN_00b12d20), DAT_010257f8 = "TESSEVEN", DAT_01025818 = 2, mode 4.
    PlayerProfile &profile = GetPlayerProfile();
    profile.StoryStep = 4;
    if ( profile.AvatarConfig.empty() ) {
        const char *pAvatar = CmdLine::GetArgValue( "-avatar" );
        profile.AvatarConfig = GetStoryAvatarConfig( pAvatar != nullptr ? pAvatar : "AVT_Male_Intro" );
    }
    if ( gpAudio != nullptr ) {
        gpAudio->setGameMode( "MISSION" );
    }
    if ( !loadEvent() ) {
        return false;
    }
    Vector3f startPosition;
    Quaternionf startOrientation;
    startPose( startPosition, startOrientation );
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
    collision.setHeightFunction(
        [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    collision.loadLibrary( view.getWorld().getCommonBank() );
    collision.streamAround( startPosition, 1 );
    setupVehicle();
    chase.load( setup.CameraId != 0 ? setup.CameraId : setup.Db.CarId );
    // DBCarPhysicsData _hud_file_name (gauge bank), as FREERIDE.
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
    // GamePC.cpr mode 4 flash movies: HUD, GAUGES, RESULTS, DASHBRD [DATA];
    // HUD + GAUGES as FREERIDE (RESULTS: state 9, skipped by the story at
    // once; DASHBRD: the in-car screen, not drawn, as FREERIDE).
    hud.load( hudFile );
    hud.setVisible( true );
    if ( ige.getCheckpoints().size() > 1 ) {
        const Vector3f target = ige.getCheckpoints().back().Position;
        hud.setTarget( true, target.x, target.z );
    }

    // Avatars: the player driver (profile avatar, the valet outfit of step 3)
    // and the event PNJ as passenger (FUN_00b84590(3000, ...) in state 4).
    avatars.load();
    faceFx.load();
    faceFx.addAnimSets( pack );
    const std::string driverPath = gpFile->getBankFolder() + "scenarics/avatars/avatarsdriver.bnk";
    if ( driverBank.load( driverPath.c_str() ) ) {
        if ( !driver.load( avatars, profile.AvatarConfig.c_str(), SeatedAvatar::eSeat::Driver, driverBank ) ) {
            OTDU_LOG_WARN( "MISSION: driver avatar '%s' not loaded\n", profile.AvatarConfig.c_str() );
        }
    } else {
        OTDU_LOG_WARN( "MISSION: '%s' not found (no seated avatars)\n", driverPath.c_str() );
    }

    // C_TRUCK at the Solar Crown spot (spots.bin first transform) [INF frame].
    IntroCinematic::SpotRecord spot;
    if ( IntroCinematic::ReadSpot( kTruckSpot, spot ) ) {
        const Matrix4x4f frame = Matrix4x4f::Translation( spot.Position ) *
                                 Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ),
                                                                                         spot.Heading ) );
        truck.load( view, pack, packHashcodes, packEntities, "C_TRUCK", frame );
    } else {
        OTDU_LOG_WARN( "MISSION: spot %s not found\n", kTruckSpot );
    }

    view.setDrawCallback( [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        truck.draw( renderer );
        if ( pass == EdenSceneView::ePass::Environment || pass == EdenSceneView::ePass::Reflection ) {
            return;
        }
        if ( car.isLoaded() ) {
            car.draw( renderer, carWorld, view.getWeather(), bEnvironmentAssigned );
        }
        // Cockpit view (mode 23): the driver's head is not drawn [INF, as
        // IntroCinematic].
        driver.draw( renderer, chase.getMode() != 23 );
        if ( bPassengerSeated ) {
            passenger.draw( renderer );
        }
    } );

    vehicle.reset( startPosition + Vector3f( 0.0f, 0.3f, 0.0f ), startOrientation, Vector3f() );
    carWorld = vehicle.getWorldMatrix();
    previousCarPosition = vehicle.getPosition();
    chase.reset( startPosition, Matrix4x4f::FromQuaternion( startOrientation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
    camera = chase.update( 0.0f, carWorld, 0.0f,
                           [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    bLoaded = true;
    setMechState( eMechState::LoadingMap );
    const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    OTDU_LOG_INFO( "MISSION: story step 4 'Driving Tess', start (%.2f, %.2f, %.2f) forward (%.2f, %.2f, %.2f), "
                   "avatar %s, passenger %s\n",
                   startPosition.x, startPosition.y, startPosition.z, forward.x, forward.y, forward.z,
                   profile.AvatarConfig.c_str(), event.PnjConfig.c_str() );
    return true;
}

void GMMission::setupVehicle() {
    // Same rig as GMFreeRide::setupVehicle (D_RIM_xx wheel nodes).
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
}

void GMMission::updateWheels() {
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

void GMMission::streamWorld( const Vector3f &position ) {
    const int32_t key = SectorKey( position );
    if ( key == lastSectorKey ) {
        return;
    }
    lastSectorKey = key;
    view.getWorld().loadAround( position, CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1 );
    collision.streamAround( position, 1 );
}

void GMMission::setMechState( eMechState state ) {
    mechState = state;
    OTDU_LOG_INFO( "MISSION: CGameMech state %d\n", static_cast<int32_t>( state ) );
    switch ( state ) {
    case eMechState::WaitingStart:
        // State 4 enter (FUN_00a1abe0): with an event (FUN_00a12680: gmth 7)
        // and the player car: passenger = FUN_00b84590(3000 (passenger
        // state), hash 0xfa4713bc_ffa1845b, ...) from the event PNJ
        // (FUN_00cbd7a0 / FUN_0048b090 [ENC]), then FUN_00b9fa00(car): car
        // +0x79cc = passenger, FUN_00b8d960(3000) [CODE].
        bPassengerSeated = driverBank.getEntries().empty()
                               ? false
                               : passenger.load( avatars, event.PnjConfig.c_str(), SeatedAvatar::eSeat::Passenger,
                                                 driverBank );
        if ( !bPassengerSeated ) {
            OTDU_LOG_WARN( "MISSION: passenger '%s' not loaded\n", event.PnjConfig.c_str() );
        }
        break;
    case eMechState::Results:
        // State 9 enter: GAUGES off, results (FUN_00a17cb0), the passenger
        // taken out of the car (FUN_00b9f950 / FUN_00b84530) [CODE].
        hud.setTarget( false );
        if ( bSuccess ) {
            creditReward();
        }
        break;
    default:
        break;
    }
}

void GMMission::creditReward() {
    // FUN_00a17980 (state 9, +0xf0 success) [CODE]: RESULTS /:nb_done,
    // /:nb_total, the reward = event record +0x5c (FUN_00a0d610 with a
    // challenge, FUN_004c3140 [ENC]) -> FUN_00abe0e0(reward, 1, 7): pending
    // money, reason 7; /:Reward = FUN_0099d810 (money text), /:event_title.
    // The event record value is the IGE parameter rewm (container +0x1030,
    // registered by FUN_00a1f2xx with loc 0x531) of tesseven.ig2: 20000
    // [DATA; the copy into the record is INF].
    const int64_t reward = ige.getInt( IgeFile::MakeTag( "rewm" ) );
    if ( reward <= 0 ) {
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    ProfileAddMoney( static_cast<int32_t>( reward ), true, 7 );
    OTDU_LOG_INFO( "MISSION: reward %lld (rewm) -> pending money %d (money %d)\n", static_cast<long long>( reward ),
                   profile.PendingMoney, profile.Money );
    // RESULTS.vmf frame 372 (results_board 'event_success'): FSCommand moup
    // (FUN_0099c490 -> FUN_00abd0d0: money panel, transfer timer 1.0 s), the
    // money clip opened [DATA + CODE]. The story tick leaves the mode in the
    // same state-9 tick (vfunc8(5)), so the RESULTS screen is not shown and
    // the 1 s counter (FUN_00abcf60) is run here at once [INF].
    ProfileStartMoneyTransfer();
    for ( int32_t i = 0; i < 120 && profile.MoneyTransferTimer >= 0.0f; i++ ) {
        ProfileTickMoney( 1.0f / 30.0f );
    }
    OTDU_LOG_INFO( "MISSION: money counter done: money %d, pending %d\n", profile.Money, profile.PendingMoney );
}

bool GMMission::checkFinish( const Vector3f &from, const Vector3f &to ) const {
    // The finish test of the racer object is not identified [ENC/INF]: the
    // car crosses the plane of the last checkpoint (normal = its forward row)
    // within its width (7.0 in tesseven, checkpoint +0x78) laterally, or comes
    // within that distance of it.
    const std::vector<IgeFile::Checkpoint> &cps = ige.getCheckpoints();
    if ( cps.size() < 2 ) {
        return false;
    }
    const IgeFile::Checkpoint &finish = cps.back();
    const float width = finish.Width > 0.0f ? finish.Width : 20.0f;
    const Vector3f flat( to.x - finish.Position.x, 0.0f, to.z - finish.Position.z );
    if ( flat.length() <= width ) {
        return true;
    }
    const float a = ( from - finish.Position ).dot( finish.Forward );
    const float b = ( to - finish.Position ).dot( finish.Forward );
    if ( ( a < 0.0f ) == ( b < 0.0f ) ) {
        return false;
    }
    const float t = a / ( a - b );
    const Vector3f crossing = from + ( to - from ) * t;
    return std::abs( ( crossing - finish.Position ).dot( finish.Right ) ) <= width;
}

void GMMission::storyTick( float deltaTime ) {
    // FUN_00b10da0 case 5, event 0xc0c1 (mode 4) [CODE].
    if ( !bStoryStarted ) {
        // this[6] == 0: FUN_00aba280(this[8] = 0x183) once (current car).
        bStoryStarted = true;
    }
    if ( mechState == eMechState::WaitingStart ) {
        // pop_goal loc 0xfc0 (4032), goal TUTO_DRIVE_TESS (DBRewardXP 178,
        // _targetspotworld "CLUB"), C_TRUCK looping without camera at
        // ECH_IA_1243I, timer 40 s.
        hud.showGoal( LocStr( 4032, "Drive Tess to the shooting location" ) );
        truck.setPlaying( true );
        storyTimer = CmdLine::HasArg( "-reactiontimer" ) ? static_cast<float>( CmdLine::GetArgInt( "-reactiontimer", 40 ) )
                                                          : kFirstTimer;
    } else if ( mechState == eMechState::Results && bSuccess && !bStoryDone ) {
        // State 9 with +0xf0: C_TRUCK stopped (FUN_00b3fc40(1)), the passenger
        // out of the car (FUN_00ccde50), story vfunc8(5).
        bStoryDone = true;
        truck.setPlaying( false );
        reactions.stop( packSounds );
        bPassengerSeated = false;
        PlayerProfile &profile = GetPlayerProfile();
        profile.StoryStep = 5;
        // Step 5 enter (case 6): profile+0x58 = 5, car 0x183 (-1, -1, -1)
        // (FUN_00aba280), CINE request kind 2 with the story callback
        // FUN_00b104d0, mode 0x1f [CODE].
        GetCineRequest().Kind = 2;
        GetCineRequest().bStoryCallback = true;
        OTDU_LOG_INFO( "MISSION: arrived at the shooting location after %.1f s -> story step 5 'Solar Crown exterior' "
                       "(CINE kind 2)\n",
                       raceTime );
        pGameInstance->setNextGameMode( eGameMode::GM_Cine );
        return;
    }
    // FUN_0045f2f0() [ENC] gates the reactions [INF: always true].
    if ( mechState != eMechState::Race || !bPassengerSeated ) {
        return;
    }
    if ( storyTimer > kFamily5Above && !reactions.isPlaying() ) {
        reactions.play( 5, 1, packSounds ); // FUN_00a15430(5, 1): toplayer01
    }
    if ( storyTimer > 0.0f ) {
        storyTimer -= deltaTime;
    } else if ( storyTimer < 0.0f ) {
        reactions.play( 6, 9, packSounds ); // FUN_00a15430(6, 9): toplayer02..05, phone01..05
        storyTimer = kReactionPeriod;
    }
}

void GMMission::tick( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    // CGameMech (FUN_00a1abe0).
    switch ( mechState ) {
    case eMechState::LoadingMap:
        // FUN_00aee280(start, 1, 1): world streamed at the start; then (gmpm
        // 0xc0c0, event) state 0xd: C_INTRO_E13_<name> is not in the pack ->
        // state 4 [CODE + DATA].
        setMechState( eMechState::WaitingStart );
        break;
    case eMechState::WaitingStart:
        // Tick: when +0x65 (start allowed) -> state 5 [CODE]; allowed after
        // one frame here (the story tick sees state 4 once; no countdown for
        // this event: /:ShowCountdown is set in state 9 only) [INF].
        if ( bStartAllowed ) {
            setMechState( eMechState::Race );
        }
        bStartAllowed = true;
        break;
    case eMechState::Race:
        raceTime += deltaTime;
        if ( checkFinish( previousCarPosition, vehicle.getPosition() ) ) {
            // FUN_00a12c60 / FUN_00a12bb0: the racer finished (state 1) and
            // none failed (state 2) -> +0xf0 = 1; -> 8 -> (FUN_00a12160) 9.
            bSuccess = true;
            setMechState( eMechState::Finished );
        }
        break;
    case eMechState::Finished:
        setMechState( eMechState::Results );
        break;
    case eMechState::Results:
        break;
    }
    previousCarPosition = vehicle.getPosition();
    storyTick( deltaTime );

    VehicleDynamics::Input input;
    if ( mechState == eMechState::Race ) {
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
    // -carcam dx,dy,dz: test view from a car-local point looking at the
    // cockpit (screenshots of the seated avatars; not a game camera).
    static const char *pCarCam = CmdLine::GetArgValue( "-carcam" );
    if ( pCarCam != nullptr ) {
        Vector3f offset( 1.5f, 1.4f, 1.5f );
        sscanf( pCarCam, "%f,%f,%f", &offset.x, &offset.y, &offset.z );
        camera.Position = carWorld.transformPoint( offset );
        camera.Rotation = ChaseCamera::LookRotation( camera.Position, carWorld.transformPoint( Vector3f( 0.0f, 0.8f, -0.3f ) ) );
        camera.Near = 0.05f;
    }
    // Car collision callback (FUN_00a15530 -> FUN_00a154d0) [CODE].
    const float impact = vehicle.takeImpactSpeed();
    if ( mechState == eMechState::Race && bPassengerSeated && impact > kImpactSpeed && !reactions.isPlaying() ) {
        reactions.play( 0, 5, packSounds );
    }

    // Seated avatars.
    reactions.tick( deltaTime, packSounds );
    driver.update( car, carWorld );
    if ( bPassengerSeated ) {
        passenger.setOverride( reactions.getClip(), reactions.getTime(), reactions.getWeight() );
        passenger.update( car, carWorld, [this]( std::vector<BoneTransform> &locals ) {
            reactions.applyFaceFx( faceFx, passenger.getAvatar(), locals );
        } );
    }
    truck.tick( deltaTime );

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
        const Vector3f cameraForward =
            EdenRotationFromQuaternion( camera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        gpAudio->setListener( camera.Position, cameraForward, Vector3f() );
    }
    view.tick( deltaTime );
    static const bool bLogDrive = CmdLine::HasArg( "-logdrive" );
    if ( bLogDrive && ( pGameInstance->getFrameIndex() % 10 ) == 0 ) {
        const std::vector<IgeFile::Checkpoint> &cps = ige.getCheckpoints();
        const float toFinish = cps.empty() ? 0.0f : ( cps.back().Position - carPosition ).length();
        OTDU_LOG_INFO( "DRIVE pos (%.2f %.2f %.2f) %.1f km/h gear %d, finish %.1f m, reaction %s, timer %.1f\n",
                       carPosition.x, carPosition.y, carPosition.z, vehicle.getSpeedKmh(), vehicle.getGear(), toFinish,
                       reactions.getName( reactions.getCurrent() ).c_str(), storyTimer );
    }
}

void GMMission::drawOffscreen() {
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

void GMMission::draw() {
    if ( !bLoaded ) {
        return;
    }
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( !bNo3D ) {
        view.draw();
    }
    hud.draw( gpRender->get2DRenderer(), &collision );
}

void GMMission::exit() {
    if ( !bLoaded ) {
        return;
    }
    reactions.stop( packSounds );
    packSounds.stopAll();
    hud.unload();
    truck.unload();
    driver.unload();
    passenger.unload();
    faceFx.unload();
    avatars.unload();
    car.unload();
    view.destroy();
    collision.clear();
    reactions.unload();
    packSounds.unload();
    if ( gpRender != nullptr && !pack.getEntries().empty() ) {
        gpRender->getTextureCache().releaseOwner( pack.getPath().c_str() );
    }
    pack.unload();
    driverBank.unload();
    bLoaded = false;
}

void GMMission::drawDebug() {
    if ( ImGui::Begin( "MISSION" ) ) {
        const Vector3f p = carWorld.getTranslation();
        ImGui::Text( "event %s, CGameMech state %d, race %.1f s, story timer %.1f", event.File.c_str(),
                     static_cast<int32_t>( mechState ), raceTime, storyTimer );
        ImGui::Text( "car (%.1f, %.1f, %.1f) %.1f km/h", p.x, p.y, p.z, vehicle.getSpeedKmh() );
        ImGui::Text( "reaction %s", reactions.getName( reactions.getCurrent() ).c_str() );
    }
    ImGui::End();
}
