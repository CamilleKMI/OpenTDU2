# TDU2 new-game flow: "Talk to Tess" walk mode, Tess cinematic, garage door, garage scene with the Ferrari California

Tags: [CODE] read from readable x86 (VA >= 0x6c1000) · [DATA] read from game files · [INF] inferred (reason given) ·
[ENC] the logic is in SecuROM-encrypted code (< 0x6c1000) or not present in readable code/data; not read.

Decompiled code: `/tmp/claude-0/tess_garage_dec1.c` … `tess_garage_dec7.c` (Ghidra, private project copy in
`/tmp/claude-0/tg/gh`). Working dir with every extracted bank and dump: `/tmp/claude-0/tg/`:
- `garage/`, `garage__us/`, `meeting_tess/`, `meeting_tess__us/`, `fade_freeride/`, `fade_freeride__us/`,
  `pack_firstfreeride*/`, `hud/`, `avd/` (scenarics/avatars/avatarsdriver.bnk), `lba/` (lod_bod_anim.bnk): extracted banks.
- `*_ent.txt` (annotated xmbdump of entities.xmb, every `rsc` resolved to a file name through hashcodes.xmb),
  `*_c.txt` (same, compacted: vectors on one line). `ni_*` = new_intro.bnk.
- Scripts: `annot.py` (bank + hashcodes + entities → annotated dump), `compact.py`, `cinsum.py` (Entity__Cinematic
  summary), `cin2.py` (.cin timeline: shots, messages, Fx; port of OpenTDU2 `Cinematic::parse`), `scerefs.py` (.sce REFS
  and object list), `asm.py` (slice of `/tmp/claude-0/text.asm`).
- Flash dumps: `intro_vmf2.txt`, `int_8.txt` (INTRO.vmf `cursor` sprite), `gen_628.txt` (GENERAL.vmf `pop_goal`).

Banks copied from the Mac into `/mnt/user-data/uploads/TestDrive2/Euro/Bnk/`: `scenarics/cinematics/intro/{garage,
garage__us,meeting_tess,meeting_tess__us,fade_freeride__us}.bnk`, `scenarics/packs/pack_firstfreeride{,__us}.bnk`,
`scenarics/events_config.cpr`, `scenarics/avatars/{avatarsdriver,lod_bod_anim}.bnk`, `frontend/hires/hud.bnk`.

---------------------------------------------------------------------------------------------------
## 0. Order of events (summary)

| # | what the player sees | engine | data | tag |
|---|---|---|---|---|
| 1 | Avatar validated in the party house | AVATAR_CHOICE (mode 0x20), story step 0 | new_intro.bnk | [CODE] |
| 2 | Short cinematic: Tess calls the avatar (`C_FIESTA_INTRO_<avatar>`, 6.37 s) while `C_WALKMODE` (party life, loop) starts | FUN_00da8d90 | §2.3 | [CODE] |
| 3 | Walk mode starts: camera from `cam_entree_%02d`, INTRO.vmf goto `wm_home` (reticle opens, Input=true) | FUN_00da8040 event 7 of C_FIESTA_INTRO_xx | §2.4 | [CODE] |
| 4 | Objective box "Talk to Tess" (loc 5033, DBRewardXP TUTO_TALK_TO_TESS) | not in readable code | §5.3 | [ENC]/[DATA] |
| 5 | The player walks to Tess. When the point at 0x01195b80 enters the box volume `Tess` → `C_TESS` (9.22 s, voices Intro_Fiesta_01 + _02), reticle hidden | FUN_00da87f0 (per frame) | §2.5 | [CODE] |
| 6 | End of C_TESS: reticle visible, objective box "Go to the garage" (loc 5108), goal TUTO_GO_TO_THE_GARAGE | FUN_00da8040 event 7 of C_TESS | §2.6 | [CODE] |
| 7 | Reticle on the garage door (`Garage` FpSelectable) → low bar "Garage" with the `garage` icon; "A" → `ItemType('valid')` → FSCommands `camn`,`wmof`,`gain` | walk-mode manager + INTRO.vmf | §4 | [CODE]+[DATA] |
| 8 | `gain` → story step 1 → mode FIRST_WM (0x23), loads garage.bnk, gives car 387 (Ferrari California) | FUN_00da91db, FUN_00b10da0 case 2 | §1, §3 | [CODE] |
| 9 | Garage cinematic `C_INTRO` (29.4 s): avatar and Tess walk into the garage, voices FiestaGarage_01/02 | FUN_009aa2c0 | §3.3 | [CODE] |
| 10 | Walk mode in the garage: `C_LIFE_TESS` (Tess idle loop), camera `cam_entree`, INTRO.vmf `wm_garage`, goal "Get into the car" (5032) | FUN_009a9d10 event 9, FUN_00b10da0 case 2 0xc0c1 | §3.4 | [CODE] |
| 11 | Tess comments: `C_VOICE_03` (idle), `C_VOICE_04`/`_05` (when a selectable is targeted) | FUN_009a9990 | §3.5 | [CODE] |
| 12 | Reticle on the car (`FST_EnterCar`, "car") + button → `C_ENTER_CAR` (3.99 s) → player seated → `C_VOICE_06` | FUN_009aa560, FUN_009a9d10 | §3.6 | [CODE] |
| 13 | Throttle > 0.7 → 3 s → `C_OUT` (4.44 s, rev needle), then `C_OUTRO` (3.57 s, the car drives out) | FUN_009aa560, FUN_009a9d10 | §3.7 | [CODE] |
| 14 | End of C_OUTRO → story step 2 "First Freeride" (fade_freeride.bnk, FREERIDE, C_FIRST_OUTRO at the house exterior) | FUN_009a9d10, FUN_00b10da0 case 3 | §1 | [CODE] |

---------------------------------------------------------------------------------------------------
## 1. Game modes and the story-step state machine

### 1.1 Game modes [CODE] (name table 0x00f7eed8; current mode at `*(DAT_00fd5080+0x128)`, previous at `+0x12c`)

| id | name | bank loaded by FUN_00a91360 | flash movies (GamePC.cpr `AddFlashMovie`) |
|---|---|---|---|
| 0x20 (32) | AVATAR_CHOICE | `Scenarics/Cinematics/intro/new_intro.bnk` | GENERAL, MAIN (all modes) + `INTRO` |
| 0x23 (35) | FIRST_WM | `Scenarics/Cinematics/intro/garage.bnk` | GENERAL, MAIN + `INTRO` |
| 0x01 | FREERIDE | world | HUD, GAUGES, DASHBRD |

GamePC.cpr [DATA, `/tmp/claude-0/bm/GamePC.dec`]: `TUTORIAL = ON`, `SCENARIOTUTORIAL = ON`, `ALLOWSKIPCINE = 0`,
`LockWalkmode TRUE`.

### 1.2 Story steps (FUN_00b10da0; object at `DAT_01025608+0x9010`, its vfunc +8 = "go to step n") [CODE]

Step names (switch at 0x00b10330, table 0x00b10398): 0 "Avatar choice / First WalkMode", 1 "First Garage",
2 "First Freeride", 3 "Meeting Tess", 4 "Driving Tess", 5 "Solar Crown exterior", 6 "Solar Crown interior",
7 "Used Car Dealer", 8 "Tuto FROG", 9 "Go Home", 10 "Home sweet home : Caravan", 11 "Done", 12 "Hawai First Freeride",
13 "Igloo First Intro".

FUN_00b10da0(this, step, dt, event): `switch(step+1)`. Events: 0xc0c0 = enter step, 0xc0c2 = leave step, 0xc0c1 =
per-tick/ready (it receives dt; in step 2 it decrements a timer with it) [CODE; "per tick" INF from the dt use].

| step | event | actions [CODE] |
|---|---|---|
| 0 | 0xc0c0 | `profile+0x58 = 0`; FUN_00acee30(0); set mode 0x20 (AVATAR_CHOICE) |
| 1 | 0xc0c0 | `profile+0x58 = 1`; `(*DAT_010017ec+0x24)(0x183,0,0,0)` + FUN_00ab9be0 → **car id 0x183 = 387 = Ferrari California** (DB `_id_car = 387`, `_car_file_name = "California"`, db.txt line 50510) becomes the current car (slot 0x280) [CODE+DATA; "current car" INF from FUN_00ab9be0 writing profile+0x54 = 0x280 after saving the old value in +0x56]; FUN_00aba5d0 / FUN_00aba310 with house id 0x49447c83_957885a4 (= `EPH_L3DI_5067I`, see intro_spec §12.1); FUN_00d61700 (closes the `pop_goal` box); set mode 0x23 (FIRST_WM) |
| 1 | 0xc0c1 | goal `FUN_00b09f50("TUTO_ENTER_IN_THE _CAR")` → FUN_00b0a090 (DBRewardXP 185, goal text 0x13a8 = 5032 "Get into the car") |
| 1 | 0xc0c2 | FUN_00aba5d0(house id) |
| 2 | 0xc0c0 | `profile+0x58 = 2`; timer = 90.0 (DAT_00ede78c); load `Scenarics\Cinematics\intro\fade_freeride`; FUN_00aba280(0x183…) (restores the previous current car, INF); GPS target cleared; `C_FIRST_OUTRO` at the house spot (`Level/%s/spots`); mode 1 (FREERIDE) |
| 2 | 0xc0c1 | at FREERIDE: `/:IsWalkmode` = false, pop_goal 0x13ae "Enjoy!", goal TUTO_ENJOY; later plays `C_FADE_FREERIDE` when the timer < 0 |

AVATAR_CHOICE command handler (FUN_00da91c0, fragment FUN_00da91db): FSCommand fourcc **`gain` (0x6761696e)** →
`(*(DAT_01025608+0x9010))->vfunc8(1)` → story step 1. FIRST_WM listener: end of `C_OUTRO` → `vfunc8(2)` → step 2.
[CODE]

---------------------------------------------------------------------------------------------------
## 2. Step 0: the party house walk mode (AVATAR_CHOICE, new_intro.bnk)

### 2.1 Coordinates

All new_intro positions are in house-local coordinates (scene origin = identity). World transform (intro_spec §12.1):
`X = Tx + x·cosθ + z·sinθ; Y = Ty + y; Z = Tz − x·sinθ + z·cosθ`, θ = 2.1934, T = (36382.324, 5.920, 5729.771)
[DATA+INF, as in intro_spec]. The house floor is at local y ≈ 31 (world ≈ 37).

### 2.2 Entities used [DATA, `/tmp/claude-0/tg/ni_c.txt`]

| entity | class | local position | data |
|---|---|---|---|
| `Tess_Wintory` | Entity__PNJ (`CPNJ`) | editor (−3.02, 64.79, −2.58), not used; placed by the cinematics | ConfigName `STO_Tess_Wintory`; CinAnimTracks `co_stand-w.anm`, `fi_hello_tess.anm`, `fi_intro_fiesta_01_npc.anm` |
| `Tess` | Entity__Volume (`VELU` 0x56454c55), OfType 2 (box) | center (9.249, 32.684, 6.705) | volume_mat (world→unit box): rows (0.30309,0,0,0) (0,0.212139,0,0) (0,0,0.235352,0) (−2.80329,−6.93345,−1.57805,1) → box 3.30 × 4.71 × 4.25 m, x ∈ [7.60,10.90], y ∈ [30.33,35.04], z ∈ [4.58,8.83]; world center (36382.38, 38.60, 5718.35) |
| `Garage` | Entity__Volume__FpSelectable (`ZSPF` 0x4650535a) | center (20.239, 33.110, 3.206), world (36373.13, 39.03, 5711.46) | OfType 2, **Type 5**, SelectableString "", Camera/Cinematic/CameraFPEntryPoint 0; volume_mat rows (−0.023816,0,−6.18051,0) (0,0.455356,0,0) (0.911972,0,−0.161405,0) (−2.4414,−15.077,125.607,1) → a door-sized box 1.10 (along z) × 2.20 (y) × 0.16 m |
| `lookAt_Tess` | Entity | (7.580, 32.779, 6.311) | look-at target animated in M_TESS |
| `cam_entree_01..06` | Entity__Camera, Fov 1.0472 rad, Ratio 1.7778 | 01 (−0.318, 32.591, 6.979) rot (0,−0.700876,0,0.713283); 02 (0.927, 32.591, 5.645) rot (0,−0.502476,0,0.864591); 03 (2.315, 32.591, 4.067) rot (0,−0.567851,0,0.823131); 04 (2.374, 32.591, 1.904) rot (0.001959,−0.621406,−0.001554,0.783485); 05 (2.333, 32.591, −0.500) rot (−0.004229,−0.533471,0.002667,0.845804); 06 (2.079, 32.591, −1.826) rot (0.002412,−0.262854,−0.000657,0.964832) | walk-mode start cameras |
| `S_WALKMODE_Music`, `_01/_02/_03` | Entity__Fx, AutoStart 1 | (2.00,32.27,−11.14), (7.19,32.27,−7.69), (−7.60,31.83,5.83), (−0.78,32.42,8.39) | party music `s_walkmode_music_AMBIENCE_INTERNAL_SFX.wav` (intro_spec §5) |
| `Spot_Detection`, `Evt_Box_` | Entity__Volume | — | not referenced by the code paths read here |

### 2.3 After the avatar is validated (FUN_00da8d90) [CODE, partly garbled decompile; names from asm string refs]

1. Stop `C_AVATAR` (ambient of the choice), start `C_WALKMODE` as the ambient cinematic: `CinProps |= 0x10`,
   loop byte +0xb5b = 1, listener = mode, kept in `mode[0x6e09e]` (offset 0x1b8278).
2. Apply the chosen avatar (FUN_00cbd680 … FUN_00b8baf0; the avatar code paths are in avatar_spec.md).
3. `sprintf("C_FIESTA_INTRO_%02d", avatarIndex+1)` (format at 0x00f29d18) and play it as the current cinematic
   `mode[0x6e09d]` (offset 0x1b8274).

`C_WALKMODE` [DATA]: Entity CinProps 114 (0x72: loop, no-camera/no-takeover, bits 5, 6), CinEnd 3; cin
`intro_lph_l3di_M_WALKMODE_TOTEM.cin` (240 s, one shot on `NO_CAMERA`), sce `M_WALKMODE_walkmode.sce` (30 objects:
every party PNJ in a static pose; **Tess_Wintory at (10.66, 31.41, 6.841), rot (0, 0.8458, 0, 0.5335)**, world
(36381.66, 37.33, 5717.12); her anim `co_stand-w`, FaceFX `Intro_:Intro_Tess_stand`, message `lkon` (look-at on)).

`C_FIESTA_INTRO_01..06` [DATA] (one per avatar slot, CinProps 0, CinEnd 3, 6.37 s): shot 0 (t 0–3.53, scene 1
`M_FIESTA_INTRO_0x_fiesta_intro_0x.sce`: camera `c_fiesta_intro`, `Avatar` with `fi_selectavatar-m/w`, FaceFX
`TheDream_Fiesta_intro`; fade type 1, 1.5 s), shot 1 (t 3.53–6.37, scene 0 `M_FIESTA_INTRO_0x_fiesta_intro.sce`:
camera `c_fiesta_02`, `Tess_Wintory` with `fi_hello_tess`, FaceFX `Intro_Fiesta_Tess_call_avatar`). Messages:
`wmod:Avatar_Avatar` @0, `zcvn` @0, `flsh:generik4` @0.51, `snd:music` @1.0.

### 2.4 End of C_FIESTA_INTRO_xx (FUN_00da8040, listener event 7 = reached end) [CODE]

```
find PNJ "Tess_Wintory" in the PNJ list (DAT_00fd9004) -> mode[0x6e09f]      (offset 0x1b827c)
mode->vfunc+0x50()
for each camera entity ('_CAM' 0x5f43414d) named sprintf("cam_entree_%02d", mode[0x6e0a1]+1):
    FUN_00950060(cam+0x10, tmp); FUN_009517c0(&mode[0x6b724] /*walk-mode manager*/, tmp)   // walk camera start
if movie "INTRO" exists: INTRO->gotoLabel("wm_home")     // intro.vmf root frame 17: cursor.cursor_anim 'open', _root.Input = true
DAT_0117a038 |= 0x100; FUN_00afbba0(0); FUN_00afa050()   // camera system (0x0117a0xx block) hand-over
```
Which value `mode[0x6e0a1]` holds was not traced (INF: the chosen avatar slot, since there are 6 cameras and 6 slots;
the avatar index proper is `mode[0x6e0a4]`, used for `/:CasinoAvatar`).

AVATAR_CHOICE init (FUN_00da8770, which first calls the base house-walk init FUN_009be3e0): **`/:IsWalkmode` = false** in
this mode (FUN_009be3e0 sets it true, the AVATAR_CHOICE and FIRST_WM inits reset it to false) [CODE]. The start method
FUN_00da92e0 finds the volume named `Tess` (`VELU`, string 0x00f29b7c) and stores it in `mode[0x6e0a2]` (offset 0x1b8288).

### 2.5 Trigger of C_TESS (FUN_00da87f0, per frame; `this` = mode+0x90) [CODE, asm 0xda8882–0xda8985]

```
if (!talked[+0x1b8280] && tessPnj[+0x1b827c] && tessVolume[+0x1b8288]
    && (*(DAT_00fd51c0+0x420))->+0x2e8 == 9                         // camera/controller type 9 (INF: walk-mode camera)
    && !(DAT_00fd960c && DAT_00fd960c->+0x345)
    && PointInVolume(tessVolume, point = DAT_01195b80..8c, 0, 0))    // FUN_00b40e50
{
    PlayCinematic("C_TESS", stopPrevious=1)      // FUN_00da7fd0
    talked = 1
    INTRO->getClip("cursor")->setVisible(0)       // vfunc +0xf0 / +0x20
    currentCine->CinProps |= 4                    // skippable
}
```
- The point is the vec4 at 0x01195b80; the walk-mode manager uses the same vector as the origin of its 50 m reticle
  ray (FUN_00952410) → [INF] camera eye position. So Tess triggers when the **camera** enters the 3.3 × 4.7 × 4.25 m box
  around her; no button is needed.
- FUN_00b40e50 (point in volume) by volume type (+0x160): 0 = half-space/plane test (complex), 1 = sphere
  `|p|² ≤ 1`, **2 = box `−0.5 ≤ p.xyz ≤ 0.5`**, 3 = cylinder (`−0.5 ≤ y ≤ 0.5`, `x²+z² ≤ 1`), 4/5 = cone; p is the
  point multiplied by volume_mat. [CODE]

`C_TESS` [DATA]: CinProps 0, CinEnd 3; cin `intro_lph_l3di_M_TESS_TOTEM.cin` 9.22 s, message `zcne` @0 (3.98 s);
shots (camera hash, sce time range): 0.00–4.44 `c_tess_01` (0.87–5.31), 4.44–5.69 `c_tess_02` (5.32–6.57),
5.69 `c_tess_01` (0.01 s), 5.70–9.22 `c_tess_01` (6.58–10.10). Scene `M_TESS_Tess.sce` 10.1 s:
- `c_tess_01` Fov 28°, start (8.566, 32.85, 6.487); `c_tess_02` Fov 30°, (15.15, 33.39, 2.995).
- **Tess_Wintory at (10.43, 31.41, 6.727), rot (0.000187, 0.7659, 0.000157, 0.6429)**, anim `fi_intro_fiesta_01_npc`.
- Fx track (id 0x7846) on Tess: ref 1 = FX 0x3f4970d009848234 = Fx__Root **`Intro_Fiesta_01`** at sce t 1.50
  (dur 3.37); ref 2 = FX 0xd5ce386a3fe932d5 = **`Intro_Fiesta_02`** at sce t 4.87 (dur 5.3).
- FaceFX (track 0x70726fb5) refs `female.fxa::Intro_:Intro_Fiesta_Tess_debut`, `Intro_Fiesta_01`, `Intro_Fiesta_02`.
- `lookAt_Tess` animated (12 keys).
- The player avatar is not in this scene: it stays where it is [DATA: object list].

### 2.6 End of C_TESS (FUN_00da8040 event 7, name == "C_TESS") [CODE]

```
FUN_0094fdb0()                                   // walk-mode manager re-enable (INF)
INTRO->getClip("cursor")->setVisible(1)
DAT_0117a038 |= 0x100; FUN_00afbba0(0)
ShowGoal(Loc(0x13f4))                            // FUN_00d61790: "/:GoalTxt" = "Go to the garage"; GENERAL pop_goal goto "open"
SetGoal(FUN_00b09f50("TUTO_GO_TO_THE_GARAGE"))   // DBRewardXP 183, _goaltext_hc 0x760 (also "Go to the garage")
```
FUN_00d61790 only opens the box when `DAT_00f803ae == 0`, then sets `DAT_00f803ae = 1` and a 30.0 s timer
(DAT_00ede1fc → DAT_00f803b4) [CODE]; FUN_00d61700 closes it (`pop_goal.close_popup()`).

### 2.7 Garage door interaction → step 1

1. Walk-mode manager picks the `Garage` FpSelectable (§4.1) → flash `/:SelItemType` = its string; INTRO movie
   `ItemType('over')` → `cursor.disp_low_bar('over')`, `_root.t_wm_destination = LowBarText()` = `_root.m_Garage_`
   = loc 3300 **"Garage"**, icon `low_bar.ic.icLoaded.gotoAndStop(_root.SelItemType)` (label `garage`, frame 35 of
   sprite 2345). [DATA]
2. Button A (cursor function `A()`): if `_root.Input`: `Input=false`, FSCommand `wmof`, `sndp:valid`,
   `ItemAction('valid')` → `cursor_anim 'close'`, `ItemType('valid')`: for `garage`: low bar 'out', cursor 'close',
   `Input=false`, FSCommands **`camn`, `wmof`, `gain`**. [DATA, intro.vmf root frame 0 action at 0x03e3]
3. `gain` → step 1 (§1.2).

Mapping FpSelectable Type 5 → string `garage`: table 0x00edd580 read by the wrapper vf0c FUN_00953350 (§4.1)
[CODE+DATA].

---------------------------------------------------------------------------------------------------
## 3. Step 1: the garage (FIRST_WM, garage.bnk)

### 3.1 Bank content [DATA] (`Euro/Bnk/Scenarics/Cinematics/intro/garage.bnk`, 12.3 MB, 187 files; `garage__us.bnk` voices)

- Meshes: `gph_l3d_8D40E0C2EA60E82D` (Actor `Intro`: garage room, lightmaps `lm_gph_l3d(_night).2db`, birthday
  banners), `gph_l3d_59E1C536C6C6FAD9` (Actor__Mesh `D_REFLECT`, `AvatarReflectlayername1 = "reflect_ground"`,
  renderAll 1), `garage_int_96068B7A5A9510D2` (Actor `AAA`: California cockpit, textures `california_int2.2db`,
  `ts_leather3.2db`, `cubeint.2db`), `garage_int_C31591700F0D216E` (Actor `ND_RPM`: rev-counter needle),
  `garage_int_349E1852FA421941` (Scenery `D_scene`, 100 models).
- Collision: `garage_int_57E5C829CCD9F2C6.shk` (64 KB) and `.dhk` (37 KB): Eden header (`02 02`, u16 count, 0x20-byte
  entries {u64 hash, u32 index, u32 type}) followed by a **Havok-5.5.0-r1 packfile** (magic 57 e0 e0 57 at 0x5a0 /
  0x970). `.shk` classes: hkpRigidBody, hkpMoppBvTreeShape, **hkpStorageExtendedMeshShape** (static triangle mesh),
  hkpBoxShape. `.dhk`: hkpListShape, hkpConvexTransformShape, hkpBoxShape, **hkpLimitedHingeConstraintData**
  (dynamic hinged parts). new_intro has the house collision `intro_lph_l3di_57E5C829CCD9F2C6.shk` (116 KB, adds
  hkpCapsuleShape). Entry hashes do not match entity `__Hc` values [DATA]. Field-level layout not decoded.
- Sounds: `c_intro_garage_CINEMATIC.wav` (13.65 s), `c_enter_car_CINEMATIC.wav` (3.97 s), `c_outro_CINEMATIC.wav`
  (11.50 s), `s_walkmode_garage_CINEMATIC_MUSIC.wav` (14.77 s), MS-ADPCM stereo 22050 Hz; 13 `.bfx`.
- Anims: `co_walk8pas-m/w`, `co_walk6passtopdroite-m/w`, `co_walk6passtopgauche-m/w`, `co_stand-m/w`, `co_bye2-m/w`,
  `fi_intro_fiesta_garage01..06_npc` (.anm, plus .bas/.trk for 03–06); FaceFX `intro-w.fxe`, `common-m/w.fxe`.
- `garage__us.bnk`: `intro_fiestagarage_01..06_VOICE_STORYLINE.wav` (3.17, 5.01, 1.80, 3.57, 3.68, 3.63 s) and
  `intro_fiesta_01/02_VOICE_STORYLINE.wav` (2.85, 4.21 s), mono MS-ADPCM 22050 Hz.

### 3.2 Entities [DATA, `/tmp/claude-0/tg/garage_c.txt`] (garage-local coordinates; floor y ≈ 0)

| entity | class | position / rotation | data |
|---|---|---|---|
| `cam_walkmode` | Camera | (−0.225, 1.972, 24.966) rot (0.025161,−0.953903,0.083589,−0.287141) | Fov 0.5236 (30°) |
| `cam_EnterCar` | Camera | (−6.163, 1.380, 17.325) rot (−0.042775,−0.941031,0.130115,0.309357) | Fov 30° |
| `cam_entree` | Camera | (−7.726, 1.211, 16.323) rot (−0.050651,−0.785507,0.064867,0.613356) | Fov 0.8727 (50°); walk camera after C_INTRO |
| `FirstPerson` | Entity__FirstPerson | — | StartCamera = cam_walkmode (0x08f81d56a05612e6) |
| `D_GarageData` | Entity__ShopData | (3.026, 0, 22.169) | MainCamera = cam_walkmode; PosCams[0] = D_CAR |
| `D_CAR` | Entity__PosCam | **(−4.385, 0.084, 15.553) rot (0,−0.957815,0,−0.287386)** | Room 1; the parked car position [INF: PosCam of the garage data, same transform as FST_EnterCar/Evt_Sphere_01] |
| `CIN_CAR` | Entity__CinCar | editor (−3.53, −8.49, 35.59) | id_car 0, rim 0, paint 0/0, interior 0 → car filled by code (INF: current car = 387); animated in M_OUTRO |
| `FST_EnterCar` | FpSelectable | center (−4.729, 0.577, 16.096); entity rot as D_CAR | OfType 2, **Type 57 (0x39)**, SelectableString **"car"**; box 1.60 × 1.22 × 1.19 m on the driver side |
| `Evt_Sphere_01` | Volume, OfType 1 (sphere) | (−4.385, 0.084, 15.553), radius 4.24 m | not referenced by readable code |
| `Tess_Wintory` | PNJ | editor (0.594, 31.44, 24.04) | `STO_Tess_Wintory`; CinAnimTracks: co_bye2-w, co_stand-w, co_walk6passtopdroite-w, co_walk8pas-w, fi_intro_fiesta_garage01..06_npc |
| `E_TESS` | Entity__AnimationHandler | — | Animation[0..3] = fi_intro_fiesta_garage03..06_npc .bas + .trk |
| `LookAt_Tess` | Entity | (0.857, 0, 20.036) | |
| `RT_Avatar_01..04` | Light, isInterior 1 | e.g. 01 (−22.09, 31.95, 21.52) | avatar rim lights, Intensity 0.6, range 65 |
| `C_OUTRO` | Entity__Fx | — | Fx 0x40a58c5b68a6f9de, AutoStart 0 |

### 3.3 Cinematics (all CinEnd 3, Priority 1.5, shadow CINE_DEFAULT; Entity transform (22.1468, 0, −28.5251) rot (0,−1,0,0) = editor totem only) [DATA]

| name | CinProps | .cin / .sce | duration | content |
|---|---|---|---|---|
| `C_INTRO` | 0 (CinEnd **2**: no SetTime on stop; only 1 → t=0 and 3 → t=end are handled, intro_spec §7.5) | `garage_int_M_INTRO_Totem_Garage.cin` / `M_INTRO_intro.sce` (34.34 s) | 29.40 | 8 shots `cam_intro_01..08`; Avatar walks (−18.81,0.23,7.76)→(−7.68,−0.02,16.38) with co_walk8pas/co_walk6passtopgauche/co_stand; Tess (−19.75,0.23,8.98)→(−6.879,−0.018,17.78) with co_walk8pas-w, co_walk6passtopdroite-w, fi_intro_fiesta_garage01/02_npc; voice Fx `Intro_FiestaGarage_01` (FX 0xa26c25f82c65d2d4) at sce 8.93 (3.44 s) and `Intro_FiestaGarage_02` (0xda761c4b616d1162) at sce 13.24 (5.4 s); FaceFX `Intro_FiestaGarage_01_NPC_debut/_01/_02`, Avatar `COMM_lookaround`; cin Fx `FxC_INTRO_GARAGE` (0x3aa8b865d588fff4) at 9.08 for 13.65 s |
| `C_LIFE_SOUND` | 274 (0x112) | `garage_int_M_LIFE_SOUND_Totem_Garage.cin` (no scene) | 0 | Fx `FxS_WALKMODE_Garage` (0x8f688df08dbbf7c5) = garage music |
| `C_LIFE_TESS` | 18 (0x12) | `…M_LIFE_TESS…cin` / `M_LIFE_TESS_life.sce` | 3.40 | **Tess idle at (−6.696, −0.018, 18.09) rot (0,−0.9492,0,0.3148)**, co_stand-w, `lkon` |
| `C_VOICE_03` | 256 | `…M_VOICE_03…` / `M_VOICE_03_voice03.sce` | 2.00 | Tess same pose; Fx `Intro_FiestaGarage_03` (0x582755a2ee5adcd8) at 0; FaceFX Intro_FiestaGarage_03 |
| `C_VOICE_04` | 256 | `M_VOICE_04_voice04.sce` | 3.60 | Fx `Intro_FiestaGarage_04` (0xe389a63420518cca) |
| `C_VOICE_05` | 256 | `M_VOICE_05_voice05.sce` | 3.80 | Fx `Intro_FiestaGarage_05` (0x6b0842355de052af) |
| `C_VOICE_06` | 256 | `M_VOICE_06_voice06.sce` | 3.80 | Fx `Intro_FiestaGarage_06` (0xf94c4f47aa0922ca) |
| `C_ENTER_CAR` | 1 | `…M_ENTER_CAR…` / `M_ENTER_CAR_entercar.sce` | 3.99 | camera `c_enter` (Fov 42→60) to `c_enter_Target`; cin Fx `FxC_ENTER_CAR` (0xd1d03c11158541bb) = `c_enter_car_CINEMATIC.wav` |
| `C_OUT` | 256 | `…M_OUT…` / `M_OUT_out.sce` (8.64 s) | 4.44 | shots c_out_06, c_out_05, c_out_04 (cockpit); `AAA` at (−4.866,0.878,15.48) rot (0,−0.9578,0,−0.2874); `ND_RPM` rotation animated (rev); Tess at (−6.696,−0.018,18.09); `OUT` marker (−4.388, 0.084, 18.14) |
| `C_OUTRO` | 256 | `…M_OUTRO…` / `M_OUTRO_outro.sce` | 3.57 | `CIN_CAR` drives (0.417,0.136,15.55)→(0.260,0.136,7.68), rot (0,−1,0,−0.0064); Tess waves `co_bye2-w`, (−5.31,−0.02,15.78)→(−4.83,−0.02,14.44); message `avm4:CIN_CAR` |

CinProps bit 8 (0x100) on C_VOICE/C_OUT/C_OUTRO = "save/override DAT_01179620 = 0x1f" (intro_spec §7.2). Messages
`weat:Garage*`, `wmod:Garage_0x`, `shad:GP/PM/PA`, `zcde/zcvn/zcne`: presets exist in
`islands/ibiza/fx/cinematic_weathers.xmb` ("Garage", "Garage_Out", "Garage_Outro") and `cinematic_modifiers.xmb`
("Garage_02" …) [DATA]; their handling is [ENC] (intro_spec §4).

Garage C_INTRO timeline (cin time; scene time in brackets) [DATA, `cin2.py`]:
```
0.00 cam_intro_01 [0.00-2.50] fade 1/0.5   weat:Garage  shad:PM  zcde
2.50 cam_intro_02 [5.45-6.88]              wmod:Garage_02 zcvn
3.93 cam_intro_03 [7.81-12.36]             wmod:Garage_03 shad:PA zcne
8.48 cam_intro_04 [21.21-24.96] fade 1/0.7 wmod:Garage_04 shad:GP zcvn
9.08 Fx FxC_INTRO_GARAGE (13.65 s)
12.23 cam_intro_05 [24.50-28.25]           wmod:Garage_05
15.98 cam_intro_08 [27.51-31.26] fade 0.6  wmod:Garage_06
19.73 cam_intro_06 [30.60-34.35]           wmod:Garage_07
23.48 cam_intro_07 [12.96-18.88]           wmod:Garage_08 shad:PA zcne
29.40 end
```
(Camera hashes 02112e3b…09112e3b ↔ cam_intro_01…08 by the sce nameHash.) Actor tracks follow the shot's scene time
(the cin remaps scene time per shot; same rule as OpenTDU2 `Shot::getSceneTime`) [DATA].

### 3.4 FIRST_WM mode code [CODE]

- Start (FUN_009aa2c0): base init; walk manager; `C_LIFE_SOUND` with `CinProps |= 0x10` started (ambient);
  `PlayCine(&"C_INTRO")` (FUN_009a9a40) and marks it skippable (`|= 4`).
- Init (FUN_009a9b30): `/:IsWalkmode` = false; camera #4 (FUN_00ca0840(4)) near = 0.01 (DAT_00ede410),
  far = DAT_00ffb668 (40000 default).
- Listener (FUN_009a9d10), event ids: 3 = ended, 8 = MSG, **9 = cinematic released by the cinematic queue
  (FUN_0095f2xx, after it ended)**:
  - event 3 for C_VOICE_03..06 (table 0x00f76a78 = {C_VOICE_03, _04, _05, _06}): `voiceDone[i] = 1`
    (bytes at mode+0x1b8e59+i), `voicePlaying(mode+0x1b8e58) = 0`.
  - event 8 `flsh:<label>` → INTRO movie gotoLabel(label).
  - event 9 `C_INTRO`: start `C_LIFE_TESS` (CinProps |= 0x1010, loop); mode->vfunc+0x50; every camera named
    `cam_entree` → FUN_009522c0(walk manager) (walk camera); INTRO gotoLabel **`wm_garage`** (cursor opens,
    Input = true); `current = 0`.
  - event 9 `C_ENTER_CAR`: FUN_008c1880(enter car, seat 0, player) (the "enter_car" sequence: puts the avatar in the
    car, `/:CanEnterCar`); `current = 0`; **`enteredCar(mode+0x1b8e45) = 1`**.
  - event 9 `C_OUT`: `DAT_01179620 = mode[0x6e399]`; play `C_OUTRO`.
  - event 9 `C_OUTRO`: story step 2.
- PlayCine (FUN_009a9a40): find `CINE` by name; release the current one (FUN_00b3fc40(1), FUN_0095f2f0); for `C_OUT`
  clears +0xb54; listener = mode; FUN_00b402a0(0) (start); `current(mode[0x6e390], offset 0x1b8e40) = cine`.

### 3.5 Tess voice lines in walk mode (FUN_009a9990 → FUN_009a9920) [CODE, asm 0x9a9920–0x9a9a33]

Called each frame from FUN_009aa560 when `modeReady(+0x1b4cd0)`, `current == 0` and the walk manager is active
(`DAT_00fd95e0+0xd1c`).
```
if enteredCar:                                   idx = 3   (C_VOICE_06)
else if walkMgr.selected (+0xd20) is valid and walkMgr.items[sel] (+0xb80) != 0:
     idx = playerVehicleState ? 2 : 1            (C_VOICE_05 : C_VOICE_04)
     // playerVehicleState = player->vfunc5c()->+0xd0 != 0 && FUN_00c3f360() && *(+0xa794) != 0.0f
else                                             idx = 0   (C_VOICE_03)
if !voiceDone[idx] && !voicePlaying:
     start cine C_VOICE_0(3+idx): CinProps |= 0x10, no loop, listener = mode; voicePlaying = 1
```
So C_VOICE_03 plays once right after the walk mode starts, C_VOICE_04 the first time the reticle is on a selectable
(the car), C_VOICE_06 after entering the car; C_VOICE_05 needs the vehicle condition above (meaning of +0xa794
not identified).

### 3.6 Entering the car (FUN_009aa560, per frame, `this` = mode+0x90) [CODE]

```
if !DAT_011fe90e && walkMgr.selected != -1 && selected->vfunc0c() == "car"       // FST_EnterCar SelectableString
   && carReady  (player->vfunc5c()->+0xd0 != 0 && FUN_00c3f360() && *(+0xa794) == -1.0f)
   && !enteredCar && (current == 0 || current->name != "C_ENTER_CAR")
   && (FUN_00d5e9f0(0x38) || FUN_00d5e4d0(3)):                                     // input action 0x38 (INF: validate/A) or 3
      INTRO.cursor.setVisible(0); DAT_00fd95e8++; DAT_010268b6 = 1; walkMgr+0xd1d = 1 (selection frozen)
      FUN_00c81cc0(1); DAT_00f80168+0x918 |= 0x20; PlayCine("C_ENTER_CAR")
```
On the walk-mode HUD, the hint for this is loc 5036 "Position the reticle over the car, and press
#command:ACCESSORIES_LEFT# to open the door." and the low bar text `m_EnterCar_` = loc 5032 "Get into the car" [DATA;
which code shows 5034–5037 is not in readable code].

### 3.7 Trying the car and leaving (FUN_009aa560) [CODE]

```
if timer(+0x1b8e48) == 0 && enteredCar && car(+0x1b81d0) && !DAT_011fe90e
   && car->+0x79bc->+0x380 > 0.7:                // INF: throttle input (loc 5035 "Start the car with #command:CAR_ACCEL#")
      timer = 3.0; save DAT_01179620; DAT_01179620 = 0x1e; FUN_009bddf0()
if timer > 0: timer -= dt
if timer < 0:  exit the car (FUN_008c11f0(player,1), restore flags/HUD), FUN_00d80340(1,0,0,1),
               PlayCine("C_OUT"), timer = 0
```
Then C_OUT → C_OUTRO → step 2 (§3.4).

---------------------------------------------------------------------------------------------------
## 4. Walk mode: controller, camera, selection, collisions

### 4.1 Walk-mode manager (global `DAT_00fd95e0`; the mode's instance at `mode+0x6b724*4`) [CODE]

- Registration (FUN_0094f1c0): every `ZSPF` FpSelectable of the scene, by its **Type (entity +0x170)**: Type 1 →
  table +0x970, Type 2 → 0x40-byte records +0x980 (FUN_009532c0, vtable 0x00f2db78), other → table +0x7e0 (8-byte
  {vtable, entity} wrappers, array constructor 0x4f4040 [ENC]); list of pointers at +0xb80, count +0xcc. In game
  mode 0x1d only the entities with +0x9c bits 0 and 1 set are registered. (+0x160 is OfType.) [CODE]
- Wrapper vtables (readable, .rdata): 0x00f2db30 / 0x00f2db78 (FpSelectable: vf04 FUN_009533b0, vf08 FUN_009533a0 =
  entity +0x40 = the placement position / FUN_00953230, vf0c FUN_00953350, vf14 FUN_009534c0, vf2c FUN_00953d50),
  0x00f2da10 and 0x00f2d9b4 (other selectable kinds). **vf2c = FUN_00953d50 in every vtable: returns DAT_00edd270 =
  10.0** [CODE+DATA].
- **Type → `/:SelItemType`** (vf0c FUN_00953350): Type 57 (0x39) → vf3c (0x4f3ec0 [ENC]; [INF] the SelectableString,
  "car"); otherwise the table at **0x00edd580** (16-byte rows {Type, name, icon id, …}, ends at Type −1):
  0 exit, 1 carseller, 4 stolencar, 2 player, 3 carrenter, **5 garage**, 6 living, 54 club, 7 realtor,
  8 photographer, 9 diary, 10 dressing, 49 game_progress, 50 bar, 51 office, 52 photos, 53 photo_expo, 12–16 / 33–48 /
  55 billboard, 17–19 / 26 / 30 / 31 npc, 20 tutor, 21 npc_receiver, 22 goodies, 23 npc_tuner, 24 / 25 / 28
  npc_stickers, 27 surgeon, 29 hairdresser, 32 npc_airport_hostess, 56 jacuzzi, 57 "" [DATA].
- Per frame (FUN_00952410, asm 0x9524a2–0x9526c0), active if +0xd1c, frozen if +0xd1d:
  - cooldown +0xd18 -= dt;
  - for each item: `pos = item->vf08()` (the volume's placement position, e.g. Garage (20.239, 33.110, 3.206)),
    `d = pos − cam(+0xd50)->pos(+0x310)`; keep if `dot(camForward, d/|d|) > 0`, **`|d|² < item->vf2c()² = 100`
    (10 m from the eye to the volume position)** and `|d|²` below the best so far (start FLT_MAX), and
    `item->vf04(camPos, camDir)`; **the nearest position wins** (not the nearest ray hit);
  - vf04 for a box (FUN_009533b0 → FUN_00953d80): FUN_00cca610 intersects the ray with the volume (0, 1 or 2 points);
    the segment eye → farthest point (or eye + vf2c() × dir when there is no point) is cast against the scene
    collision (FUN_00bd2f80); no hit → FUN_00cca690, hit → FUN_00ccb960(hit point) (point in the volume) [CODE];
    the exact return values of FUN_00cca690 / FUN_00ccb960 are not traced [INF: "visible": no obstacle before the
    volume, or the obstacle hit lies inside the volume];
  - `SetSelected(best)` (FUN_0094f440): ignores changes while cooldown > 0 (except to −1); calls old->vf18 (leave),
    new->vf14 (enter), **flash `/:SelItemType` = new->vf0c()** (string), sets the item icon/text vars, cooldown = 0.2 s,
    then calls the movie's item function (vfunc +0xd4 of the walk movie at +0xd10) (INF: ActionScript `ItemType('over'/'out')`).
  - Reticle depth: ray from DAT_01195b80 along the camera forward × 50.0 (DAT_00f4a040, FUN_00bd2f80); `/cursor` clip
    gets `hitFraction × 50` or 50.0 when nothing is hit (vfunc +0x100) [CODE]. **The 50 m is only this cursor depth,
    not a selection distance.**
- OpenTDU2 (`source/game/walkmode/walk_mode.cpp`): the rule above with the volume slab test and
  `RaycastHavokCollision` for vf04. The "Garage" prompt appears when the eye is within 10 m of (20.239, 33.110,
  3.206) and the reticle is on the door box.
- FSCommands handled by the manager (FUN_00952fa0): `camn`, `camf`, `cain`, `caou`, `emop`, `emcl`, `wmof`
  (walk mode off: FUN_0094ed80 + FUN_0094fb30), `wmon` (walk mode on: FUN_0094fb90), `rqca`, `shou` [CODE].
  FIRST_WM: `opdo` (open door: FUN_00a48770(2.0,1,1,1) when not seated), `wmof` ignored while in "INTRO" [CODE,
  FUN_009a9c4e].

### 4.2 Avatar controller parameters [DATA, `scenarics/avatars/avatarsdriver.bnk`, Entity__AvatarOnline]

The walk avatar is the entity `WalkModeMan` or `WalkModeWoman` (class 0x41564f4e), chosen by the avatar sex
(FUN_00952de0 / FUN_0094fae0 / FUN_00b9b1d0); the code copies its +0x188 field into manager+0xd48 [CODE].

| field | WalkModeWoman | WalkModeMan |
|---|---|---|
| Cam_Speed_Max | 2.8 | 2.8 |
| Cam_Back_Speed | 2.8 | 2.8 |
| Walk_Speed_Max | 2 (m/s, INF) | 2 |
| Head_Yaw_Max / Min | 70 / 70 | 70 / 70 |
| Head_Pitch_Max / Min | 60 / 45 | 60 / 45 |
| Time_Before_Reorientation | 1 | 1 |
| Reorient_Speed | 0.5 | 0.5 |
| Allow_Turn_Back | 0 | 0 |
| RotationSpeed | 0.3 | 0.3 |

animAvatar[17] (.bas + .trk): [0] stand `co_stand-w/m`, [1]/[2] turn on the spot right/left
`co_demitoursurplaced/-g`, **[3] walk `co_walkt02-w` / `cd_walk-m`**, [4]/[5] lean `cd_low_leanloop` (man: [4]
`cd_low_stand2lean-m`), [6] `cd_lookhummer2`, [7] `co_waitlobby`, [8..16] emotes (hello(a), cheers, funny, followme,
angry, sad(_02), strong, laugh, followme). No separate run clip [DATA]. Speed/blend curves: [ENC].

### 4.3 Cameras

- AVATAR_CHOICE walk start: `cam_entree_%02d` (§2.2, Fov 60°); FIRST_WM: `cam_entree` (Fov 50°) after C_INTRO;
  FirstPerson.StartCamera / ShopData.MainCamera = `cam_walkmode` (Fov 30°) [DATA+CODE].
- Camera type 9 / 10 at `DAT_01195bf0+0x2e8` selects shadow/post presets in both modes (FUN_009bde30, FUN_009aa560:
  type 9 → 0xe8ece05b_cc264251 (base) / 0x29a263e7_5e5a7488 (FIRST_WM), type 10 → 0x44fbf9e9_9df061b4 /
  0x85b57a55_0f8c576d) [CODE]; C_TESS requires the camera type 9 (§2.5) [CODE] → [INF] type 9 = walk-mode camera.
- Follow-camera math (orbit, collision, head look): [ENC].

### 4.3b Walk camera eye height (camera type 9) [CODE]

The manager's camera `+0xd50` = `FUN_00ca0840(9)` (FUN_009511d0). Camera factory FUN_00ca3f70: type 9 →
FUN_00c823e0 (0x480 bytes, vtable 0x00ef4cb4; per-frame update vf02 = FUN_00c82840).

- **Character proxy** (FUN_00c825a0, created when +0x448 is set and +0x380 is null): ray from the camera
  position (+0x310) straight down (0, −1, 0), length 10.0 (DAT_00edd270) (FUN_00bd51a0 / FUN_00bd4230). Hit →
  proxy start = hit point + (0, **0.3** (double 0x00f4a178) + **0.6** (double 0x00f49ea0), 0). No hit → camera
  `+0x330` + (0, 2.0, 0) (the `+0x330` field is not traced). `FUN_00bcea00(pos, 0.3 (DAT_00ede42c), 1.2
  (DAT_00edf24c), 80.0 (DAT_00edf1dc), 0, 0, 1.0)`: capsule radius 0.3, segment 1.2 (vertices at ±1.2 × 0.5
  (0x00f49748)); total height 1.8, centre at floor + 0.9. The `80` goes to FUN_006929b0 [ENC] (max slope [INF]).
- **Eye offset** (FUN_00c82840 at 0xc8358b, when +0x44b is clear): `+0x3c0 = (0, 0.7 (DAT_00eddb08), 0, 0)`
  (rotated by FUN_0045b810 [ENC] when the lean value +0x444 ≠ 0); `+0x390` (camera position) = proxy position
  (proxy +0x110) combined with `+0x3c0` and the world origin (FUN_004298d0 [ENC]; [INF] vector add/sub).
- **Avatar under the camera** (FUN_00950470): avatar position = camera position − `+0x3c0` − (0, 0.6 + 0.3, 0)
  (the same two doubles), facing the camera target (+0x320) with y = 0 → the avatar's feet are on the floor,
  confirming `eye = proxy centre + 0.7`.
- **Eye height = floor + 0.3 + 0.6 + 0.7 = 1.6 m**, for WalkModeMan and WalkModeWoman alike: none of these
  functions reads the avatar sex, height or scale (the AvatarOnline entity only gives +0x284/+0x288/+0x2a4 →
  camera +0x460/+0x468/+0x46c, walk speeds [INF]). The start camera (`cam_entree_xx`, `cam_entree` y 1.211 above
  the garage floor) only gives x/z and the floor below it.
- OpenTDU2 (`walk_mode.cpp`): before, eye height = start camera − floor (1.21 m in the garage); now the eye is
  put at floor + 1.6 m under the start camera (ray 10 m) and kept at 1.6 m above the floor while walking; body
  sphere radius 0.3 (the proxy radius). Step height / slope of the proxy: [ENC] (OpenTDU2 keeps 0.4 m).

### 4.4 Collisions

House: `new_intro.bnk` → `intro_lph_l3di_57E5C829CCD9F2C6.shk`; garage: `garage.bnk` → `garage_int_…shk` (static)
and `.dhk` (dynamic, hinges). Both are Havok 5.5 packfiles wrapped in an Eden header (§3.1) [DATA].

---------------------------------------------------------------------------------------------------
## 5. Flash (frontend/hires/intro.bnk `intro.vmf` = movie "INTRO"; GENERAL.vmf)

### 5.1 INTRO.vmf walk-mode UI [DATA, `/tmp/claude-0/tg/intro_vmf2.txt`, `int_8.txt`]

- Root labels: `avatar` (f1), **`wm_home` (f17)** and **`wm_garage` (f26)**: both do `cursor.cursor_anim.gotoAndPlay
  ('open'); _root.Input = true; stop()`; `generik1..5`, `sex`.
- Root vars/functions (frame 0): `SetHelp(HelpFrame, Change)`, `SetBDD()` (db_home = 1, FSCommand `sbdd` = load the
  "home"-category strings, e.g. `m_Garage_`), `_root.m_EnterCar_` (default "_Enter in the car"),
  **`ItemType(action)`** (switch on `_root.SelItemType`: `garage`, `car`, `vehicle`; actions `over`/`out`/`valid`;
  garage valid → `camn`,`wmof`,`gain`; car valid → `camn`,`wmof`,`enve`; vehicle over → Input true,
  cursor_anim 'on', `opdo`), `CloseAllDisp()`, `LowBarText()` (garage → `m_Garage_`, car → `m_EnterCar_`),
  `WmAfterValid()`, `BackToWM()`. EditText `/:t_wm_destination` (default "5_destination Walk Mode").
- `cursor` (sprite 8, 82 frames, placed d1 at (0,120)): labels default 0, on 8, info 22, on_stop 36, mini_menu 50,
  off 59, close 67; functions `SetCursorDisplay(vis)`, `ItemAction(action)`, `disp_low_bar(action, is_only_txt)`,
  `SetMenu()`, `A()` (validate; frame 8), `B()` (back: `wmon`, `camf`, `sndp:back`). Children:
  - **`cursor_anim`** (sprite 2342, 78 frames, at (606.8, 327.9) = screen centre for 1280×960): the circular
    reticle; labels default 0, open 7, stop 17, on 22, loop 31, out 43, close 54, menu 66.
  - **`low_bar`** (sprite 2354, at (129.7, 585.1)): labels default 1, open 8, stop 19, valid 25, close 58, open_down 63,
    stop_down 74, valid_down 84, close_down 108; child **`ic`** (sprite 2345: icon by label: living 7, home 13,
    dressing 19, billboard 27, **garage 35**, diary 42, game_progress 48, game_plug 60, exit 70, gme 83, club 90;
    contains `ic_container`), `top_bar` (2350).
  - `mini_menu` (2384), `maskMiniMenu` (2357).
- **`low_bar.ic.icLoaded`** [DATA, `vmf2.py intro.vmf --sprite 2354/2345/3`]: low_bar frame 0 action
  `Stop(); ic.attachMovie('specificIcons', 'icLoaded', 0); opened = 0`. `specificIcons` is the ExportAssets name of
  **character 3** (tag at file offset 0x192: count 1, id 3). Sprite 3 (18 frames): label `garage` at f0 (`stop()`),
  shape ch2 = bitmap 1 (75×75, the garage-door icon) at (11.2, 11.2) scale 0.7. `disp_low_bar` does
  `low_bar.ic.icLoaded.gotoAndStop(_root.SelItemType)` → for `garage` the icon stays on frame 0. `ic` (sprite 2345:
  only `ic_container`, an invisible 75×75 rectangle; its labels living/home/…/club just stop) is placed at
  (500.5, −530.6) × 0.402 in low_bar with alpha 0 and is faded/scaled in by the `open` animation (f12 alpha 0.25 …),
  i.e. **the garage icon is drawn well above the "Garage" bar**, near the top of the screen. The Eden player
  implements `attachMovie` (method name string at 0x00e4b37c, list "attachMovie, removeMovieClip, gotoAndStop, …")
  [CODE string]; OpenTDU2's FlashPlayer did not → icLoaded was missing. Now `FlashPlayer::attachMovie` (exports of the
  movie, scripted depth above the timeline depths, kept across timeline seeks).
- In the original screenshot (walk mode, reticle on the garage door) the two garage icons are: the **upper,
  screen-centred one with the flat elliptic ring = this flash `icLoaded` + `top_bar` (sprite 2350)**, and the lower
  one in a dark disc = the 3D `HOLOGRAMS` marker (§5.1b). OpenTDU2 renders both now
  (scratchpad garage/after_near.png) [DATA + render check].
- The round glyph just above the "Garage" label is the low_bar child d9 (sprite 2334, placed at f11 at
  (492.2, 37.5)): the pad shows `A`, OpenTDU2 shows the movie's own glyph (green tick) for keyboard play — unchanged
  [INF: which variant the PC keyboard build shows was not traced].

### 5.1b HOLOGRAMS (3D garage marker above the door) [DATA + CODE]

- new_intro.bnk `Entity__Actor` **`HOLOGRAMS`** (`__Hc` 0x2df8c23b60396fe4, identity placement, Flags[0] = 2): mesh
  `intro_lph_l3di_71E55BD0121EDBE1.3DD` (one INS `HOLOGRAM` at **(20.38, 33.76, 3.19)**, 90° yaw: on the garage door,
  0.65 m above the selectable centre) / `.3DG` / `.2DM` (materials `T_Sprite_Garage` (texture `ico_garage.2db`,
  FADEALFA) and two `Pulse_DarkBlue_` (`pulse_darkblue_.2db`, `pulse_blue_02_.2db`) animated by
  `Pulse_DarkBlue_A_1.UVA` / `Pulse_DarkBlue_Al_0.UVA` (the pulsing rings)).
- Visibility: entity +0x9c bit 0 = active (base ctor sets +0x9c = 7 at 0xb94ef0; FUN_00dc7290 updates only when set;
  FUN_00b45960(show) sets / clears it). `FUN_009b42d0` sets it on the entity named "HOLOGRAMS" (show),
  `FUN_009b4360` clears it (hide). Callers: photo mode enter/leave (FUN_009412f0 hide / FUN_00941860 show, house modes
  8, 9, 0x16), the money/shop UI (FUN_0090ba70 hide / FUN_0090bf10 show), FUN_009a3ae0 (cam_WalkMode sub-mode),
  LIVING loaders FUN_009b5210 / FUN_009f4ab0 (hide), FUN_009bd8d0 (hides 8 sub-objects when not the owner). **None in
  AVATAR_CHOICE**, and no .cin/.sce of new_intro.bnk references the entity hash → shown all along [CODE + DATA].
- No distance or selection dependency in readable code: the pulse is the UV animation of the ring materials.

### 5.2 Objective box: GENERAL.vmf `pop_goal` [DATA, `gen_628.txt`]

- Sprite 628 (177 frames) placed on the GENERAL root at (56.8, 389.7), depth 1033, name `pop_goal`.
- Labels: default 0 (defines `close_popup()`: goto `close_WM` if `_root.IsWalkMode` else `close`), **open 7**
  (or `open_WM` 98 when `_root.IsWalkMode`), close 76 (→ default at 97), open_WM 98, close_WM 155. Sounds via
  FSCommand `sndp:mopen` / `sndp:mclose`.
- Content: sprite 618 (dark 50%-alpha panel with bitmap corners, frames for 1 or 3 lines), sprite 622 (**the red dot**:
  bitmap 619, 31×31 px, pulsing scale/alpha over 30 frames, placed at (9.9, −228.9) scale 2 → 0.9), sprite 627/626
  (text: `name_scroll` reading `/:GoalTxt`, scrolling when longer than 180 px; frame_length 374).
- Engine side (FUN_00d61790): `/:GoalTxt` = text (string), goto `open` on `pop_goal`; 30 s auto timer;
  FUN_00d61700 sends `close_popup`. In both AVATAR_CHOICE and FIRST_WM `/:IsWalkmode` is false (§2.4), so the
  non-WM `open` animation is used [CODE].

### 5.3 Localization ids [DATA: db (`/tmp/claude-0/bm/loc_us.json`, `dbloc.tsv`), DBRewardXP in db.txt]

| id | hex | US text | key / use |
|---|---|---|---|
| 5033 | 0x13a9 | Talk to Tess | DBRewardXP `TUTO_TALK_TO_TESS` (hashcode 0xd7, goal+name 0x13a9) — **no reference in readable code or data → the code that shows it is [ENC]** |
| 5108 | 0x13f4 | Go to the garage | pop_goal after C_TESS [CODE 0xda85cc] |
| 1888 | 0x760 | Go to the garage | DBRewardXP `TUTO_GO_TO_THE_GARAGE` goal text |
| 5032 | 0x13a8 | Get into the car | `m_EnterCar_`; DBRewardXP `TUTO_ENTER_IN_THE _CAR` |
| 3300 | 0xce4 | Garage | `m_Garage_` (low bar text) |
| 5034 | 0x13aa | Go next to the car | "home" category (not referenced in readable code) |
| 5035 | 0x13ab | Start the car with #command:CAR_ACCEL# | idem |
| 5036 | 0x13ac | Position the reticle over the car, and press #command:ACCESSORIES_LEFT# to open the door. | idem |
| 5037 | 0x13ad | Press #command:CAR_ACCEL# to crouch / CAR_BRAKE to stand up | idem |
| 5038 | 0x13ae | Enjoy! | step 2 |
| 7697 | 0x1e11 | Tess Wintory | name |

No subtitle strings were found for the Tess voice lines (voices only) [DATA: no matching loc entries].

---------------------------------------------------------------------------------------------------
## 6. Tess

- Avatar config `STO_Tess_Wintory` in `avatar/config/pnj.bnk`: base `STO_Tess`, hair `W_HR_R_Tess`(14), dress
  `W_PN_D_Dress_TESS`(0), shoes `W_PN_S_Shoes_TESS`(0), accessory `W_PN_A_Jewels_TESS` (see avatar_spec.md, table of
  party PNJ) [DATA].
- She is never randomized (only `W*`/`M*` PNJ names are; FUN_00da8bb0) [CODE].
- Positions (house-local): C_WALKMODE (10.66, 31.41, 6.841) / C_TESS (10.43, 31.41, 6.727); garage: C_LIFE_TESS and
  C_VOICE (−6.696, −0.018, 18.09) rot (0,−0.9492,0,0.3148) [DATA].
- Voices (Fx__Root → Fx__SourceData → .bfx `LSGL` per language → `sound\voices\<lang>\intro\…_VOICE_STORYLINE.wav`):

| Fx__Root | wav (US, from new_intro__us / garage__us) | cinematic |
|---|---|---|
| Intro_Fiesta_01 | intro_fiesta_01 (2.85 s) | C_TESS sce 1.50 |
| Intro_Fiesta_02 | intro_fiesta_02 (4.21 s) | C_TESS sce 4.87 |
| Intro_FiestaGarage_01 | intro_fiestagarage_01 (3.17 s) | garage C_INTRO sce 8.93 |
| Intro_FiestaGarage_02 | intro_fiestagarage_02 (5.01 s) | garage C_INTRO sce 13.24 |
| Intro_FiestaGarage_03..06 | intro_fiestagarage_03..06 (1.80, 3.57, 3.68, 3.63 s) | C_VOICE_03..06 at 0 |

  FaceFX: `female.fxa::Intro_:<same names>` (`intro-w.fxe`).
- `meeting_tess.bnk` (`C_MEETING_TESS`, `C_MEETING_TESS_W`, 66.7 s, "the dream" wake-up) belongs to story step 3, not
  this flow. `fade_freeride__us.bnk` contains the same 16 `intronew-e_meet_tess_*` voices as `meeting_tess__us.bnk`
  [DATA].

---------------------------------------------------------------------------------------------------
## 7. Unknown / encrypted

- Who shows "Talk to Tess" (5033) and sets TUTO_TALK_TO_TESS: [ENC] (no readable xref; the string and the id are not
  in readable code or data). Implement: show it when walk mode starts (end of C_FIESTA_INTRO_xx) [INF].
- FpSelectable wrappers: the array constructors 0x4f4040/0x4f4050/0x4f4060 are [ENC], but the vtables are readable
  (§4.1): radius vf2c = 10 m [CODE], Type → string table 0x00edd580 [DATA] (Type 5 → "garage"). Still [ENC]: vf3c
  (Type 57 string: [INF] SelectableString "car") and which of the two FpSelectable vtables the 0x4f4040 wrappers get
  ([INF] 0x00f2db30; both have the same vf04/vf0c/vf2c).
- Walk controller physics (step-up, slopes), follow-camera algorithm, turn/walk blending: [ENC]. The capsule
  (radius 0.3, height 1.8) and the 1.6 m eye height are readable (§4.3b) [CODE].
- `zcde/zcvn/zcne`, `wmod`, `weat`, `shad` message handlers: [ENC] (intro_spec).
- What `mode[0x6e0a1]` holds (cam_entree index), meaning of vehicle field +0xa794, input codes 0x38 / 3.
- Havok packfile field layout (not decoded here).

---------------------------------------------------------------------------------------------------
## 8. Implementation plan for OpenTDU2

Existing pieces: `GMAvatarChoice` (plays C_INTRO only), `IntroCinematic`, `CinematicScene`/`Cinematic` parsers,
`EdenAvatar`, `GSPhysics` (Jolt; `createStaticMesh`), flash player.

1. **Cinematic runtime shared by both modes** (`game/cinematic/`): an entity table from `entities.xmb`
   (Entity__Cinematic with CinProps/CinEnd/CinFile/additionalsAnims, PNJ, Camera, Volume, FpSelectable, PosCam,
   Fx roots). A `CinematicPlayer` with: start, tick(rate·dt), loop (CinProps bit 1 → +0xb5b), skippable (bit 2),
   "no camera takeover" (bit 4), CinEnd 1/2/3 on stop, and listener events 3 (ended), 7 (reached end), 8 (MSG), and
   9 (released after end). One "current" and one "ambient" slot per mode (C_AVATAR/C_WALKMODE, C_LIFE_TESS/
   C_LIFE_SOUND). Actor tracks of a shot use the shot's scene time; Fx track 0x7846 events {u32 ref, …, f32 dur at
   +0x0c} play the Fx__Root voice (resolve `.bfx` → language wav in `__us`).
2. **Story controller** (new `game/story/`): steps 0–2 with enter/tick/leave as in §1.2; `gain` → step 1, end of
   C_OUTRO → step 2. Step 1 enter: switch to a new `GMFirstWalkMode`, set the current car to id 387 (California).
3. **AVATAR_CHOICE walk part** (`GMAvatarChoice`): after avatar validation start C_WALKMODE (ambient loop) and
   `C_FIESTA_INTRO_%02d`; on its end: spawn the player avatar (EdenAvatar with the chosen config and
   `WalkModeMan/Woman` animAvatar clips), put the walk camera at `cam_entree_%02d`, INTRO goto `wm_home`, show
   pop_goal "Talk to Tess" (5033). Per frame: if not talked and the camera eye is inside the `Tess` box
   (volume_mat transform, |p| ≤ 0.5) → play C_TESS (skippable), hide `cursor`, freeze input. On its end: show `cursor`,
   pop_goal 5108, resume walk.
4. **Walk-mode manager** (`game/walkmode/`): load FpSelectables; per frame select the nearest item in front of the
   camera whose box is hit by the camera-forward ray (50 m), 0.2 s re-selection cooldown; set `/:SelItemType`
   ("garage" for Type 5, "car" for Type 57), call `ItemType('over'/'out')`; on A call `cursor.A()` (or
   `ItemType('valid')`) and receive FSCommands `camn`/`wmof`/`gain`/`enve`/`opdo`/`wmon`/`camf`.
5. **Controller + camera**: Jolt `CharacterVirtual` (capsule ≈ 0.3 m × 1.8 m [INF]) on the house collision; max walk
   speed 2.0 (Walk_Speed_Max), rotation speed 0.3, camera speed 2.8, head yaw ±70°, pitch +60/−45, reorientation after
   1 s at 0.5; anims: stand `co_stand`, walk `co_walkt02-w`/`cd_walk-m` (speed-scaled), turn in place
   `co_demitoursurplaced/g`. Third-person follow camera at the cam_entree height/distance (≈1.3 m behind, FOV 60° in
   the house, 50° in the garage) [INF until the camera code is read].
6. **Collision**: write a Havok 5.5 packfile reader for `hkpStorageExtendedMeshShape` (vertices + triangle indices) and
   `hkpBoxShape`/`hkpListShape` → `GSPhysics::createStaticMesh/createStaticBox`. Until then, build the collision from
   the render meshes of `lph_l3di` / `gph_l3d` (floor + walls).
7. **FIRST_WM** (`GMFirstWalkMode`, new): load garage.bnk (+ garage__us); start C_LIFE_SOUND (ambient), play C_INTRO
   (CinEnd 2). On release: C_LIFE_TESS ambient, walk camera `cam_entree`, INTRO goto `wm_garage`, pop_goal 5032.
   Spawn the California (vehicule bank of car 387) at D_CAR (−4.385, 0.084, 15.553) rot (0,−0.957815,0,−0.287386).
   Voice logic §3.5; enter car §3.6 (A while the "car" selectable is selected → C_ENTER_CAR → seat the avatar
   (driver anims `stand_to_drive`, `drive_*` in avatarsdriver.bnk) → C_VOICE_06); throttle > 0.7 → 3 s → C_OUT →
   C_OUTRO (CIN_CAR = the California) → story step 2.
8. **Flash**: GENERAL `pop_goal` (`/:GoalTxt`, labels open/close, 30 s auto-close) and INTRO `cursor`
   (`cursor_anim`, `low_bar.ic` label = SelItemType, `/:t_wm_destination`, db strings `m_Garage_`=3300,
   `m_EnterCar_`=5032 pushed by `sbdd`).
9. **Audio**: party music emitters S_WALKMODE_Music* (AutoStart), garage music FxS_WALKMODE_Garage, cinematic Fx
   sounds, voice lines per §6 through the existing `cinematic_audio`.
