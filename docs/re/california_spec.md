# Ferrari California (FIRST_WM garage): retractable hardtop + cockpit LOD

Tags: [CODE] read from the readable exe (VA >= 0x6c1000), [DATA] game files,
[INF] inference, [ENC] SecuROM range (not read).

## 1. Car node names (table 0x00edef08, 8 byte names, vfunc +0x84 of the car class, vtable 0x00ef5dd8)

Node slot i lives at car + 0x710 + i * 0x60 (resolved in FUN_00c61480) [CODE].
Names longer than 8 chars are folded (char i added into byte i & 7) [CODE+DATA].

| idx | name | car offset | notes |
|---|---|---|---|
| 0 | D_CAR | 0x710 | high resolution car (incl. HANIM_S) |
| 3 | BODY_LR | 0x830 | |
| 4 | BODY_MR | 0x890 | |
| 29 / 30 | DOOR_L / DOOR_R | 0x11f0 / 0x1250 | INT_LR_L / INT_LR_R looked up below them (FUN_00c69e30) |
| 31 | COCK_LR | 0x12b0 | exterior low-res cockpit (california.3dd) |
| 32 | BODY_HR | 0x1310 | |
| 33 | COCK_HR | 0x1370 | interior bank cockpit (california_i.3dd) |
| 34 / 35 | INT_HR_L / INT_HR_R | 0x13d0 / 0x1430 | interior bank door panels |
| 38 | HANIM_S | 0x1550 | skinned roof; its skin palette -> car + 0xae50 (anim pose), FUN_00c6a610 |
| 39 | HARDTOP | 0x15b0 | detachable hardtop object (absent on the California) |
| 40 | (0x44a68f91_5f504f54, interior hardtop) | 0x1610 | looked up in the interior instance (absent) |
| 41 / 42 | HARDTOP_MR / HARDTOP_LR | 0x1670 / 0x16d0 | static CLOSED roofs (california.3dd nodes 108 / 107, root level) |
| 43 | HANIM_MR | 0x1730 | (absent) |

## 2. Hardtop animation [CODE]

* FUN_00c67560 hashes `\HardTop.anm`, `\Front_Right.anm`, `\Rear_Right.anm`, `\Front_Left.anm`,
  `\Rear_Left.anm`, `\Steer.anm` into 6 animation players at car + 0xb8d0 + k * 0x8e0
  (anim hash at player + 0x18). The California bank holds Hardtop.anm (12 tracks on california.xsb,
  15 bones, duration 17.0) and Default.anm [DATA].
* Player fields: +0x8c4 time, +0x8c0 last applied time, +0x8d0 mode (0 forward, 1 backward, 2 stopped).
  Constructor FUN_00c68c80: time 0, mode 2 (car + 0xc1a0 = 2).
* FUN_00c4ac00 (per frame): time += dt (mode 0) / -= dt (mode 1), stops (mode 2) at the ends.
  FUN_00c4a770: 2 while 0 < t < end, 1 at t = 0, 0 at the end.
* FUN_00c4a390: when t changed, samples the clip at t / duration into the pose at car + 0xae50
  (palette of HANIM_S).
* FUN_00c668c0 mode 1 (towards 0), FUN_00c668f0 mode 0 (towards the end),
  FUN_00c66920(f): t = duration * min(f, 0.99999), mode 2.
* **t = 0 is CLOSED**: FUN_00c662b0 (= FUN_00c4a770) == 1 shows the static HARDTOP_MR / HARDTOP_LR at
  LOD 2 / 1 (FUN_00c66380); the car wash (FUN_009a8fbc) calls FUN_00c668c0 (close) before washing.
* **Opening**: FUN_00c3fcc0(car): if the DB record (0x470 bytes, `_id_car` at +0x21c) has
  `+0x8c` (= `_car_body`, layout check: bool 1, string 28, uint64 aligned 8 -> size 0x470) == 2 or 4 and
  the vehicle object kind (vfunc +0x2c, 0x0052ac80 [ENC], taken as "car") is 0:
  FUN_00c66150(1.0) / FUN_00c66110(1.0) (car + 0xa780 / 0xa784 = 1: side windows WND_L1 / WND_R1
  moved onto WND_L2 / WND_R2, FUN_00c674a0 + FUN_009544e0), FUN_00c668f0, FUN_00c66920(1.0) -> open.
  Called for every spawned car (FUN_00a51aa0) and at story step 3 (FUN_00b10da0, first freeride).
* DB: California `_car_body = 4`, `_noroof = 0` [DATA]. `_car_body` 2/4 = 29 cars (roadsters /
  convertibles). Title screen cars: One_77 (3), Zonda_Cinque (3), ML63_AMG (6), Touareg (6): no change.
* No hardtop state exists in garage.bnk (CIN_CAR: id_car 0, no roof field; `avm4:CIN_CAR` is not a
  roof message) [DATA]; the garage car is the convertible current car -> open [INF from FUN_00c3fcc0
  being applied to every created car].

## 3. LOD switch FUN_00c66380 [CODE]

LOD (car + 0x6b4) from FUN_00c4e630 / FUN_00c4dcb0: metric = |car - camera| * camera(+0x17c, not
identified [INF: 1]) * (1 + 10 * behind-camera depth, 0 for the player's car), first i with
table[i] < metric, table 0x00f7e788 = {250, 125, 15, 5, 0} -> 0 (> 250 m), 1 LR, 2 MR, 3 HR (5..15 m),
4 HR + interior (< 5 m). Game mode CASINO (0x1d) forces 4.

* LOD 1: hide D_CAR, HARDTOP, BODY_MR, HARDTOP_MR; show BODY_LR; HARDTOP_LR shown iff roof closed.
* LOD 2: hide D_CAR, HARDTOP, BODY_LR, HARDTOP_LR, HANIM_MR; show BODY_MR; HARDTOP_MR iff closed.
* LOD 3 / 4: show D_CAR (+ HANIM_S), HARDTOP; hide BODY_LR / BODY_MR / HARDTOP_MR / HARDTOP_LR.
* Cockpit (every call): hide COCK_HR, COCK_LR, INT_HR_L/R, INT_LR_L/R; then
  LOD 2 / 3 (or no COCK_HR): show COCK_LR + INT_LR_L / R;
  LOD 4: show COCK_HR + INT_HR_L / R, BELT_L / BELT_R only if car + 0xadd8 / 0xadd9 (0 at creation).

## 4. Interior bank (FUN_00c69e30) [CODE]

Instances COCK_HR (+ ENGINE_I, EXHAUST_I, FAN_I, interior hardtop lookups), INT_HR_L, INT_HR_R
(attached to the doors). The interior root nodes GPS and GLASS_I are not instanced. GLASS_I is a
material carrier: FUN_00c53d10 swaps the car "GLASS" material for GLASS_I (TD_CarGlassDirt) when the
camera flag 0x40000 is set (inside view) [CODE; when the flag is set: not identified].
Materials named C1_ / C2_ / C3_ are collected for the interior colour.
Gauge needles (FUN_00c52240): D_ND_SPD / D_ND_RPM rotation reset to identity (already identity in the
data), needle between ND_x_1 (value 0, rest pose of ND_x) and ND_x_2.

## 5. OpenTDU2 (source/game/cinematic/cinematic_car.cpp)

* HANIM_S drawn skinned (EdenModel::drawSkinned) with california.xsb + Hardtop.anm sampled at
  0.99999 * duration when `_car_body` is 2 or 4 (else 0 = closed); WND_x1 placed on WND_x2 when open.
  HARDTOP_MR / HARDTOP_LR / HANIM_MR hidden (LOD 3 / 4). `-hardtop 0|1` forces the state.
* Cockpit LOD per frame from the view camera (shadow / reflection passes keep the last one):
  < 5 m -> COCK_HR subtree (without BELT_L / R) + INT_HR_L / R of the interior bank, COCK_LR /
  INT_LR_L / INT_LR_R hidden; otherwise the reverse. `-carlod 4|3` forces it. LODs 0..2 not used
  (camera factor +0x17c unknown).
* Reflections (docs/re/car_glass_spec.md): in FIRST_WM the car materials whose REFLECTION texture is
  CUBEMAP get the garage bank cube CUBECAR (static, SPECPARM.z / RAMPPOW.z = 1, FUN_009aa2c0 ->
  FUN_00a4baf0 -> FUN_00c62440 / FUN_00d58d20); elsewhere the dynamic cube (z = 0).
