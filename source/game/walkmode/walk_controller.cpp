#include "shared.h"
#include "walk_controller.h"

#include "input/gs_input.h"

namespace {
constexpr float kWalkSpeedMax = 2.0f;        // Walk_Speed_Max
constexpr float kCamSpeedMax = 2.8f;         // Cam_Speed_Max (rad/s on the stick) [INF: unit]
constexpr float kHeadPitchMax = 60.0f * kDegToRad;
constexpr float kHeadPitchMin = 45.0f * kDegToRad;
constexpr float kMouseRadiansPerPixel = 0.0025f; // [INF]
} // namespace

void WalkController::reset( const Vector3f &eyePosition, const Quaternionf &heading ) {
    position = eyePosition;
    // forward = R(q*) (0, 0, 1).
    const Quaternionf conjugate( -heading.x, -heading.y, -heading.z, heading.w );
    const Vector3f forward = conjugate.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
    yaw = std::atan2( forward.x, forward.z );
    pitch = std::asin( Clamp( forward.y, -1.0f, 1.0f ) );
}

Quaternionf WalkController::getHeading() const {
    // q* = Ry(yaw) Rx(-pitch): R(q*) (0, 0, 1) = (sin yaw cos p, sin p, cos yaw cos p).
    const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ) *
                                  Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), -pitch );
    return Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w );
}

Vector3f WalkController::getForward() const {
    return Vector3f( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
}

void WalkController::update( float deltaTime, bool bInputEnabled ) {
    if ( !bInputEnabled || gpInput == nullptr ) {
        return;
    }
    // Look: mouse (relative motion) and right stick.
    // OpenTDU2's camera frame puts the screen right at -X for forward +Z
    // (EdenSceneView: right-handed), so turning right decreases the yaw.
    yaw -= gpInput->getMouseDeltaX() * kMouseRadiansPerPixel + gpInput->getRightStickX() * kCamSpeedMax * deltaTime;
    pitch -= gpInput->getMouseDeltaY() * kMouseRadiansPerPixel + gpInput->getRightStickY() * kCamSpeedMax * deltaTime;
    pitch = Clamp( pitch, -kHeadPitchMin, kHeadPitchMax );

    // Move: AVT_MOVE_* (arrows), WASD, left stick.
    float forwardInput = ( gpInput->isDown( eInputAction::IA_AvatarForward ) ? 1.0f : 0.0f ) -
                         ( gpInput->isDown( eInputAction::IA_AvatarBackward ) ? 1.0f : 0.0f );
    float sideInput = ( gpInput->isDown( eInputAction::IA_AvatarRight ) ? 1.0f : 0.0f ) -
                      ( gpInput->isDown( eInputAction::IA_AvatarLeft ) ? 1.0f : 0.0f );
    forwardInput += ( gpInput->isDown( eInputAction::IA_CarAccel ) ? 1.0f : 0.0f ) -
                    ( gpInput->isDown( eInputAction::IA_CarBrake ) ? 1.0f : 0.0f );
    sideInput += ( gpInput->isDown( eInputAction::IA_CarRight ) ? 1.0f : 0.0f ) -
                 ( gpInput->isDown( eInputAction::IA_CarLeft ) ? 1.0f : 0.0f );
    forwardInput -= gpInput->getLeftStickY();
    sideInput += gpInput->getLeftStickX();
    forwardInput = Clamp( forwardInput, -1.0f, 1.0f );
    sideInput = Clamp( sideInput, -1.0f, 1.0f );
    if ( forwardInput == 0.0f && sideInput == 0.0f ) {
        return;
    }
    const Vector3f forward( std::sin( yaw ), 0.0f, std::cos( yaw ) );
    // Screen right: forward x up (-X at yaw 0, see above).
    const Vector3f right( -std::cos( yaw ), 0.0f, std::sin( yaw ) );
    Vector3f move = forward * forwardInput + right * sideInput;
    const float length = move.length();
    if ( length > 1.0f ) {
        move = move * ( 1.0f / length );
    }
    const Vector3f target = position + move * ( kWalkSpeedMax * deltaTime );
    position = collide ? collide( position, target ) : target;
}
