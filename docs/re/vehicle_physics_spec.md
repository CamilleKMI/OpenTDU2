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
  RK4 drive train). Its step, drive train, controllers and aids are decoded in §5 - §6; the chassis / contact
  constraint core is in the SecuROM range (§6.8).
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
  columns: the game replaces the ABS curve end point by the aid level (§6.6); the level itself was not found [INF].
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
## 5. edVehicle object layout (RWD car, vtable 0xf39e80, 0x1200 bytes) [CODE]
Read from the constructors FUN_0088c9d0 (RWD), FUN_0086dbb0 (4-wheel base), FUN_0087d4a0 (vehicle), FUN_00888d90
(engine), FUN_00884a00 (gearbox), FUN_00877ca0 (clutch), FUN_00882ea0 (differential), FUN_0086fca0 (wheel axle) and
the serializers (`fields*.txt`). Work files: `/tmp/claude-0/vs/` (`all_va.c` decompiles of every vtable entry of
the vehicle / wheel / part / controller classes plus callees, constants annotated; `notes.md`).

| object | where | vtable | content |
|---|---|---|---|
| vehicle | car +0 | 0xf39e80 (base 4 wheels 0xf3af48, vehicle 0xf396e8) | SubStepCount +0x5a0, TimeStep +0x5a4, Frequency +0x5a8, DriveTrainRk4Step +0x5b0, DriveTrainSubStepCount +0x5b4, DriveTrainTimeStepArray +0x5b8 [4], FrequencyArray +0x5c8 [4], Mode +0x5d8, ConfigType +0x5dc, CurrentContext +0x5e0, damping curves +0x4e0..+0x570, LinearDampingOffset +0x4d0 |
| engine E | car +0x8e0 | 0xf394f8 | curves Starter +0x10 / NoThrottle +0x40 / MaxThrottle +0x70, gearbox G at E+0x110, DriveShaft end +0x350 (inertia +0x360, velocity +0x368, connection +0x37c, combined inv. inertia +0x38c), Position +0x390, PreviousCurveAction +0x394, Idle / Blue / Red / Max velocity +0x398..+0x3a4, engine controller at +0x3b0 (Flags +0x3c4, gearbox controller ptr +0x3c8, EngineState +0x3cc (1 at ctor), StarterState +0x3d0, ElapsedTime +0x3d4, StartingTime +0x3d8 (0.65), StallingTime +0x3dc (0.3), LimiterTime +0x3e0 (0.09), PedalPosition +0x3e4, Position +0x3e8, IdlePosition +0x3ec, MaxPosition +0x3f0) |
| gearbox G | car +0x9f0 | 0xf39558 | clutch C at G+0x10, input end +0xd0, output end +0xf0 (connection +0x11c = differential input), engine ptr +0x130, gear index +0x134, GearInertiaArray +0x138, RatioArray +0x144, GearRatioArray +0x14c [10] / Inv / Sqr / InvSqr, gearbox controller at +0x1f0 (Flags +0x14, clutch controller +0x18, Type +0x1c, Mode +0x20, State +0x24, ElapsedTime +0x28, ShiftTimeDown +0x2c, ShiftTimeUp +0x30, ReverseTime +0x34, DriveTime +0x38, MinGearRatio +0x3c, MaxGearRatio +0x40, GearRatio +0x44, ShiftedGearRatio +0x48; ctor 0.15 / 0.15 / 0.6 / 0.5, 0..7, gear 1) |
| clutch C | car +0xa00 | 0xf39498 | input end +0x10 (connection +0x2c = engine DriveShaft), output end +0x30 (connection +0x5c = gearbox input), PartInertiaArray +0x74 (0.001, 0.001), Position +0x7c, MaxAction +0x80 (10000), DegreeOfFreedom +0x84 (slip coefficient), clutch controller at +0x90 (Type +0x14 = 0, State +0x18, ElapsedTime +0x1c, EngagingTime +0x20 = 0.2, DisengagingTime +0x24 = 0, Position +0x28) |
| spoilers | car +0xcf0 / +0xd40 / +0xd90 | | front spoiler, ground effect, rear spoiler (0x50 each) |
| wheels | car +0xce0..+0xcec | 0xf3abb8 | 0x970 bytes each: Tire +0x270, WheelJoint +0x580 (anchor +0x5a0, LoadDistributionAtRest +0x61c), suspension +0x640 (Force +0x6a8, ForceAtRest +0x6b0, flags +0x6d0 bit0 = no contact, stabilizer +0x6f4.., Lower/UpperStop +0x708/+0x70c, partner suspension +0x720), axle part +0x7b0 (vtable 0xf3acc0: shaft end +0x10, connection +0x2c, wheel +0x30, Direction +0x34 (+-1), BrakingPosition +0x38, slip coefficient +0x3c, MaxBrakingAction +0x40, BrakingBalance +0x44, PartInertia +0x48), brake controller at axle +0x50 (vtable 0xf3ad44: Flags +0x14, BrakingPosition +0x1c, ForcedSlipRatio +0x20, SlipRatio +0x24, Velocity +0x28) |
| differential D | car +0xe30 | 0xf399a8 | input end +0x10, first output +0x30 (<-> rear left axle), second output +0x70 (<-> rear right), inertia matrix +0xc8..+0xfc, GearRatioArray +0x100 (final drive), Distribution +0x118, Primacy +0x11c, MaxTorqueBias +0x120 (ctor 0.7), controller +0x130 (vtable 0xf39bc8, Flags only) |
| aids | car +0xf80 | 0xf38fac | curves Abs +0x20 / HandBrake +0x50 / TractionControl +0x80, VelocityActivations +0xb0, SlipRatios +0xbc [3], PedalPreviousPosition +0xcc, PedalPosition +0xd0 (brake), Position +0xd4, Flags +0x10 (bit0 hold, bit2 ABS active, bit3 hand brake, 0x20 TC active) |
| controls | car +0x1190 | 0xf39e38 | vfunc 0xfc / 0xf8 of the vehicle (ENC getter) return it; +8 aids, +0xc steering, state +0x28 |

Base 4-wheel ctor defaults: MaxBrakingAction 12000, BrakingBalance 0.6, WheelBase 2.468, FrontAxleLocalPos 1.188,
LoadDistribution 0.56; per wheel brake MaxBrakingAction = car value x BrakingBalance (front) / x (1 - balance)
(rear), recomputed by the pre-step when the car values change (flag bit 27 -> FUN_0086b750).
Car hierarchy note: the RWD car overrides vfuncs 0x28 / 0x2c / 0x30 / 0x134 / 0x138 / 0x108 / 0x10c / 0x124 / 0x13c...
with functions at 0x545xxx and the wheel class uses 0x54cxxx for vfuncs 0x28 / 0x2c / 0x30 / 0x54 / 0x58: **these
are in the SecuROM range** [ENC].

---------------------------------------------------------------------------------------------------
## 6. The edVehicle step [CODE unless tagged]

### 6.1 Frequency and sub steps
- The physics service thread "PhysThread" (FUN_00bd07c0, started at 0xbe03f6) ticks at **90 Hz** (period =
  counter frequency / DAT_00ede78c = 90.0); each tick FUN_00bd03a0 clamps the car dt to **1/90 s** (DAT_00f4a0b0)
  and calls, per car, FUN_00bbc590(dt) -> FUN_00bacd50(physics car, dt, controls) (controls copied from game car
  +0x380) -> manager vfunc 0xc (FUN_00880e60) -> **vehicle vfunc 0x58 FUN_0087b8a0(dt)**.
- FUN_0087b8a0: vfunc 0x54 (car pre-step FUN_0086da40: FUN_0087d280, FUN_0086c2c0 wheel / spoiler anchors,
  FUN_0086c0d0 LoadDistributionAtRest = LoadDistribution / 2 (front), (1 - LoadDistribution) / 2 (rear) and
  ForceAtRest = share x mass x |g|, FUN_0086b750 brakes, FUN_0086b630), vfunc 0x48 setFrequency(1/dt)
  (FUN_0087ada0 -> FUN_0087ac80), then Mode 0 -> FUN_0087b590, Mode 1 -> FUN_0087b050.
- FUN_0087ac80: Frequency = SubStepCount x f, TimeStep = 1 / that; drive train frequency F = DriveTrainSubStepCount
  x vfunc 0x44() [ENC getter, INF = Frequency]; **DriveTrainTimeStepArray = [1/F, 0.5/F, 0.5/F, 1/F]**,
  FrequencyArray = [F, F/2, F/2, F].
- **SubStepCount = 1** (vehicle ctor 0x87d672; no other writer), DriveTrainSubStepCount = 5 at ctor; the game car
  state 7 (FUN_00baa810, normal driving) sets Mode 0 and **DriveTrainSubStepCount = Physics.cpr
  NbLoopForMiddlePrecision** (+0x1c4, value 3); state 8 (FUN_00baa580) Mode 1 and 1 sub step.
- FUN_0087b590 (Mode 0): if the world object (+0x260) vfunc 0x20 is false: per chassis sub step vfunc 0x30, vfunc
  0x2c, FUN_0087aa70; otherwise FUN_00549640, FUN_00542930 [ENC], 0x30, 0x2c, 0x5c, FUN_0087aa70, 0x60, 0x64,
  0x8c; then the steering column integration (controls +0xc object, +0xf0..+0x11c).
- FUN_0087aa70 (drive train): for DriveTrainSubStepCount: controls vfunc 0x28; for stage 0..3: DriveTrainRk4Step
  (+0x5b0) = stage, parts[i] vfunc 8, vehicle vfunc 0x198, 0x2c, 0x134, 0x138, parts[i] vfunc 0xc; then parts[i]
  vfunc 0x10, vfunc 0x198. The part list (vfuncs 0x108 / 0x10c) and the stage vfuncs 0x198 / 0x134 / 0x138 are
  [ENC]; the parts save / restore their state in vfuncs 0x28 / 0x2c (engine FUN_00887cd0 / FUN_00887ca0, axle
  FUN_0086fb00 / FUN_008706c0): an RK4 of the drive train velocities whose combination is not readable.

### 6.2 Drive train parts: impulses and shafts
Every part has vfunc 0x40 (inertia update), 0x48 (impulses: calls the downstream part's 0x48 first through the
connected shaft end's owner, then adds its own), 0x4c (apply), 0x54 (velocity propagation upwards) and 0x58
(downwards). A shaft end holds Inertia (+0x10), Velocity (+0x18), Impulse (+0xc / +0x30), DampingArray[3]
(+0x20, default [0, 0, 1e6]) and its combined inertia (+0x38, inverse +0x3c); the velocity of a connection is
the momentum weighted mean (v1 I1 + v2 I2) / (I1 + I2) unless the "locked" flag (+8 bit1) is set.
- Shaft damping FUN_008877e0: c = |v| > D2 ? D1 |v| : D0; impulse = -c v dt, clamped by FUN_00887400 so that it
  never reverses the shaft (|J| <= |v I|). Engine D0 = `_engine_damping` (0.05), gearbox: `_gearbox_damping` on
  the clutch output end.
- **Engine torque FUN_008880f0**: EngineState 0: CurveActionStarter(w); 1 or 3: **T = P Tmax(w) + (1 - P) Tno(w)**
  with P = engine Position (+0x390); other states: Tno(w). Tno(w) = min(CurveNoThrottle(w), -1e-5) (w >= 0, else
  -1e-5, FUN_008880a0); Tmax(w) = max(CurveMax(Idle), CurveMax(w), 1e-5) for w <= MaxVelocity, 1e-5 above
  (FUN_00888020). IdlePosition = Tno(idle) / (Tno(idle) - Tmax(idle)) (FUN_00888200).
- **Engine impulse FUN_00888550**: downstream impulses first; if T > 0 the damping impulse is added; then
  T x dt_stage; PreviousCurveAction = T; stall detection (flag 4) when T < 0 after T > 0 and FUN_00476920 [ENC]
  > 300.
- **Engine speed clamp** (vfunc 0x58 FUN_008884c0 -> FUN_00887f10): EngineState 1 and controller flag bit1 clear
  -> v = clamp(v, IdleVelocity, MaxVelocity).
- **Clutch** (FUN_00877e50): maxImp = clamp((1 - Position) x MaxAction x dt, 0, 100); need = |(v_in - v_out)
  I_in I_out / (I_in + I_out)| (I_in = engine side + PartInertia[0], I_out = gearbox side + PartInertia[1]);
  DegreeOfFreedom c = 1 if maxImp = 0, clamp(1 - maxImp / need, 0, 1) if maxImp < need - 1e-5 and c != 0, else 0:
  **a locked clutch stays locked** until it is opened. Combined inertias FUN_008785e0: ((1 - c) I1 I2) / (c I2 + I1)
  + PartInertia; velocities (vfuncs 0x54 / 0x58 FUN_00877720 / FUN_00877dc0): momentum weighted x (1 - c) + own x c.
- **Gearbox** (FUN_008852d0 / FUN_00885230 / FUN_00884310): input inertia = GI0 + (I_down + GI2) InvSqr[g] InvSqr[9]
  + GI1 InvSqr[g]; output inertia = GI2 + GI1 Sqr[9] + (I_up + GI0) Sqr[g] Sqr[9]; RatioArray = (R[g] R[9],
  1 / (R[g] R[9])); output impulses go up divided by the ratio (FUN_00884480), input impulses down multiplied
  (FUN_00884c30). GearRatioArray index 0 = reverse (-1st), 1 = neutral, 2.. = 1st.., 9 = 1.0.
- **Differential** (FUN_00882920 / FUN_00881540 / FUN_00881680 / FUN_00881ab0 / FUN_00882a70): two degrees of
  freedom with an inertia matrix (part inertias 0.001 x 5, GearRatioArray (3 / 9 / 1 at ctor, final drive)); the
  velocity propagation FUN_00882420: **MaxTorqueBias = 0: both outputs at the carrier speed (spool); 0 < bias < 1:
  the half difference of the outputs is limited to |bias m / (2 - bias)|** (m = carrier speed in output units),
  i.e. the speed ratio of the two wheels stays under 1 / (1 - bias) (California 0.70: 3.3).
- **Wheel axle** (vfunc 0x48 FUN_00870630): inertia update, then the **tyre update FUN_00869c30 at every stage**
  and impulse += Torque x dt_stage x Direction. Slip coefficient FUN_0086fea0: Imax = MaxLongitudinalForce x R x
  dt_stage, Icar = mass x LoadDistributionAtRest x R^2, Itot = PartInertia + connected shaft inertia, need = |(w -
  LongVel / R) Icar Itot / (Icar + Itot)|; c = 1 if Imax = 0, else clamp(1 - Imax / need, 0, 1) when Imax < need -
  1e-5, else 0. **Axle inertia FUN_008701f0 = ((1 - c) Itot Icar) / (c Icar + Itot) + PartInertia**: a gripping
  wheel carries its share of the car inertia, a sliding one only its own.
  FUN_0086fd50: setting the spin updates ContactLongitudinalVelocity = LongVel - R w and the slip ratio
  (FUN_0046e760 [ENC]).
- **Brake impulse** (FUN_00870300): J = -sign(v) x dt x MaxBrakingAction(wheel) x position, stop clamped.

### 6.3 Engine controller (FUN_0088a410, first the gearbox controller update)
State 0 starting: t += dt, t > StartingTime -> 1. State 1 running: Position = min(Position, MaxPosition); if
FUN_00546910(engine) [ENC]: Position clamped to [IdlePosition, MaxPosition], and with flag bit1 (stall allowed)
and the clutch not open and not in neutral -> state 3; else if FUN_00546810(engine) [ENC] and LimiterTime > 0 ->
**state 2 (limiter: no-throttle torque) for LimiterTime** (`_rupteur_time`). State 3 stalling -> 4 after
StallingTime. Phase E+0x100 += dt (mod 2 pi, rumble). The engine Position used by the torque is restored from the
controller Position (vfunc 0x2c).

### 6.4 Clutch controller (FUN_008810c0, FUN_00880fe0)
States 0 engaging, 1 engaged, 2 disengaging, 3 disengaged. Engaging: Position = max(1 - t / EngagingTime, 0) (0 time
-> 0); disengaging: min(t / DisengagingTime, 1) (0 time -> 1). setPosition(p): p = 0 -> 1, p = 1 -> 3, p > Position
-> 2, else 0.

### 6.5 Gearbox controller (FUN_008866a0 and state handlers)
- Each update: aids Position = brake pedal, engine controller Position = throttle pedal; flag 8 ("both pedals") =
  automatic box and low speed (|v|^2 <= 100, vehicle flag 0x8000 from FUN_0087ebf0) and brake != 0 and throttle >
  IdlePosition.
- States: 0 shifting down / 1 up (FUN_00885ae0 / FUN_00885a50 -> FUN_008858e0 with ShiftTimeDown / Up; engine
  Position = 0 while shifting), 2 reverse (FUN_00885e20: **pedals swapped**), 3 neutral (FUN_00886440), 4 drive
  (FUN_00886230).
- Shift FUN_008858e0: neutral target -> immediate; else the clutch is opened (state 2 -> 3 with DisengagingTime 0),
  the shift time runs while it is open, then FUN_00546f00 [ENC] completes the shift; the drive state re-engages the
  clutch (EngagingTime) and **cuts the throttle pedal during the engagement except in 1st or under 10 m/s**.
- Neutral: clutch opened; automatic box with the engine running: both pedals -> aids Position = max(brake,
  throttle); at low speed: rolling back (FUN_0053fbd0 [ENC] < -0.1): full brake -> reverse, throttle -> 1st; standing
  and FUN_00546ec0 [ENC] with full brake for **ReverseTime -> reverse**; throttle > IdlePosition -> 1st.
- **Automatic shift** (drive state, flags of FUN_00884d80 computed after each velocity propagation): **up when the
  engine is above the red line and the car speed (vfunc 0xcc [ENC]) is above 0.8 x vfunc 0x120(red line) [ENC]**;
  **down when gear > 1st and R[9] R[g-1] x gearbox output speed < blue line**; neutral when both pedals or when
  the engine idles (Position <= IdlePosition and v <= Idle + 1) with the throttle under IdlePosition.
- End of the update: clutch open request (flag 0x10) = handler request | both pedals (Mode 0) | hand brake; aids
  bit0 (wheels held) = both pedals.
- The game (FUN_00bacd50) switches BlueLine / RedLine between `_blue_line` / `_red_line` and `_blue_line_sport`
  / `_red_line_sport` with a throttle hysteresis (sport when throttle >= `_throttle_ratio_sport` x 0.01, back to
  normal when throttle <= `_throttle_ratio_normal` x 0.01, tested when the throttle rises).

### 6.6 Brake controller, ABS, traction control, hand brake (FUN_00865750, per wheel, per stage)
- Hand brake (controller flag bit2 and aids bit3): target velocity (1 + SlipRatios[1]) LongVel / R, BrakingPosition 1
  (under 1 m/s: target 0); held wheels (aids bit0 or controls state 3): target 0, position 1.
- Pedal: BrakingPosition = aids Position; **ABS FUN_00865460** (when FUN_0054eae0 [ENC]): target = (1 +
  SlipRatios[0]) LongVel / R (0 under 1 m/s); fraction = |(target x Direction - v) I| / (dt MaxBrakingAction)
  (FUN_0086f950: 0 if the impulse would not oppose the spin, at most 1); if fraction < BrakingPosition:
  BrakingPosition = fraction, aids bit2.
- **Traction control FUN_008652b0** (when FUN_0054eaa0 [ENC]): same with SlipRatios[2]; if BrakingPosition <
  fraction: BrakingPosition = fraction, aids 0x20 — **the traction control brakes the spinning wheel**, it does not
  cut the engine.
- SlipRatios[i] = the three aid curves at |speed| (FUN_0087ebf0). The game replaces the high speed point of the ABS
  curve with `_abs_slip_ratio_secure / sport / hypersport` (or `_off`) by the aid level (game car +0x40, FUN_00bacd50
  tail); the level itself is a player option (not read).
- Dynamic FrictionScale (FUN_0086a990): grass and contact normal y <= 0.8 (manager +0x2c4) -> 0.2; braking
  (BrakingPosition != 0) and slip ratio < 0 -> BrakingFrictionScale; wet (tyre flag bit3, set by FUN_00bacd50 when the
  weather wetness > 0.33) -> WetFrictionScale (x BrakingFrictionScale when braking); else 1.

### 6.7 Chassis-level laws
- Tyre NormalForce clamped to 1e5 N (FUN_008739c0 / FUN_008720e0).
- Spoiler anchors (FUN_0086c2c0): front (0, 0, FrontAxleLocalPos - COM z), rear (0, 0, FrontAxleLocalPos -
  WheelBase - COM z) in the centre of mass frame (at the centre of mass height), ground effect at the centre of
  mass.
- Angular damping FUN_0087af20: CurveOffRoadAngularDamping when every grounded wheel is on dirt / grass, else
  CurveAngularDamping. Linear damping FUN_0087af70: LinearDampingOffset + CurveOffRoadDirt / Grass by
  CurrentContext (FUN_0087ab80: 1 when all wheels are on dirt, 2 all on grass, 0 when no wheel is off road).
- Extra gravity (FUN_0087ebf0): NoWheelOnGroundElapsedTime t; factor = Min (t < AccelDelay), linear to Max over
  AccelDuration, Max after; passed to FUN_00548370 [ENC].
- Ground material -> tyre surface (game FUN_00ba81a0): 1 dirt for ids {1, 0x3d, 0x3e}, 2 grass for {2, 4, 5, 0xb..0xd,
  0x2b..0x2e, 0x42}, 0 tarmac otherwise.
- World gravity -9.81 (DAT_00f4b4d0).

### 6.8 Not found / encrypted [ENC]
Motion integration and the constraint solver (car vfuncs 0x28 / 0x2c / 0x30 / 0x134 / 0x138 / 0x194 / 0x198 at
0x544xxx-0x545xxx, wheel vfuncs 0x54cxxx), the suspension force law (suspension vfunc 0x24 = 0x54bc70 / 0x54bac0),
the wheel contact (FUN_008720e0 calls ENC vector helpers 0x42xxxx-0x47xxxx), the axle brake application (axle vfuncs
0x54 / 0x58), the RK4 combination, the ABS / TC activation tests (FUN_0054eae0 / FUN_0054eaa0), the slip ratio from
the spin (FUN_0046e760), the wheel inertia from its mass (vfunc 0x9c), the chassis inertia (0x544400), the predicates
FUN_00546910 / FUN_00546810 / FUN_00546ec0 / FUN_0053fbd0 / FUN_00546f00 and the speed getters vfunc 0xcc / 0x120 /
0x124.

---------------------------------------------------------------------------------------------------
## 7. OpenTDU2 implementation (`source/physics/vehicle_data.*`, `vehicle_dynamics.*`)
- [CODE] 90 Hz ticks (1/90 s, frame time consumed in ticks), one chassis sub step, NbLoopForMiddlePrecision drive
  train sub steps of 4 stages [h, h/2, h/2, h]; engine torque / damping / speed clamp, engine / clutch / gearbox
  controllers and the automatic shift flags, sport / normal lines, clutch slip law, gear ratios, open differential
  with the MaxTorqueBias speed limit, axle slip coefficient and effective inertia, tyre torque at every stage, brake
  impulse, ABS / TC / hand brake regulation, FrictionScale, NormalForce clamp, aero anchors, extra gravity ramp,
  damping curves and context, Pacejka / combination (§4).
- [INF] (ENC or not read): the RK4 combination (classic weights 1/6, 2/3, 2/3, 1/6 over the stage increments);
  the controllers run once per drive train sub step with the sub step time; the shaft network reduced to the
  engine shaft, the open differential carrier (equal torque split) and the axles, the locked chain solved as one
  impulse; ENC predicates: idle governor when v <= idle, limiter when v >= MaxVelocity, vfunc 0xcc = forward speed,
  vfunc 0x120(w) = w / (ratio x final) x R, FUN_00546ec0 = throttle under IdlePosition, FUN_00546f00 = gear engaged,
  ABS active above AbsControlActivation, TC on driven wheels above TractionControlActivation; brakes with a target
  drive the wheel towards it within BrakingPosition x MaxBrakingAction x dt; brake controller flags (brakes on all
  wheels, hand brake on the rear); reverse exit at low speed when the reverse pedal (brake) is released.
- [INF] chassis (§6.8): raycast wheels on one rigid body (semi-implicit Euler), spring k (x - LowerStop), dampers,
  bump stop, anti-roll; contact coupling: longitudinal chassis impulse = (drive + brake impulse - own axle inertia x
  spin change) / R, limited by MaxLongitudinalForce x dt within the friction ellipse (the rest stays in the spin),
  gripping axles then follow the contact speed (momentum weighted with their car share) and the engine follows the
  carrier when the clutch is locked; lateral = Pacejka force limited at low speed; rolling resistance 0.015;
  wheel inertia 1/2 m r^2; box chassis inertia; centre of mass height = hub line + `_gravity_center_height`;
  grass = heightfield (no material ids in the OpenTDU2 collision); no weather (dry); steering angle rate-limited by
  the SteeringVelocity curve (the steering column / feedback of FUN_0087b590 not decoded).
- Debug: `-vehtest` (flat plane, friction 0.7): 0-100 / 0-200 / 402 m / top speed, 100-0, steady-state cornering
  (steering 0.25 / 0.5 / 1, 60 / 100 km/h, speed loop) logged as `VEHTEST`; drive train trace `VEHTEST dt`
  (`getDebugState`); `-aids sport|hypersport` selects the aid columns.

### 7.1 Measurements (California, `-vehtest`)
| | before (raycast model, decoded set-up) | after (edVehicle drive train) | DB |
|---|---|---|---|
| 0-100 km/h | 4.08 s | **4.65 s** (launch from neutral: shift 0.1 s + clutch 0.2 s, wheel spin until the TC activation at 10 km/h, TC target slip 0.5 -> 0.1 between 10 and 70 km/h) | _acceleration_0_100_kmh 4.1 |
| 0-200 km/h | 13.10 s | 14.02 s | - |
| 402 m | 12.12 s | 12.78 s | - |
| top speed | 312.4 km/h | **309.7 km/h** (7th gear) | _max_theorical_speed 310 |
| 100-0 km/h | 28.8 m (2.08 s) | 34.5 m (3.33 s, ABS target -0.08) | _braking_100_0 100, _brakingdist 60 (units not known) |
| corner steer 0.25, 60 km/h | 0.90 g, R 32.0 m | 0.88 g, R 31.8 m | - |
| corner steer 0.25, 100 km/h | 1.44 g, R 54.4 m | 1.44 g, R 53.8 m | - |
| corner steer 0.5, 100 km/h | slows to 32 km/h, 0.86 g | spins (speed loop throttle) | - |

Shift points measured: up-shifts at 7900-8000 rpm (red line 8000, normal = sport for the California), 1st -> 2nd at
64 km/h. FREERIDE (`-logdrive`): full throttle down the village street: 1st -> 2nd at 64 km/h,
2nd -> 3rd at 101 km/h, the car leaves the road where the street bends (no steering) and stops upright against a
tree; throttle to 80 km/h, brake, then steering left: down-shifts 2 -> 1 at 51 km/h (blue line rule), the car turns
into the fence on the left side and stays on its wheels (screenshots `fr_accel.png`, `fr_turn.png`, `fr_steer*.png`,
plot `accel_before_after.png` in the session scratchpad `vehsolver/`).

---------------------------------------------------------------------------------------------------
## 8. Open points
- The chassis / contact constraint solver, suspension law, RK4 combination and the ENC predicates listed in §6.8.
- Steering column dynamics (FUN_0087b590 tail, FeedbackImpulse) and the steering control object.
- The game's gear request object (game car +0x24: manual / sequential requests) and its ReverseTime use.
- Aid level of the player (game option, game car +0x40) and the TC curve replacement per level.
- Ground material ids of the road / terrain collision (needed by FUN_00ba81a0); weather wetness.
- `physics/tires.bnk` `.bpjka`: unreferenced by readable code.
