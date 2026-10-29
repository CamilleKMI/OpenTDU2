#pragma once

#include "game/avatar/eden_avatar.h"
#include "game/avatar/ik_offsets.h"
#include "render/eden/eden_animation.h"
#include "render/eden/eden_renderer.h"

class CinematicCar;
struct WeatherPreset;
struct EdenSceneParams;

// An avatar seated in a car (docs/re/driver_seat_spec.md): the driver
// (avatar state 2000) or the passenger (state 3000, FUN_00b84590 /
// FUN_00b9fa00 put a PNJ there: car +0x79cc) [CODE].
//   - Root (FUN_00b8ade0 [CODE]): car world rotation, translation = car *
//     (seat node position + seat group adjustment (0, y, z) + R * IKOffsetDB
//     offset of the seat node). Driver: DvrSeat (node 0, group (0, r3, r2));
//     passenger: PsgSeat (node 10, avatar +0x1ae4 / +0x1ae8, group (0, r5, r4)).
//   - Pose: the base clip of the seat (avatarsdriver.bnk: driver
//     stance_turn.anm, Entity__AvatarPlayer layer 0; passenger
//     Entity__AvatarPassenger PassengerWoman / PassengerMan layer 0
//     shoppinggirlcarlayer0.bas = SHO_stand_car.xan / hitchhikerlayer0.bas =
//     hit_stand_car.xan) [DATA], or a clip played over it (the event
//     reactions of FUN_00a151c0, FUN_007cdf40 [ENC]).
//   - IK (layer 6 of the driver, layer 1 of the passenger [DATA]): driver
//     Spine -> IK_Back aim to Back_LR_L, both arms to Hand_LR_R / Hand_LR_L,
//     both legs to Foot_LR_R / Foot_LR_L; passenger Spine -> IK_Back aim and
//     both legs only (shoppinggirlcarlayer1.bas: type 6 Spine / IK_Back,
//     type 2 UpLeg_x / Leg_x / IK_x_Foot) [DATA], goals Back_LR_R,
//     PsgFoot_LR_R / PsgFoot_LR_L + their IKOffsetDB offsets (nodes 11, 12,
//     13) [INF: the passenger goal reader is not identified; same rule as
//     FUN_00b7e940 for the driver]. Solver: SolveTwoBoneIK / SolveAimIK [INF,
//     the edAnim solver is ENC].
class SeatedAvatar {
public:
    enum class eSeat { Driver, Passenger };

    // driverBank: scenarics/avatars/avatarsdriver.bnk (base clips).
    bool load( EdenAvatarLibrary &library, const char *pConfigId, eSeat seat, const Bank &driverBank );
    void unload();
    inline bool isLoaded() const { return avatar.isLoaded(); }
    inline EdenAvatar &getAvatar() { return avatar; }
    inline eSeat getSeat() const { return seat; }

    // Clip played over the base clip (nullptr: base clip only); clip time,
    // blend weight against the base pose.
    void setOverride( const EdenAnimation *pClip, float time, float weight = 1.0f ) {
        pOverride = pClip;
        overrideTime = time;
        overrideWeight = weight;
    }
    // Per frame: pose + IK; pFacial applied on the local pose before the
    // skinning (FaceFX bones).
    void update( const CinematicCar &car, const Matrix4x4f &carWorld,
                 const std::function<void( std::vector<BoneTransform> & )> &facial = nullptr );
    // Out-of-interior lighting of the avatar (no scene light: the branch of
    // IntroCinematic::computeAvatarLighting without spot lights).
    void computeLighting( const WeatherPreset *pWeather, const EdenSceneParams &sceneParams );
    void draw( EdenRenderer &renderer, bool bDrawHead = true ) const;
    inline const Matrix4x4f &getWorld() const { return world; }
    // Head bone world position (look / sound anchor).
    Vector3f getHeadPosition() const;

private:
    EdenAvatar avatar;
    eSeat seat = eSeat::Driver;
    EdenAnimation baseClip;
    bool bBaseClip = false;
    const EdenAnimation *pOverride = nullptr;
    float overrideTime = 0.0f;
    float overrideWeight = 1.0f;
    CarIKOffsets offsets;
    int32_t offsetsCar = -2;
    Matrix4x4f world;
    std::vector<Matrix4x4f> worldBones;
    std::vector<Matrix4x4f> skin;
    EdenAvatarLighting lighting;
    bool bVisible = false;
};
