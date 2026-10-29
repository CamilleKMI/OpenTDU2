# TDU2 story steps (new game tutorial): step machine, step 3 "Meeting Tess", what steps 4..13 need

Tags: [CODE] readable x86 (VA >= 0x6c1000) · [DATA] game files · [INF] inferred (reason given) ·
[ENC] logic in SecuROM-encrypted code (< 0x6c1000) or not in readable code, not read.

Decompiles: `/tmp/claude-0/tess_garage_dec1.c` (FUN_00b10da0, the whole step switch), `/tmp/claude-0/s3/d1.c` …
`d12.c` (this work: CINE mode, story callback, spot object, used-car stock). Extracted banks: `/tmp/claude-0/s3/mt/`
(meeting_tess.bnk), `ff/` (fade_freeride.bnk), `hot/` (islands/ibiza/exterior/e_hotela.bnk). Spot names:
`/tmp/claude-0/s3/spots_ibiza.txt` (spots.bin ids brute-forced to `<bank>_<n><suffix>` names, script `spotnames.py`).
Earlier steps: `tess_garage_spec.md` (steps 0, 1), `freeride_spec.md` (step 2).

---------------------------------------------------------------------------------------------------
## 1. The story step machine [CODE]

Object at `DAT_01025608 + 0x9010`, vtable 0x00f139d4: [1] FUN_00b10da0 (handler), [2] FUN_00b99310 ("go to step n":
handler(old, 0xc0c2) then handler(n, 0xc0c0), current step at +4, previous at +8), [6] FUN_00b10540 (tick:
handler(step, dt, 0xc0c1) while the step is not 11, the profile is loaded and the mode is not LOGIN). Second
vtable at +0x14 (0x00f139f8): the cinematic listener FUN_00b10580 (C_FADE_FREERIDE, §2.2).
`FUN_00b10da0(this, step, dt, event)`: `switch(step + 1)`; events 0xc0c0 enter, 0xc0c1 tick, 0xc0c2 leave.
Step names (table 0x00b10398): 0 Avatar choice / First WalkMode, 1 First Garage, 2 First Freeride, 3 Meeting Tess,
4 Driving Tess, 5 Solar Crown exterior, 6 Solar Crown interior, 7 Used Car Dealer, 8 Tuto FROG, 9 Go Home,
10 Home sweet home : Caravan, 11 Done, 12 Hawai First Freeride, 13 Igloo First Intro.
Every enter writes `profile+0x58` (the saved step) and calls FUN_00acee30(0) [INF: profile save].
Game modes: name table 0x00f7eed8 (0x01 FREERIDE, 0x03 SHOP, 0x04 MISSION, 0x09 LIVING, 0x1f CINE, 0x20
AVATAR_CHOICE, 0x23 FIRST_WM); a step sets `*(DAT_00fd5080+0x128)` = mode (previous in +0x12c) unless EXIT (0x25)
or a switch is pending (+0x130), and `DAT_00fd50fc+0x31 = 1` (switch requested).

| step | enter (0xc0c0) | tick (0xc0c1) → next | implemented |
|---|---|---|---|
| 0 | mode 0x20 AVATAR_CHOICE (new_intro.bnk) | `gain` FSCommand → 1 | yes |
| 1 | car 0x183 (387 California) current, house EPH_L3DI_5067I, mode 0x23 FIRST_WM (garage.bnk) | goal TUTO_ENTER_IN_THE _CAR; end of C_OUTRO → 2 | yes |
| 2 | timer 90 s, load `intro\fade_freeride`, C_FIRST_OUTRO at the house spot, mode 1 FREERIDE | "Enjoy!" (5038), 90 s, C_FADE_FREERIDE, its end → 3 | yes |
| 3 | FUN_00b105d0 (avatar out of the dream), car 0x183 current, CINE request kind 1 + callback FUN_00b104d0, mode 0x1f CINE | when CINE started (+0x1b18d4): car physics re-attached; end → callback → 4 | **yes (this work)** |
| 4 | car 0x183 current, event "TESSEVEN" (FUN_00abba10 / FUN_00b12d20), mode 4 MISSION | §4 | **yes (this work)** |
| 5 | car 0x183 (−1,−1,−1) FUN_00aba280, mode CINE kind 2 + callback | end → 6 | **yes** |
| 6 | mode CINE kind 3, callback set once the switch is done | end → 7 | **yes** |
| 7 | car 0x146 (−1,−1,−1) FUN_00aba280, spot ESTOL_B_1275I, mode 3 SHOP | §5.2 | yes (GMShop) |
| 8 | FREERIDE at the dealer (FUN_00a6a650), pack_freerideFROG, timer 300 s, goal TUTO_FROG | §5.3 | stub (GMFreeRide) |
| 9 | FREERIDE, timer 0.5 s, Todd comment | mode 9 LIVING → 10 | no |
| 10 | house EPH_L0C_1254I, mode 9 LIVING, RACING_LICENCE_C4, COLLECTION_HOUSES_1 | back to FREERIDE → 11, pop_goal 4036, GPS ERS_C_1274 | no |
| 11 / 12 / 13 | Done / FUN_00ad8b10 (Hawai) / CINE kind 0x2b C_IGLOO | | no |

Story callback FUN_00b104d0(story) [CODE]: step 3 → 4, 5 → 6, 6 → 7, 13 → 11 (`story->vfunc8(n)`).

---------------------------------------------------------------------------------------------------
## 2. From step 2 to step 3

### 2.1 Step 2 tick [CODE, freeride_spec §1.3]
Timer 90 s (DAT_00ede78c) after "Enjoy!"; when it is < 0: `C_FADE_FREERIDE` of the step bank
(`FUN_00b34fd0(this+0xc, 'CINE', "C_FADE_FREERIDE")`), listener = story+0x14, `+0x628 = 1`, `+0xb54 = 0`, start
FUN_00b40180, timer = 0. Not found → step 3 at once.

### 2.2 End of C_FADE_FREERIDE: listener FUN_00b10580 [CODE]
Only event 7 (reached end): `DAT_00fd50fc+0x38 = 1`, story+0x700 (= `this[0x1c0]`) = 1, roof opened again
(FUN_00c3fcc0 on the player car), car +0x1e8 / +0x1ea = 0. The next step 2 tick sees `this[0x1c0]` → vfunc8(3).
C_FADE_FREERIDE [DATA, fade_freeride.bnk]: CinProps 32, CinEnd 3, `fade_blanc_M_FADE_FREERIDE.cin` 15.00 s, one shot
on `NO_CAMERA` (no fade in the shot record), Fx `FxIntroNew-E_Meet_Tess_02_00` at 2.68 s (12.34 s), scene
`M_FADE_FREERIDE_life.sce` (10.17 s) actor `WOMAN` (anim `ev_tessincar_toplayer01_npc`) with the Fx
`FxIntroNew-E_Meet_Tess_01_00` / `_02_00`, `s_toctoc_CINEMATIC.wav` (knocks). What turns the screen white is not in
the bank (CinProps bit 5 goes to FUN_00d80340, a flash movie on/off helper) [ENC/INF].
Implemented: 2 s fade to white [INF], held while C_FADE_FREERIDE plays its Fx (sounds mapped on the car
position [INF]); at its natural end (15 s) story step 3 → game mode CINE.

---------------------------------------------------------------------------------------------------
## 3. Step 3 "Meeting Tess" (game mode CINE 0x1f, kind 1)

### 3.1 Step enter (FUN_00b10da0 case 4) [CODE]
```
profile+0x58 = 3; FUN_00acee30(0)
FUN_00b105d0()                                  // the avatar out of the dream (3.2)
FUN_00ab9be0(0x183)                             // California current car
FUN_00b9a1f0(kind = 1 (edx), callback = 0xb104d0, arg = story (ebx), -1, -1, 0, 0)
DAT_00fd50fc+0x31 = 1; mode := 0x1f (CINE); this[6] = 0
```
FUN_00b9a1f0 stores the request: `DAT_01025608+0x9730` kind, `+0x9734` callback, `+0x973c` callback argument,
`+0x9738/+0x9740/+0x9744/+0x9748/+0x974c`, `+0x9750 = (eax ? eax[400] : 0)` [CODE asm 0xb114f9–0xb1150c].
Tick: when the mode is CINE and `mode+0x1b18d4` (cinematic started) → FUN_00c3c2d0(1), FUN_00c3c050(1),
FUN_00c3c140() (car re-attached), this[6] = 1.

### 3.2 Avatar out of the dream (FUN_00b105d0) [CODE + DATA]
- Sex = avatar record +8 (FUN_00cbb3f0, default 2): "Female" if 1, else "Male" (DBPNJ `_sex` 1 woman, 2 man).
- Ethnic index = (profile+0x30 u16 − sex) / 2: 0 "", 1 "_Asia", 2 "_Afrik" → config `AVT_%s%s`
  (e.g. `AVT_Male`, `AVT_Female_Asia`), loaded by FUN_00cb5ee0 (DBPNJ table at DAT_01024c94, rows 0xf8 bytes).
- Copy into the profile avatar: 10 clothing slots × 2 rows (u16 at +0x90 + 2i + 0x16j) and colours (+0xbc + i + 11j),
  **except slots 0 and 8**. FUN_00cb5bd0 fills slot 0 from DBPNJ row +0x40 and slot 8 from +0x18; the row layout is the
  XMB field order (`_accessories_color` +0, `_beard_color` +4, `_bottom_color` +8, `_clothe_accessories` +0x10,
  `_clothe_beard` +0x18, … `_clothe_hair` +0x40, … `_configid` +0x60, `_fileconfigid` +0x84 (read by FUN_00cb5b10),
  `_sex` +0xe8, `_shoes_color` +0xec, `_top_color` +0xf0, row 0xf8) → **the player keeps hair and beard**, takes the
  rest of `AVT_<sex><ethnic>` (the hotel valet outfit: `m_ed_t_shirt_groom_01`, `m_ed_b_pants_groom_01`,
  `m_ed_s_shoes_groom_01`, badge "Valet"). The six `*_Intro` configs differ from their `AVT_*` base only by
  bottom / shoes / top (and their colours) [DATA], so `AVT_<sex><ethnic>` is the exact result for a new profile.
- Then FUN_00cbbcd0(profile+0x30) (required default items), the 8 avatar objects reset, FUN_00b85cb0 on the player PNJ.

### 3.3 CINE mode [CODE]
Vtables 0x00f29b00 / 0x00f29b54 (game state, `this` = mode+0x90): init FUN_009b6df0, activate FUN_009b6c20, tick
FUN_009b54f0, FUN_009b54c0, deactivate FUN_009b6cc0, exit FUN_009b6d40; cinematic listener FUN_009b48c0.
- Init FUN_009b6df0: player driver put in the car (FUN_00baec60 / FUN_00bbb810); request copied (kind → mode
  +0x1adc80, +0x9740/44/48/50 → +0x1b1848..); **FUN_009b57b0** (kind → names, below); spot found by name (hash
  FUN_008bb130, index FUN_00b30ed0 in the spot list DAT_01025608+0x6720) → mode +0xb3b8, `DAT_01025784` = index,
  `+0xc230 = 1`, streaming centre (FUN_00995d00 / FUN_00aee890 / FUN_00995d50), `DAT_010257d0..dc` = spot +0x20
  (map position), FUN_00ddb550(spot +0x44, +0x48, +0x4c) = rotation from the spot Euler angles; the cinematic bank
  is loaded asynchronously (FUN_00d89d90 at +0x1b0fe4, then FUN_00a4f030); an interior bank (3rd name) with
  FUN_00b9ff50. No spot and no bank → the callbacks are called at once.
- Kind table FUN_009b57b0 / FUN_009b46c0:

| kind | bank (`scenarics/Cinematics/...`) | spot | cinematic | other |
|---|---|---|---|---|
| 1 | `intro/meeting_tess` | E_HOTELA_1266I | `C_MEETING_TESS` (FUN_00cb5fb0() == 0) / `C_MEETING_TESS_W` | |
| 2 | `intro/solar_crown_ext` | ECH_IA_1243I | `C_SOLAR_CROWN_E` | |
| 3 | `intro/solar_crown_int` | ECH_IA_1243I | `C_SOLAR_CROWN_I` | interior `%sInterior/ich_ib.bnk` |
| 4 | `intro/generique` | ECH_IA_1243I | `C_GENERIQUE` | |
| 5–7 | | | `C_FIRST_INTRO` | interiors irs_c / irs_b / irs_a |
| 0x2b | `island/isl_igloo` | EPH_L0A_1276 | `C_IGLOO` | |
| 0x28.. | | | C_NEWFACE_B, C_FIRST_LANDED, C_LANDED, C_CASINO_/HAWAI_/IBIZA_ARRIVAL / _DEPARTURE | |

- Tick FUN_009b54f0: when the world is ready (vfunc +0x24 [ENC chain]) and not yet started: FUN_009b5210, started
  flag +0x1b18d4 = 1. FUN_009b5210: name FUN_009b46c0; cinematic searched in the bank (FUN_00b34fd0 'CINE') or in
  the spot slots (FUN_00b315e0); not found → callbacks; `cine+0xb61 = 0`, listener = mode, **skippable**
  (`CinProps |= 4`) when FUN_009b41b0(kind) (true for 1..3, 5..0x34; false for 4 C_GENERIQUE). With a mode spot slot
  (+0xb448, only kinds ≥ 8) → FUN_00b402a0 (start at the spot); else, with a player car driver: **the cinematic
  transform `cine+0x120` (flag `cine+0x11e = 1`) is built from the driver matrix (driver +0x160) projected on the
  road network** (FUN_00a6a5c0 → FUN_00cfb280 / FUN_00cfab80 on `DAT_00fd5134+0x3610`, the same road query as the
  spot "exit" teleport FUN_00a6a650) then FUN_00b40180. The matrix helpers FUN_0041d9e0 / FUN_0042a7d0 /
  FUN_0042b4f0 are encrypted [ENC].
- Listener FUN_009b48c0: event 0 (start) → GENERAL `/:PlayerName` = profile name, `DAT_011f8cc4` = (name != "C_A4");
  event 2 (stop) → callbacks +0x9734 / +0x9738 (arg +0x973c); event 6 (near end) → callback +0x9734 then cleared;
  event 8 (message) → `flsh:<label>` (GENERAL vfunc 0xdc) and `phot` (photo of the frame, FUN_00a97fb0).
- Flash movies of CINE (GamePC.cpr `AddFlashMovie "INTRO" 31`): INTRO (+ GENERAL, MAIN). Audio: audiosetup.xmb game
  mode `CINE` (GameModes[30], MixPreset 814562238; OpenTDU2's AudioSetup does not resolve that value to one of the
  13 MixPresets → warning, no preset gains; the same value is used by CINVIEWER, JAIL, CAR_WASH, EXIT; AVATAR_CHOICE and FIRST_WM use 1569898762) [DATA].

### 3.4 The spot object (FUN_00b2eb80 from a spots.bin 0x110 record) [CODE + DATA]
rt+0x10 id ← rec+0x00 · rt+0x20/24/28 position ← rec+0x10 · rt+0x2c = 1 · rt+0x44/48/4c Euler ← rec+0x1c/0x20/0x24
(0, heading, 0) · **rt+0x50/54/58 second position ← rec+0x28** · rt+0x5c = 1 · **rt+0x60 ← rec+0x38 (second heading)** ·
rt+0x70/74 bank name ← rec+0x08 · rt+0x30.. area / sector bytes ← rec+0x34..0x37 · rt+0x79 ← rec+0x100 ·
rt+0x7a ← rec+0x40 (64 bytes) · rt+0x141 ← rec+0x105.
E_HOTELA_1266I (id 0x414c8e8a857a90a4): position (11099.14, 3.66, 17233.46), heading −2.4403; second position
(11073.13, 3.588, 17203.56), second heading −0.8548; area 1-2, sector 6-6 [DATA].

### 3.5 meeting_tess.bnk [DATA]
- `C_MEETING_TESS` / `C_MEETING_TESS_W`: CinProps 384 (0x180: bits 7, 8), CinEnd 3, Priority 1.5, CINE_DEFAULT;
  cin `cin_meeting_tess_M_MEETING_TESS(_W)_totem.cin` **66.67 s**, 16 shots on one scene
  `M_MEETING_TESS_thedream.sce` (82.46 s; the _W version adds `..._thedream_woman.sce`); additionalsAnims (m) +
  additionalsStyleAnims "w"; FaceFX `intronew-e-m.fxe` / `intronew-e-w.fxe`.
- Shot 1: c_meet_01, **fade type 1, 4.0 s, colour (1, 1, 1)** = fade in from white (continues the white screen of
  C_FADE_FREERIDE). Shot cameras c_meet_02..14, 11bis; last shot 61.98–66.67 (c_meet_14).
- Messages: `weat:Meeting_Tess` (cinematic_weathers.xmb) with `wmod:Meeting_Tess_02..16` on each cut; shadows
  `shad:GP / PA / PR / PG / PM / CINE_VERY_NEAR`; `zcvn` 0–5.55, `zcne` 5.55–66.7 [ENC handler]; `phot` at 55.73.
- Fx (cin): 7 events (ambience `FxC_MEETING_TESS` 66.76 s at 0, `FxS_KNOCK`, `FxC_MEETING_TESS_1-getout`,
  `_2-phone`, `_3-get_keys`, `_4-submitkeys`, `_7-open2`); Tess voice Fx on her .sce actor (9 keys); voices
  `IntroNew-E_Meet_Tess_01..12` in `meeting_tess__<lang>.bnk`.
- Scene origin: translation (−6.178, −1.220, −13.033) with a **130° rotation** (rows (−0.764, 0, 0.645) (0, 1, 0)
  (−0.645, 0, −0.764)); actors: `Tess` (PNJ STO_Tess_Wintory, anims intro_meet_tess_01..06_npc, meet_gocar_tess),
  `Avatar` (meet_sleep-m, intro_meet_tess_02..06-m, meet_gocar-m; style w), `PLAYER_CAR` (static at
  (−13.655, 0.219, 19.004) heading (0, 0.416, 0, 0.909) = **Entity__PosCam D_CAR** (−8.007, −1.001, −36.363) once the
  origin is applied; 4 message tracks: `clwr:0`, `clwl:0` at 0, `opdl:0.64` 5.15, `cldl:0.40` 11.72, `opdl:1.08`
  78.85, `cldl:0.60` 81.72, `opdr:0.56` 77.77, `cldr:0.57` 81.62), `CF_Phone` (a-phone, skinned, anim
  cf_phon_intro_meet_tess_03_npc, visible 17.02–44.26), `CF_KCar` (key_ring_ferrari, skinned, 4 anims, visible
  14.10–50.24 and 51.84–77.76), `Stuart_Phone` / `Fx_Synch` (sound anchors), look-at targets.
- Entity__Actor `ND_RPM` / `AAA` (garage_int: the California rev needle and cockpit of garage.bnk C_OUT), not used by
  the cinematic, parked under the ground (y −7.29 / −2.31); collision `cin_meeting_tess_*.shk` holds no packfile; `.dhk` = a car chassis.
- The story of the scene: the avatar wakes up in the California at the hotel (Tess knocks), gets out, Tess phones
  (Stuart), hands the Ferrari key, both get in (the passenger door opens at 77.77).

### 3.6 Placement of the scene [CODE + ENC → INF]
The bank entities (D_CAR at y −1.0) are 1.3 m under the hotel floor of `e_hotela_sc_*.shk` in the spot frame, and D_CAR
straddles the terrace step: the bank is not authored in the spot frame. FUN_009b5210 places the cinematic from the
car (3.3) [CODE], the helpers are [ENC]. OpenTDU2 [INF]: **the scene is moved so that PLAYER_CAR / D_CAR is at the
spot's second transform** (rt+0x50 position, rt+0x60 heading): the California stands on the sea-front road in
front of the hotel, the actors on the pavement (checked by rendering). The first spot transform (hotel origin, inside
the building) only places the e_hotela exterior. `-cinelift` keeps the spot frame lifted onto the floor (comparison).

### 3.7 Implemented (game/cine/gm_cine.*, IntroCinematic)
GMCine (`-gm CINE`, `-storystep 3`, `-cinekind n`, `-cinetime s`): request kind 1, avatar `AVT_<sex><ethnic>`,
IntroCinematic on meeting_tess.bnk with the E_HOTELA_1266I exterior (e_hotela.bnk, its collision for the car tyres),
world streaming around the camera, `C_MEETING_TESS` / `_W` by the avatar sex, PLAYER_CAR driven California (door
channels from the actor messages, door object FUN_00a48770 / ease FUN_00a48310), skinned props with their .sce
Animation and Visibility tracks, scene-origin rotation applied to cameras and cars, GENERAL `/:PlayerName`, skip with
validate / pause [INF key]. End → story step 4 (MISSION, §4).
FREERIDE: C_FADE_FREERIDE played (fade_freeride.bnk), its end → CINE.

---------------------------------------------------------------------------------------------------
## 4. Step 4 "Driving Tess" (mode 4 MISSION, event TESSEVEN)

Decompiles (this work): `/tmp/claude-0/s4/d1.c` … `d14.c` (IGE2 reader, CGameMech, reactions, passenger, IGE checkpoint
users), extracted `pack_events13.bnk` in `/tmp/claude-0/s4/pe13/` (entities `pe13_ent.txt`, resolved hashcodes
`pe13_hash.txt`), `solar_crown_ext/`, `solar_crown_int/`, `avd/` (avatarsdriver.bnk).

### 4.1 Story step code [CODE, FUN_00b10da0 case 5]
Enter: profile+0x58 = 4; car 0x183 current (FUN_00ab9be0); `mission = FUN_00abba10(profile vfunc 0x50, "TESSEVEN")`
(search of the profile event list, 20-byte entries, name at entry +0x24 (std::string)), FUN_00b12d20(mission)
(selects it, creates the entry with FUN_00abb430 if absent); `DAT_010257f8 = "TESSEVEN"`, `DAT_01025818 = 2`;
mode := 4 MISSION; `this[8] = 0x183`.
Tick (mode 4): FUN_00c3c1a0; once (`this[6]`): FUN_00aba280(this[8]); mission object = `mode+0x1b0790`, state `+4`:
- state **4** (every tick while 4): `pop_goal` loc 0xfc0 = 4032 "Drive Tess to the shooting location", goal
  `TUTO_DRIVE_TESS` (DBRewardXP 178, `_targetspotworld "CLUB"`); cinematic **`C_TRUCK`** of the mission bank
  (`mode+0x1b53a0`): loop byte +0xb5b = 1, FUN_00b3f7a0, `CinProps |= 0x10` (no camera), started at spot
  **ECH_IA_1243I** (FUN_00b31490(0xa87b7777, 0x315f4149) → FUN_00b402a0); `this[7]` = 40.0 (DAT_00f90bc8).
- state **5**, when FUN_0045f2f0() [ENC]: if `this[7] > 25.0` (double DAT_00f4a208) and a passenger (mission +0x124)
  whose first anim layer is idle (FUN_00d96e60: layer +0xd0 bit 1): FUN_00a15430(5, 1); then `this[7] > 0` →
  `this[7] -= dt`; `< 0` → FUN_00a15430(6, 9), `this[7] = 25.0` (DAT_00edd53c).
- state **9** with mission +0xf0 (success): C_TRUCK stopped if playing (FUN_00b3fc40(1)), FUN_00ccde50 (passenger
  out: car +0x7a0c == 1, +0x7a10 == 0, +0x79d4 → FUN_00ce24f0(0)), vfunc8(5) → step 5.
Constants read from the exe: 40.0, 25.0 (double), 25.0 [CODE].

### 4.2 The event script `ig2/tesseven.ig2` (IGE2) [CODE + DATA]
Big endian chunks `{ char Tag[4]; u32 Size }` (`/tmp/claude-0/s4`, game/mission/ige_file.*):
- `IGE2` (0x1c): u32 version 0x305 (FUN_00b25fc0: `< 0x305` → an extra conversion vfunc), 16 bytes 0.
- `MDSC` (0x4e0): u32 90, u32 0, u32 4, u32 0x305, then records `{ char Tag[4]; u8 3; u8 group; u16 size; value }`
  (4 / 8 byte integers, 0x40 byte strings; 89 records + one with tag 0x00000023). The record reader FUN_00492be0 is
  [ENC]; the layout is fixed by the bytes [DATA]. Values: **gmth 7** (game type: event → `pack_events%02d`),
  **gmpm 0xc0c0**, isla 0, **leme 3214** ("On the road to "Solar Crown"", the event title), timG 160000, CtDw 210000,
  SP2R 13000, tras 5000, SPPG 1000, FiMi 25 / FiMa 250, CkMi 10 / CkMa 50, nblp 2, cops 2, Gmin 20 / Gmax 50,
  SLMi / SLMa 30000, SLTr 10000, SLT2 3000, Cmid / Cdlm 50, DmMi 5 / DmMa 20, MDTL 1000, DiCh 20, NbIA 3, NbCp 3,
  ChGP 1, Cwwa 1, Vlt2 1, fee1 50, **rewm 20000**, dscr 1, nbtm 1, mnme "Mission Honolulu", DesC "OAHU" (template
  left-overs), lcpi 1, dbna 6111. Most of them are race / traffic / cops parameters of the generic event engine; their
  readers are spread over the mode (vfunc +0x30 of the parameter objects, e.g. gmth at IGE container +0xf0, gmpm
  +0x178) and were not mapped one by one.
- `ENTI`: u32 count (2), entities. FUN_00b25fc0 [CODE] dispatches by tag: `CHK2` / `CHKP` checkpoints (count
  container +0x3dd4, objects of 0x80 bytes at +0x3de0, CHKP = old format flag), `DEC2` / `DECO` (+0x56e4), `VEHI`
  (+0x76f0), `CRUL` (+0x7fc0), others skipped; each object reads itself (vfunc, FUN_00b21a50).
- `CHK2` payload (0x74 bytes) [DATA]: u32 0, u32 id, u32 ×3, f32 k, **3×4 matrix `[R | T]` rows** (column-vector
  convention: the axes are the columns of R — the start gate's third column runs along the sea-front road, its third
  row across it [DATA + render check]), f32[4] (0, 0, 0, 1), u32 0, u32 0, **f32 7.0** (gate width; the editor add
  FUN_00b26990 defaults checkpoint +0x78 to 20.0 (DAT_00eddfd8) for types 0 / 3) [CODE + DATA], 4 × u32 −1. The
  runtime position is checkpoint +0x50 (FUN_00b22500, default (0, 0, 0, 1)) [CODE].
  - #0 id 715 (0, 0, 1), k 0.727: **(11095.28, 4.40, 17184.43)**, along (0.757, 0.017, −0.653): the start, on the
    sea-front road in front of the hotel E_HOTELA.
  - #1 id 127 (1, 2, 0), k 0.372: **(9957.18, 9.84, 14856.54)**, along (−0.706, −0.018, −0.708): the finish, on the
    road leading to the Solar Crown venue ECH_IA (83 m east of the spot).

### 4.3 The mission object (CGameMech) [CODE]
- Mode init FUN_00a24960 (mode+0x90): parameters gmth → mode +0x1b0704, gmpm → +0x1b0708; `DAT_01025818 == 2` (the
  story event) → FUN_00a20ca0; with gmth 7 the pack `pack_events%02d` of the event type (+0x40) is loaded from
  `Scenarics\Packs` into +0x1b5310 / the mission bank +0x1b53a0 (`pack_events13.bnk`); FUN_00a945f0 → FUN_00a93a60
  creates the mission object: FUN_00a157a0 (0x1910 bytes, vtables 0xf23368 / handler **FUN_00a1abe0**) or, for
  type 4, FUN_00a1d0f0 (0x1980, online freeride handler FUN_00a1d640). Vtable [2] = FUN_00b99310 (go to state).
- FUN_00a1abe0 states used here (logged names "MS_ONLINE_RACE_…"): 0 init → 1 installing → **2 Loading_Map** (waits
  FUN_00aee280 = world streamed at the start +0x1b5220) → with gmpm 0xc0c0 and an event (FUN_00a12680: gmth 7)
  **0xd**: C_INTRO_E%02d_%s (FUN_00b14160: "C_INTRO_E13_TESS") — **not in pack_events13** [DATA] → 4.
  **4 Waiting_Start**: enter: with an event and the player car: FUN_00cbd7a0 / FUN_0048b090 [ENC] then
  **passenger = FUN_00b84590(3000, 0xfa4713bc, 0xffa1845b, …)** (+0x124; 3000 = the passenger avatar state, 2000 the
  driver), FUN_00b9fa00(car) (car +0x79cc = passenger, FUN_00b86ac0, FUN_00b8d960(3000)); tick: when +0x65 → 5.
  **5** race: FUN_00a12c60 / FUN_00a12bb0 scan the 26 racer slots (+0x7c…, state values stored XOR-obfuscated with
  0x11ee11ee): a racer in state 1 = finished, 2 = failed; when one is done: `+0xf0 = (no racer in state 2)`, → 8 →
  (FUN_00a12160) **9**: GAUGES off, results FUN_00a17cb0 (`/:event_title`, `/:Reward`), `/:ShowCountdown`,
  `/:IsRaceOver`, passenger removed (FUN_00b9f950 / FUN_00b84530). State 5 enter registers the player car
  collision callback 0xa15530 (FUN_00c3f2a0, removed on leave by FUN_00c3f240) → FUN_00a154d0: with a passenger,
  FUN_00a15430(0, 5) at story step 4 (`DAT_01025608+0x9014 == 4`), else (0, 2).
- The finish test of the racer and the race start trigger (+0x65) were not found in the functions read [ENC/INF].

### 4.4 Passenger reactions (FUN_00a15430 / FUN_00a151c0) [CODE + DATA]
- Handler `"E%02d_%s"` = event type + FUN_00abb560 short name (table 0xf19744: 1 ADR, 2 TIM, 3 SHA, 4 FIL, 5 CHA,
  6 SPE, 7 DRI, 8 / 14 / 15 CON, 9 CTI, 10 CTR, 12 JAN, **13 TESS**) → `E13_TESS` (Entity__AnimationHandler of
  pack_events13: 15 AnmRef { File .bas, File_trk .trk }); FUN_00a127a0(index, name) returns AnmRef[index].
- Order [DATA, hashcodes]: 0–4 `ev_tessincar_impact01..05`, 5–9 `toplayer01..05`, 10–14 `phone01..05`. So family
  (5, 1) = toplayer01 (29.27 s: voices EVTE_to_player_01 / 01b / 01c), (6, 9) = toplayer02..05 + phone01..05, (0, 5)
  = the impacts.
- Choice: `index = random(count) + first` (FUN_0095b3e0: seed (DAT_01025608+0x9bb0) = seed × 0x7ff8a3ed + 0x2aa01d31,
  then FUN_006c8f00 [ENC: does not decode]); if already played (flags DAT_00fd944c) the first unplayed one of the
  family; all played → flags cleared, random again. Started only if the passenger's layer is idle (or forced); the
  start passes (−1, −1, 0.3) (DAT_0144b610..618 = DAT_00ede404 / DAT_00ede42c) [CODE].
- `.bas` = BAS! with one REFS entry (the .xan, a .anm of the bank). `.trk` = **EVT!** (u32 8, 0x71, 0x666; DATA
  { u32 n; f32 duration ×2; 16 bytes 0; items { u32 size; tag; body } }; REFS { type, size, u32 1, u32 item
  offset, body }): `MORF` { u32 n; f32 duration; f32 times[n]; { u32 ref; f32 length }[n] } = FaceFX animations
  (REFS 0x0302 "…\female.fxa::EVTE_:EVTE_to_player_01"), `FXS!` { u32 1; f32 start ×2; u32 1; u32 ref; u32 flags
  (0x37); u32 0; f32 duration; f32 matrix[16] } = sounds (REFS 0x0004 u64 Fx__Root, voices `evte_*` of
  pack_events13__<lang>.bnk) [DATA]. The player of AnmRef File_trk is not identified [INF: the track starts with
  the clip].

### 4.5 Data [DATA]
- DBEvent[21]: `_eventfile "TESSEVEN"`, `_eventtype 13`, `_pnj1 0x293` = DBPNJ[227] **`STO_Tess_Phone`** (= STO_Tess_Wintory
  with `W_PN_G_Phone_TESS` in the glasses slot: the phone of the phone reactions), `_lifetime 1`, `_day 1`, `_night 1`.
- pack_events13.bnk: Entity__Actor Carpet / Truck_01 / Truck_02 (+ Body, Body_01, D_SPLANE_*), PNJ NPC_W01 and
  Tess_Wintory, cinematics C_LOOSE_LOOP (CinProps 614), C_LOOSE_E13_TESS (516), **C_TRUCK** (0, CinEnd 3: cin
  `event_tess_M_SC_LIFE_Totem_LIFE.cin` 2.0 s, scene `M_SC_LIFE_life.sce`: NO_CAMERA, Truck_02, Carpet, Truck_01, one
  key each = static Solar Crown trucks and red carpet), E13_TESS, 59 Fx roots, `evte-w.fxe` / `evte-m.fxe`.
- Passenger base pose: avatarsdriver.bnk `Entity__AvatarPassenger` PassengerWoman (animPassenger =
  shoppinggirlcarlayer0.bas → **SHO_stand_car.xan** (weight 1), shoppinggirlcarlayer1.bas = IK layer: type 6 Spine →
  IK_Back, type 2 UpLeg_x / Leg_x → IK_x_Foot (120 / 30 / 50), type 8 belt nodes, + sho_front_back.xan) /
  PassengerMan (hitchhikerlayer0/1, hit_stand_car) [DATA]. Car nodes PsgSeat_LR, Back_LR_R, PsgFoot_LR_R / _L (tables
  0xf7dbf0 / 0xf7db68); FUN_00b8ade0 state 3000: seat group (0, +0x22c, +0x230) = IKOffsetDB (r5, r4), root = PsgSeat
  (+0x1ae4) + offset (+0x1ae8) [CODE].
- No DBPhoneCall row of step 4 (33 rows, none for this event) [DATA]: `frontend/allres/phonecalls/sto_tess.bnk` is
  only Tess' portrait (sto_tess.2db / .2dm) for later calls (FrontEnd/AllRes/PhoneCalls/%s%s%s.bnk); the "phone"
  of step 4 is Tess' own phone reactions.
- Flash (GamePC.cpr, mode 4): HUD, GAUGES, RESULTS, DASHBRD. Audio: audiosetup.xmb GameModes[4] MISSION (same
  MixPreset as FREERIDE).
- Route: start (11095, 17184) → finish (9957, 14857), 2.6 km straight line; sectors 1-2-6-6 … 1-2-4-3.

### 4.6 Implemented (game/mission/*, game/avatar/seated_avatar.*, ik_offsets.*)
GMMission (`-storystep 4` / `-gm MISSION`; reached from the end of C_MEETING_TESS):
- loads DBEvent TESSEVEN, `ig2/tesseven.ig2` (IgeFile), `pack_events13.bnk` (+ `__<lang>` voices), the E13_TESS
  reactions, C_TRUCK props at ECH_IA_1243I (CinematicProps, spot first transform [INF frame]);
- CGameMech states 2 → 4 → 5 → 8 → 9 (0xd skipped: no C_INTRO_E13_TESS); the car (California) starts at checkpoint
  #0 along its gate axis [INF: placement call not found], FREERIDE driving / chase camera / HUD (goal 4032, GPS
  marker and distance on the minimap [INF drawing]);
- the player avatar at the driver seat (stance_turn + driver IK) and **Tess (STO_Tess_Phone) at the passenger
  seat**: SHO_stand_car + spine aim / leg IK to Back_LR_R, PsgFoot_LR_R / _L with the IKOffsetDB passenger record;
- reactions: family (5, 1) while the 40 s timer is above 25 s, (6, 9) every 25 s, (0, 5) on a chassis impact
  (approach speed > 2 m/s [INF]); clip blended 0.3 s [INF], FaceFX from MORF, voices from FXS! (2D);
- **finish [INF]**: the car within 7.0 m (the gate width) of checkpoint #1 or crossing its plane within 7 m
  laterally → +0xf0 = 1 → 8 → 9 → C_TRUCK stopped, Tess out, story step 5 (CINE kind 2).
Test options: `-startnearfinish d`, `-reactiontimer s`, `-carcam dx,dy,dz` (screenshot view), `-logdrive`, `-logseat`.

### 4.7 The event reward (this work) [CODE + DATA]
- Results (CGameMech state 9 enter, FUN_00a17980 from 0xa185e4): with success (+0xf0) RESULTS gets `/:nb_done`,
  `/:nb_total`, then the reward = event record +0x5c (or FUN_00a0d610 for a challenge, gate FUN_004c3140 [ENC]) →
  **FUN_00abe0e0(reward, 1, 7)**, `/:Reward` = FUN_0099d810 money text, `/:event_title`. The reward of TESSEVEN is the
  IGE parameter `rewm` = **20000** (registered at IGE container +0x1030 by the parameter table code at 0xa1f303, loc
  0x531) [DATA; its copy into the event record is INF].
- FUN_00abe0e0(amount, bPending, reason) [CODE]: statistics +0xf05c[reason] += amount (reason < 0x1c, 7 here);
  bPending 0: money (FUN_0041b580 / FUN_004671e0 [ENC accessors], ≥ 0) and profile save; **bPending 1: pending money**
  (FUN_004aaf70 / FUN_004aafd0 [ENC accessors]); the 1 000 000 checks (+0x151) are the millionaire unlocks.
- Pending → money: FUN_00abd0d0 (shows the HUD "money" element, money object +0x50 = **1.0 s**, DAT_00edf668) starts
  the counter; the profile tick (0xabd460, 0xadc870) calls **FUN_00abcf60(dt)** while +0x50 ≥ 0: rate =
  round(pending / timer), step = round(rate × dt) clamped to the pending amount (sign aware), money += step,
  pending −= step, timer −= dt; timer < 0: money += the rest, pending 0, timer −1 (DAT_00ede404), "money" hidden
  [CODE 0xabcf60..0xabd0c2; the rounding FUN_006c8f00 is ENC → nearest INF]. FUN_00abd0d0 is called by the FSCommand
  **`moup`** (FUN_0099c490), sent by RESULTS.vmf at frame 372 (`results_board` 'event_success', `money` clip
  'open') [DATA], by SHOPSCAR at the purchase, and after the tuning / shop costs (FUN_009f1880, FUN_009e2cc0…) [CODE].
- The shop money test FUN_00ddec10 compares the price with the money only (FUN_0041b580, not the pending amount)
  [CODE], so the reward has to be through the counter before the dealer.
- Implemented (GMMission::creditReward, game/player_profile.*): at state 9 with success, ProfileAddMoney(rewm, pending,
  reason 7); the RESULTS `moup` → ProfileStartMoneyTransfer and the 1 s counter (ProfileTickMoney, FUN_00abcf60) are
  run at once, since the story tick leaves the mode in the same state-9 tick (vfunc8(5)) and the RESULTS screen is not
  shown [INF] → **27 000 + 20 000 = 47 000 $** at the dealer. The dealer then allows any of its three cars
  (24 500 / 15 000 / 20 000 $: /:EnoughMoney 1). The RESULTS movie itself is not loaded by GMMission.

## 5. Steps 5–7 (Solar Crown, used car dealer)

### 5.1 Step 5 "Solar Crown exterior" / 6 "interior" [CODE + DATA] — implemented
- Code: 5 (case 6): profile 5; FUN_00aba280(0x183, −1, −1, −1); mode CINE, FUN_00b9a1f0(callback FUN_00b104d0) kind 2
  → callback → 6. 6 (case 7): **no profile write**, mode 0x1f again, FUN_00b9a1f0(0 callback) kind 3; the tick sets
  `DAT_01025608+0x9734 = FUN_00b104d0` once `DAT_00fd50fc+0x31 == 0`; leave resets `DAT_00f805a0` → callback → 7.
- solar_crown_ext.bnk [DATA]: C_SOLAR_CROWN_E (CinProps 256, CinEnd 3), cin `solar_crown_ext_M_SOLAR_CROWN_E.cin`,
  scene `M_SOLAR_CROWN_E_SolarCrown.sce` **12.46 s**: cameras cam_sol_01..06, `PLAYER_CAR` (134 position keys, message
  `moof` at 2.68), `Avatar` (messages avm4 at 0, avm1 at 4.32; sol_run-m from 4.32), Vigile_01 / 02, Regie_01 / 02,
  Sound_Op_01, CF_Boom, NoteBook, Tess, Assistante; 31 Entity__Actor props (trucks, TV boxes, columns, spot lights,
  cables, carpet), PNJ configs STO_Cameraman02 / 03, STO_Vigile01 / 02, STO_SoundOperator, STO_Tess_Wintory,
  STO_Tess_Assistant02; FaceFX intro-e-m / -w.
- solar_crown_int.bnk [DATA]: C_SOLAR_CROWN_I (CinProps 0, CinEnd 3), scene `M_SOLAR_CROWN_I_SolarCrown.sce`
  **107.14 s** (18 cameras, Tess, Avatar, Todd (SPT_Todd_Bishop), Assistant_01 / 03, Cameraman_01 / 02 / 03_director,
  SoundOperator, look-at targets), voices Intro-I_Solar_Crown_* in `solar_crown_int__<lang>.bnk`; the interior
  `Interior/ich_ib.bnk` (2 models) [CODE path "%sInterior/ich_ib.bnk"].
- Implemented in GMCine: kinds 2 / 3 in the spot frame of ECH_IA_1243I (first transform) [INF: as C_FIRST_OUTRO
  whose PLAYER_CAR lands on the road in its spot frame; the car of kind 2 drives on the venue paving, terrain 9.0 /
  car 9.4]; the interior bank (IntroCinematic::Setup::InteriorBank); `avm4` / `avm1` of Avatar seat / unseat the
  avatar in the scene car [INF: handler ENC]; ends chain 5 → 6 → 7. `-storystep 5` / `6`.

### 5.2 Step 7 "Used Car Dealer" (Todd Bishop) [CODE + DATA] — implemented (GMShop)
- Enter: profile 7; FUN_00aba280(0x146 = 326 Dino_246_GTS, −1, −1, −1) [INF: meaning of the −1 call];
  `DAT_01025784` = index of spot **ESTOL_B_1275I** (0x5f425f95_848b8576: (12255.6, 6.09, 16370.1), heading 1.585;
  second transform (12298.7, 6.01, 16374.6), −3.046); mode 3 **SHOP**.
- Tick (mode 3): when the shop is ready (`mode+0x1b4cd0`), `mode+0xb43c == 0`, the walk manager is active
  (DAT_00fd95e0 +0xd1c) and the camera is the walk camera (type 9): pop_goal 0xfc1 = 4033 "Buy a car!", goal
  TUTO_BUY_A_CAR (DBRewardXP 179), this[6] = 1; then, back in FREERIDE (mode 1) → step 8.
- Leave: FUN_00de3ee0(profile vfunc 0x70): **regenerates the used-car stock** (per island, 8 slots × 0x158 bytes:
  random car from the candidate lists FUN_00de38f0, colour, 3 condition bytes 0..2, a 5..10 value…).
- Data: DBCarShop[25] `_spot_name "ESTOL_B_1275I"`, `_libelle_hc 0xcd7` (3287 "Used Car Dealer"), `_slot_1..3`
  = **371 Mustang_Fastback_68, 380 Esprit_S3, 355 Delta_Integrale_Evo_2** (`_isbuyable_1..3 = 1`) — the proposed
  used cars; DBCarShop[24] `ESTOL_B_2265` same slots. Banks: `islands/ibiza/exterior/estol_b.bnk` (+`__<lang>`),
  `interior/istol_b.bnk` (+`__<lang>`); PNJ `SPT_Todd_Bishop` (DBPNJ, `_fileconfigid "SPT_Todd"`, clothes
  `M_PN_*_Tbishop`, hair `M_HR_R_Todd`); tutorial DBTutoMess "used" (sound HSFR_voice_43); flash SHOPSCAR + DASHBRD
  (mode 3); `scenarics/cinematics/car_dealer.bnk`.

#### 5.2.1 Shop objects [CODE]
- Spot record +0x100 (rt+0x79) = **0x11** → FUN_009f3050 creates the used car dealer FUN_00dde970 (0x1fb0 bytes,
  vtable 0x00f26380, base FUN_009ed810), shop type +0xf0 = 3, SHOPSCAR page "scr_open" (FUN_009f23e0). Dealer vtable:
  [1] FUN_00dde7e0 (FUN_009ed1b0 money texts + walk +0xd68 = 1), [3] FUN_00dde830 tick (C_EXPLAIN_FROG when
  +0x1fa9 && DAT_010268b0 == 0 && DAT_010268b6), [8] FUN_00dde990 (used stock, story steps 11 / −1 only),
  [10] FUN_00dde890 (C_CHOICE_TODD end), [13] FUN_00ddf4c0 FSCommands (`shca`, `eval` → FUN_00ddec10(1),
  `buin` / `buyc` → FUN_00ddec10(0), `stci` stops the loop +0xb440, `wrck`), [14] FUN_00dde780 price.
- Mode 3 vtable 0x00f25c38: [1] FUN_009f2eb0 listener (event 6 of +0x1ba334 → mode+0xc1 = 1, mode := 1 FREERIDE),
  [7] FUN_009f2c90 (IsCarInStock 2, goto "scr_open"), [18] FUN_009f4ab0 init (C_FIRST_INTRO CinProps |= 4 skippable,
  loop C_FIRST_LOOP, step 7: /:IsTutorial, /:num_car 1, /:nb_cars, /:Message = DB 0x9f3; buy cinematic +0x1ba334 =
  **C_EXPLAIN_FROG** at step 7, else C_BUY_CAR_I), [30] FUN_009f3d00 FSCommands. Interior bank FUN_00a91360
  `"%sInterior/%s.bnk"` with the first letter of the spot bank replaced by 'I' → `interior/istol_b.bnk`; intro /
  loop listeners FUN_00a91ab0 / FUN_00a91820 (intro end → vfunc 0x1c, C_FIRST_LOOP).
- Slots FUN_009f4080: a buyable DBCarShop slot gets colour 0, the DBCarsAndRims row equal to `_default_rims`,
  interior 0; random cars use the LCG seed * 0x7ff8a3ed + 0x2aa01d31.
- Price FUN_00dde780 → 0 for a DBRewardCar `_carunlocked` car (FUN_00b18370), else FUN_00d8d140: `_price` + rims
  `_price` (DBRims +0x44) + colour `_price` (+0xf8) + interior `_price` (+0x94) − stock discount % − DBCollectionLevel
  `_percent` (table 0x1024cf8, 0x28-byte rows; points getter FUN_00445780 [ENC] → 0 points [INF]).
  **Mustang Fastback 68 24 500 $** (23 000 + rims F_02 1 500, Raven Black), **Esprit S3 15 000 $** (Light Blue),
  **Delta HF Integrale Evo 2 20 000 $** (Dealers Collection, `Delta_Evo2_Ext_Rouge_02`) [DATA]; 0 km (new slot
  record FUN_009f2790 → FUN_00abffd0) [CODE].
- Money: new story profile 27 000 (FUN_00abe000, 0xadb2e6 `eax = 0x6978`) [CODE]; FUN_00abe0e0(amount, bFlag,
  reason): bFlag 0 → money +0x2c (clamped ≥ 0), 1 → pending +0x44. Purchase FUN_00ada490 → FUN_00adc920: garage
  record FUN_00abffd0(car, colour, rims, interior), condition bytes, money −= price, COLLECTION_CARS_DIF_n.
- Car data FUN_0099dbd0 (DBCarPhysicsData row 0x470 bytes, fields in name order): /:Current_car, /:cur_group (table
  0xedfbe0), /:cur_difficulty, /:Km_m / /:Km_i `"%08d"` and /:Centaine_m / _i `"%01d"` = tenths ((km − int) × 10)
  [CODE 0x99e035..0x99e0b6], /:IsWindows, /:IsDoors, /:CurrentPrice, power `"%d %s"` "hp", torque `"%d %s @%d %s"`
  (DB 0x434 / 0x443, imperial ÷ 1.356, 0x442 / 0x2e8), weight (0x438 kg / 0x446 lb ÷ 0.45359237), 0–100 `"%.2f %s"`
  (0x2e2, imperial × 60 / 62.15), max speed DB 0x3e1 `§SPEED§ km/h` / 0x3e2 `§SPEED§ Mph` (× 0.6215), 0x2e7 cc,
  gearbox 0x80a / 0x80c / 0x80d, 0x1342 Gears, MotorPattern, WheelsPattern, gauges FUN_00adea90 (accel 35 → 2.25,
  speed 70 → 470, braking 170 → 96, handling 0 → 100).
- Text markers: § (0xA7, string 0xee5a74, UTF-8 in the DB texts) delimits tokens: FUN_00999b00 replaces `§TOKEN§`
  (FUN_00999d00 formats `"%d"`), FUN_00999d70 counts `§n§` markers and FUN_00999fc0 puts argument n in place of
  marker n. Money FUN_0099d810: 3-digit groups `"%s%03d%s"` ("," when the language is "us", else " ") into DB 932
  `$§1§` → **"$27,000"**.
- Common movie variables: FUN_00dbadb0 sets on every loaded movie `/:Version` = "W.I.P" and `/:state_Demo` = false
  (0xdbae48 / 0xdbaeb1); FUN_00a92010 (0xa920e7) / 0xad9b02 set `/:state_Metric` = (unit option DAT_010017f0+0x14
  != 1). SHOPSCAR reads `state_metric`: **Eden ActionScript names are case-insensitive** (AS1; also SetMiniMenu
  pushes the "Test drive" entry to `SetBt` for `setBt`) [CODE + DATA]. TestMoney only evaluates /:EnoughMoney when
  `state_Demo == 0`.

#### 5.2.2 SHOPSCAR flow [DATA, shopscar.bnk SHOPSCAR.vmf]
- Walk manager FUN_0094f440: a selection sets /:SelItemType then `cursor` gotoAndPlay "on" / "off" [CODE 0x94f78a].
  Cursor (sprite 187) "on" → ItemAction('over') → car_datas (sprite 220 / datas_base 5054: SetDatas reads the _root
  variables, text fields bound to `../../:cur_power`...). Validate (key 13 → A()): `wmof`, `sndp:valid`,
  ItemType('valid') → `shko`, **SetMiniMenu** (vehicle: m_Buy_ "Buy", m_EnterCar_ "Get into the car", m_Data "Data",
  m_Compare, m_Test "Test drive"), cursor SetMenu → `camn`, WhatPage "scr" → `scr_buying`.
- Mini menu (sprite 5192, keys 14 / 15 move, 13 validate) → ValidMenu → BtValid "buy" → **TestMoney**: `eval` →
  /:EnoughMoney: 1 → `sndp:bigvalid`, DBXO("confirm_buy", m_BuyCar "Buy this vehicle?", yes / no); 3 (+ IsTutorial)
  → DBXO("not_enough_money_tuto_scr", m_NotEnough "Not enough money!", OK only) → BackToWM; 0 / 2 / 4 (cars to
  sell, garage full, no garage) need a garage.
- DBXO → FSCommand `dbxo` → FUN_00d80ba0 [CODE]: /:Msg1Txt, /:Msg2Txt, /:Msg1Type, /:Msg2Type, /:YesNo copied to the
  **GENERAL** movie (0xee4c74), /:DialogBoxSource = source movie name, GENERAL DialogBox() (dialog_box ch451:
  `wmof`, open, keys 13 → A() "a", 8 → B() "b" on ch449 / ch450 [DATA]). EndClose: `wmon`, `dbxc` → FUN_00d80ba0
  copies GENERAL /:PlayerAnswer to the source movie and calls its DialogBox().
- SHOPSCAR DialogBox "confirm_buy", answer "a": `moup` (FUN_0099c490 → FUN_00abd0d0 money refresh) and
  **BuyVehicle**: `stci`, gotoAndPlay "scr_close", `buyc` → FUN_00ddec10(0) → purchase, then FUN_00ddea60: FREERIDE
  start cinematic DAT_0102582c = "C_INSTALL_FROG", goal closed, money display hidden, +0x1fa9 = 1 → the dealer tick
  starts **C_EXPLAIN_FROG** (21.4 s, Todd explains the F.R.I.M.) once the walk is off → mode listener event 6 →
  FREERIDE → step 8.
- Selectables of istol_b (Entity__FpSelectable): NPC_Todd_area (type 21 npc_receiver → `camn` → C_CHOICE_TODD;
  allowed in SHOP only at step 7), NPC_Jude_area (type 4 stolencar → C_JUDE_FRONTEND), Exit (type 0); walk start
  camera CAM_INTRO (4.36, 1.64, 22.56), Fov 0.610865; Entity__ShopData D_ShopData PosCams D_CAR_01..08 [DATA].

#### 5.2.3 Implemented (game/shop/gm_shop.*, IntroCinematic show cars / selectables, flash player)
`-storystep 7` / `-gm SHOP` (+ `-skipcine`, `-money n`, `-walkat x,y,z,yaw`, `-no3d`): istol_b interior + estol_b
exterior and the world around the spot, Todd and Jude (C_FIRST_INTRO, then C_FIRST_LOOP), the loaned Dino as
PLAYER_CAR, the 3 cars on D_CAR_01..03 (high banks, DB rims, colour 0 from `<car>_color.2dm`), walk mode with the
SHOPSCAR cursor, goal "Buy a car!", car data panel, mini menu, price check, GENERAL confirm / "Not enough money!"
dialog boxes, purchase (profile garage + money), C_EXPLAIN_FROG (skippable), FREERIDE story step 8 (stub: logs the
step data, "not implemented" text). Flash player fixes found on the way: EditText variables resolved from the owning
clip, `../` paths, Array (`new Array(...)`, push / pop / join, length), case-insensitive names.
[INF]: the mode / pad routing of the keys (validate = key 13, back = B() of /:Clip; dialog 13 / 8); the car hit boxes
(car body bounds); the cinematic skip input; the unit option (metric); C_FIRST_INTRO played whatever the previous
mode (FUN_00425d70 [ENC]); collection points 0 (FUN_00445780 [ENC]); the car name (brand + model + version; descriptor built in encrypted code); the
GENERAL FSCommands (`wmof` / `wmon`) reach the shop handlers. Not done: Todd's C_CHOICE_TODD menu answers, the step 8
FREERIDE tutorial itself. Enter / Data / Compare / Test drive: 5.2.4–5.2.6; the step 4 reward (47 000 $ at the
dealer): 4.7.

#### 5.2.4 The car mini menu: Enter, Data, Compare, Test drive (this work) [CODE + DATA]
SHOPSCAR `SetMiniMenu` (vehicle) pushes `[label, BtValid, '', 0]`: m_Buy_ "buy", m_EnterCar_ "enter" (m_GetOnBike_
for a bike), m_Data "datas", m_Compare "compare" (m_ResetCompare "reset_compare" when `_root.IsCompare`), m_Test
"test"; **ValidMenu** (0x13156) dispatches on `_root.BtValid` [DATA]:
- **enter** → `ItemInfo = false`, `enter_car.gotoAndPlay("enter")` (sprite 221): frame 'enter' closes car_datas,
  `_root._buttons = false`, `_root.Clip = enter_car`, `Input = false`; frame 9 `initLoading()`: `TimeOut` /
  `CanEnterCar` false, `cmon_loader.initLoading("_root.CanEnterCar", true, true)` (sprite 4866: `upcart.onEnterFrame`
  polls the variable each frame → `closeLoading` → `endLoading`), FSCommand **`rqca`**. endLoading: help "in_car"
  (or "in_car_p" when `IsPassenger`), FSCommands **`cain`**, **`shin`**, gotoAndStop 'enter_stop'. `B()` while
  `Input`: during the loading `caca` + onClosed, else `sndp:back` + ExitVehicle → onClosed: **`caou`**, `mcou`,
  BackToWM. C++ [CODE]: `rqca` → walk manager FUN_00952f50 (walk avatar hidden, +0xd62 = 1) and the mode handler
  FUN_009f3d00 → FUN_008c1880(DAT_01025608 + 0x1a898, selected slot (+0x1ae9b0), player): the slot car must be loaded
  (car +0x79bc: +0x3d0, +0x7460), seat FUN_008c1020 (driver seat, +0xac = 0), FUN_008c12f0: FUN_00a503c0 (door /
  light object of the shop scene FUN_00a49370), FUN_00a512d0, **FUN_00a519b0(slot, 0)**: the player avatar
  (FUN_00b83860, +0x9c |= 1) becomes the driver of the slot car (FUN_00dc8c50 current car, FUN_00b9fa00),
  **FUN_00a50550**: in-car view (car category not a bike 0x12 / 0x13 / 0x16: the slot camera FUN_00ca0cf0(+0x34[slot]),
  FUN_00ca0d90(1.0), eye from the car node `STEER_HR` [the matrix helpers FUN_0044f4c0 / FUN_0042b4f0 are ENC], state
  +0x268 = 2); then SHOPSCAR `/:IsPassenger` = false, `/:TimeOut` = false, **`/:CanEnterCar` = true**. `cain`: mode
  +0x1ba174 = slot. `caou` → walk FUN_00952f00 (+0xd62 = 0, FUN_0094fdb0: walk camera back) and FUN_008c11f0 →
  FUN_008c11a0 → FUN_00a517d0 (avatar out, state 1000, previous current car back). `caca`: DAT_01025608 + 0x1a94c = 0.
  `mcou` (FUN_00d82692): mouse cursor flag off.
- **datas** → `car_datas.gotoAndPlay("fd_open")` (sprite 220: `inFullData`, `data_full` sprite 5119 'default' /
  'bike'), SetHelp close; 'fd_stop' B() → `sndp:back`, 'fd_close' (back to the walk via `cursor.SetMenu`). The sheet
  reads (FUN_0099dbd0) /:cur_max_speed_m/_i, cur_0_100, cur_power, cur_weight, **cur_bhp_t_m / _i** ("%.3f %s/%s" mass /
  power, kg or lb / hp), **cur_red_line_m / _i** ("%d hp", "%d hp @%d rpm": power and _power_rpm, +0x31c),
  **cur_type** (_engine_type +0x170 → DB 0x7f0, 0x7e5, 0x7ef, 0x7ed, 0x7e4, 0x7ec (7), 0x7e7, 0x7e8, 0x7ee, 0x7eb,
  0x7e6, 0x7e3 (13), 0x7ea, 0x1fb6, 0x7e9, 0x1fb3, default 0x7e2), cur_torque_i, cur_displacement_i, cur_nb_gears,
  cur_type_gears, MotorPattern / WheelsPattern, **cur_brakes_front_i / _rear_i** (FUN_004d89b0 / FUN_004d8990 [ENC] →
  INF: the DB texts `_brakes_characteristics_front_hc` / `_rear_hc`), **cur_tires_dim_f / _r** ("%d/%d/%d" of the
  DBRims row: _largeur, _hauteur, _diameter; "ZRF" / "WR" variants for the special cars 0x193 / 0x194),
  cur_q_mile_m / _i (_quartermile_sec, 0 → "n/a"), /:l_dim_01 / 02 ("%d mm\n%d mm\n%dmn": _width, _length, _height /
  _track_front, _track_back, _wheelbase), cur_interior / cur_abs / cur_susp_f / cur_susp_r = "n/a" [CODE]. With the
  language "us" and the unit option 0 the code writes the literal "hp" (0xf2bc78), else DB 0x443 "bhp" [CODE].
- **compare** → `car_datas.gotoAndPlay("cp_in")`: FSCommand **`comp`**, `_root.IsCompare = true`,
  `datas_base.cp_in`; `Input = true`, `Clip.B()` (back to the walk). `comp` → FUN_00b95840(DAT_01025608 + 0x9bb4,
  mode + 0x1ba1b0 = **the record of the selected shop car** (FUN_009f2790)) and FUN_00b959e0 [CODE]: record +4 name,
  +8 group, +0xc upgrade, +0x10 speed / +0x14 accel / +0x18 handling / +0x1c braking gauges, +0x20 mph / +0x24 km/h
  speed, +0x28 0-60 / +0x2c 0-100, +0x30/34 power, +0x38 difficulty (1..5) → `/:Comp_car`, `/:comp_speed`,
  `comp_accel`, `comp_handling`, `comp_braking`, `comp_max_speed_m` (0x3e1 SPEED), `_i` (0x3e2), `comp_0_100_m / _i`
  ("%.1f %s", 0x2e2), `comp_power_m / _i` ("%d %s", 0x443), `comp_difficulty`, `comp_group`, `comp_upgrade`. The next
  car under the reticle opens car_datas 'cp_open' → `SetDatasCp` (g_speed_cp / g_accel_cp / g_braking_cp /
  g_difficulty_cp gauges, ca_name_cp, cp_max_speed / cp_0_100 / cp_power texts) next to its own data [DATA]. The
  record is global: FUN_009f2c90 (scr_open) sends it again through FUN_00b95e30 (`/:is_compare`). **reset_compare** →
  'cp_out': FSCommand `rsco` (no SHOP handler: only FUN_00a712d4 of another mode → FUN_00b957c0), IsCompare false. The
  garage cars are not compared [CODE].
- **test** → SetHelp close, cursor / car_datas / price closed, `_root.gotoAndPlay("cd_close")`, `AfterValid = "test"`;
  root frame 236: `AfterValid == "test"` → FSCommand **`tstk`** [DATA] → FUN_009f3b30 (5.2.5).

#### 5.2.5 Test drive: mode 7 TESTCAR (this work) [CODE]
- `tstk` FUN_009f3b30: DAT_00fd92a0 = the slot car (FUN_009f2570), DAT_00f77688 / 8c / 90 = colour / rims / interior of
  the slot record (+8 / +0xc / +0x10), **DAT_00f77694 = 120.0 s** (DAT_00edf610), passenger: shop type 0 "Hostess",
  3 **"Jude"** → FUN_00b34fd0(+0x1b6330, 'CPNJ', name) entity +0xe0 (istol_b `Entity__PNJ Jude`, ConfigName
  **SPT_Jude** [DATA; +0xe0 = the config name: INF]) or "shop_lamborghini" → FUN_00dc2070: DBPNJ row of that
  `_configid` into DAT_00fdb450 (FUN_00cb5bd0); FUN_004cc860 [ENC]; mode +0xc1 = 1; **mode := 7** (previous 3). The
  casino path FUN_008d9a10 sets DAT_00f77698 = 0x1d (back to CASINO), the shop leaves it −1.
- Mode vtables 0x00f1bc50 / 0x00f1bc9c: init **FUN_00a90cf0** (DAT_0116081c..e, FUN_00ab9be0(test car, colour, rims,
  interior): the test car is the current car, FUN_00a945f0); activate **FUN_00a90990** (objects 'bird' / 'papr' on,
  FUN_00a94320, FUN_00afbb10 when the story step is not 11 / −1, **FUN_00a906a0**: passenger FUN_00b84590(3000,
  0xfa4713bc_ffa1845b, DAT_00fdb450) seated by FUN_00b9fa00; **FUN_00960280(6, 1)**: the car at the spot: an entity
  'CARP' "PosCarParking" of the spot bank (none in estol_b [DATA]) else FUN_00a39630 (the spot position projected on
  the road network FUN_00cfb280 / FUN_00cfab80 of DAT_0119b420); start time +0x1b07f0 = −1; the driver seated
  FUN_00baec60 / FUN_00bbb810; HUD bindings `/:MinCd` (0..999), `/:SecCd` (0..59), `/:CentCd` (0..99), 2 digits
  (FUN_00968ef0) and **`/HUD/time_cd`** shown + 'open' (FUN_00a12300(1)); `/:IsTest` true); tick **FUN_00a90850**
  (first tick: start = now, end = now + 120; remaining = max(end − now, 0) → FUN_00a11120 (ms = round(t × 1000 + 0.5),
  min / sec / cent); paused (DAT_011fe90e): end += dt; now > end → FUN_00a90300); FSCommands **FUN_00a90570**
  (`quit` → FUN_00a90300; `sbdd` → `/:cur_gm` "test", `/:IsRestart` false, `/:PauseQuit` = DB 0x207); end
  **FUN_00a90300** (the spot DAT_01025784 entered again: FUN_00b30030(1, 0) → its mode SHOP; else mode 3);
  deactivate FUN_00a90730 (`/:IsTest` false, objects off, passenger out FUN_00b9f950 / FUN_00b84530); exit
  FUN_00a90c70 (FUN_00ab9b70: the previous current car back **only at story steps 11 / −1**).
- Back in SHOP (FUN_009f4ab0, used dealer at step 7): previous mode 7 → the intro **C_INTRO_I** (not skippable)
  instead of C_FIRST_INTRO; C_FIRST_LOOP; IsTutorial [CODE + DATA].

#### 5.2.6 Implemented (this work)
- GMShop: `rqca` → the player avatar on the driver seat of the showroom car (IntroCinematic::setSeatedShowCar +
  the driver pose / IK of updateSeatedPlayer with the car's IKOffsetDB record), the cockpit camera of the car
  (camerasingame.xmb mode 23 of `_camera`, CinematicCar interior LOD), look around (mouse / right stick / menu
  arrows, ±120° / ±45° [INF]), `/:IsPassenger` / `/:TimeOut` / `/:CanEnterCar`; `caou` / `caca` → out, walk camera
  back. Flash player: **MovieClip.onEnterFrame** (the enter_car loader polls through it) and the function closure
  scope. Data: the full sheet variables above. Compare: `comp` record + `/:comp_*`, `/:is_compare` at scr_open.
  Test drive: `tstk` → GetTestDriveRequest() → GMTestCar.
- GMTestCar (`-gm TESTCAR`, `-testcar id`, `-testtime s`): the slot car (DB banks, colour, rims) with FREERIDE driving
  (VehicleDynamics with its DBCarPhysicsData row, chase / cockpit cameras, HUD + GAUGES), the player driver and Jude
  (SPT_Jude) as passenger, the HUD countdown /HUD/time_cd, /:IsTest; start = the dealer spot's second transform
  (12298.7, 6.01, 16374.6) heading −3.046 [INF: the road projection FUN_00a39630 needs the road network]; pause =
  `quit` [INF key]; at 0 → SHOP (C_INTRO_I, the test car stays the current car at step 7 [CODE]).

### 5.3 Steps 8–10 [CODE]
8 "Tuto FROG": FREERIDE at the dealer exit (FUN_00a6a650 on ESTOL_B), `pack_freerideFROG` bank, timer 300 s, GPS
cleared, goal TUTO_FROG (4034 "Bank the first level with the F.R.I.M."); tick: Todd as passenger
(FUN_00b10d00("SPT_Todd_Bishop") → FUN_00b84590(hash 0xfa4713bc_ffa1845b)), FROG tutorial voices FUN_00b10890
(FUN_00a151c0 families 10/8/1/5/12), done (+0x716 and FUN_00d96e60) → 9. 9 "Go Home": TUTO_GO_HOME (4035), Todd
comment family 0xd; mode LIVING → 10. 10: LIVING at EPH_L0C_1254I (caravan), RACING_LICENCE_C4,
COLLECTION_HOUSES_1; back in FREERIDE → 11 "Done", pop_goal 4036 "Join Todd at the Classic Racing School", GPS
target spot ERS_C_1274.

---------------------------------------------------------------------------------------------------
## 6. Open points
- The white of C_FADE_FREERIDE [ENC/INF]; the exact cinematic transform of CINE (3.6) [ENC → INF]; where the car is
  put before the cinematic [ENC]; `zcvn` / `zcne` handlers; the `phot` photo is not taken.
- ND_RPM / AAA (garage leftovers) are drawn at their entity placement like every Entity__Actor: 1.31 m under the
  car level, i.e. under the ground, as their rest pose in garage.bnk (AAA (2.459, −1.229, 16.41) vs D_CAR y 0.084)
  [DATA]; checked: hiding them changes nothing on screen.
- Car windows (`clwl` / `clwr`) are not animated; FaceFX look-at as in the intro.
- Rendering at `-envquality 2` / `3` crashed in the container. Cause (this work): **lavapipe maps one buffer per
  descriptor set**; a frame at envquality 3 records ~46.6k draws (first frame, with the environment cube; then
  ~27–28k), i.e. 46.6k descriptor sets in one frame slot plus the other slot's ~15–28k: at ~62.4k live sets the
  process reaches `vm.max_map_count` (65530) and lavapipe's set allocation writes through a failed mapping
  (crash in `vkAllocateDescriptorSets` → memset). It is **lavapipe / container only** (MoltenVK does not map per
  set); the 512 MB transient buffer holds ~63k draws (8.3 KB of constants each) and was not exceeded (no overflow
  warning at 46.6k draws). Container workaround: `sysctl -w vm.max_map_count=262144` (then envquality 3 renders).
  Margin: 46.6k / ~63k draws per frame; a denser place could reach the transient limit (draws dropped with a warning).
- Step 4: the start pose (checkpoint #0), the finish test (7 m gate), the race start (+0x65), the reaction random
  mapping (FUN_006c8f00), the reaction blend (0.3 s), the C_TRUCK frame (spot first transform), the passenger IK goals
  and the in-car avatar light factor are [INF]; the reaction track player and FUN_0045f2f0 are [ENC].
- Steps 5 / 6: the scene frame (spot first transform) and the avm4 / avm1 handlers are [INF].
- Step 7: see 5.2.3 ([INF] list); 5.2.4–5.2.6: the in-car look input, the test-drive start pose (road projection
  not loaded), the passenger config field (+0xe0), the brake texts (FUN_004d89b0 [ENC]) are [INF]; step 8 "Tuto FROG"
  is a stub.
- Step 4 reward: the RESULTS screen is not shown; the 1 s money counter is run at once [INF] (4.7).
