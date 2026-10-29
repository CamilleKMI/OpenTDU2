#include "shared.h"
#include "seated_avatar.h"

#include "game/cinematic/cinematic_car.h"
#include "render/eden/eden_weather.h"
#include "system/cmd_line.h"

bool SeatedAvatar::load( EdenAvatarLibrary &library, const char *pConfigId, eSeat newSeat, const Bank &driverBank ) {
    unload();
    seat = newSeat;
    if ( !avatar.load( library, pConfigId ) ) {
        return false;
    }
    // avatarsdriver.bnk [DATA]: Entity__AvatarPlayer layer 0 = stance_turn.xan
    // (driver); Entity__AvatarPassenger PassengerWoman animPassenger[0] =
    // shoppinggirlcarlayer0.bas -> SHO_stand_car.xan, PassengerMan
    // hitchhikerlayer0.bas -> hit_stand_car.xan (the .xan are .anm in the bank).
    const char *pClip = seat == eSeat::Driver ? "stance_turn.anm"
                        : avatar.isWoman()    ? "sho_stand_car.anm"
                                              : "hit_stand_car.anm";
    const BankEntry *pEntry = driverBank.findEntry( pClip );
    bBaseClip = pEntry != nullptr && baseClip.parse( pEntry->pData, pEntry->Size );
    if ( !bBaseClip ) {
        OTDU_LOG_WARN( "SeatedAvatar: base clip '%s' not found in avatarsdriver.bnk\n", pClip );
    }
    offsetsCar = -2;
    return true;
}

void SeatedAvatar::unload() {
    avatar.unload();
    bBaseClip = false;
    pOverride = nullptr;
    bVisible = false;
}

void SeatedAvatar::update( const CinematicCar &car, const Matrix4x4f &carWorld,
                           const std::function<void( std::vector<BoneTransform> & )> &facial ) {
    bVisible = false;
    const bool bDriver = seat == eSeat::Driver;
    Matrix4x4f seatNode;
    if ( !avatar.isLoaded() || !car.getNodeModelTransform( bDriver ? "DVRSEAT_LR" : "PSGSEAT_LR", seatNode ) ) {
        return;
    }
    if ( offsetsCar != car.getIdCar() ) {
        offsetsCar = car.getIdCar();
        if ( !LoadCarIKOffsets( offsetsCar, avatar.isWoman(), offsets ) ) {
            OTDU_LOG_WARN( "SeatedAvatar: no IKOffsetDB record for car %d (offsets 0)\n", offsetsCar );
        }
    }
    const EdenSkeleton &skeleton = avatar.getSkeleton();
    std::vector<BoneTransform> locals( skeleton.getNumBones() );
    for ( uint32_t b = 0; b < skeleton.getNumBones(); b++ ) {
        locals[b] = skeleton.getBindLocal( b );
    }
    if ( bBaseClip ) {
        baseClip.sample( skeleton, 0.0f, true, locals );
    }
    if ( pOverride != nullptr && overrideWeight > 0.0f ) {
        std::vector<BoneTransform> clip = locals;
        pOverride->sample( skeleton, overrideTime, false, clip );
        if ( overrideWeight >= 1.0f ) {
            locals.swap( clip );
        } else {
            std::vector<BoneTransform> blended;
            BlendPoses( locals, clip, overrideWeight, blended );
            locals.swap( blended );
        }
    }

    // FUN_00b8ade0: seat group adjustment (0, y, z) then the seat node
    // position + R * offset (identity node rotation for the California
    // seats) [CODE]; FUN_0042b4f0's add-vs-set is [ENC]: added [INF].
    const Vector3f seatAdjust =
        bDriver ? Vector3f( 0.0f, offsets.driverSeatY(), offsets.driverSeatZ() )
                : Vector3f( 0.0f, offsets.passengerSeatY(), offsets.passengerSeatZ() );
    const int32_t seatIndex = bDriver ? CarIKOffsets::DvrSeat : CarIKOffsets::PsgSeat;
    const Matrix4x4f seatMatrix = Matrix4x4f::Translation( seatAdjust + offsets.Node[seatIndex] ) * seatNode;
    const Matrix4x4f seatInverse = seatMatrix.inverse();

    auto goal = [&]( const char *pNode, int32_t index, bool bInSeatGroup, Vector3f &out ) {
        Matrix4x4f node;
        if ( !car.getNodeModelTransform( pNode, node ) ) {
            return false;
        }
        Vector3f position = node.getTranslation() + node.transformVector( offsets.Node[index] );
        if ( bInSeatGroup ) {
            position += seatAdjust;
        }
        out = seatInverse.transformPoint( position );
        return true;
    };
    auto bone = [&]( const char *pName ) { return skeleton.findBone( EdenNameHash4( pName ) ); };
    Vector3f target;
    if ( goal( bDriver ? "BACK_LR_L" : "BACK_LR_R", bDriver ? CarIKOffsets::BackL : CarIKOffsets::BackR, true,
               target ) ) {
        SolveAimIK( skeleton, locals, bone( "Spine" ), bone( "IK_Back" ), target );
    }
    if ( bDriver ) {
        if ( goal( "HAND_LR_R", CarIKOffsets::HandR, false, target ) ) {
            SolveTwoBoneIK( skeleton, locals, bone( "Arm_R" ), bone( "ForeArm_R" ), bone( "IK_Right_Hand" ), target,
                            bone( "Hand_R" ) );
        }
        if ( goal( "HAND_LR_L", CarIKOffsets::HandL, false, target ) ) {
            SolveTwoBoneIK( skeleton, locals, bone( "Arm_L" ), bone( "ForeArm_L" ), bone( "IK_Left_Hand" ), target,
                            bone( "Hand_L" ) );
        }
    }
    if ( goal( bDriver ? "FOOT_LR_R" : "PSGFOOT_LR_R", bDriver ? CarIKOffsets::FootR : CarIKOffsets::PsgFootR, false,
               target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "UpLeg_R" ), bone( "Leg_R" ), bone( "IK_Right_Foot" ), target,
                        bone( "Foot_R" ) );
    }
    if ( goal( bDriver ? "FOOT_LR_L" : "PSGFOOT_LR_L", bDriver ? CarIKOffsets::FootL : CarIKOffsets::PsgFootL, false,
               target ) ) {
        SolveTwoBoneIK( skeleton, locals, bone( "UpLeg_L" ), bone( "Leg_L" ), bone( "IK_Left_Foot" ), target,
                        bone( "Foot_L" ) );
    }
    if ( facial ) {
        facial( locals );
    }
    skeleton.computeSkinMatrices( locals, worldBones, skin );
    world = carWorld * seatMatrix;
    bVisible = true;
    static int32_t logSeat = CmdLine::HasArg( "-logseat" ) ? 2 : 0;
    if ( logSeat > 0 ) {
        logSeat--;
        const Vector3f p = world.getTranslation();
        OTDU_LOG_INFO( "SEAT %s car %d IKOffsetDB %d seat (%.3f %.3f) root (%.3f %.3f %.3f)\n",
                       bDriver ? "driver" : "passenger", offsetsCar, offsets.bValid ? 1 : 0, seatAdjust.y,
                       seatAdjust.z, p.x, p.y, p.z );
    }
}

Vector3f SeatedAvatar::getHeadPosition() const {
    const int32_t head = avatar.isLoaded() ? avatar.getSkeleton().findBone( EdenNameHash4( "Head" ) ) : -1;
    if ( head < 0 || static_cast<size_t>( head ) >= worldBones.size() ) {
        return world.transformPoint( Vector3f( 0.0f, 0.8f, 0.0f ) );
    }
    return world.transformPoint( worldBones[head].getTranslation() );
}

void SeatedAvatar::computeLighting( const WeatherPreset *pWeather, const EdenSceneParams &sceneParams ) {
    // IntroCinematic::computeAvatarLighting without scene lights (FUN_00a4da90
    // finds no Entity__Light outdoors): sun light, shadow influence; the in-car
    // factor of FUN_00b87aa0 (m) is [ENC]: 1 as out of a car [INF].
    auto getFloat = [pWeather]( const char *pName, float value ) {
        return pWeather != nullptr ? pWeather->getFloat( pName, value ) : value;
    };
    const Vector3f root = world.getTranslation();
    lighting.Shadow = getFloat( "avatar_shadow_influence", 1.0f );
    lighting.SpotAmbient = lighting.SpotDiffuse = lighting.SpotSpecular = Vector3f();
    lighting.SpotPosition = root + Vector3f( 0.0f, 1.0f, 0.0f );
    const float v = Clamp( 1.0f, getFloat( "avatar_lum_min", 0.0f ), getFloat( "avatar_lum_max", 1.0f ) );
    lighting.Ambient = sceneParams.Ambient * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_ambiant_factor", 1.0f ) );
    lighting.Diffuse = sceneParams.SunDiffuse * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_diffuse_factor", 1.0f ) );
    lighting.Specular =
        sceneParams.SunSpecular * ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_sun_specular_factor", 1.0f ) );
    lighting.Direction = sceneParams.SunDirection.normalized();
    lighting.Environment = getFloat( "avatar_car_envmap_factor", 0.7f ) *
                           ( 1.0f + ( v - 1.0f ) * getFloat( "avatar_car_night_reflection_factor", 0.0f ) );
}

void SeatedAvatar::draw( EdenRenderer &renderer, bool bDrawHead ) const {
    if ( bVisible ) {
        avatar.draw( renderer, world, skin, &lighting, bDrawHead );
    }
}
