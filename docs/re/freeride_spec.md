# TDU2 story step 2 "First Freeride": C_FIRST_OUTRO, driving the California, chase camera, HUD

Tags: [CODE] readable x86 (VA >= 0x6c1000) · [DATA] game files · [INF] inferred (reason given) ·
[ENC] logic not in readable code (SecuROM range < 0x6c1000 or not found), not read · [MISSING] the data file is
not in the uploaded game copy.

Work dir: `/tmp/claude-0/fr/` (decompiles `d1.c` … `d7.c`, private Ghidra copy `gh/` + `dec.sh`; `eph_c.txt` =
annotated entities of `islands/ibiza/exterior/eph_l3di.bnk`; `cams.txt` = xmbdump of `database/camerasingame.xmb`;
`Physics.txt` = decrypted Physics.cpr; `s567/` = extracted `sector-5-0-6-7.bnk`; `cal/` = extracted
`vehicules/high/california.bnk`; `tires/` = `physics/tires.bnk`; `gw.txt` = `islands/ibiza/fx/game_weathers.xmb`).
Previous step: `docs/re/tess_garage_spec.md` §1.2 / §3.7 (end of C_OUTRO in FIRST_WM → story step 2).

---------------------------------------------------------------------------------------------------
## 1. Story step 2 sequence

### 1.1 Step enter (FUN_00b10da0 `case 3`, event 0xc0c0) [CODE, tess_garage_dec1.c 540–620]
```
profile+0x58 = 2; FUN_00acee30(0)
timer(this[7]) = DAT_00ede78c (90.0)
FUN_00b9ff50("Scenarics\\Cinematics", "\\intro\\fade_freeride", this+0xc, 0)  // loads fade_freeride.bnk into the step's scene slot
car 0x183 (387 California) -> FUN_00aba280 (current car)              // [INF] "set current car", as step 1
DAT_01025938 = &DAT_00f35018; DAT_01025a60/64 = 0                      // [INF] GPS target cleared
spot = FUN_00b31490(0x957885a4, 0x49447c83)   // house EPH_L3DI_5067I (spot list lookup by 64-bit id)
if spot: sprintf("Level/%s/spots", islandName); FUN_00b99c20("C_FIRST_OUTRO", spot)
*(DAT_010017f0+0x1c) = 0; DAT_00fd50fc+0x31 = 1
game mode := 1 (FREERIDE)  (previous mode kept in +0x12c)
this+0x701 = 1 (re-sync the car next tick); this[0x1c0] = 0
```
FUN_00b99c20 [CODE d1.c] only **stores a request**: `DAT_01025824 = name, DAT_01025828 = spot, DAT_01025820 = 1,
DAT_01025830 = 1 (target mode), DAT_0102582c/34 = extra args`.

### 1.2 FREERIDE start consumes the request [CODE d2.c, d3.c]
- FUN_00a7e500 (FREERIDE init, `this` = mode+0x90): copies the request to mode+0x1c3e1c..+0x1c3e30, clears it.
  With a request: `+0x1c3e3e = 0`, FUN_00a7cc70 (play it), `+0x1c3e50 = FUN_00b32d30()`; without:
  `+0x1c3e3e = 1`, DAT_010017f8 = 2.
- FUN_0099c090 (camera / HUD setup of FREERIDE; skipped while the request targets this mode): HUD + `GAUGES`
  movies enabled (FUN_00d7fd80/FUN_00d7fbe0), clear colour (0x22,0x20,0x32,0xff), **camera type 3** (FUN_00ca0840(3),
  [INF] the in-game car camera; type 9 is the walk camera, tess_garage_spec §4.3), car graphics +0x3a9 = 0.
- FUN_00a7cc70 (per frame while `+0x1c3eac == 1` and no cinematic yet): when `mode->vfunc+0x24() == 1`
  ([INF] world streaming ready) and the request kind `+0x1c3ebc == 1`: `cine = FUN_00b315e0(name)` — looks the
  spot up in the 6 loaded spot slots (DAT_01024b88 + i·0x5f4, slot type `+0x13c == 4`) and finds the `CINE`
  entity by name (FUN_00b34fd0(spot, 'CINE', name)); listener = mode, `cine+0xb61 = 1`; **FUN_00c41100(1) on the
  player car** ([INF] puts the car under cinematic control: toggles car+0x79bc state through FUN_00baec60 /
  FUN_00bbb810); skippable (`CinProps |= 4`) when FUN_00425d70(); spot+0x20..+0x2c copied to DAT_010257d0
  ([INF] spot position for the map). No cinematic → `+0x1c3eac = 0`, FUN_00a7c5b0, `+0x1c3ece = 1`, FUN_00ada090.
- Listener FUN_00a7c630 [CODE]: event **3 (ended)**: FUN_00a7c5b0 (detach), `+0x1c3eac = 0`, `+0x1c3ece = 1`,
  FUN_00ada090; then FUN_00ca18b0 (**camera back to the game camera**: FUN_00ca0df0(0, 1.0), clears cam+0xf60,
  FUN_00c8b3e0(1) unless flag 0x20000), FUN_00d45ea0, FUN_00d7e690/FUN_00d7c120 (HUD back on). Event 5 (start):
  HUD movies hidden (FUN_00d7bed0, FUN_00d7d8c0/FUN_00d7e950 for the 6 slots). Event 6: mode flags |= 0x26.
- How the car pose/velocity is handed from the PLAYER_CAR track to the physics at the end: [ENC] (the cinematic
  object binding FUN_00b3c5f0 ['PLAYER_CAR' string] only links the actor to the car +0x79bc; the per-frame drive is
  not in the functions read). [INF] the car keeps the last cinematic pose and its track velocity.

### 1.3 Step tick (event 0xc0c1, `case 3`) [CODE tess_garage_dec1.c 445–520]
```
if this[0x1c0]: goto step 3
if timer > DAT_00ecb138 (0): timer -= dt
if car && this+0x701: FUN_00c3c2d0(1); FUN_00c3c050(1); FUN_00c3c140(); [FUN_00c3c1c0()]; FUN_00c3c1a0(); this+0x701 = 0
if timer < 0 && this[6]:  C_FADE_FREERIDE of the step bank (fade_freeride): listener = this+5, +0x628 = 1, +0xb54 = 0,
                          start (FUN_00b40180); timer = 0;   (not found -> step 3 at once)
if mode == FREERIDE && car && !freeride+0x1c3eac (no spot cinematic) && !this[6]:
    "/:IsWalkmode" = false (GENERAL)
    pop_goal(Loc 0x13ae = 5038 "Enjoy!")             // FUN_00d61790: /:GoalTxt + goto "open", 30 s auto close
    SetGoal(FUN_00b09f50("TUTO_ENJOY"))
    FUN_00d46f10(&DAT_01197e10, 0, 0)                 // traffic manager reset, flags 0 ([INF] traffic on)
    [DAT_00fd92d8: FUN_00a766d0(0)]
    FUN_00c3fcc0(car)                                 // **opens the roof** (California _car_body 4, california_spec §2)
    car+0x1e8 = car+0x1ea = 0
    FUN_00c3c2d0(1); FUN_00c3c050(1); FUN_00c3c140(); [FUN_00c3c1c0()]; FUN_00c3c1a0()
    timer = 90.0; this[6] = 1
```
So: **C_FIRST_OUTRO (14.31 s) → control given → "Enjoy!" → 90 s of free driving → C_FADE_FREERIDE (white fade,
Tess knocks) → step 3 "Meeting Tess"**. Leave (0xc0c2): FUN_00b9ff00 (unload the step bank), FUN_00a766d0(1).
FUN_00c3c2d0 / c050 / c140 / c1a0 [CODE d1.c]: re-attach the car physics (+0x79b4 → FUN_00aa85b0 / FUN_00c43c00 copies
the rigid body state +0x124.. / +0x274.. / +0x144.. / +0x334.. / +0x3b4) and graphics (+0x79c0) of the player car and
its driver (+0x79bc, FUN_00bb39f0) [INF names].

### 1.4 C_FIRST_OUTRO [DATA, eph_l3di.bnk, `eph_c.txt`]
Entity__Cinematic `C_FIRST_OUTRO`: CinProps 897 (0x381: bits 0, 7, 8, 9), CinEnd 3, Priority 1.5,
DefaultShadowConfig CINE_DEFAULT, cin `eph_l3di_sc_M_FIRST_OUTRO.cin`, sce `M_FIRST_OUTRO_first_outro.sce`,
additional anims co_walk6passtop*, co_walk8pas, ph_removalgivekey (‑m/‑w), FaceFX realtor‑e‑m/w (unused here).
Siblings in the same bank: C_OUTRO (CinProps 4), C_OUTRO_GARAGE (FUN_00a70b20 case 3), C_INTRO_E_1/2, C_OPEN,
C_CLOSE, C_INTRO_SHORT, C_VTOUR_E, C_REMOVAL.

Timeline (cin 14.31 s):
```
0.00  c_first_outro_01 [sce 0.00-3.74]  fade 1 / 1.0 s   shad:CINE_NEAR  zcvn   Fx FxM_FIRST_OUTRO (music m_first_outro_CINEMATIC.wav, 14.32 s)
3.74  c_first_outro_02 [3.75-5.75]
5.74  c_first_outro_03 [6.49-13.06]                     shad:CINE_FAR
7.12                                                     zcde
12.31 c_first_outro_04 [13.07-15.07]                    shad:FREERIDE
14.31 end
```
Scene (15.06 s, origin identity = house frame): cameras c_first_outro_01 (25.58, 0.415, −1.47) Fov 35,
_02 (31.9, 0.41, −29.3) Fov 45, _03 (19.95, 0.59, −35.7)→(20.72, 12.57, −37.11) Fov 45→50, _04 (140, 3.0, −347.8)
→(140.7, 1.80, −348.3) Fov 40; **PLAYER_CAR** (object type 7, 351 position keys / 8 rotation keys): (24.89, 0.15,
−3.425) rot (0, −0.7234, 0, 0.6904) → **(148.3, −0.4754, −353.4) rot (0, −0.8838, 0, 0.4678)**; `Door_garage`
(entity pos (27.686, 2.6, −3.5315), roll-up rotation animated, 8 keys); `Avatar` hidden below ground (0.80, −12.8,
−39.5). REFS messages hlof / hlon / moof / moon / avm4 (handlers [ENC]; [INF] headlights / motor off-on).
- House spot (spots.bin, id EPH_L3DI_5067I): T = (36382.324, 5.920, 5729.771), heading θ = 2.1903 rad (read by
  OpenTDU2 from spots.bin; intro_spec quotes 2.1934);
  world = T + R_y(θ)·local (intro_spec §12.1). End pose of PLAYER_CAR in world (on the road collision) =
  **(36008.46, 5.51, 5814.22)** — this is the village street under the "BIENVENIDOS" banner of the reference
  screenshot (checked by rendering it: same houses, banner, palm trees, road) [DATA+render check], heading
  of the car ≈ 92.7° + scene yaw. The car model forward is +z (the car moves along conj(q)·(0,0,1)) [DATA].
- Game-side spawn: at the end of C_FIRST_OUTRO the car is where PLAYER_CAR ends (sce t = 15.06) [INF, §1.2], moving
  at the track speed (last 0.5 s of keys ≈ 20–25 m/s) [INF], the camera is the game camera (type 3) [CODE].

### 1.5 C_FADE_FREERIDE [DATA, fade_freeride.bnk]
CinProps 32, CinEnd 3; cin `fade_blanc_M_FADE_FREERIDE.cin` 15.0 s, one shot on `NO_CAMERA` (5.90, 1.22, −5.01) Fov 35
(fade to white [INF from the cin name]); Fx at 2.68 s `FxIntroNew-E_Meet_Tess_02_00` (12.34 s); actor WOMAN (Tess,
`ev_tessincar_toplayer01_npc.anm`) with two voice Fx at 3.90 / 10.17; `s_toctoc_CINEMATIC.wav` (knock on the window).
`fade_blanc_57E5C829CCD9F2C6.shk` collision. The 16 `IntroNew-E_Meet_Tess_*` Fx roots are the step-3 voices.
Its end (listener FUN_00b10580, event 7: story +0x700 = 1) → story step 3 "Meeting Tess" (game mode CINE,
`docs/re/story_spec.md` §2–3). Implemented: the white fade [INF] held while C_FADE_FREERIDE plays its Fx, then CINE.

---------------------------------------------------------------------------------------------------
## 2. Car simulation

### 2.1 Engine of the simulation
- The exe links the **Havok vehicle SDK**: RTTI hkpVehicleInstance (vtable 0xea8794, methods 0x7bf750…0x7cb2b0),
  hkpVehicleDefaultEngine (0xea7d98), …DefaultTransmission, …DefaultSteering (0xea8100), …DefaultSuspension (0xea7fc8),
  …DefaultBrake, …DefaultAerodynamics, …DefaultVelocityDamper, …DefaultAnalogDriverInput, hkpVehicleRaycastWheelCollide
  (0xea7eac) [DATA RTTI]. Their code is in the readable range (0x7bf000–0x7cc000) [CODE address], not decompiled
  here: it is the stock Havok 5.5 raycast vehicle (per-wheel raycast suspension, friction solver on the chassis).
- Eden layer: FUN_00bb7120 [CODE d7.c] creates the driving parameter block: copies **0x4c8 bytes of the Physics.cpr
  [Car] block** (DAT_00fa99e8; [Bike] DAT_00fa9eb0 for categories 0x12/0x13/0x16) to vehicle+0x84, then reads the
  DB record (DB record +0x358/+0x35c ints / constant → +0x18/+0x1c, +0x1d4 → params+0x1c0 …). The Physics.cpr
  loader (FUN_00d6b5b0) stores e.g. STEERINGMAXANGLE as 5 (speed, angle) pairs at config+0x39c..0x3c0 (handler
  0xd677e0) [CODE]. The per-frame use of the block (vehicle +0x84) by the dynamics: not followed → [INF].
- `_newdyna = 1` for the California [DATA].

### 2.2 Physics.cpr [Car] [DATA, Physics.txt]
| key | value |
|---|---|
| ElectronicParams | SPORT (SECURE 1.7, SPORT 2.5, HYPERSPORT 3.0; "old dynamics, useless") |
| NbLoopForMiddlePrecision 3, ReverseTime 0.6, DriveTime 0 | |
| ExtraGravityMin / Max / AccelDuration / AccelDelay | 0.1 / 1.0 / 0.1 / 0.01 |
| CollisionOffset | 0.1 (bottom of the body to the bottom of the collision box) |
| Off/NotOffRoad Dirt/Grass LinearDamping | (km/h, damping): NotOffRoadGrass 0:0.15 100:0.15 200:0.5 300:0.5 400:1.0 … |
| SportAngularDamping | 0:0.15 100:0.3 200:0.6 300:1.0 400:1.0 (Secure 0.4…2.0, HyperSport 0…0.8) |
| AbsControl | activation 0, v 1–50 km/h, slip −0.2 (all aids) |
| TractionControl | activation 10, v 10–70, slip ratio 0.5 / 2.0 / 20.0 (SECURE/SPORT/HYPERSPORT) |
| HandBrake | activation 0, v 1–50, slip −0.4 / −0.5 / −0.6 |
| SteeringVelocity (km/h → °/s) | 0:380 100:370 200:360 300:350 400:340 |
| SteeringMaxAngle (km/h → °) | 0:30 100:13 200:10 300:8 400:6.5 |
| SteeringSensitivity | 1.0 everywhere |
| Force feedback / vibration | (not used here) |

### 2.3 Database record (DBCarPhysicsData id 387 "California") [DATA, db.txt line ~50510]
mass 1735 kg, gravity_center_height 18 (cm above the axle line [INF]), gravity_center_length 47 (% front [INF]),
wheelbase 2693, track 1620 / 1605, length 4555, width 1895, height 1300 (mm), ride_height 110 / 110 (max 60),
overhang 943 / 919, tire_radius 64 (?), tires_type "Tire1_Supercar", tires_physics 0xc (= DBTiresPhysics
"Tarmac3"), suspension_length 100 / 100 (mm), suspension_rate 80 / 90, damper_rate 8 / 9, antirollbar 16 / 16,
toe 1 / −0.5, caster 6, camber 0; max_lateral_friction 1.85 / 1.9, max_longitudinal_friction 1.85 / 1.9,
max_normal_friction 2 / 2, wet_friction 80 %; brakes_power_front 15000, brakes_power_rear 90 (%), brake_balance 60,
abs 1 (slip −0.08 sport); drive 4 (enum, [INF] RWD for the California), differential rear 1 / 70;
engine: power 454 bhp @ 7750, torque 485 Nm @ 5000, rpm 900–8500, red line 8000, blue line 6200, engine_inertia 20,
engine_damping 0.05, engine_brake_nm 0, rupteur_time 0.09, time_ignition 0.55; gearbox 3, 7 gears 3.40 / 2.19 / 1.63 /
1.28 / 1.09 / 0.86 / 0.72, final 4.44, shift time 0.1 / 0.1, max_power_shift_up 70 %; aero: cx 0.32,
lift_load_front / rear 170 / 300 (kg at lift_velocity 300 km/h [INF]), ground_effect 150 @ 300; max theoretical
speed 310 km/h, 0–100 4.1 s; wheel_mass 20 / 25; camera 387; hud_file_name "California"; car_body 4 (convertible).
`source/database/gs_database.cpp` parses most of these into CarPhysicsData.

### 2.4 Car bank data [DATA, vehicules/high/california.bnk]
- **Torque curves**: `california_max.xmb` (full throttle, Nm vs rpm): (0,0) (586,374) (831,397) (1842,424) (3032,450)
  (4156,474) **(5000,485)** (6281,475) (7083,456) (7754,430) (8424,388) (8500,0); `california_min.xmb` (closed throttle,
  engine brake): (0,0) (359,−40) (633,−48) (8500,−155). Curve {NbPoints, PointArray Vector2[], Abscissa[0..8500],
  MaxPoint}. (Power check: 430 Nm × 7754 rpm = 349 kW = 468 hp ≈ DB 454 bhp.)
- **Chassis collision** `california.dhk` (Havok 5.5, havok_collision_spec): body 0 (motion 4) at (0, 0.48, −0.388)
  = hkpListShape of 12 boxes (main half-extents (0.936, 0.291, 1.320) …); bodies 1–5: hood (mass 50) / doors (10) /
  mirrors (5) hinged (limits 0…0.017, 0…0.175, 0…1.484 rad). Extra block: chassis **mass 1301, COM (0, 0.48, −0.388),
  inertia diag ≈ (966.6, 1296, 406)** (pitch, yaw, roll). DB mass 1735 differs → [INF] the DB value is used for the
  Havok chassis (inertia scaled).
- Wheels: nodes `D_RIM_xx` / tires `?_TIRE_xx`, 19" rims, 245/40 (CinematicCar::Desc).

### 2.5 Tyres [DATA, physics/tires.bnk]
6 files `tire1..6.bpjka` (384 bytes): Pacejka-style coefficient block (e.g. 1.51, −45.33, 1150, … / 1.353, −45.21,
1271, 1602, 20.49 …, matching the '94 longitudinal b0..b13 / lateral a0..a17 magnitudes [INF]) and two 32-entry
decreasing tables at +0x80 (55.8 → 5.9) and +0x100 (0.093 → 0.0043) [layout not decoded]. Mapping
`_tires_type`/`_tires_physics` → file: [ENC]. Not used in OpenTDU2 yet (friction-circle model with the DB friction
coefficients instead, §2.7).

### 2.6 Road / terrain collision [DATA]
Each sector bank holds `Sector-a-b-c-d.shk` (static world: buildings, walls, boxes, capsules — poles/trunks) and
**`Sector-a-b-c-d-R.shk` (road surfaces: hkpMoppBvTreeShape → hkpStorageExtendedMeshShape, friction 0.7)**, both
Havok 5.5 packfiles readable with `LoadHavokCollision`. Body transforms are **area-local**: e.g. sector-5-0-6-7 bodies
T = (4992, 0, 5376) = (6·768 + 384, 0, 7·768), so world = vertex + (8·768·areaX, 0, 8·768·areaY) [DATA, checked:
road bounds x 4989–5285 + 30720, z 5629–6146 around the house]. Sector-5-0-6-7: R 2115 triangles, static 3491 triangles
+ 37 boxes + 82 capsules. The heightfield (257×257, 3 m) is the terrain. Other sector files: `.rd` ("SR20" road
splines, world-space points, e.g. (35951.1, 4.0, 5921.5)), `.psa` ("ZONE"), `.pgr` / `.flg` (RLE grids), `.lmp`
("LAMP") — layouts not decoded. `EdenWorld::getSurfaceHeight` uses the heightfield + the render meshes of the
sector objects (not the Havok data).


### 2.6b Library collision (walls, planters, poles, trees) [DATA]
The village walls are not in the sector `.shk`: they are **library models** placed by the sector `.pmi` lists, with
their collision in `islands/ibiza/level/commonworld.bnk` → **`Library.dhk`** (976 KB, Eden header + Havok 5.5
packfile; hkpPhysicsSystem of **864 fixed bodies**: 677 boxes, 140 capsules, 35 spheres, 12 MOPP meshes).
- Eden entries (1786): type 0 (index 0..460, one per model), **type 1 (index 461..921, hash = the library node
  hashcode used by the `.pmi` MOB records**, e.g. `SF_WALOB`, `V_AGAV_C` folded names), type 3 (`"Phys"+k`, index
  922.. = body k).
- Extra block (0x1e9a0 bytes at 0xdf60): per model `{u32 model; u32 0; cd cd; u32 numBodies; cd; 0; 0}` followed by
  numBodies × 0x80 `{hkTransform model-local body pose (3 rotation columns + translation); …; u32 motion 7 at +0x74}`;
  models in order, bodies consecutive in packfile order.
- The packfile body transforms are an **export layout** (models side by side, e.g. x ≈ −20 m); the model-local
  pose is the extra-block one → shape world = pmiInstance.World × extraPose × inverse(packfileBody) × shape.
  Checked: planter `SF_AFLWM` box (0.38 × 0.24 × 1.49 half extents) lands on the planters of the reference street.
- Per sector around the house: 1600–2500 placed library instances (V and O lists).

> **Superseded**: §2.1 (Havok vehicle SDK), §2.5 (tyres) and §2.7 are replaced by `docs/re/vehicle_physics_spec.md`:
> the cars use Eden's edVehicle library, not hkpVehicle*; the tyres are the PacejkaConfig XMB of
> `vehicules/tires/tires.bnk` (`.bpjka` unreferenced); the factory FUN_00bdb970 / FUN_00df3b30 mapping of every DB /
> Physics.cpr field and the OpenTDU2 model built from it are documented there.

### 2.7 Former model of OpenTDU2 (replaced, kept for history)
- Chassis: rigid body, DB mass 1735 kg, COM (0, 0.48, −0.388) and inertia (966.6, 1296, 406) of the .dhk scaled by
  1735/1301; semi-implicit Euler, Physics.cpr NbLoopForMiddlePrecision = 3 sub-steps per frame.
- 4 raycast wheels at the D_RIM_xx hubs (Havok style: from the hard point, length travel + tyre radius 0.339);
  spring = suspension_rate kN/m (80 / 90), damper = damper_rate kN·s/m (8 / 9), travel 0.1 m, anti-roll 16 kN/m,
  bounded bump stop; static compression puts the hubs at their modelled place.
- Tyres: slip ratio / slip angle magic-formula shapes (B, C = 12 / 1.65 long., 9 / 1.35 lat.), μ = DB max friction ×
  surface friction (0.7 roads, product rule), friction ellipse, wheel spin integrated implicitly in the tyre slope;
  tyre torques with reduced roll (0.35) / pitch (0.5) components (Havok torque factors).
- Engine: `<car>_max.xmb` / `<car>_min.xmb` curves blended by throttle, launch clutch slip, rev limiter at red line
  for `_rupteur_time`; driveline efficiency 0.9; `_drive` 4 → rear wheels, open differential.
- Automatic 7-speed box (DB ratios, final 4.44, shift times 0.1 s): up-shift at `_power_rpm` (ground speed or
  non-slipping engine rpm), down-shift under 0.6 × blue line; reverse after ReverseTime (0.6 s) of brake at rest,
  pedals swapped in reverse; reverse ratio = 1st [no DB field].
- Brakes: `_brakes_power_front` (15000) as total N·m, `_brake_balance` 60 % front; ABS at `_abs_slip_ratio_sport`
  (−0.08); traction control below TractionControlVelocityMax (70 km/h) on `_traction_control_slip_ratio_sport` (0.5);
  hand brake on the rear.
- Steering: SteeringMaxAngle / SteeringVelocity / SteeringSensitivity curves. Angular damping SportAngularDamping.
  Aero: drag ½ρ·cx·(width × height × 0.85)·v², lift loads as downforce at the axles; rolling resistance 0.015.
  Extra gravity ExtraGravityMin→Max (g) over AccelDuration after AccelDelay while airborne.
- World contact of the body: 12 spheres (r 0.3) on the corners of the .dhk chassis boxes raised by CollisionOffset,
  pushed out of triangles / boxes / capsules / heightfield, inelastic impulse (e 0.05, μ 0.3), boxes left through
  their entry face.
- Collision world (`VehicleWorldCollision`): sector `-R.shk` + `.shk` (area-local) + `Library.dhk` instances of the
  V / O `.pmi` lists + the spot `.shk` (house frame) + the heightfield (ground only from above), 8 m grid.
- Validation (`-no3d` runs): 0→52 km/h in ≈ 2.0 s, 85 km/h in 2nd at 4.5 s; the car follows the village street,
  crashes into the SF_WALOC wall when steered off at 85 km/h, stays on its wheels, reverses with the brake.

---------------------------------------------------------------------------------------------------
## 3. Chase camera [DATA, database/camerasingame.xmb; the file name `%sDataBase/CamerasInGame.xmb` and every field
name are strings of the exe, the update code is not identified → math [INF]]
`CameraInGame`: nb_camera_switch 5, p_array_camera_switch = {21, 20, 23, 24, 22} (the cycle of IA_CarCamera);
169 vehicle blocks {id_car, nb_cameras, Camera[]}. **California (id_car 387, "Car 141") has 6 cameras: modes 20, 23,
24, 43, 44, 40** (20 = far chase; 40 = its back view, `backviewmode` of 20 is 40; 23/24 inside views, near 0.05;
43/44 their back views). The first switch entry the car has is **20** → default chase [INF: 21 is absent].
Mode 20 fields: fov_deg 45, fov_is_lerp 1, fov_min 45, fov_max 65, fov_speed_min 0, fov_speed_max 150 (km/h);
nearProjPlane 0.8, far 40000; **follow_pos_offset −6.45, follow_pos_offset_H 1.5, follow_targ_offset 1.5,
follow_targ_offset_H 0.91, follow_speed_offset 50; follow_pos_max_offset −6.4, _h 1.6, follow_targ_max_offset 1.6,
_h 1.3, follow_speed_max_offset 250; follow_delay_offset 12 → follow_delay_max_offset 15; follow_lean_factor_offset
1.6348 → 0.4**; max_angle_up 40, max_angle_down 16, angle_inertia 90, range_angle_left_right 255; brake_g −1,
brake_g_inertia 0.985, brake_g_min 0.5, max 2.5, brake_pos_offset 0.59 / _h −0.03, brake_targ_offset 0.0027;
shake_* (rpm 70 %, speeds 150 / 220), slip_shake_*, blur_* (radial blur 0–150 km/h).
Interpretation used [INF]: offsets along the car heading (z) and up (H); the offsets lerp from the `*_offset` set at
follow_speed_offset (50 km/h) to the `*_max_offset` set at follow_speed_max_offset (250 km/h); follow_delay = yaw
follow stiffness (1/s); fov lerps 45° → 65° from 0 to 150 km/h.

---------------------------------------------------------------------------------------------------
## 4. HUD [DATA GamePC.cpr, hud.bnk; CODE FUN_0099c090]
- FREERIDE movies (GamePC.cpr AddFlashMovie): **HUD**, **GAUGES**, **DASHBRD** (flag 1) + GENERAL, MAIN. Only
  `frontend/hires/hud.bnk` was in the first upload; gauges.bnk, dashbrd.bnk and gauges/california.bnk were added later (§4b) (the rev counter /
  speed / gear gauge of the reference screenshot is GAUGES; `_hud_file_name = "California"` [INF] selects its skin).
- HUD.vmf (1280×960, 18 root frames): root labels `default` (f0), `hud` (f1), `cinematik` (f6); root clip `HUD`
  (sprite 1290) holds `minimap`, `gauge`, `compteur_int`, `gps_display`, `phone*`, `radars`, `time`, `position`,
  `distance`, `objectif_01..08`, `enter` (spot), `help`, `life_gge`, `chase_gge`, … (the `default` frame action
  hides/shows them from `WhatGameMode`, `WhatFormat`); vars `WhatGear` ("auto"), `state_Metric`, `IsShortGear`,
  `cur_gm`, `WhatGM`. The values (speed, rpm, gear) are pushed by encrypted code [ENC]; OpenTDU2 sets
  `/:WhatGear`.
- Minimap: drawn by the engine inside HUD `minimap` with GamePC.cpr [HudMap]: road colours per AIRL type
  (0 highway 29/180/215 width 4, 1 secondary 0/255/255 w3, 2 road 0/255/255 w3, 3 offroad 243/196/6, 4 circuit
  102/102/204 w4, 5 wip, 6 tunnel 37/82/129), locked roads 140/140/140, trace 254/216/197/200 w2, GPS path 255/102/0
  w5 [DATA]; road geometry = sector `.rd` splines [INF]; drawing code [ENC].
- Zone badge: DBCruisingZones `i1` / `i2` (island 0) → name_hc 0xd08 / 0xd09 = loc 3336 "Ibiza Area 1" / 3337 "Ibiza
  Area 2" (FR "Ibiza Zone 2" in the reference) [DATA]; which zone contains a position: [ENC] (candidate: sector `.psa`
  "ZONE" data) → OpenTDU2 shows zone 2 for area X ≥ 4 [INF, matches the house region of the screenshot].
- Goal box: GENERAL `pop_goal` with `/:GoalTxt` = loc 5038 "Enjoy!" ("Faites-vous plaisir !" in FR), §1.3.
- Odometer: part of GAUGES [MISSING].


### 4b. GAUGES and the car gauge bank [CODE FUN_00965810 / FUN_00967f10 / FUN_00966ed0 / FUN_00964ef0 / FUN_0096f670 / FUN_0096f720 / FUN_0096f8c0 / FUN_009667a0 / FUN_00966a10 / FUN_009661b0; DATA gauges.bnk, gauges/california.bnk]
- Movie GAUGES (`frontend/hires/gauges.bnk`, 1280x960): root labels `on` (HUD fade in) / `off`; `/HUD/compteur_ext`
  (sprite 462, 1457 frames, one label per car style, e.g. `California` f658) with `c_ext` (dial rectangle),
  `aiguille` (needle), `rpm_digit`; `/HUD/compteur_int` (sprite 627, `c_int_sp`, `c_int_rpm`, `aiguille_rpm`,
  `aiguille_speed`); `/HUD/speed_cokpit(_sh)`; EditTexts `/:Gear`, `/:Speed_c/_d/_u`, `/:SpeedUnit`, `/:Km`,
  `/:Centaine`.
- Init (FUN_00965810): `/:WhatFormat` = "4_3" / "16_9" (DAT_00fe4c78), `/:Gear` = "0", `/:SpeedUnit` (unit table),
  digit widgets Speed_c / Speed_d / Speed_u (FUN_00968c80, "%s/:%s"), `/:Km` (0..99999999, zero padded to 8),
  `/:Centaine` (0..99, 1 digit), needles `/hud/compteur_ext/aiguille`, `/hud/compteur_int/aiguille_rpm`,
  `.../aiguille_speed` (x2).
- Car bank `%sFrontEnd/%s/Gauges/%s.bnk` with `_hud_file_name` (FUN_0096fe90): `hud.ini` [HUD] `Style "California"`
  → compteur_ext / compteur_int go to that label (FUN_0096f8c0); `AddHud "<needle>" t1 t2 t3 t4 [t5 t6 ...]`
  (FUN_0096f720 needs > 4 tokens; FUN_0096f670: N = (tokens−1)/2 points, values (t1, t5, t7, t9 …, last t2),
  angles (t3, t6, t8, t10 …, last t4)); California: exterior_rpm 0..10000 → 0..265°, interior_rpm same,
  interior_kmh (0,0) (20,26) (320,282) (300,308), interior_mph (0,0) (200,281) (210,308). Textures ext_rpm, int_kmh,
  int_mph, int_rpm (dial, drawn as a quad on the c_ext / c_int_* rectangle, UV 1×1 above 640 px wide,
  0.836/0.703×0.695 below; FUN_009661b0 / FUN_00966070 render callbacks) and needle.2db (replaces the aiguille
  bitmap, FUN_009667a0).
- Per frame (FUN_00967f10 → FUN_00964ef0 snapshot): gear char from car+0x250 (0 'R', 1 'N', 2.. '1'..'9'); speed =
  v·3.6 (km/h) / v·2.2374 (mph) as int → Speed digits (units first, clamp 0..999, FUN_00968e20); rpm = ω·30/π →
  needles (FUN_00968fe0 piecewise map, FUN_00968d60 sets the clip rotation, vfunc +0x90) and `/:Rpm` (int, HUD and
  GAUGES); odometer → `/:Km` / `/:Centaine` (FUN_00968ef0).
- Visibility (FUN_00966ed0) by the camera mode (camera +0x2e8): 22 / 42 → compteur_int, 23 / 43 → speed_cokpit(_sh),
  else compteur_ext (California chase mode 20 → compteur_ext). HUD/minimap from mode+0x254c.
- DASHBRD (256x256, AddFlashMovie flag 1; labels per car, `speed`, `gauge`, `led1..6`, `/:SpeedInteger`,
  `/:Distance`, `/:Gear`) is the in-car dashboard screen [INF: rendered into a texture of the cockpit]; not used by
  the chase view, not implemented.
- GPS texts: FUN_009365a0 sets HUD `obj_Distance` = "" and `gps_Unit` = "" when there is no GPS target (step 2
  clears it) → the movie default texts "2500" / "mil" of `/HUD/minimap/gps_distance` are not shown [CODE].
- Havok: `hkpTransformShape` (0x70: child pointer +0x14, transform +0x30) and `hkpSphereShape` (0x20: radius
  +0x10) added to `LoadHavokCollision` (house spot eph_l3di_sc_*.shk: 46 wall boxes recovered; Library.dhk: 35
  spheres).

---------------------------------------------------------------------------------------------------
## 5. Weather
C_FIRST_OUTRO has no `weat:` message; FREERIDE uses the time-of-day presets of `islands/ibiza/fx/game_weathers.xmb`
("00", "01.30", … "12", "14", "18", … "23.20"); the in-game clock is [ENC] → OpenTDU2 uses "14" by default
(`-weather <name>`) [INF].

---------------------------------------------------------------------------------------------------
## 6. Open points
- Car hand-off at the end of the spot cinematic (pose/velocity), FUN_00c41100 internals: [ENC]/not followed.
- Havok vehicle tuning from the DB (which DB field feeds which hkpVehicleData member): the Eden glue reading
  vehicle+0x84 is not followed; the tyre .bpjka layout is not decoded.
- Camera update math (follow/lean/brake/shake): [ENC] / not found.
- GAUGES / DASHBRD movies [MISSING]; minimap drawing and zone lookup [ENC].

---------------------------------------------------------------------------------------------------
## 7. Additional data facts found while implementing
- DBCarPhysicsData `_drive` enum [DATA, inferred from known cars]: 1 = AWD (Veyron, R8, GT-R, Impreza, Touareg,
  ML63), 3 = FWD (Golf 6 GTI, Brera II), 4 = RWD (Enzo, Zonda, Camaro, Corvette, **California**). The comment of
  `CarPhysicsData::Drive` in gs_database.h ("1: RWD, 2: FWD, 4: AWD") is wrong (file not edited).
- California model wheel hubs (D_RIM_xx, car frame): FL (0.826, 0.219, 1.335), FR (−0.828, 0.219, 1.335),
  RL (0.806, 0.219, −1.358), RR (−0.804, 0.219, −1.358) → car frame +x = left, +z = forward, origin 0.12 m above
  the ground plane (tyre radius 0.339) [DATA].
- camerasingame.xmb California modes: 23 = driver view fixed_pos (0.37, 1.01, −0.36) → x > 0 is the driver (LHD)
  side, consistent with +x = left; 24 = bumper (0, 1.15, 0.45); 43/44 their back views; 40 = back view of 20.
- HUD.vmf runtime tree (OpenTDU2 FlashPlayer dump, `-dumphud`): `/HUD` (sprite 1290, 13 frames), `/HUD/minimap`
  (sprite 150) at stage (123.85, 577.95) px with children `mmap_lure`, `ic_status`, `cursor`, `nb_players`,
  `distance` (hidden), `obj_dist_01..`, `#19` (sprite 82); `/HUD/flash`, `/HUD/codriving_end`, … .
- Sector collision: some `Sector-*.shk` hold no packfile (no Havok magic: sector-5-0-6-6, 5-0-7-6, 5-1-5-0,
  5-1-7-0 …); others use **hkpTransformShape**, not supported by `LoadHavokCollision` (havok_collision.cpp, not
  edited here) → those bodies are missing from the driving collision.
- Spot collision `eph_l3di_sc_57E5C829CCD9F2C6.shk`: 104 triangles in the house frame (used for the driveway).
