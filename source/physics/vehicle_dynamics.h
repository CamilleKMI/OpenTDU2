#pragma once

#include "core/math.h"
#include "physics/vehicle_data.h"

class VehicleWorldCollision;

// Driving simulation of the player car (docs/re/vehicle_physics_spec.md).
//
// The original drives its cars with Eden's edVehicle library ("new vehicle
// dynamics", DBCarPhysicsData _newdyna = 1), not with the Havok vehicle SDK
// linked in the exe [CODE, spec 1.1]. edVehicle is a constraint based
// multibody solver (chassis Motion, WheelJoint / WheelSpringJoint, tyre
// contact constraints, RK4 drive train of shafts, clutch, gearbox,
// differentials); its solver is not reimplemented. This class keeps a
// raycast vehicle integrator [INF] and uses the decoded edVehicle laws and
// parameters (spec 3, 4):
//   - tyre forces: the TDU2 Pacejka '89 variant (FUN_00869090 lateral,
//     FUN_00868dc0 longitudinal, FUN_00868720 aligning) with D = DB friction
//     x 1000 x Fz(kN), the game's fast atan, and the friction weighted
//     sliding direction combination FUN_00868a60 [CODE];
//   - suspensions: StiffnessAtRest / DampingArray / Lower-UpperStop /
//     StabilizerBar from FUN_00df3b30 + FUN_00bdb970 [CODE], force law [INF];
//   - engine curves (CurveActionNoThrottle / MaxThrottle, IdlePosition),
//     engine inertia / damping, red line limiter (LimiterTime) [CODE values,
//     INF integration];
//   - gearbox ratios (reverse = -1st), final drive, brakes (MaxBrakingAction,
//     BrakingBalance), driving aid slip ratio curves, steering curves,
//     angular / off-road linear damping, spoilers / ground effect [CODE].
//
// Car frame: +z forward, +y up, +x left (the car model and the cinematic
// tracks [DATA]).
class VehicleDynamics {
public:
    struct WheelDesc {
        Vector3f HubLocal; // D_RIM_xx node (car frame) at rest
        bool bFront = true;
        bool bLeft = true;
        float Radius = 0.339f;
    };

    struct Input {
        float Throttle = 0.0f;  // [0, 1]
        float Brake = 0.0f;     // [0, 1]
        float Steering = 0.0f;  // [-1 left, 1 right]
        bool bHandBrake = false;
        bool bGearUp = false;   // manual override requests (ignored in automatic) [INF]
        bool bGearDown = false;
    };

    struct WheelState {
        bool bContact = false;
        float Compression = 0.0f;     // m
        float PreviousCompression = 0.0f;
        float AngularVelocity = 0.0f; // rad/s (forward roll positive)
        float SpinAngle = 0.0f;       // rad (drawing)
        float Load = 0.0f;            // N
        float SlipRatio = 0.0f;
        float SlipAngle = 0.0f;
        Vector3f ContactPoint;
        Vector3f ContactNormal;
        float SurfaceFriction = 0.7f;
        float LateralForce = 0.0f;      // N (tyre frame, + = towards +x of the wheel)
        float LongitudinalForce = 0.0f; // N (+ = forward)
        float SlippingCoefficient = 0.0f; // combined force / friction ellipse (FUN_00868a60)
        bool bAbs = false, bTcs = false;
    };

public:
    VehicleDynamics();

    void setup( const VehicleSetup &setup, const std::vector<WheelDesc> &wheels );
    // Body pose: car origin (model origin) and body -> world rotation.
    void reset( const Vector3f &position, const Quaternionf &orientation, const Vector3f &velocity );
    void step( float deltaTime, const Input &input, const VehicleWorldCollision &collision );

    // Car model origin -> world.
    Matrix4x4f getWorldMatrix() const;
    inline const Vector3f &getPosition() const { return position; }
    inline const Quaternionf &getOrientation() const { return orientation; }
    inline const Vector3f &getVelocity() const { return velocity; }
    float getSpeedKmh() const;        // along the car heading
    inline float getRpm() const { return engineRpm; }
    // 0 neutral, -1 reverse, 1..n
    inline int32_t getGear() const { return gear; }
    inline float getSteerAngle() const { return steerAngle; } // rad, positive = left
    inline float getOdometer() const { return odometer; }    // m
    inline const std::vector<WheelState> &getWheels() const { return wheelStates; }
    inline const std::vector<WheelDesc> &getWheelDescs() const { return wheels; }
    inline const VehicleSetup &getSetup() const { return params; }
    inline bool isOnGround() const { return bOnGround; }
    void setLogContacts( bool bLog ) { bLogContacts = bLog; }
    // Hub offset along the car up axis from its modelled place (drawing):
    // the suspension offset (compression > 0, full droop = LowerStop).
    float getWheelOffset( size_t i ) const { return i < wheelStates.size() ? wheelStates[i].Compression : 0.0f; }

private:
    VehicleSetup params;
    std::vector<WheelDesc> wheels;
    std::vector<WheelState> wheelStates;
    std::vector<Vector3f> rayStarts; // car frame
    std::vector<float> staticCompressions;
    std::vector<Vector3f> collisionSpheres; // car frame (chassis corners)
    std::vector<Vector3f> previousSphereCenters; // world, last sub step
    float collisionRadius = 0.3f;

    float mass = 1500.0f;
    Vector3f centerOfMass;   // car frame
    Vector3f inertia;        // body diagonal
    Vector3f position;       // car origin, world
    Quaternionf orientation; // body -> world
    Vector3f velocity;       // centre of mass
    Vector3f angularVelocity;

    float steerAngle = 0.0f;
    float engineVelocity = 94.0f; // rad/s
    float limiterTimer2 = 0.0f;
    float engineRpm = 900.0f;
    int32_t gear = 1;
    float shiftTimer = 0.0f;
    float limiterTimer = 0.0f;
    float reverseTimer = 0.0f;
    float airTime = 0.0f;
    float odometer = 0.0f;
    bool bOnGround = false;
    bool bLogContacts = false;

    void substep( float dt, const Input &input, const VehicleWorldCollision &collision );
    void resolveChassisCollision( float dt, const VehicleWorldCollision &collision );
    void updateGearbox( float dt, const Input &input, float speedKmh );
    float evaluateTyre( size_t wheelIndex, float fz, float vx, float vy, float omega, uint32_t surface, bool bBraking,
                        float &outFy ) const;
    float gearRatio( int32_t g ) const;
    Vector3f worldInverseInertia( const Vector3f &torque ) const;
    void applyImpulse( const Vector3f &impulse, const Vector3f &point );
};
