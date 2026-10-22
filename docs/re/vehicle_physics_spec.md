# TDU2 car physics: data files, edVehicle set-up and runtime laws (California first)

Tags: [CODE] readable x86 (VA >= 0x6c1000) · [DATA] game files · [INF] inferred / chosen (reason given) ·
[ENC] in the SecuROM range (< 0x6c1000) or an encrypted CRT stub, not read.

Work dir `/tmp/claude-0/vp/`: `ev_all.c` (Ghidra decompile of 0x860000-0x8a0000, the edVehicle library), `ev.asm`
(annotated disassembly of the same range), `fields.txt` / `fields_rest.txt` (edVehicle serializer field names ->
offsets), `dbfields.txt` (DB field -> in-memory record offset, DB decoder 0xdf0000-0xe08000), `cprkeys.txt` /
`cprmap.txt` (Physics.cpr keys -> handler -> block offset), `carb.asm` (car factory FUN_00bdb970 annotated with DB /
Physics.cpr / GEO names), `build.asm` (bike factory FUN_00bd8140), `d1.c`..`d4.c` (decompiles), `tires/` (extracted
`vehicules/tires/tires.bnk`), `tools/` (string, xref, annotation scripts).

---------------------------------------------------------------------------------------------------
## 1. Which engine drives the cars

### 1.1 Not the Havok vehicle SDK [CODE]
- hkpVehicleInstance & co. are linked (RTTI, hkClass tables, code 0x7bf000-0x7cc000), but the only constructor call
  of hkpVehicleInstance (0x7c1800) is from 0x7c3600, which is referenced only by the hkpVehicleInstance vtable itself
  (0xea87b0 = vtable + 0x1c, the clone method); the other references are the packfile "finish" constructors
  (0x7bf1xx-0x7bf7xx). No Eden code instantiates a Havok vehicle. The car `.dhk` only holds the chassis rigid body
  + collision boxes (havok_collision_spec), no hkpVehicle* object.
- `DBCarPhysicsData._newdyna` (record +0x2b8) = 1 for the cars; Physics.cpr has "Old vehicle dynamics (useless)"
  (ElectronicParams*) and "New vehicle dynamics" sections [DATA].

### 1.2 Eden "edVehicle" library [CODE]
- Strings "edVehicle", build date "Dec 20 2011" (0xf3a0cc), ~600 functions at 0x860000-0x8a0000, no RTTI.
- Manager singleton DAT_01459f7c (ctor FUN_0088abe0), init FUN_00be0010 (the physics service "Physics": allocates
  0x28 cars of 0x74a0 bytes, starts the thread "PhysThread", loads `%sVehicules/Tires/Tires.bnk`).
- Object model (from the serializer functions, `fields*.txt`): Motion (Mass +0x1c, InvMass, Size, LocalInertia,
  InertiaArray +0x50, AppliedImpulseArray +0x200 ...), WheelJoint / WheelSpringJoint (AnchorArray, LocalAxisArray,
  NormalArray, SteeringControlImpulse, LoadDistributionAtRest, Caster, Camber), suspension (+0x640 in a wheel:
  DampingArray[2] +0x60, Force +0x68, MaxDamperForce +0x6c, ForceAtRest +0x70, StiffnessBias +0x74,
  StiffnessAtRest +0x78, EmptyLength +0x84, Offset +0x88, StabilizerBarDamping +0xb4 / Stiffness +0xb8,
  LowerStop +0xc8, UpperStop +0xcc, StopOffset +0xd0), Tire (wheel +0x270, layout §4.1), engine (Curve
  ActionStarter +0x10, ActionNoThrottle +0x40, ActionMaxThrottle +0x70, DriveShaft +0x350, IdleVelocity +0x398,
  BlueLine +0x39c, RedLine +0x3a0, MaxVelocity +0x3a4; controller: EngineState, StarterState, StartingTime +0x28,
  StallingTime, LimiterTime +0x30, PedalPosition, Position, IdlePosition +0x3c, MaxPosition +0x40), shafts
  (Inertia, InvInertia, Velocity, DampingArray[3]), clutch (EngagingTime, DisengagingTime, Position), gearbox
  (GearRatioArray[10] +0x14c, InvGearRatioArray, SqrGearRatioArray, InvSqr..., GearInertiaArray), gearbox
  controller (Mode, State, ShiftTimeDown +0x2c, ShiftTimeUp +0x30, ReverseTime +0x34, DriveTime +0x38,
  MinGearRatio, MaxGearRatio +0x40, GearRatio, ShiftedGearRatio), differential (OutputVelocityRatio, Ratio /
  GearRatio / SqrGearRatio, Distribution +0x118, Primacy +0x11c, MaxTorqueBias +0x120), spoiler (LocalAnchor,
  LocalNormal, IsDragActionEnabled +0x30, LiftDragRatio +0x38, DragCoefficient +0x3c, Coefficient +0x40), driving
  aids (CurveAbsControlSlipRatio +0x20, CurveHandBrakeSlipRatio +0x50, CurveTractionControlSlipRatio +0x80,
  VelocityActivations[3] +0xb0, SlipRatios[3] +0xbc, PedalPosition ...), steering (CurveMaxVelocity +0x20,
  CurveSensitivity +0x50, CurveMaxAngle +0x80, Feedback* +0xd8..+0xf0, Inertia, MaxAngle +0x120, Angle),
  world config (ExtraGravityMin..Delay, TangentialConstraintSolverThreshold), vehicle (SubStepCount,
  DriveTrainRk4Step, DriveTrainSubStepCount, DriveTrainTimeStepArray, CurveOffRoadDirt/GrassLinearDamping +0x4e0
  / +0x510, CurveAngularDamping +0x540, CurveOffRoadAngularDamping +0x570, TrackWidthArray +0xe00, MaxBrakingAction
  +0xe08, BrakingBalance +0xe0c, WheelBase +0xe10, FrontAxleLocalPos +0xe14, LoadDistribution +0xe18).
- It is a constraint/impulse multibody solver (ContactNormal/TangentialConstraint, MaxImpulse, ErrorImpulse,
  RK4 drive train). That solver is not reimplemented (§5).
- Several CRT math stubs it calls are encrypted [ENC]: 0x6c8d38 (used as sin), 0x541080 (atan), 0x403620 (fabs),
  0x448620 (sqrt), 0xe2db8a = `jmp [0xe414e4]` (exp), FUN_004774a0 (curve interpolation), FUN_0053f600 (curve add
  point), vfunc 0x9c of the vehicle = 0x544400 (mass + size -> inertia), vfunc 0xf4 = 0x544920 (centre of mass).
  Their identity is [INF] from the call context.

---------------------------------------------------------------------------------------------------
## 2. Data files used for a car

| file | content | loader |
|---|---|---|
| `database/db_data.cpr` DBCarPhysicsData | §2.1 | DB decoder 0xdf0000-0xe08000 [CODE] |
| `Physics.cpr` [Car] | §2.2 | FUN_00d6b5b0 key table -> handlers FUN_00d673c0..FUN_00d68100 [CODE] |
| `Euro/Bnk/vehicules/tires/tires.bnk` | 5 PacejkaConfig XMB (§2.3) | FUN_00be0010 + FUN_00869af0 [CODE] |
| DBRims (`_default_rims`) | tyre radius / width (§3.1) | FUN_00df3b30 [CODE] |
| `vehicules/high/<car>.bnk` `<car>_max.xmb`, `<car>_min.xmb` | torque curves (Nm vs rpm) | edVehicle Curve reader FUN_00892800 [CODE] |
| `vehicules/high/<car>.bnk` `<car>.dhk` | Havok chassis rigid body + boxes (collision only) | havok_collision_spec |
| `Euro/Bnk/physics/tires.bnk` `tire1..6.bpjka` | 384-byte blocks | **not referenced**: no "bpjka" / "physics/tires" string in the readable sections [CODE, INF: leftover of the old dynamics] |

### 2.1 DBCarPhysicsData in memory [CODE DB decoder]
Offsets of the decoded record (field -> +offset; full list `dbfields.txt`): _abs +0x0, _abs_slip_ratio_hypersport
+0x4 / _off +0x8 / _secure +0xc / _sport +0x10, _acceleration_0_100_kmh +0x14, _antirollbar_damping_front +0x18 /
rear +0x1c, _antirollbar_front +0x20 / rear +0x24, _blue_line +0x44, _brake_balance +0x4c, _brakes_dim_front +0x60
/ rear +0x64, _brakes_power_front +0x68 / rear +0x6c, _braking_friction_percent_front +0x74 / rear +0x78,
_camber_front +0x80 / rear +0x84, _caster_front +0x108 / rear +0x10c, _cx +0x110, _damper_rate_front +0x114 / rear
+0x118, _default_rims +0x120, _differential_type_central/front/rear +0x128/+0x12c/+0x130, _differential_value_*
+0x134/+0x138/+0x13c, _drive +0x150, _engine_brake_nm +0x15c, _engine_damping +0x160, _engine_inertia +0x164,
_final_drive_ratio +0x17c, _gear_ratio1 +0x1a0, 10 +0x1a4, 11 +0x1a8, 2..9 +0x1ac..+0x1c8, _gearbox_damping
+0x1d0, _gearbox_inertia +0x1d4, _gravity_center_height +0x1dc, _gravity_center_length +0x1e0,
_ground_effect_load +0x1e4 / velocity +0x1e8, _hand_brake_slip_ratio +0x1ec, _haspowersteering +0x1f5, _height
+0x1fc, _isoffroad +0x224, _length +0x22c, _lift_drag_ratio +0x230, _lift_load_front +0x234 / rear +0x238,
_lift_velocity +0x23c, _mass +0x240, _max_lateral_friction_front +0x244 / rear +0x248, _max_longitudinal_friction
+0x254 / +0x258, _max_normal_friction +0x25c / +0x260, _max_power_shift_up_purcent +0x264, _max_theorical_speed
+0x270, _nb_gears +0x294, _nb_turbos +0x2b4, _newdyna +0x2b8, _offroad_dirt_lateral/longitudinal/normal_friction_
percent +0x2bc/+0x2c0/+0x2c4, _offroad_dirt_tires_type +0x2c8 (string), _offroad_grass_* +0x2e4/+0x2e8/+0x2ec,
_offroad_grass_tires_type +0x2f0, _overhang_front +0x310 / rear +0x314, _power_bhp +0x318, _power_rpm +0x31c,
_red_line +0x34c, _ride_height_front +0x354, _ride_height_max_front +0x358 / rear +0x35c, _ride_height_rear +0x360,
_rpm_max +0x364, _rpm_min +0x368, _rupteur_time +0x36c, _shifttime_down +0x374 / up +0x378, _suspension_lenght_rear
+0x39c, _suspension_length_front +0x3a0, _suspension_rate_front +0x3a4 / bias +0x3a8, _rear +0x3ac / bias +0x3b0,
_throttle_ratio_normal +0x3b4 / sport +0x3b8, _time_ignition +0x3bc, _tire_radius +0x3c0, _tires_physics +0x3c8,
_tires_type +0x3d0 (string), _toe_front +0x3ec / rear +0x3f0, _torque_nm +0x3f4, _torque_rpm +0x3f8, _track_back
+0x3fc, _track_front +0x400, _traction_control +0x404, _traction_control_slip_ratio_hypersport +0x408 / off +0x40c
/ secure +0x410 / sport +0x414, _transmission_primacy +0x418, _vehiclecategory +0x41c, _wet_friction_percent
+0x440, _wheel_mass_front +0x44c / rear +0x450, _wheelbase +0x454, _width +0x458.
California (DBCarPhysicsData[228], id 387) values: `docs/re/freeride_spec.md` §2.3 plus _cx 0.32,
_differential_type_rear 1, _differential_value_rear 70, _gearbox_inertia 0, _gearbox_damping 0,
_brakes_dim_front/rear 390/360, _default_rims 0x114 [DATA].

### 2.2 Physics.cpr [Car] block (DAT_00fa99e8, 0x4c8 bytes, copied to vehicle +0x84 by FUN_00bb7120) [CODE]
NbLoopForMiddlePrecision +0x1c4, ReverseTime +0x1c8, DriveTime +0x1cc, ExtraGravityMin/Max/AccelDelay/AccelDuration
+0x1d0/+0x1d4/+0x1d8/+0x1dc, CollisionOffset +0x1e0, MinDragCoefficient +0x1e4, OffRoadAngularDamping +0x1ec (5 x
(km/h, value)), Secure/Sport/HyperSportAngularDamping +0x214/+0x23c/+0x264, OffRoadDirt/OffRoadGrass/NotOffRoadDirt/
NotOffRoadGrassLinearDamping +0x28c/+0x2b4/+0x2dc/+0x304, AbsControlActivation/HandBrakeActivation/
TractionControlActivation +0x32c/+0x330/+0x334, AbsControlVelocityMin/Max +0x338/+0x33c, AbsControlSlipRatio[3]
+0x340, HandBrakeVelocityMin/Max +0x34c/+0x350, HandBrakeSlipRatio[3] +0x354, TractionControlVelocityMin/Max
+0x360/+0x364, TractionControlSlipRatio[3] +0x368, SteeringVelocity +0x374, SteeringMaxAngle +0x39c,
SteeringSensitivity +0x3c4, Basic/PowerSteeringFeedbackFriction +0x3ec/+0x3f8, SteeringFeedbackSpring +0x404,
SteeringVibration +0x410, VelocityForBlendingCoeff +0x420, GyroscopicEffect +0x428, MaxLeanVelocity +0x450,
MaxRiderLieDown/SitUpPosition +0x478/+0x4a0. Values: freeride_spec §2.2.

### 2.3 Tires.bnk PacejkaConfig XMB [DATA]
`Euro/Bnk/vehicules/tires/tires.bnk`: Offroad_Good, Tire1, Tire1_Bike, Tire1_Supercar, Tire2 (group 11 sub 3).
Schema: `PacejkaConfig { Name; LateralCoefficientArray float[15]; LongitudinalCoefficientArray float[11];
AligningMomentCoefficientArray float[18]; OptimalSlipRatio; OptimalSlipAngle }`.

| | Tire1_Supercar (California `_tires_type`) | Offroad_Good (dirt / grass types) |
|---|---|---|
| Lat a0..a7 | 1.1, 40, 2000, 3000, 15, 0, -2.5, 1 (a8..a14 = 0) | 1.7, 50, 2000, 3000, 50, 0, 0, 0 |
| Long b0..b10 | 1.8, 20, 2000, 0, 500, 0, 0, 0, 0.1, 0, 0 | 1.8, 10, 2000, 0, 500, 0, 0, 0, 0.2, 0, 0 |
| Align c0..c17 | 1.5, -6.49, -21.85, 0.416, -21.31, 0.02942, 0, -1.197, 5.228, -14.84, 0, 0, -0.00374, 0.03891, 0, 0, 0.639, 1.693 (same in all files) | |
| OptimalSlipRatio / Angle | 1.98621 / 0.05537 | 1.98621 / 0.05537 |
Tire1: lat a0 1.2; Tire1_Bike: lat 1.012, 27.3, ..., long 1.412, -4.35, ...; slip 0.23448 / 0.07463.
Lookup FUN_0088a920(name): table DAT_01459f7c+0x2b8 (0xf8-byte entries, name at +0xb8), fallback "Default" =
FUN_008684b0 constants = the Tire1_Supercar values [CODE].

---------------------------------------------------------------------------------------------------
## 3. Car factory [CODE]
`FUN_00bdeac8 -> FUN_00bbde70`: FUN_00bb7120 (vehicle + Physics.cpr block, freeride_spec §2.1), FUN_00df3b30
(geometry block "GEO", §3.1), then FUN_00baddd0 -> **FUN_00bdb970 (cars)** or FUN_00bd8140 (bikes,
_vehiclecategory 0x12 / 0x13 / 0x16), then the upgrade post set-up of FUN_00baddd0 (§3.3).

### 3.1 GEO block (FUN_00df3b30) [CODE]
| GEO | value | California |
|---|---|---|
| +0x00/+0x04/+0x08 size | width/1000, height/1000 - ride_height_front/1000 - 0.1, length/1000; replaced by the model AABB extents when the model is available (FUN_00592790 [ENC] path) | 1.895, 1.09, 4.555 |
| +0x10 FrontAxleLocalPos | length/2 - overhang_front/1000 (overhang 0: (length - wheelbase)/2000 [- overhang_rear]) | 1.3345 |
| +0x14/+0x18 | wheel_graphical_offset / 1000 | 0 |
| +0x1c/+0x20 tyre radius | DBRims: largeur_front x hauteur_front / 100 / 1000 + diameter_front x 2.54 x 0.5 / 100 (front rim on both axles; TRAFFIC brand cars: brakes_dim / 2000) | 245, 40 %, 19" -> **0.3393** |
| +0x24/+0x28 tyre width | largeur_front / largeur_rear / 1000 | 0.245 / 0.285 |
| +0x2c/+0x30 track | track_front / track_back / 1000 | 1.62 / 1.605 |
| +0x34/+0x38 ride height | /1000 | 0.11 |
| +0x3c/+0x40 | ride_height_max /1000 | 0.06 |
| +0x54/+0x58 UpperStop | max(rh - rh_max, 0) | 0.05 |
| +0x4c/+0x50 LowerStop | min(UpperStop - suspension_length/1000, 0) | -0.05 |
| +0x44/+0x48 | UpperStop - LowerStop | 0.10 |
| +0x5c/+0x60 | suspension_rate x 1000 (N/m) | 80000 / 90000 |
| +0x64..+0x70 | damper_rate x 1000 (bump, rebound) | 8000 / 9000 |
| +0x74/+0x78 | antirollbar x 1000 | 16000 |
| +0x7c/+0x80 | antirollbar_damping x 1000 | 0 |
| +0x84 LoadDistribution | gravity_center_length / 100 | 0.47 |
| +0x88 | gravity_center_height / 100 | 0.18 |

### 3.2 FUN_00bdb970 (car) [CODE, `carb.asm`]
- Drive: `_drive` 1 -> FUN_00bdb800 (4WD, three differentials), 3 -> FUN_00bdb770 (FWD), 4 -> FUN_00bdb6e0 (RWD:
  FUN_0088c9d0 0x1200-byte car, rear differential +0xe30: GearRatio = _final_drive_ratio, SqrGearRatio, flag if
  _differential_type_rear == 3, MaxTorqueBias = _differential_value_rear x 0.01 (California 0.70)).
- Curves +0x4e0 / +0x510 / +0x540 / +0x570: not off-road cars: NotOffRoadDirt / NotOffRoadGrass LinearDamping,
  **SecureAngularDamping** (CurveAngularDamping), SecureAngularDamping again (CurveOffRoadAngularDamping); off-road
  cars: OffRoadDirt / OffRoadGrass, Secure, OffRoadAngularDamping. x scaled km/h -> m/s (DAT_00f4a010 = 1/3.6).
- Chassis: vfunc 0x9c(mass = _mass, GEO size) [ENC body], vfunc 0xf4((0, GEO+0x88, 0)) then centre of mass z =
  FrontAxleLocalPos - (1 - LoadDistribution) x WheelBase; TrackWidthArray = GEO +0x2c/+0x30 (+0xe00/+0xe04);
  WheelBase = _wheelbase x 0.001 (+0xe10); FrontAxleLocalPos (+0xe14); LoadDistribution (+0xe18).
- Engine controller: StartingTime = _time_ignition, LimiterTime = _rupteur_time; _nb_turbos != 0 -> flag 2.
- Torque curves: param_4[0] -> CurveActionNoThrottle (+0x920), param_4[1] -> CurveActionMaxThrottle (+0x950), x
  rpm -> rad/s (x 0.10471976). FUN_00888870: MaxThrottle y scaled by _torque_nm / MaxPoint.y (California 485/485 =
  1). Inertia (+0xc40) = _engine_inertia x 0.01 (0.20 kg m^2), InvInertia; +0xc50 = _engine_damping (0.05).
  IdleVelocity = _rpm_min, BlueLine, RedLine, MaxVelocity = _rpm_max (rad/s). IdlePosition = a / (a - b) with a =
  min(Tno(idle), -1e-5), b = max(Tmax(idle), Tno(idle), 1e-5) (California 0.115); MaxPosition = max(MaxPosition,
  IdlePosition + 1e-5). Rumble amplitude _exhaust_rumble_amplitude x 0.1 x deg->rad.
- Gearbox controller: ShiftTimeDown/Up, ReverseTime (Physics.cpr), MaxGearRatio = _nb_gears + 1, start gear
  logic. Gearbox (+0x9f0): GearInertia = _gearbox_inertia x 0.01, damping = _gearbox_damping; GearRatioArray (via
  FUN_00884220 index edi): [0] = -_gear_ratio1 (**reverse = first gear ratio**), [1] = 0, [2..8] = gear 1..7,
  [9] = 1.0 (gears 8.. not read for cars).
- Wheels +0xce0/+0xce4 (front L/R), +0xce8/+0xcec (rear): suspension (+0x640): LowerStop / UpperStop / StopOffset,
  StiffnessBias = _suspension_rate_xx_bias x 0.01, StiffnessAtRest, DampingArray[0..1] (bump / rebound), StabilizerBar
  Damping / Stiffness; wheel mass (_wheel_mass_xx), radius (GEO +0x1c/+0x20), width (GEO +0x24/+0x28).
- Tyres (wheel +0x270): copies of the PacejkaConfig of _tires_type (+0x5f0), _offroad_dirt_tires_type (+0x6e8),
  _offroad_grass_tires_type (+0x7e0); MaxLateral/Longitudinal/NormalFriction[surface] = 1000 x DB max friction x
  {1, offroad_dirt_%/100, offroad_grass_%/100}; WetFrictionScale = _wet_friction_percent / 100;
  BrakingFrictionScale = _braking_friction_percent_xx / 100; FrictionScale stays 1 (FUN_008699a0 defaults).
- Alignment: FrontAlignmentAngleArray = (0, _toe_front, 0), RearAlignmentAngleArray = (0, _toe_rear, 0) (camber /
  caster DB fields not used by the car factory).
- Aero: spoilers front (+0xcf0) / rear (+0xd90): LiftDragRatio = _lift_drag_ratio, IsDragActionEnabled = ratio != 0,
  Coefficient = |2 x lift_load x (-|g|) / (1.184 x (lift_velocity/3.6)^2)| (California 0.406 / 0.716); ground effect
  (+0xd40) Coefficient from _ground_effect_load / velocity (0.358). **_cx is not read** by the factory.
- Brakes: MaxBrakingAction = _brakes_power_front (15000), BrakingBalance = _brake_balance x 0.01 (0.6).
  _brakes_power_rear is not read.
- Aids (+0x1098): VelocityActivations = Abs/HandBrake/TractionControlActivation / 3.6; curves (VelocityMin/3.6,
  Physics.cpr SECURE column) -> (VelocityMax/3.6, DB _xx_secure) for ABS (-0.2 -> -0.08), hand brake (-0.4 ->
  _hand_brake_slip_ratio -0.2), traction control (0.5 -> 0.1); SlipRatios = the DB values. The SPORT / HYPERSPORT
  columns are used by a later aid switch that was not found [INF].
- Steering (+0x109c): feedback parameters (Power/BasicSteeringFeedbackFriction by _haspowersteering,
  SteeringFeedbackSpring), CurveMaxVelocity = SteeringVelocity (deg -> rad), CurveMaxAngle = SteeringMaxAngle,
  CurveSensitivity.
### 3.3 Post set-up (FUN_00baddd0) [CODE]
Upgrade factors from GamePC.cpr AccelIncreaseByUpgradeLevel / BrakingIncrease / TopSpeedIncrease (level 0 -> 1):
vehicle +0x1f0 = topSpeedFactor x _max_theorical_speed / 3.6 (use not read), MaxThrottle curve y x accel factor,
final ratio / topSpeedFactor, MaxBrakingAction x braking factor; throttle_ratio_normal/sport x 0.01, blue/red line
(normal, sport) rad/s, abs/tc slip ratios copied to the Eden car object.

---------------------------------------------------------------------------------------------------
## 4. Runtime laws read [CODE]
### 4.1 Tire object (wheel +0x270, serializer FUN_008693a0)
+0x00 WetFrictionScale, +0x04 BrakingFrictionScale, +0x08 FrictionScale, +0x0c Radius, +0x10 Width, +0x14 Camber,
+0x18 NormalForce, +0x1c LateralVelocity, +0x20 LongitudinalVelocity, +0x24 AngularVelocity, +0x28 slip ratio,
+0x2c LoadedRadius, +0x30 LateralStiffness, +0x34 LongitudinalStiffness, +0x38 MaxLateralForce, +0x3c
MaxLongitudinalForce, +0x40 MaxNormalTorque, +0x44/48/4c Lat/Long/NormalFriction, +0x50 SlipAngle, +0x54
TanSlipAngle, +0x58 OptimalSlipAngle, +0x5c OptimalSlipRatio, +0x60 AbsLongitudinalVelocity, +0x64
ContactLongitudinalVelocity, +0x68 LateralForce, +0x6c LongitudinalForce, +0x70 ContactSlippingCoefficient, +0x74
NormalTorque, +0x78 Torque, +0x7c SlippingCoefficient, +0x80 Flags, +0x84/+0x90/+0x9c Max*Friction[3], +0xa8
surface index (0 tarmac, 1 dirt, 2 grass).
### 4.2 Tyre update FUN_00869c30
10-sample moving averages of AngularVelocity, slip ratio, SlipAngle, ContactLongitudinalVelocity, +0x70; flag 0x10 =
|SlipAngle| > 0.5 or |slip ratio| > 0.5; unless Flags bit 0: FUN_00869090 (lateral), FUN_00868dc0 (longitudinal),
FUN_00868720 (aligning) with the PacejkaConfig of the surface; then FUN_00868a60 (combination).
All with Fz = NormalForce x 0.001 (kN), fast atan(x) = x / (1 + 0.28 x^2) for |x| < 1 else pi/2 - x / (x^2 + 0.28)
(- pi if x < 0), y = D sin(C atan(Bx - E (Bx - atan Bx))), no Sh / Sv, B = 99999 when C, D (or a4) vanish:
- lateral: C = a0, D = MaxLateralFriction x Fz x FrictionScale, BCD = a3 sin(2 atan(Fz / a4)) (1 - |Camber x 57.3|
  a5) -> LateralStiffness, E = a6 Fz + a7, x = SlipAngle x 57.29578 (deg) -> LateralForce, D -> MaxLateralForce.
- longitudinal: C = b0, D = MaxLongitudinalFriction x Fz x FrictionScale, BCD = (b3 Fz^2 + b4 Fz) exp(-b5 Fz), E =
  b6 Fz^2 + b7 Fz + b8, x = slip ratio x 100 -> LongitudinalForce. (b1, b2, b9, b10 and a1, a2, a8..a14 unused: the
  peak is the DB friction, not the Pacejka D.)
- aligning: C = c0, D = (MaxNormalFriction + c1 Fz) Fz FrictionScale, BCD = (c3 Fz^2 + c4 Fz)(1 - c6 |gamma|)
  exp(-c5 Fz), E = (c7 Fz^2 + c8 Fz + c9)(1 - c10 |gamma|), x = slip angle deg -> NormalTorque.
- combination FUN_00868a60: w = (MaxLat x Fz x FS x (-LateralVelocity), MaxLong x Fz x FS x
  (-ContactLongitudinalVelocity)); |w| <= 1e-5 -> forces 0; d = clamp(w / |w|); m = min(|Fy0| / |dy|, |Fx0| / |dx|)
  (one term if the other direction < 1e-5); ellipse = sqrt((MaxLateralForce dy)^2 + (MaxLongitudinalForce dx)^2);
  m = min(m, ellipse); LateralForce = dy m, LongitudinalForce = dx m, ContactSlippingCoefficient = m,
  SlippingCoefficient = m / ellipse, Torque = -LoadedRadius x LongitudinalForce.
California Tire1_Supercar on tarmac: peak longitudinal slip ~7.9 % (B = 0.150 / %, C 1.8, E 0.1); peak force =
1.85 / 1.9 x Fz.
### 4.3 Spoiler / ground effect FUN_00883b20
v_t = point velocity minus its component along the spoiler normal n; L = -0.592 x Coefficient x |v_t| x body(vfunc
0x40) [INF: a speed]; force = n L; with IsDragActionEnabled: + v_t x (DragCoefficient x L / (LiftDragRatio x
|v_t|)). So at lift_velocity the spoiler pushes lift_load x g down and drags lift_load x g / lift_drag_ratio.

---------------------------------------------------------------------------------------------------
## 5. OpenTDU2 implementation (`source/physics/vehicle_data.*`, `vehicle_dynamics.*`)
Decoded values / laws used: all of §3 (GEO, suspension stops / rates / dampers / anti-roll, DBRims radius 0.3393,
tyre friction arrays, Tires.bnk Pacejka, engine curves + torque_nm scaling + IdlePosition, engine / gearbox inertia,
engine damping, rpm limits, reverse = -1st, final drive, shift times, MaxBrakingAction / BrakingBalance, aid curves,
steering curves, SecureAngularDamping, aero coefficients, spoilers drag through LiftDragRatio, no _cx) and §4.2 /
§4.3 formulas (fast atan, float constants).
[INF] (solver not reimplemented): raycast wheels with semi-implicit Euler chassis (3 sub steps); spring law k (x -
LowerStop) (zero at full droop: k |LowerStop| equals the static wheel load of the DB cars, California 4000 / 4500 N);
slip definitions (wR - v) / max(|v|, 1 m/s) and atan(vy / max(|vx|, 1)); wheel inertia 1/2 m r^2 plus the engine
and gearbox inertia x ratio^2 on the driven wheels; open differential (equal split); engine torque Tno + position
(Tmax - Tno), position = IdlePosition + pedal (MaxPosition - IdlePosition), damping torque -_engine_damping x w;
limiter cut for LimiterTime at the red line; automatic shift points (power rpm), launch clutch; brake torque split
half per wheel; ABS / TCS regulation (torque cut against the decoded slip-ratio limits); aero anchors at the axles;
grass = heightfield; box inertia m/12 (..) of the GEO size (edVehicle 0x544400 [ENC]); centre of mass height =
hub line + 0.18; rolling resistance 0.015; sin / atan / exp of the CRT stubs.
Debug: `-vehtest` (flat road plane, friction 0.7): 0-100 / 0-200 / 402 m / top speed / 100-0 runs logged as
`VEHTEST`; `-aids sport|hypersport` selects the other aid columns.

### 5.1 Measurements (California, `-vehtest`, 60 Hz)
| | before (approximation) | after (decoded set-up) | DB |
|---|---|---|---|
| 0-100 km/h | 4.42 s | **4.08 s** | _acceleration_0_100_kmh 4.1 |
| 0-200 km/h | 13.22 s | 13.10 s | - |
| top speed | 315.0 km/h | **312.4 km/h** (7th gear at 7750 rpm = _power_rpm) | _max_theorical_speed 310 |
| 100-0 km/h | 40.5 m | 28.8 m | _braking_100_0 100, _brakingdist 60 (units not known) |

FREERIDE runs (`-startstopped -logdrive`): full throttle down the village street reaches 102 km/h in 3rd before the
street bends (the car leaves the road and ends resting on its chassis spheres on the roadside, as the collision
spheres are unchanged); throttle to 60 km/h then steering right: the car turns into the palm tree / planter on the
right of the street, stops and stays upright on its wheels (`/tmp/claude-0/vp_turn.png`, `vp_drive.png`).

---------------------------------------------------------------------------------------------------
## 6. Open points
- edVehicle solver step (constraint solver, RK4 drive train, clutch / differential / shift controllers, wheel
  contact kinematics filling slip ratio / slip angle): not read in detail [INF in OpenTDU2].
- vfunc 0x9c / 0xf4 (inertia, centre of mass), CRT stubs (sin, atan, exp, fabs, sqrt), curve interpolation
  FUN_004774a0: [ENC].
- Driving aid level switch at runtime (SPORT / HYPERSPORT columns), the player's default level: not found [INF].
- Use of vehicle +0x1f0 (max theorical speed), spoiler DragCoefficient default, world Material (friction / damping
  / RollingResistance per surface), aligning torque application: not read.
- `physics/tires.bnk` `.bpjka`: unreferenced by readable code.
