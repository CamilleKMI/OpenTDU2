#pragma once

#include "core/math.h"
#include "physics/vehicle_data.h"

class VehicleWorldCollision;

// Driving simulation of the player car (docs/re/vehicle_physics_spec.md).
//
// The original drives its cars with Eden's edVehicle library ("new vehicle
// dynamics", DBCarPhysicsData _newdyna = 1), not with the Havok vehicle SDK
// [CODE, spec 1]. This class follows the edVehicle step as read in the exe
// (spec 6):
//   - Physics thread at 90 Hz, car step dt <= 1/90 s (FUN_00bd07c0 /
//     FUN_00bd03a0), one chassis sub step (SubStepCount = 1, ctor 0x87d672),
//     DriveTrainSubStepCount = Physics.cpr NbLoopForMiddlePrecision drive
//     train sub steps (FUN_00baa810) of 4 stages each, stage time steps
//     [h, h/2, h/2, h] (FUN_0087ac80) [CODE];
//   - drive train of impulses (engine FUN_00888550 / FUN_008880f0, shaft
//     damping FUN_008877e0 + stop clamp FUN_00887400, clutch slip
//     FUN_00877e50, gearbox ratios FUN_008852d0 / FUN_00885230, open
//     differential with the MaxTorqueBias speed limit FUN_00882420, wheel
//     axles with the car inertia share FUN_0086fea0 / FUN_008701f0 and the
//     tyre torque FUN_00870630) [CODE laws];
//   - controllers: engine (FUN_0088a410: starting / running / limiter /
//     stalling, Position clamp, speed clamp to [idle, max] FUN_00887f10),
//     clutch (FUN_008810c0 / FUN_00880fe0), gearbox (FUN_008866a0 and the
//     state handlers, automatic shift flags FUN_00884d80), per wheel brake
//     controller with ABS / traction control / hand brake regulation
//     (FUN_00865750, FUN_00865460, FUN_008652b0, FUN_0086f950, FUN_00870300)
//     [CODE];
//   - tyre forces: the TDU2 Pacejka variant (FUN_00869090, FUN_00868dc0,
//     FUN_00868a60) with the dynamic FrictionScale (FUN_0086a990) [CODE];
//   - aerodynamics at the decoded anchors (FUN_0086c2c0, FUN_00883b20),
//     extra gravity ramp (FUN_0087ebf0), damping curves (FUN_0087af20 /
//     FUN_0087af70) [CODE].
// The chassis contact / constraint solver itself (Motion integration,
// ContactNormal / ContactTangentialConstraint, suspension force law, RK4
// combination of the stages) is in the SecuROM range (car vtable entries
// 0x54xxxx) and stays [INF] here: raycast wheels on a rigid body (spec 6.9).
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
        bool bGearUp = false;   // manual requests (automatic box: ignored)
        bool bGearDown = false;
    };

    struct WheelState {
        bool bContact = false;
        float Compression = 0.0f;     // m
        float PreviousCompression = 0.0f;
        float AngularVelocity = 0.0f; // rad/s (forward roll positive)
        float SpinAngle = 0.0f;       // rad (drawing)
        float Load = 0.0f;            // N (tyre NormalForce)
        float SlipRatio = 0.0f;
        float SlipAngle = 0.0f;
        Vector3f ContactPoint;
        Vector3f ContactNormal;
        float SurfaceFriction = 0.7f;
        uint32_t Surface = 0;           // tyre surface index (0 tarmac, 1 dirt, 2 grass)
        float LateralForce = 0.0f;      // N (tyre frame, + = towards +x of the wheel)
        float LongitudinalForce = 0.0f; // N (+ = forward)
        float SlippingCoefficient = 0.0f; // combined force / friction ellipse (FUN_00868a60)
        float AxleSlipCoefficient = 0.0f; // axle part +0x3c (FUN_0086fea0)
        float BrakingPosition = 0.0f;     // brake controller +0x1c
        bool bAbs = false, bTcs = false;
    };

public:
    VehicleDynamics();

    void setup( const VehicleSetup &setup, const std::vector<WheelDesc> &wheels );
    // Body pose: car origin (model origin) and body -> world rotation.
    void reset( const Vector3f &position, const Quaternionf &orientation, const Vector3f &velocity );
    // Advances by deltaTime in 1/90 s physics ticks (FUN_00bd03a0).
    void step( float deltaTime, const Input &input, const VehicleWorldCollision &collision );

    // Car model origin -> world.
    Matrix4x4f getWorldMatrix() const;
    inline const Vector3f &getPosition() const { return position; }
    inline const Quaternionf &getOrientation() const { return orientation; }
    inline const Vector3f &getVelocity() const { return velocity; }
    float getSpeedKmh() const;        // along the car heading
    inline float getRpm() const { return engine.Velocity * 30.0f / kPI; }
    // 0 neutral, -1 reverse, 1..n
    int32_t getGear() const;
    inline float getSteerAngle() const { return steerAngle; } // rad, positive = left
    inline float getOdometer() const { return odometer; }    // m
    inline const std::vector<WheelState> &getWheels() const { return wheelStates; }
    inline const std::vector<WheelDesc> &getWheelDescs() const { return wheels; }
    inline const VehicleSetup &getSetup() const { return params; }
    inline bool isOnGround() const { return bOnGround; }
    inline bool isSportLines() const { return bSportLines; }
    void setLogContacts( bool bLog ) { bLogContacts = bLog; }
    // One line of drive train state (debug logs).
    std::string getDebugState() const;
    // Hub offset along the car up axis from its modelled place (drawing):
    // the suspension offset (compression > 0, full droop = LowerStop).
    float getWheelOffset( size_t i ) const { return i < wheelStates.size() ? wheelStates[i].Compression : 0.0f; }

private:
    // --- Drive train (edVehicle parts and controllers, spec 6.3 - 6.6) ----
    // Engine part (car +0x8e0, vtable 0xf394f8) and controller (engine +0x3b0).
    struct EngineState {
        float Velocity = 94.0f;      // DriveShaft velocity (rad/s)
        float Position = 0.0f;       // engine +0x390 (torque mix)
        float PreviousAction = 0.0f; // +0x394
        uint32_t PartFlags = 0;      // +0x4
        // Controller: EngineState +0x1c (0 starting, 1 running, 2 limiter,
        // 3 stalling, 4 stopped), StarterState +0x20, ElapsedTime +0x24,
        // PedalPosition +0x34, Position +0x38.
        int32_t State = 1;
        int32_t StarterState = 1;
        float Elapsed = 0.0f;
        float Pedal = 0.0f;
        float CtrlPosition = 0.0f;
        uint32_t CtrlFlags = 0;
    } engine;
    // Clutch part (gearbox +0x10, vtable 0xf39498) and controller (+0x90).
    struct ClutchState {
        float Dof = 0.0f;      // DegreeOfFreedom +0x84 (slip coefficient, 0 = locked)
        int32_t State = 1;     // 0 engaging, 1 engaged, 2 disengaging, 3 disengaged
        float Elapsed = 0.0f;
        float Position = 0.0f; // 0 engaged .. 1 disengaged
        int32_t Type = 0;      // 0: automatic clutch
    } clutch;
    // Gearbox part (engine +0x110, vtable 0xf39558) and controller (+0x1f0).
    struct GearboxState {
        int32_t Gear = 2;      // GearRatio +0x44 (index in GearRatioArray: 0 R, 1 N, 2.. 1st..)
        int32_t Shifted = 2;   // ShiftedGearRatio +0x48
        int32_t State = 4;     // 0 shift down, 1 shift up, 2 reverse, 3 neutral, 4 drive
        float Elapsed = 0.0f;  // +0x28
        uint32_t CtrlFlags = 2; // +0x14
        uint32_t Flags = 0;     // part +0x4 (4, 8, 0x10, 0x20 down, 0x40 up)
        int32_t Type = 0;       // 0 automatic
        int32_t Mode = 0;
        int32_t MinGear = 0, MaxGear = 8;
        float Ratio[10] = {};   // GearRatioArray
    } gearbox;
    // Per wheel brake controller (axle +0x50, vtable 0xf3ad44).
    struct BrakeController {
        uint32_t Flags = 1;    // bit0 brakes, bit2 hand brake wheel
        float Position = 0.0f; // BrakingPosition +0x1c
        float Velocity = 0.0f; // target +0x28
        bool bHasTarget = false;
    };
    std::vector<BrakeController> brakes;
    // Driving aids object (car +0xf80): SlipRatios[3] (+0xbc, FUN_0087ebf0),
    // pedal Position (+0xd4), Flags (+0x10: bit0 lock, bit2 ABS active,
    // bit3 hand brake, 0x20 traction control active).
    float aidSlipRatios[3] = { -0.2f, -0.4f, 0.5f };
    float aidBrakePosition = 0.0f;
    float aidBrakePreviousPosition = 0.0f;
    float aidBrakePositionApplied = 0.0f; // aids Position +0xd4 (after the gearbox controller)
    uint32_t aidFlags = 0;

    VehicleSetup params;
    std::vector<WheelDesc> wheels;
    std::vector<WheelState> wheelStates;
    std::vector<float> wheelPartInertia;   // axle PartInertia (wheel body about its axle)
    std::vector<float> wheelCarInertia;    // m x LoadDistributionAtRest x R^2 (FUN_0086fea0)
    std::vector<bool> drivenWheels;
    std::vector<float> wheelDriveImpulse;  // drive + brake impulse on each axle during the tick (N m s)
    std::vector<float> wheelEffInertia;    // last effective axle inertia (FUN_008701f0)
    std::vector<float> stageDriveImpulse;
    std::vector<Vector3f> rayStarts; // car frame
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
    float tickAccumulator = 0.0f;
    float reverseTimer = 0.0f;
    float airTime = 0.0f;     // NoWheelOnGroundElapsedTime (WorldConfig +0x24)
    float odometer = 0.0f;
    int32_t surfaceContext = 0; // CurrentContext +0x5e0 (FUN_0087ab80)
    bool bSportLines = false;   // game car object +0x4c (FUN_00bacd50)
    float blueLine = 0.0f, redLine = 0.0f;
    bool bOnGround = false;
    bool bLogContacts = false;

    void tick( float h, const Input &input, const VehicleWorldCollision &collision );
    void applyControls( const Input &input, float forwardSpeed );
    void updateDriveTrain( float h, float forwardSpeed );
    void driveTrainStage( float dt, float forwardSpeed, std::vector<float> &wheelOmega, float &engineOmega,
                          std::vector<float> &wheelRate, float &engineRate );
    void updateGearboxController( float dt, float forwardSpeed );
    void updateClutchController( float dt );
    void clutchSetPosition( float p );
    void updateEngineController( float dt );
    void updateBrakeControllers( float dt, const std::vector<float> &wheelOmega, float engineOmega );
    void updateGearboxFlags( float forwardSpeed, const std::vector<float> &wheelOmega );
    void requestGear( int32_t gear );
    float engineTorque( float w ) const;
    float gearRatio( int32_t index ) const; // GearRatioArray x final drive
    float tyreTorque( size_t i, float omega, bool bUpdateState );
    void resolveChassisCollision( float dt, const VehicleWorldCollision &collision );
    Vector3f worldInverseInertia( const Vector3f &torque ) const;
    void applyImpulse( const Vector3f &impulse, const Vector3f &point );
    // Per tick tyre kinematics (contact frame, velocities, load).
    struct ContactFrame {
        Vector3f Longitudinal, Lateral, Point;
        float LongVelocity = 0.0f, LatVelocity = 0.0f;
        float FrictionScale = 1.0f;
    };
    std::vector<ContactFrame> contactFrames;
};
