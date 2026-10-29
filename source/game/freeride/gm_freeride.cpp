#include "shared.h"
#include "gm_freeride.h"

#include "audio/gs_audio.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "physics/havok_collision.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"
#include "physics/vehicle_collision.h"

namespace {
// Story step 2 timer (DAT_00ede78c) [CODE].
constexpr float kFirstFreerideTime = 90.0f;
// C_FADE_FREERIDE ("fade_blanc"): fade to white [INF duration; the
// cinematic itself lasts 15.0 s, docs/re/story_spec.md 2].
constexpr float kFadeDuration = 2.0f;

Quaternionf Conjugate( const Quaternionf &q ) { return Quaternionf( -q.x, -q.y, -q.z, q.w ); }

// Rotation part of a rigid matrix -> quaternion.
Quaternionf QuaternionFromMatrix( const Matrix4x4f &m ) {
    const float m00 = m.m[0][0], m11 = m.m[1][1], m22 = m.m[2][2];
    const float trace = m00 + m11 + m22;
    Quaternionf q;
    if ( trace > 0.0f ) {
        const float s = std::sqrt( trace + 1.0f ) * 2.0f;
        q = Quaternionf( ( m.m[1][2] - m.m[2][1] ) / s, ( m.m[2][0] - m.m[0][2] ) / s, ( m.m[0][1] - m.m[1][0] ) / s, 0.25f * s );
    } else if ( m00 > m11 && m00 > m22 ) {
        const float s = std::sqrt( 1.0f + m00 - m11 - m22 ) * 2.0f;
        q = Quaternionf( 0.25f * s, ( m.m[1][0] + m.m[0][1] ) / s, ( m.m[2][0] + m.m[0][2] ) / s, ( m.m[1][2] - m.m[2][1] ) / s );
    } else if ( m11 > m22 ) {
        const float s = std::sqrt( 1.0f + m11 - m00 - m22 ) * 2.0f;
        q = Quaternionf( ( m.m[1][0] + m.m[0][1] ) / s, 0.25f * s, ( m.m[2][1] + m.m[1][2] ) / s, ( m.m[2][0] - m.m[0][2] ) / s );
    } else {
        const float s = std::sqrt( 1.0f + m22 - m00 - m11 ) * 2.0f;
        q = Quaternionf( ( m.m[2][0] + m.m[0][2] ) / s, ( m.m[2][1] + m.m[1][2] ) / s, 0.25f * s, ( m.m[0][1] - m.m[1][0] ) / s );
    }
    return q;
}

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

GMFreeRide::GMFreeRide() : GameMode( eGameMode::GM_FreeRide ) {}

GMFreeRide::~GMFreeRide() { exit(); }

bool GMFreeRide::surfaceHeight( float x, float z, float y, float &outHeight ) const {
    VehicleWorldCollision::Hit hit;
    if ( collision.raycast( Vector3f( x, y, z ), Vector3f( 0.0f, -1.0f, 0.0f ), 60.0f, hit ) ) {
        outHeight = hit.Position.y;
        return true;
    }
    return false;
}

bool GMFreeRide::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    if ( GetPlayerProfile().StoryStep == 8 ) {
        // Story step 8 "Tuto FROG" (FUN_00b10da0 case 9, docs/re/story_spec.md
        // 5.3) [CODE]: profile+0x58 = 8, FREERIDE at the dealer exit
        // (FUN_00a6a650 on ESTOL_B_1275I: the spot exit snapped on the road),
        // the exit cinematic DAT_0102582c = "C_INSTALL_FROG" of estol_b.bnk
        // (set by the purchase, FUN_00ddea60), bank pack_freerideFROG, timer
        // 300 s, GPS cleared, goal TUTO_FROG (4034 "Bank the first level with
        // the F.R.I.M."), Todd Bishop as passenger (FUN_00b10d00
        // "SPT_Todd_Bishop" -> FUN_00b84590); tick: FROG tutorial voices
        // (FUN_00b10890), done -> step 9 "Go Home". Not implemented: stub.
        bStep8Stub = true;
        const PlayerProfile &profile = GetPlayerProfile();
        const OwnedCar *pCar = profile.CurrentGarageSlot >= 0 &&
                                       static_cast<size_t>( profile.CurrentGarageSlot ) < profile.Garage.size()
                                   ? &profile.Garage[static_cast<size_t>( profile.CurrentGarageSlot )]
                                   : nullptr;
        step8Text = "Story step 8: Tuto FROG (FREERIDE) - not implemented";
        OTDU_LOG_INFO( "FREERIDE: story step 8 'Tuto FROG': current car %d (garage slot %d of %zu), money %d, exit "
                       "cinematic C_INSTALL_FROG at ESTOL_B_1275I: not implemented (hand-off stub, "
                       "docs/re/story_spec.md 5.3)\n",
                       pCar != nullptr ? pCar->IdCar : -1, profile.CurrentGarageSlot, profile.Garage.size(),
                       profile.Money );
        gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
        return true;
    }
    // FUN_00b10da0 case 3 (0xc0c0): profile+0x58 = 2, current car 0x183.
    GetPlayerProfile().StoryStep = 2;
    if ( !outro.locate() ) {
        return false;
    }
    const int32_t radius = CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1;
    if ( !view.initialize( "ibiza", outro.getHousePosition(), radius ) ) {
        return false;
    }
    // Island ambient effects of the streamed sectors (-F.pmi + fx/vfx_env.bnk,
    // Particles service FUN_00c38b10); -noworldfx: off.
    view.getWorld().setEffectsEnabled( !CmdLine::HasArg( "-noworldfx" ) );
    roads.load( "ibiza" );
    // In-game weather: game_weathers.xmb time of day presets; the clock is
    // [ENC] -> "14" [INF].
    gameWeathers.load( ( gpFile->getBankFolder() + "islands/ibiza/fx/game_weathers.xmb" ).c_str() );
    const char *pWeatherName = CmdLine::GetArgValue( "-weather" );
    pWeather = gameWeathers.find( pWeatherName != nullptr ? pWeatherName : "14" );
    if ( pWeather == nullptr && !gameWeathers.getPresets().empty() ) {
        pWeather = &gameWeathers.getPresets().front();
    }
    OTDU_LOG_INFO( "FREERIDE: weather '%s'\n", pWeather != nullptr ? pWeather->Name.c_str() : "(none)" );
    view.setWeather( pWeather );
    if ( !outro.load( view ) ) {
        OTDU_LOG_WARN( "FREERIDE: house spot / C_FIRST_OUTRO not available\n" );
    }
    // Step 2 enter: FUN_00b9ff50("Scenarics\\Cinematics", "\\intro\\fade_freeride")
    // into the step scene slot (C_FADE_FREERIDE).
    fade.load();
    car.load( carDesc );

    // Driving collision: sector .shk / -R.shk + the spot collision
    // (eph_l3di_sc_*.shk, house frame) + the heightfield.
    collision.setHeightFunction(
        [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    // Library.dhk models (walls, poles, trees...) placed by the sector V / O .pmi lists.
    collision.loadLibrary( view.getWorld().getCommonBank() );
    collision.streamAround( outro.getHousePosition(), 1 );
    for ( const BankEntry *pEntry : outro.getBank().findEntriesByExtension( "shk" ) ) {
        HavokCollision spotCollision;
        if ( LoadHavokCollision( pEntry->pData, pEntry->Size, spotCollision, true ) ) {
            collision.addCollision( spotCollision, outro.getSceneOrigin(), true );
            OTDU_LOG_INFO( "FREERIDE: spot collision %s: %zu triangles, %zu boxes\n", pEntry->Filename.c_str(),
                           spotCollision.getNumTriangles(), spotCollision.Boxes.size() );
        }
    }
    setupVehicle();
    chase.load( setup.CameraId != 0 ? setup.CameraId : setup.Db.CarId );
    hud.load( hudFileName() );
    // Accessories menu, roof, windows, blinkers (docs/re/accessories_spec.md).
    accessories.load( carDesc.FileName, car );

    view.setDrawCallback( [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        outro.draw( renderer );
        if ( pass != EdenSceneView::ePass::Environment && pass != EdenSceneView::ePass::Reflection && car.isLoaded() ) {
            car.draw( renderer, carWorld, view.getWeather(), bEnvironmentAssigned );
        }
    } );

    bLoaded = true;
    if ( CmdLine::HasArg( "-skipcine" ) || !outro.isLoaded() ) {
        startDriving();
    } else {
        // FUN_00a7cc70: C_FIRST_OUTRO, the player car under cinematic
        // control (FUN_00c41100(1)), HUD hidden (listener event 5).
        outro.start( static_cast<float>( CmdLine::GetArgInt( "-cinetime", 0 ) ) );
        state = eState::Outro;
        carWorld = outroCarWorld();
        hud.setVisible( false );
    }
    return true;
}

std::string GMFreeRide::hudFileName() const {
    // DBCarPhysicsData _hud_file_name (gauge bank name).
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
            if ( StringCmpI( car["_car_file_name"].asString().c_str(), carDesc.FileName.c_str() ) == 0 ) {
                return car["_hud_file_name"].asString();
            }
        }
    }
    return carDesc.FileName;
}


namespace {
// -vehtest: straight line runs of the player car on a flat road plane
// (friction 0.7, the road material) with the driving model, to compare with
// DBCarPhysicsData _acceleration_0_100_kmh / _max_theorical_speed /
// _braking_100_0 (debug only, no game state is touched).
void RunVehicleTest( const VehicleDynamics &templateVehicle ) {
    VehicleWorldCollision flat;
    flat.setFlatGround( true, 0.0f, 0.7f );
    const VehicleSetup &setup = templateVehicle.getSetup();
    const float dt = 1.0f / 60.0f;
    auto logState = [&]( const char *pTag, float t, const VehicleDynamics &v ) {
        const std::vector<VehicleDynamics::WheelState> &w = v.getWheels();
        float slipFront = 0.0f, slipRear = 0.0f, loadFront = 0.0f, loadRear = 0.0f;
        for ( size_t i = 0; i < w.size(); i++ ) {
            const bool bFront = v.getWheelDescs()[i].bFront;
            ( bFront ? slipFront : slipRear ) += 0.5f * w[i].SlipRatio;
            ( bFront ? loadFront : loadRear ) += w[i].Load;
        }
        OTDU_LOG_INFO( "VEHTEST %s t %6.2f v %6.1f km/h rpm %5.0f gear %d slip f %.3f r %.3f load f %.0f r %.0f N y %.3f\n", pTag, t,
                       v.getSpeedKmh(), v.getRpm(), v.getGear(), slipFront, slipRear, loadFront, loadRear, v.getPosition().y );
    };
    // Acceleration from rest, full throttle.
    VehicleDynamics v = templateVehicle;
    v.reset( Vector3f( 0.0f, 0.12f, 0.0f ), Quaternionf(), Vector3f() );
    VehicleDynamics::Input input;
    for ( int32_t i = 0; i < 60; i++ ) { // settle 1 s
        v.step( dt, input, flat );
    }
    input.Throttle = 1.0f;
    float t100 = -1.0f, t200 = -1.0f, top = 0.0f, tTop = 0.0f, d400 = -1.0f;
    const Vector3f start = v.getPosition();
    for ( int32_t i = 0; i < 120 * 60; i++ ) {
        v.step( dt, input, flat );
        const float t = ( i + 1 ) * dt;
        const float kmh = v.getSpeedKmh();
        if ( t100 < 0.0f && kmh >= 100.0f ) {
            t100 = t;
        }
        if ( t200 < 0.0f && kmh >= 200.0f ) {
            t200 = t;
        }
        if ( d400 < 0.0f && ( v.getPosition() - start ).length() >= 402.3f ) {
            d400 = t;
        }
        if ( kmh > top + 0.05f ) {
            top = kmh;
            tTop = t;
        }
        if ( ( i % 30 ) == 29 && t <= 40.0f ) {
            logState( "accel", t, v );
        }
        if ( t <= 6.0f && ( i % 3 ) == 2 ) {
            OTDU_LOG_INFO( "VEHTEST dt t %5.2f v %6.1f %s\n", t, v.getSpeedKmh(), v.getDebugState().c_str() );
        }
    }
    OTDU_LOG_INFO( "VEHTEST result '%s': 0-100 km/h %.2f s (DB %.2f), 0-200 %.2f s, 402 m %.2f s (DB %.2f), top %.1f km/h at %.1f s (DB %.0f)\n",
                   setup.Db.FileName.c_str(), t100, setup.Accel0To100, t200, d400, setup.QuarterMile, top, tTop,
                   setup.MaxTheoricalSpeed );
    // Braking from 100 km/h.
    v = templateVehicle;
    v.reset( Vector3f( 0.0f, 0.12f, 0.0f ), Quaternionf(), Vector3f( 0.0f, 0.0f, 100.0f / 3.6f ) );
    input = VehicleDynamics::Input();
    input.Brake = 1.0f;
    const Vector3f brakeStart = v.getPosition();
    float tStop = -1.0f;
    for ( int32_t i = 0; i < 20 * 60; i++ ) {
        v.step( dt, input, flat );
        if ( ( i % 15 ) == 14 ) {
            logState( "brake", ( i + 1 ) * dt, v );
        }
        if ( v.getSpeedKmh() < 0.5f ) {
            tStop = ( i + 1 ) * dt;
            break;
        }
    }
    OTDU_LOG_INFO( "VEHTEST result '%s': 100-0 km/h %.1f m in %.2f s (DB _braking_100_0 %.0f, _brakingdist %.0f)\n",
                   setup.Db.FileName.c_str(), ( v.getPosition() - brakeStart ).length(), tStop, setup.Braking100To0,
                   setup.BrakingDistance );
    // Steady-state cornering: fixed steering input, throttle held by a
    // speed loop (debug driver), yaw rate / lateral acceleration after 8 s.
    for ( const float steering : { 0.25f, 0.5f, 1.0f } ) {
        for ( const float targetKmh : { 60.0f, 100.0f } ) {
            v = templateVehicle;
            v.reset( Vector3f( 0.0f, 0.12f, 0.0f ), Quaternionf(), Vector3f( 0.0f, 0.0f, targetKmh / 3.6f ) );
            input = VehicleDynamics::Input();
            float yawRate = 0.0f, lateral = 0.0f, speedSum = 0.0f;
            int32_t samples = 0;
            for ( int32_t i = 0; i < 12 * 60; i++ ) {
                const float kmh = v.getSpeedKmh();
                input.Steering = steering;
                input.Throttle = Clamp( ( targetKmh - kmh ) * 0.2f + 0.3f, 0.0f, 1.0f );
                input.Brake = kmh > targetKmh + 8.0f ? 0.3f : 0.0f;
                const Quaternionf before = v.getOrientation();
                v.step( dt, input, flat );
                if ( i >= 8 * 60 ) {
                    const Vector3f f0 = before.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
                    const Vector3f f1 = v.getOrientation().rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
                    const float yaw = std::atan2( f0.x * f1.z - f0.z * f1.x, f0.x * f1.x + f0.z * f1.z ) / dt;
                    yawRate += yaw;
                    lateral += yaw * v.getVelocity().length();
                    speedSum += kmh;
                    samples++;
                }
            }
            yawRate /= Max( samples, 1 );
            lateral /= Max( samples, 1 );
            OTDU_LOG_INFO( "VEHTEST corner '%s': steer %.2f target %.0f km/h -> %.1f km/h, yaw %.3f rad/s, lateral %.2f m/s2 "
                           "(%.2f g), radius %.1f m, steer angle %.2f deg, gear %d\n",
                           setup.Db.FileName.c_str(), steering, targetKmh, speedSum / Max( samples, 1 ), yawRate, lateral,
                           lateral / 9.81f, std::abs( yawRate ) > 1e-3f ? v.getVelocity().length() / std::abs( yawRate ) : 0.0f,
                           v.getSteerAngle() * 57.29578f, v.getGear() );
        }
    }
}
} // namespace

void GMFreeRide::setupVehicle() {
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
            node.bFront = hub.z > 0.0f; // car frame: +z forward, +x left
            node.bLeft = hub.x > 0.0f;
            wheelNodes.push_back( node );
            VehicleDynamics::WheelDesc wheel;
            wheel.HubLocal = hub;
            wheel.bFront = node.bFront;
            wheel.bLeft = node.bLeft;
            wheel.Radius = carDesc.TireRadius;
            wheels.push_back( wheel );
            OTDU_LOG_INFO( "FREERIDE: wheel %s hub (%.3f, %.3f, %.3f) %s %s\n", nodes[i].Name.c_str(), hub.x, hub.y,
                           hub.z, wheel.bFront ? "front" : "rear", wheel.bLeft ? "left" : "right" );
        }
    }
    if ( wheels.empty() ) {
        // No model: wheels from the database (track, wheelbase [INF]).
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
    vehicle.setLogContacts( CmdLine::HasArg( "-logcontacts" ) );
    if ( CmdLine::HasArg( "-vehtest" ) ) {
        RunVehicleTest( vehicle );
    }
}

Matrix4x4f GMFreeRide::outroCarWorld() {
    Vector3f position, velocity;
    Quaternionf orientation;
    if ( !outro.getCarPose( position, orientation, velocity ) ) {
        return carWorld;
    }
    if ( !car.isLoaded() ) {
        return Matrix4x4f::Translation( position ) * Matrix4x4f::FromQuaternion( orientation );
    }
    // The cinematic pose rests on the surface (Havok road / spot collision).
    return car.fitOnGround( position, Conjugate( orientation ),
                            [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } );
}

void GMFreeRide::startDriving() {
    // End of C_FIRST_OUTRO (CinEnd 3: end pose; listener event 3): the car
    // keeps the last cinematic pose and the track velocity [INF, hand-off
    // ENC], the game camera returns (FUN_00ca18b0).
    Vector3f position, velocity;
    Quaternionf orientation;
    if ( outro.isLoaded() && outro.getCarPose( outro.getDuration(), position, orientation, velocity ) ) {
        const Matrix4x4f fitted =
            car.isLoaded()
                ? car.fitOnGround( position, Conjugate( orientation ),
                                   [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } )
                : Matrix4x4f::Translation( position ) * Matrix4x4f::FromQuaternion( orientation );
        position = fitted.getTranslation();
        orientation = QuaternionFromMatrix( fitted );
    } else {
        position = outro.getHousePosition();
    }
    if ( CmdLine::HasArg( "-startstopped" ) ) {
        velocity = Vector3f();
    }
    collision.streamAround( position, 1 );
    vehicle.reset( position + Vector3f( 0.0f, 0.05f, 0.0f ), orientation, velocity );
    carWorld = vehicle.getWorldMatrix();
    chase.reset( position, Matrix4x4f::FromQuaternion( orientation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
    // Step 2 tick at FREERIDE without spot cinematic: /:IsWalkmode false,
    // pop_goal 0x13ae (5038 "Enjoy!"), TUTO_ENJOY, roof open, timer 90 s.
    hud.setVisible( true );
    hud.showGoal( LocStr( 5038, "Enjoy!" ) );
    // -steptimer s: test shortcut (the story timer is 90 s, DAT_00ede78c).
    stepTimer = CmdLine::HasArg( "-steptimer" ) ? static_cast<float>( CmdLine::GetArgInt( "-steptimer", 90 ) )
                                                : kFirstFreerideTime;
    state = eState::Drive;
    setShadowConfig( "FREERIDE" );
    OTDU_LOG_INFO( "FREERIDE: driving from (%.2f, %.2f, %.2f) at %.1f km/h, story timer %.0f s\n", position.x, position.y,
                   position.z, velocity.length() * 3.6f, stepTimer );
}

void GMFreeRide::setShadowConfig( const std::string &name ) {
    if ( name != shadowConfig ) {
        shadowConfig = name;
        view.setShadowConfig( EdenShadows::FindConfig( name ) );
    }
}

void GMFreeRide::streamWorld( const Vector3f &position ) {
    const int32_t key = SectorKey( position );
    if ( key == lastSectorKey ) {
        return;
    }
    lastSectorKey = key;
    view.getWorld().loadAround( position, CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1 );
    collision.streamAround( position, 1 );
}

void GMFreeRide::updateWheels() {
    if ( !car.isLoaded() || state == eState::Outro ) {
        return;
    }
    EdenModel &model = car.getModel();
    const std::vector<VehicleDynamics::WheelState> &states = vehicle.getWheels();
    for ( size_t i = 0; i < wheelNodes.size() && i < states.size(); i++ ) {
        const WheelNode &node = wheelNodes[i];
        // Car frame: steering about +y (positive = left), rolling about +x.
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

void GMFreeRide::tick( float deltaTime ) {
    if ( bStep8Stub ) {
        return;
    }
    if ( !bLoaded ) {
        return;
    }
    switch ( state ) {
    case eState::Outro: {
        outro.tick( deltaTime );
        outro.getCamera( camera );
        carWorld = outroCarWorld();
        setShadowConfig( outro.getShadowConfig() );
        // Skippable when FUN_00425d70 (CinProps |= 4) [INF: always].
        if ( !outro.isPlaying() || gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            startDriving();
        }
        break;
    }
    case eState::Drive:
    case eState::Fade: {
        VehicleDynamics::Input input;
        if ( state == eState::Drive ) {
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
        lastBrakeInput = input.Brake;
        vehicle.step( deltaTime, input, collision );
        carWorld = vehicle.getWorldMatrix();
        streamWorld( carWorld.getTranslation() );
        camera = chase.update( deltaTime, carWorld, vehicle.getSpeedKmh(),
                               [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
        // Story step 2 tick: timer, then C_FADE_FREERIDE -> step 3.
        if ( state == eState::Drive ) {
            stepTimer -= deltaTime;
            if ( stepTimer < 0.0f ) {
                state = eState::Fade;
                fadeTime = 0.0f;
                hud.closeGoal();
                // C_FADE_FREERIDE: listener story +0x14 (FUN_00b10580),
                // +0x628 = 1, +0xb54 = 0, start (FUN_00b40180).
                fade.start( carWorld );
                OTDU_LOG_INFO( "FREERIDE: 90 s elapsed -> C_FADE_FREERIDE (%.2f s)\n",
                               fade.isLoaded() ? fade.getDuration() : 0.0f );
            }
        } else {
            fadeTime += deltaTime;
            fade.tick( deltaTime );
            // FUN_00b10580 (event 7 of C_FADE_FREERIDE): story +0x700 = 1 ->
            // next tick vfunc8(3): story step 3 "Meeting Tess" (FUN_00b10da0
            // case 4: profile+0x58 = 3, FUN_00b105d0, car 0x183, CINE request
            // kind 1 with the story callback, mode 0x1f CINE). Without the
            // bank: at the end of the white fade.
            const bool bEnded = fade.isLoaded() ? fade.isEnded() : fadeTime >= kFadeDuration;
            if ( bEnded && !bCineRequested ) {
                bCineRequested = true;
                GetPlayerProfile().StoryStep = 3;
                GetCineRequest().Kind = 1;
                GetCineRequest().bStoryCallback = true;
                OTDU_LOG_INFO( "FREERIDE: end of C_FADE_FREERIDE -> story step 3 'Meeting Tess' (CINE)\n" );
                pGameInstance->setNextGameMode( eGameMode::GM_Cine );
            }
        }
        static const bool bLogDrive = CmdLine::HasArg( "-logdrive" );
        if ( bLogDrive && ( pGameInstance->getFrameIndex() % 10 ) == 0 ) {
            const Vector3f p = carWorld.getTranslation();
            OTDU_LOG_INFO( "DRIVE pos (%.2f %.2f %.2f) %.1f km/h rpm %.0f gear %d steer %.3f contacts %d%d%d%d\n", p.x, p.y,
                           p.z, vehicle.getSpeedKmh(), vehicle.getRpm(), vehicle.getGear(), vehicle.getSteerAngle(),
                           vehicle.getWheels().size() > 0 && vehicle.getWheels()[0].bContact,
                           vehicle.getWheels().size() > 1 && vehicle.getWheels()[1].bContact,
                           vehicle.getWheels().size() > 2 && vehicle.getWheels()[2].bContact,
                           vehicle.getWheels().size() > 3 && vehicle.getWheels()[3].bContact );
        }
        break;
    }
    }
    updateWheels();
    // FUN_00929be0 (menus / accessory keys, driving only) and the car roof /
    // windows / blinker state (FUN_00c67c60) every frame.
    accessories.tick( deltaTime, state == eState::Drive, car, hud );
    // Car lights (docs/re/car_lights_spec.md): brake (+0x1d0) / reverse
    // (+0x1d1) state from the vehicle [INF: brake pedal > 0, reverse gear],
    // blinker timer (FUN_00c57ef0).
    car.setBraking( state != eState::Outro && lastBrakeInput > 0.0f );
    car.setReverse( state != eState::Outro && vehicle.getGear() == -1 );
    car.updateLights( deltaTime );
    // Projected headlight (HEADLIGHTS pass, docs/re/car_lights_spec.md 8).
    {
        std::vector<EdenSceneView::HeadlightSpot> spots;
        EdenSceneView::HeadlightSpot spot;
        if ( state != eState::Outro && car.getHeadlightSpot( carWorld, camera.Position, spot ) ) {
            spots.push_back( spot );
        }
        view.setHeadlightSpots( spots );
    }
    // Tunnel (FUN_00c62a70, docs/re/car_lights_spec.md 6).
    car.setTunnelDistance( roads.getCarTunnelDistance( carWorld.getTranslation(), camera.Position ), deltaTime );

    FreeRideHud::State hudState;
    hudState.SpeedKmh = state == eState::Outro ? 0.0f : vehicle.getSpeedKmh();
    hudState.Rpm = state == eState::Outro ? setup.Db.RPMMin : vehicle.getRpm();
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
    if ( gpAudio != nullptr ) {
        const Vector3f cameraForward =
            EdenRotationFromQuaternion( camera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        gpAudio->setListener( camera.Position, cameraForward, Vector3f() );
    }
    view.tick( deltaTime );
}

void GMFreeRide::drawOffscreen() {
    if ( bStep8Stub ) {
        return;
    }
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !bLoaded || pRenderer == nullptr ) {
        return;
    }
    // -no3d: physics / HUD tests without the (software) 3D rendering.
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( bNo3D ) {
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

void GMFreeRide::draw() {
    if ( bStep8Stub ) {
        Renderer2D &renderer = gpRender->get2DRenderer();
        renderer.begin();
        renderer.drawText( gpFlash->getTitleFont(), step8Text.c_str(), 640.0f, 340.0f, 28.0f, 0xffffffffu,
                           eTextAlign::TA_Center );
        renderer.end();
        return;
    }
    if ( !bLoaded ) {
        return;
    }
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( !bNo3D ) {
        view.draw();
    }
    Renderer2D &renderer = gpRender->get2DRenderer();
    hud.draw( renderer, &collision );
    if ( state == eState::Fade ) {
        const uint32_t alpha = static_cast<uint32_t>( Clamp( fadeTime / kFadeDuration, 0.0f, 1.0f ) * 255.0f );
        renderer.begin();
        renderer.drawRect( Rect2D( -10000.0f, -10000.0f, 20000.0f, 20000.0f ), 0x00ffffffu | ( alpha << 24 ) );
        renderer.end();
    }
}

void GMFreeRide::exit() {
    if ( !bLoaded ) {
        return;
    }
    hud.unload();
    fade.unload();
    outro.unload();
    car.unload();
    view.destroy();
    collision.clear();
    roads.unload();
    bLoaded = false;
}

void GMFreeRide::drawDebug() {
    if ( ImGui::Begin( "FREERIDE" ) ) {
        const Vector3f p = carWorld.getTranslation();
        ImGui::Text( "state %d, story timer %.1f s", static_cast<int32_t>( state ), stepTimer );
        ImGui::Text( "car (%.1f, %.1f, %.1f) %.1f km/h, %.0f rpm, gear %d", p.x, p.y, p.z, vehicle.getSpeedKmh(),
                     vehicle.getRpm(), vehicle.getGear() );
        ImGui::Text( "collision: %zu sectors, %zu triangles, camera mode %d", collision.getNumSectors(),
                     collision.getNumTriangles(), chase.getMode() );
        for ( const VehicleDynamics::WheelState &wheel : vehicle.getWheels() ) {
            ImGui::Text( "wheel %d load %.0f N comp %.3f slip %.3f / %.3f", wheel.bContact, wheel.Load, wheel.Compression,
                         wheel.SlipRatio, wheel.SlipAngle );
        }
    }
    ImGui::End();
}
