#pragma once

#include "core/math.h"

#include <functional>

// First person walk mode controller (house-local coordinates). Tuning from
// scenarics/avatars/avatarsdriver.bnk Entity__AvatarOnline WalkModeMan /
// WalkModeWoman: Walk_Speed_Max 2, Cam_Speed_Max 2.8, Head_Pitch_Max 60,
// Head_Pitch_Min 45 [DATA]. The original controller and follow camera are
// encrypted [ENC]: mouse sensitivity and the strafe mapping are OpenTDU2
// choices.
class WalkController {
public:
    // Collision: from -> to (eye positions), returns the allowed position.
    using CollideFunction = std::function<Vector3f( const Vector3f &from, const Vector3f &to )>;

    void reset( const Vector3f &eyePosition, const Quaternionf &heading );
    void setCollide( CollideFunction function ) { collide = std::move( function ); }
    void update( float deltaTime, bool bInputEnabled );

    inline const Vector3f &getPosition() const { return position; }
    // Heading quaternion (camera convention: forward = R(q*) (0, 0, 1)).
    Quaternionf getHeading() const;
    Vector3f getForward() const;

private:
    Vector3f position;
    float yaw = 0.0f;
    float pitch = 0.0f; // > 0: looking up
    CollideFunction collide;
};
