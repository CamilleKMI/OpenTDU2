#pragma once

#include "core/math.h"
#include "database/gs_database.h"

// Driving parameters of a car (docs/re/vehicle_physics_spec.md).
//
// TDU2 does not drive its cars with the Havok vehicle SDK (no Eden code
// creates an hkpVehicleInstance, spec 1.1): DBCarPhysicsData _newdyna = 1
// cars use Eden's own "edVehicle" multibody library (code 0x860000 -
// 0x8a0000, "New vehicle dynamics" of Physics.cpr). The set-up below is the
// one of the car factory FUN_00bdb970 (+ FUN_00df3b30 geometry block and
// FUN_00baddd0 post set-up) [CODE], fed with:
//   - DBCarPhysicsData (db_data.cpr) [DATA];
//   - Physics.cpr [Car] block (DAT_00fa99e8, offsets of the loader handlers
//     FUN_00d673c0 ... FUN_00d68080) [DATA];
//   - Vehicules/Tires/Tires.bnk PacejkaConfig XMB (one per _tires_type,
//     _offroad_dirt_tires_type, _offroad_grass_tires_type) [DATA];
//   - DBRims (_default_rims): tyre radius and width [DATA];
//   - the car bank vehicules/high/<car>.bnk <car>_max.xmb / <car>_min.xmb
//     torque curves (CurveActionMaxThrottle / CurveActionNoThrottle) and
//     <car>.dhk (Havok chassis collision only) [DATA].
// Units: SI (m, kg, N, rad, s); speeds of the curves in m/s.
struct VehicleCurve {
    std::vector<std::pair<float, float>> Points; // (x, y), x increasing
    float sample( float x ) const;               // linear [INF: FUN_004774a0 ENC], clamped at the ends
    bool empty() const { return Points.empty(); }
    float maxY() const;
    float minY() const;
};

// PacejkaConfig (Tires.bnk XMB; edVehicle object 0xf8 bytes: Lat[15] +0x00,
// Long[11] +0x3c, Align[18] +0x68, OptimalSlipRatio +0xb0, OptimalSlipAngle
// +0xb4, Name +0xb8; parser FUN_00869af0, table lookup FUN_0088a920 with
// fallback "Default" = FUN_008684b0 = the Tire1_Supercar values) [CODE+DATA].
struct PacejkaConfig {
    std::string Name = "Default";
    float Lat[15] = { 1.1f, 40.0f, 2000.0f, 3000.0f, 15.0f, 0.0f, -2.5f, 1.0f };
    float Long[11] = { 1.8f, 20.0f, 2000.0f, 0.0f, 500.0f, 0.0f, 0.0f, 0.0f, 0.1f };
    float Align[18] = { 1.5f, -6.49f, -21.85f, 0.416f, -21.31f, 0.02942f, 0.0f, -1.197f, 5.228f,
                        -14.84f, 0.0f, 0.0f, -0.00374f, 0.03891f, 0.0f, 0.0f, 0.639f, 1.693f };
    float OptimalSlipRatio = 1.98621f;
    float OptimalSlipAngle = 0.05537f;
};

// edVehicle Tire (wheel + 0x270) [CODE FUN_00bdb970 / FUN_008699a0].
struct TyreSetup {
    enum eSurface : uint32_t { Tarmac = 0, Dirt = 1, Grass = 2, NumSurfaces = 3 };
    float MaxLateralFriction[NumSurfaces] = { 1000.0f, 1000.0f, 1000.0f };      // +0x84: 1000 x mu x surface %
    float MaxLongitudinalFriction[NumSurfaces] = { 1000.0f, 1000.0f, 1000.0f }; // +0x90
    float MaxNormalFriction[NumSurfaces] = { 1000.0f, 1000.0f, 1000.0f };       // +0x9c
    float WetFrictionScale = 0.8f;      // +0x00 _wet_friction_percent / 100
    float BrakingFrictionScale = 1.0f;  // +0x04 _braking_friction_percent_xx / 100
    float FrictionScale = 1.0f;         // +0x08 (default 1, not set by the factory)
    float Radius = 0.33f;               // +0x0c DBRims
    float Width = 0.24f;                // +0x10 DBRims _largeur / 1000
    float Mass = 20.0f;                 // _wheel_mass_xx (wheel body)
    PacejkaConfig Config[NumSurfaces];  // tarmac (_tires_type), dirt, grass
};

// edVehicle wheel suspension (wheel + 0x640) [CODE FUN_00bdb970 + FUN_00df3b30].
struct SuspensionSetup {
    float StiffnessAtRest = 80000.0f; // +0x78 _suspension_rate_xx x 1000 (N/m)
    float StiffnessBias = 0.0f;       // +0x74 _suspension_rate_xx_bias x 0.01
    float DampingBump = 8000.0f;      // +0x60 DampingArray[0] _damper_rate_xx x 1000 (N s/m)
    float DampingRebound = 8000.0f;   // +0x64 DampingArray[1] (same DB value)
    float LowerStop = -0.05f;         // +0xc8 min(rh - rh_max - length, 0)
    float UpperStop = 0.05f;          // +0xcc max(rh - rh_max, 0)
    float StabilizerBarStiffness = 16000.0f; // +0xb8 _antirollbar_xx x 1000 (N/m)
    float StabilizerBarDamping = 0.0f;       // +0xb4 _antirollbar_damping_xx x 1000
};

struct VehicleSetup {
    CarPhysicsData Db;

    // DBCarPhysicsData extras.
    enum class eDrive : int32_t { AllWheels = 1, Front = 3, Rear = 4 }; // [CODE FUN_00bdb970: 1 -> FUN_00bdb800, 3 -> bdb770, 4 -> bdb6e0]
    eDrive Drive = eDrive::Rear;
    int32_t CameraId = 0;              // _camera (camerasingame.xmb id_car)
    // Reference performance figures (DB, for -vehtest only).
    float Accel0To100 = 0.0f;          // _acceleration_0_100_kmh (s)
    float QuarterMile = 0.0f;          // _quartermile_sec (s)
    float MaxTheoricalSpeed = 0.0f;    // _max_theorical_speed (km/h)
    float Braking100To0 = 0.0f;        // _braking_100_0
    float BrakingDistance = 0.0f;      // _brakingdist

    // Chassis (FUN_00df3b30 geometry block, edVehicle +0xe00..+0xe18).
    float Mass = 1500.0f;              // _mass
    Vector3f Size = Vector3f( 1.9f, 1.1f, 4.5f ); // width, height - rh_front - 0.1, length (m) (model AABB in game [ENC])
    Vector3f Inertia;                  // box of Size [INF: vfunc 0x9c = 0x544400 ENC]
    float CgHeight = 0.18f;            // _gravity_center_height / 100 (COM y offset)
    float LoadDistribution = 0.5f;     // _gravity_center_length / 100 (front share)
    float WheelBase = 2.6f;            // _wheelbase / 1000
    float FrontAxleLocalPos = 1.3f;    // length / 2 - overhang_front / 1000
    float TrackFront = 1.6f, TrackRear = 1.6f;
    float MaxVelocity = 80.0f;         // edVehicle +0x1f0 = _max_theorical_speed / 3.6 (use not read [INF])
    float ToeFront = 0.0f, ToeRear = 0.0f; // Front/RearAlignmentAngleArray[1] = _toe_xx (deg [INF]); camber/caster written 0

    TyreSetup TyreFront, TyreRear;
    SuspensionSetup SuspFront, SuspRear;

    // Engine (edVehicle + 0x8e0; serializer FUN_00888a4c / FUN_0088a67a).
    VehicleCurve TorqueMax;            // CurveActionMaxThrottle: rad/s -> N m, scaled to _torque_nm
    VehicleCurve TorqueMin;            // CurveActionNoThrottle: rad/s -> N m (x _engine_brake_nm / min if set)
    float EngineInertia = 0.2f;        // _engine_inertia x 0.01 (kg m^2)
    float EngineDamping = 0.05f;       // _engine_damping (edVehicle +0xc50)
    float IdleVelocity = 94.0f;        // _rpm_min (rad/s)
    float BlueLine = 650.0f;           // _blue_line (rad/s)
    float RedLine = 840.0f;            // _red_line (rad/s)
    float EngineMaxVelocity = 890.0f;  // _rpm_max (rad/s)
    float IdlePosition = 0.1f;         // -Tno(idle) / (Tmax(idle) - Tno(idle))
    float MaxPosition = 1.0f;
    float LimiterTime = 0.05f;         // _rupteur_time
    float StartingTime = 0.5f;         // _time_ignition

    // Gearbox (edVehicle + 0x9f0, GearRatioArray[10]: R = -1st, N, 1st.., then 1.0).
    std::vector<float> Gears;          // forward ratios
    float ReverseRatio = 3.0f;         // _gear_ratio1 (index 0 = -ratio)
    float FinalDrive = 4.0f;           // differential GearRatio (+0x100) = _final_drive_ratio
    float DifferentialMaxTorqueBias = 0.0f; // _differential_value_xx x 0.01 (+0x120)
    bool bDifferentialLocked = false;  // _differential_type_xx == 3 (flag +0x144)
    float ShiftTimeUp = 0.1f, ShiftTimeDown = 0.1f; // _shifttime_up / _down (gearbox controller +0x30 / +0x2c)
    float ReverseTime = 0.6f;          // Physics.cpr ReverseTime (controller +0x34)
    float GearboxInertia = 0.0f;       // _gearbox_inertia x 0.01
    float GearboxDamping = 0.0f;       // _gearbox_damping
    float PowerRpm = 7000.0f;          // _power_rpm (shift rule [INF])

    // Brakes (edVehicle +0xe08 / +0xe0c).
    float MaxBrakingAction = 10000.0f; // _brakes_power_front (N m, whole car)
    float BrakingBalance = 0.6f;       // _brake_balance x 0.01 (front share)
    float HandBrakeSlipRatio = -0.2f;  // _hand_brake_slip_ratio

    // Aerodynamics: spoilers (+0xcf0 front, +0xd90 rear) and ground effect (+0xd40).
    // Coefficient = |2 load g / (1.184 v^2)|, force = -0.592 C |v_t| |v| along
    // the spoiler normal, drag = force x DragCoefficient / LiftDragRatio
    // (FUN_00883b20) [CODE].
    float SpoilerFrontCoefficient = 0.0f;
    float SpoilerRearCoefficient = 0.0f;
    float GroundEffectCoefficient = 0.0f;
    float LiftDragRatio = 0.0f;        // _lift_drag_ratio (0 = no drag action)

    // Driving aids (edVehicle + 0x1098 object: VelocityActivations[3],
    // CurveAbsControlSlipRatio, CurveHandBrakeSlipRatio,
    // CurveTractionControlSlipRatio, SlipRatios[3]); speed m/s -> slip ratio.
    bool bAbs = true;                  // _abs
    bool bTractionControl = true;      // _traction_control
    VehicleCurve AbsSlipRatio;         // (AbsControlVelocityMin, cpr col) (AbsControlVelocityMax, DB col)
    VehicleCurve HandBrakeSlipRatio2;  // (HandBrakeVelocityMin, cpr col) (HandBrakeVelocityMax, DB)
    VehicleCurve TractionSlipRatio;    // (TractionControlVelocityMin, cpr col) (..Max, DB col)
    float AbsActivation = 0.0f;        // m/s
    float HandBrakeActivation = 0.0f;
    float TractionActivation = 2.8f;
    uint32_t AidLevel = 0;             // 0 SECURE (factory default [CODE]), 1 SPORT, 2 HYPERSPORT

    // Steering (edVehicle + 0x109c): m/s -> rad/s, rad, factor.
    VehicleCurve SteeringVelocity;
    VehicleCurve SteeringMaxAngle;
    VehicleCurve SteeringSensitivity;

    // Chassis damping / world (edVehicle + 0x4e0 .. + 0x570, WorldConfig).
    VehicleCurve AngularDamping;       // CurveAngularDamping (m/s -> 1/s)
    VehicleCurve DirtLinearDamping;    // CurveOffRoadDirtLinearDamping
    VehicleCurve GrassLinearDamping;   // CurveOffRoadGrassLinearDamping
    int32_t Substeps = 3;              // NbLoopForMiddlePrecision [INF use]
    float ExtraGravityMin = 0.1f;
    float ExtraGravityMax = 1.0f;
    float ExtraGravityAccelDuration = 0.1f;
    float ExtraGravityAccelDelay = 0.01f;
    float CollisionOffset = 0.1f;

    // <car>.dhk chassis (Havok rigid body: collision only).
    float HavokMass = 0.0f;
    Vector3f HavokCenterOfMass;
    Vector3f HavokInertia;
    struct Box {
        Vector3f Center;
        Vector3f HalfExtents;
    };
    std::vector<Box> ChassisBoxes;
};

// carFileName: DBCarPhysicsData _car_file_name ("California").
bool LoadVehicleSetup( const std::string &carFileName, VehicleSetup &out );
