# TDU2 driver seating in a car (cockpit / seated avatar)

Tags: [CODE] read in readable x86 (VA >= 0x6c1000), [DATA] read from game files, [INF] inferred,
[ENC] in the SecuROM range (< 0x6c1000), not read. Ghidra output: scratchpad `seat/dec1..5.c`.

## 1. Data files

### 1.1 avatarsdriver.bnk (Euro/Bnk/scenarics/avatars) [DATA]
- `Entity__AvatarPlayer` DriverMan / DriverWoman: `anim_blend_drive[0..6]` = `anims\car\driveiklayer0..6.bas`
  (layer 5 has a woman variant `driveiklayer5w.bas` with the `-W` gear clips).
- `.bas` = "BAS!" blend node file: `DATA` (node records, tag 0x666) + `REFS` (referenced `.xan` names; the bank
  holds them as `.anm`). Per layer node: u32 layer index, children count n, n × 0x11111111, then n (f32 weight, f32 2.0):

| layer | clips (weight) |
|---|---|
| 0 | stance_turn.xan (1.0), turn.xan (0.0 → the steering drive) |
| 1, 3 | stance.xan (1.0) — empty clip (0 tracks) |
| 2 | hand_size.xan (1.0) — finger grip, sample time not decoded |
| 4 | hand_open.xan (0.0) — right hand open (gear) |
| 5 | stance + gear_seq_up / gear_seq_down / gear_lever_up / gear_lever_down (0.0) |
| 6 | IK node list (below) |

- driveiklayer6 IK nodes (records: u32 type, f32 weight 1.0, -1, 3 floats, bone hash4 ×3, pad):

| type | bones (hash4) | params |
|---|---|---|
| 6 | Spine → IK_Back | 0,0,0 |
| 2 | Arm_R, ForeArm_R → IK_Right_Hand | 0,0,0 |
| 2 | Arm_L, ForeArm_L → IK_Left_Hand | 0,0,0 |
| 2 | UpLeg_R, Leg_R → IK_Right_Foot | 120, 30, 50 (not decoded: knee limits?) |
| 2 | UpLeg_L, Leg_L → IK_Left_Foot | 120, 30, 50 |
| 8 | belt helpers 0xd4fddb3a, 0xd4fedb3a, bones 1/2 (0xe8d2fbbe/fcbe), SideBelt, 0xd93741e5 | constraint, not decoded |

- `stance_turn.anm` = static (1 key) pose: identical to `turn.anm` at mid-clip (wheel straight) for every
  bone, **plus a ROOT translation (0, −0.019, 0.059)** (kind 0x12) that turn.anm lacks.
- The IK_* bones are skeleton bones (IK_Left_Hand child of Hand_L, etc.) animated by the clips: they are the
  effectors (grip point / foot sole / back point).

### 1.2 Car nodes (california.3dd LR, california_i.3dd HR) [DATA]
Same positions in both: DvrSeat (0.365, 0.247, −0.382) (child of the seat group `SEAT_L_LR` / `SEAT_HR_L`),
Back_*_L (0.364, 0.795, −0.659), Foot_*_L (0.391, 0.106, 0.586), Foot_*_R (0.259, 0.102, 0.570),
Steer (0.369, 0.724, 0.068, pitched 16°) with Hand_*_L (0.551, 0.762, 0.064) / Hand_*_R (0.187, 0.732, 0.054)
(car frame +X left). The exterior LR group is named `SEAT_L_LR`, not `Seat_LR_L` as the code looks up → on
the LR model the seat adjustment finds no node.

### 1.3 IKOffsetDB
- Path `Euro/Bnk/DataBase/IKOffsetDB.bin` ("%s%s", bank root 0xfe4f88 + string 0xf09a4c) [CODE]; on the Mac
  install it is `database/ikoffsetdb.bin` (409168 bytes) [DATA]. Loaded by FUN_00b97cc0 (FUN_00d88ab0 load,
  FUN_00b96ba0 parse), written by the dev tool FUN_00b97340 [CODE].
- **Big endian**: the parser checks the magic "IKDB" and byte-swaps when it reads "BDKI" (FUN_0047b010) [CODE];
  the file has "BDKI" [DATA]. The element reader FUN_0047ae90 is [ENC]; the layout below is fixed by the bytes
  (the CAIK walk ends exactly on the next chunk header at 0x61664) [DATA]:

```
+0x00 u16 2, u16 3 (version check (short)==2 && hi==3 [CODE]), u32 0, u32 0x36650, "IKDB"
+0x10 "CAIK", u16 2 (sexes: iStack_70 = 2 when > 1 [CODE]), u16 0, u32 0x35330, u32 0x35330 (meaning unknown)
+0x20 u32 numCars (271)
      per car (1472 bytes):
        u32 _id_car                                     (FUN_00b96280 lookup key, record +0x210) [CODE+DATA]
        2 x sex (index 1 = FUN_00cabec0() == 1, woman [INF]) {
          f32 r1..r5 -> record +0x220, +0x228, +0x224, +0x230, +0x22c         [CODE]
          27 x { f32 v0,v1,v2 -> entry +0x20 (the offset FUN_00b7e940 reads);
                 f32 v3,v4,v5 -> entry +0x00 (no readable reader; all 0 for 387) }  [CODE+DATA]
        }
        11 x f32[3] -> car record +0x20 + k*0x30 (meaning unknown)                 [CODE]
+0x61664 "BIIK" chunk (bikes, 0x170 per sex) — not decoded here
```
- Runtime: one 0xc80 record per car, 2 sub-records of 0x530 per sex: +0x220 the 5 seat floats (driver seat group
  y = +0x224 = r3, z = +0x228 = r2; passenger +0x22c / +0x230), +0x240 27 × 0x30 node entries [CODE].
- Defaults (car absent from the file): FUN_00b97cc0 zero-fills; FUN_00b96030 writes constants only in the
  Seat_*_L / Seat_*_R entries (15..26) [CODE].
- The 27 node names: tables 0xf7db68 (HR) / 0xf7dbf0 (LR): 0 DvrSeat, 1 Hand_R, 2 Hand_L, 3 Foot_R, 4 Foot_L,
  5 Radio, 6 Back_L, 7 Gear, 8 HBK, 9 Wcd, 10 PsgSeat, 11 Back_R, 12 PsgFoot_R, 13 PsgFoot_L, 14 PsgSeat,
  15..20 Seat_L, 21..26 Seat_R [CODE].
- California (id_car 387) [DATA]:

| | man (sex 0) | woman (sex 1) |
|---|---|---|
| seat y, z (r3, r2) | 0, 0.01 | −0.04, 0.08 |
| 0 DvrSeat | (0, 0, −0.01) | (0, 0, −0.03) |
| 1 Hand_R | (−0.006, 0.017, 0.023) | (0.003, 0.003, 0.013) |
| 2 Hand_L | (0.006, −0.014, 0.023) | (−0.003, −0.027, 0.010) |
| 3 Foot_R | (0, 0, −0.11) | (0, 0, −0.09) |
| 4 Foot_L | (0, 0, −0.13) | (0, 0, −0.13) |
| 6 Back_L | (0, −0.20, 0.01) | (0, −0.22, −0.02) |
| 11 Back_R / 12, 13 PsgFoot | (0, −0.2, 0.04) / (0, 0, 0.03) / 0 | (0, −0.19, 0.08) / (0, 0, 0.1) / (0, 0, 0.04) |

## 2. Code path [CODE]
- Enter car FUN_00b7ffe0: car (`+0x1c54 = 100`) vs bike (99). FUN_00b86c00 → FUN_00b964b0 fills the avatar's
  27 (node, offset entry) pairs at +0x1a94 (HR node by name hash, else LR via FUN_008bb130). Seat group
  `Seat_HR_L`/`Seat_LR_L` (hash 0x5f52485f5441459f) → +0x1a90 (FUN_00b8b730); its rest matrix +0x1a50;
  steer node (Steer_HR / Steer_LR) FUN_00b7b5d0; GEAR. Pointers +0x1b6c/+0x1b70 = &record[+0x224/+0x228].
- FUN_00b8ade0 (car, state 2000): seat group local = rest + (0, *0x1b6c, *0x1b70) (seat adjust, 0 by default);
  `+0x1bb0` = model-space position of DvrSeat (FUN_00b9d850 chain) + R·offset[DvrSeat].
- FUN_00b82450: avatar world = car world rotation, translation = car · `+0x1bb0`.
- FUN_00b7e940 (per frame, seated): IK goals = node world pos + R·offset (node entry +0x20), minus the root
  position, into the edAnim IK solvers [ENC: FUN_0047e170 / FUN_0047e350 / FUN_0047f920]:
  - left hand ← Hand_*_L, solver +0xf80, weight +0x1df4;
  - right hand ← Hand_*_R (Gear_* while shifting `+0xc60 == 2`, Radio_* when `== 3`), solver +0x1160 (+0x1a00
    pre-step), weight +0x1df0;
  - right foot ← Foot_*_R (+0x14d0), left foot ← Foot_*_L (+0x1340), weight 1;
  - back ← Back_*_L via FUN_0047e350 (+0x1680), weight 1.
  Hand weights = 1.0 at seat-in (FUN_00b7fc74); FUN_00b78f60 switches them (0/1 per hand) for gear / radio.
- Console command `DEFAULTSEATPOSITION y z` (FUN_00d65fb0) → globals 0xfd9138 / 0xfd913c, read only from [ENC].
- Camera `show_flags` (+0x300 of the camera record): bit 18 (cockpit) drives the steering-wheel offset
  (FUN_00b7f890) and the cockpit avatar render branch FUN_00b75130 → FUN_00b940a0(DAT_00ffbfd4) (not decoded).

## 3. Implementation (OpenTDU2)
- `IntroCinematic::setPlayerSeated`: pose clip = `stance_turn.anm` (layer 0 at wheel 0) [DATA].
- `IntroCinematic::loadDriverIKOffsets`: parses IKOffsetDB.bin (big endian) for `CinematicCar::getIdCar()`
  (DBCarPhysicsData `_id_car` of the car file, 387) and the avatar's sex.
- `updateSeatedPlayer`: avatar world = car world · (DvrSeat + seat adjust (0, y, z) + offset[0]); goals =
  node + R_node·offset[n] (Back_L also + seat adjust, it is a child of the seat group). FUN_0042b4f0
  (translation add vs set) is [ENC]: applied as an add, the zero default must leave a seat in place [INF]. IK in the
  game's order: Spine aim to Back_LR_L, both arms to Hand_LR_R / Hand_LR_L, both legs to Foot_LR_R / Foot_LR_L,
  effectors IK_* bones [DATA]. The ad-hoc 12 cm body shift is removed.
- Solver (eden_animation `SolveTwoBoneIK` / `SolveAimIK`): analytic two-bone IK in the clip's bend plane, the
  hand / foot keep their clip model-space rotation, re-solved 6× because the effector is a child of the kept
  bone [INF: the edAnim solver is ENC]. Result with the California male record: hands and both feet 0 mm
  from their offset targets (the hand offsets make both grips symmetric: y 0.493 / 0.493), the back aim
  reclines the spine toward the lowered back target (IK_Back 7 cm from it: aim only, no translation).
  Without the record: hands / right foot 0 mm, left foot 2.2 cm, back 22 cm.
- Cockpit view: head mesh + hair/beard/glasses/helmet parts not drawn while seated under the game camera
  [INF from the reference screenshot; mechanism not decoded].

## 4. Open points
- IKOffsetDB: meaning of the second triplet per entry, the 11 per-car triplets, the CAIK size fields, BIIK.
- hand_size sample time (finger grip size), turn.xan weight vs steering angle, gear/radio layers.
- IK solver details (type 6 semantics, leg params 120/30/50, type 8 belt constraints) [ENC].
- How the head is hidden in cockpit view (FUN_00b75130 branch).
