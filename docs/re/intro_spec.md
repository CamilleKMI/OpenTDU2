# TDU2 intro cinematic ("The Dream", C_INTRO) and press-start cinematic: reimplementation spec

Tags: [CODE] read from readable x86 · [DATA] read from game files · [SHADER] · [INF] inferred (reason given) ·
[ENC] the code is in the SecuROM-encrypted range (< 0x6c1000) and was not read.

Decompiled code: `/tmp/claude-0/intro_dec1.c` … `intro_dec9.c`, `intro_dec_ocean.c`, `intro_dec_ocean2.c`.
Python dumpers in `/tmp/claude-0/py/`:
`scedump.py` (.sce), `cindump.py` (.cin), `bnkhash.py` (hashcode to bank entry through `hashcodes.xmb`),
`edenid.py`, `vmftree.py` (.vmf timeline/sprites), `hx.py` (hexdump).
Extracted banks: `/tmp/claude-0/intro/{x,ff,fe,ps}/`; dumps: `/tmp/claude-0/intro/{gen_dump,def_dump,entities,cinematics,hashmap}.txt`.

---------------------------------------------------------------------------------------------------
## 0. Overview: what plays, and when

1. Boot: two video bumpers, `Euro/Bnk/Video/Bumper_ATARI.mkv` then `Bumper_EDEN_TDU.mkv`. They are listed in the
   table at 0x00f7eec8, count 2, installed by FUN_00d514d7 [CODE]. Despite the `.mkv` extension they are
   **RIFF AVI files**: H.264 Constrained Baseline 1280x720 at 30 fps, with MS-ADPCM stereo 44100 Hz audio [DATA, ffprobe].
   The ATARI bumper lasts 3.07 s. The intro cinematic does not use these bumpers.
2. GAME MODE `BOOTMENU` (0): the press-start screen, with a real-time looping cinematic from
   `islands/ibiza/scenarics/press_start.bnk` (§9).
3. Starting a new story goes through the first-intro state machine FUN_00b10da0, which sets the game mode to 0x20
   [CODE]. Mode 0x20 is **`AVATAR_CHOICE`** in the name table at 0x00f7eed8 (FIRST_WM is 0x23). For mode 0x20,
   FUN_00a91360 loads **`Euro/Bnk/Scenarics/Cinematics/intro/new_intro.bnk`**. For mode 0x23 (FIRST_WM) it loads
   `.../intro/garage.bnk` [CODE].
   - So "the intro with *ATARI presents* over the sea" is the **real-time cinematic `C_INTRO`**. It is played when the
     AVATAR_CHOICE mode starts, after the profile is created, not before the title screen.
   - It runs for 52.62 s, then hands over to `C_AVATAR` (the avatar choice) and the party (`C_FIESTA_INTRO_xx`,
     `C_WALKMODE`) [CODE+DATA].
4. The AVATAR_CHOICE start method FUN_00da92e0 (vtable slot 18 of the vtable at 0x00f29d60) does the following [CODE]:
   - looks up the `CINE` entity named `"C_INTRO"` (FUN_00b34fd0(world, 'CINE'=0x43494e45, "C_INTRO"));
   - sets the entity's listener (+0x590) to the game mode, sets +0x8f0 and sets +0x11e = 1;
   - calls FUN_00b40180, which starts the cinematic;
   - sets the flash variable `/:IsDreaming` = true.

   Before that, FUN_00da8bb0 does two things [CODE]:
   - every PNJ entity (`'CPNJ'` = 0x43504e4a) whose name starts with `W` or `M` (WOMAN_xx, MAN_xx) is given a
     randomly generated avatar (FUN_00cbd680 / FUN_00dc2070 / FUN_00b84590(2000, …));
   - the `Dance_*`/`Pool_*`/`Tess_*` PNJs use their fixed `ConfigName` (for example `STO_Party_Woman02`).

---------------------------------------------------------------------------------------------------
## 1. Bank contents

### 1.1 new_intro.bnk (12.9 MB, 360 entries) [DATA]

All paths are under `...\4Build\PC\Euro\`.

| group/sub | ext | count | content |
|---|---|---|---|
| 8/1 | install.bin | 1 | 32 bytes: `b5006bb1 00000020 00000000 0504052b 3f866666(1.05) 0 7f5df0a1 4641bace` (meaning not decoded) |
| 8/10 | hashcodes.xmb | 1 | `___CmdSaveFileInfoArray[]`: for each {Type, SubType}, `FileReferences[i]` (u64 hashcode, sorted) maps to `FileIndices[i]` (bank entry index). **This is how every `rsc = 0x…` resolves to a file.** Use `py/bnkhash.py`. |
| 8/11 | entities.xmb | 1 | world entities (xmbdump: `/tmp/claude-0/intro/entities.txt`) |
| 0/0, 0/1, 0/2 | 3DD/3DG/2DM | 24+24+24 | house `lph_l3di` (Luxury Player House L3D, Interior), props `cf_*` (DJ mix table, speakers, turntable, buoy), furniture |
| 0/3 | 2DB | 151 | textures, including lightmaps `lm_eph_l3d.2db` and `lm_eph_l3d_night.2db` |
| 0/7 | XSB | 2 | skeletons (`.XSB` + `SKEL` chunk, bone names such as `RootBuoy`) for animated props (buoy, turntable) |
| 0/8 | .anm | 40 | avatar/NPC/prop animations (`anims\firstintro\fi_xan\...`), format in §6.2 |
| 10/6 | .wav | 8 | sounds (§5) |
| 0/11 | .shk | 1 | 116 KB, header `02 02 72 00` (not decoded) |
| 0/15 | .UVA | 4 | UV animations: an `ANM!` file with a `MAT!` track (for example `Oasis_02_0.UVA`, 20 s) |
| 0/16 | .cin | 26 | cinematic descriptions (`C_INTRO`, `C_AVATAR`, `C_AV_0x`, `C_xTOy`, `C_FIESTA_INTRO_0x`, `C_TESS`, `C_WALKMODE`) |
| 0/17 | .sce | 33 | cinematic scenes |
| 0/31 | .bfx | 16 | sound FX descriptors (§5.3) |
| 0/35 | .fxe | 3 | FaceFX (`FACE` magic, "Eden Games / evaluation use only"): `thedream-w/m.fxe`, `intro-w.fxe` |

- `new_intro__us.bnk` (copied from the Mac) contains the localized voices of the party only:
  `sound\voices\us\intro\intro_fiesta_0x_VOICE_STORYLINE.wav` and `intro_fiestagarage_0x…`, sub 36, plus
  `hashcodes__us.xmb`. Sub 37 is FR, and so on (`Fx__Sound__EN/FR/GE/SP/IT/RU/PL/JP`).
- **C_INTRO has no voice.**
- `fade_freeride.bnk`: cinematic `C_FADE_FREERIDE` (12.3 s, `s_toctoc_CINEMATIC.wav`, Tess animation, voices).
  FUN_00b10da0 plays it at the end of FIRST_WM, before FREERIDE [CODE].

### 1.2 frontend/hires/intro.bnk [DATA]

- `intro.vmf`: the Eden flash movie (stage, labels, sprites). Dump: `/tmp/claude-0/intro/vmf_tree.txt`.
- `intro.2dm` and `fla_*.2db`: the bitmap pages. **The credits are in this movie** (§4).

### 1.3 Entities used by C_INTRO (entities.xmb) [DATA]

The `Entity__Cinematic` named `C_INTRO` has these fields:

| field | value |
|---|---|
| Priority | 1.5 |
| CinProps[0] | 0 (so no loop) |
| CinEnd | 3 |
| Znear / Zfar | −1 / −1 (use the defaults) |
| DefaultShadowConfig | `CINE_DEFAULT` |
| CinFile[0] (sub 16) | `intro_lph_l3di_M_INTRO_TOTEM.cin` |
| CinFile[1] (sub 17) | `M_INTRO_Default.sce` (the party, 20.4 s) |
| CinFile[2] (sub 17) | `M_INTRO_generique.sce` (sea flight + credits, 35.36 s) |
| additionalsAnims[35] | dance animations (.anm) |
| additionalsStyleAnims | per style `m`/`w` |

Other entities:
- `Entity__Fx` emitters: `S_TimeStart`, `S_TimeStop` (AutoStart 0), `S_WALKMODE_Music` and `_01/_02/_03`
  (AutoStart 1, positioned in the house), `FxS_WATER`.
- `Fx__Root` → `Fx__SourceData` → `.bfx` + `SoundList[]` → `Fx__Sound` → `.wav` (§5).
- 55 `Entity__Light` (`RT_Int_xx`/`RT_Ext_xx`).
- 30 `Entity__Actor` props.
- 31 `Entity__PNJ`: `ConfigName` (avatar config `.avt`, in `avatar/config/pnj.bnk`) and `CinAnimTracks`.
- 7 `Entity__Camera` (`cam_entree_0x`, Fov 1.0472 rad, Ratio 1.7778).
- `Scenery D_SCENE` (103 models).

---------------------------------------------------------------------------------------------------
## 2. .cin format ("CIN!") [DATA, verified on 30 files]

```
0x00 char[4] "CIN!"; u32 0x08; u32 0xa0; u32 0x107 (version)
0x10 chunk "REFS" {u32 size; u32 numRefs; entries}   entry = {u16 type; u16 entrySize; body[entrySize-4]}
     type 0x020b MSG: zero-terminated string "<4cc>:<arg>" (zero padded with 'A4' garbage to 4-byte alignment)
     type 0x010d FX : u64 Fx__Root hashcode (entities.xmb)
     type 0x0303 SUB: u64 hash + full source path of a .sce (the scene list)
     chunk "DATA" {u32 size; body}
```

DATA body:

| off | type | C_INTRO | press start | meaning |
|---|---|---|---|---|
| +0x00 | f32 | 52.62 | 60.0 | total duration (s) |
| +0x04 | f32 | 52.62 | 60.0 | duration (copy) |
| +0x08 | u32 | 0 | 0 | ? |
| +0x0c | u32 | 2 | 1 | number of scenes |
| +0x10 | u32 | 1 | 1 | number of shot tracks [INF] |
| +0x14 | u32 | 8 | 1 | number of other tracks (MSG + FX) [INF: matches the counts] |
| +0x18 | u32 | 89 | 4 | total scene objects (52+37) [INF: matches] |
| +0x1c | u32 | 18 | 3 | total cameras [INF: matches] |
| +0x20 | u32 | 71 | 1 | total actors [INF: matches] |
| +0x24..+0x2c | 3×u32 | 0 | 0 | |
| +0x30 | u32 | 34 | 4 | numRefs |
| +0x34 | u32 | 4 | 0 | number of FX refs |
| +0x38 | u32 | 110 | 6 | ? |
| +0x3c | {u32 1; u32 refIndex}[numScenes] | (1,32),(1,33) | (1,3) | REFS index of each scene (0x0303) |
| then | tracks | | | until the end of DATA |

Track: `{u16 kind; u16 flags; u32 size (whole track); u32 id; u32 numKeys; f32 duration; f32 times[numKeys]; value[numKeys]}`.
The value size is `(size − 0x14 − 4·numKeys) / numKeys`.

Track ids are **4-byte folded name hashes** [DATA+INF]. The hash adds character i into byte i&3, with **no case
folding**. Short names are the ASCII bytes themselves ("Fx" = 0x7846, "Fov" = 0x766f46, "c_01" = 0x31305f63). Every
id observed is explained by a meaningful name:

| id | name | where | value |
|---|---|---|---|
| 0x00000000 | (shot track, kind 1, flags 0) | .cin | 0x3c shot record |
| 0x73d8ccae | **"Message"** | .cin, .sce actors | {u32 refIndex (MSG); f32 duration} |
| 0x00007846 | **"Fx"** | .cin | 0x54 FX record |
| 0xd7e2d8c4 | "Position" | .sce | vec3 |
| 0x64c8d3b1 | **"Heading"** (quaternion) | .sce | quat xyzw |
| 0x6c6163b8 | "Scale" | .sce | vec3 |
| 0x00766f46 | "Fov" | .sce camera | f32 **degrees** |
| 0xdcd2e210 | "Animation" | .sce actor | 10 dwords (§3.3) |
| 0xd2df4b2c | "Visibility" | .sce | 1 dword, low byte = visible (0x34344101: byte0 = 1, rest garbage) |
| 0x70726fb5 | "Morph" | .sce actor | 5 dwords {u32 refIndex (FaceFX ref), ?, ?, f32 length, 0} |

The name strings "Heading", "Position Property Type Unknown !" and "edCinCamState::POSITION_HEADING" are in .rdata
next to `.\Sources\CinScene.cpp` (0xf43034–0xf43138) [DATA]. The parser that uses them is **[ENC]**: no readable
code references these strings, and none of the track ids appear as immediates in the readable range.

**Shot record (0x3c)**:

| off | type | meaning |
|---|---|---|
| +0x00 | f32 | shot duration |
| +0x04 | u32 | scene index (order of the `+0x3c` list) |
| +0x08 | u32 | camera name hash (fold4; for a 4-char name like "c_01" this is the ASCII bytes) |
| +0x0c | f32 | camera time at shot start (scene-local) |
| +0x10 | f32 | camera time at shot end |
| +0x14 | u32 | 1 on C_INTRO's first shot, else 0 |
| +0x18 | f32 | 4.0 on C_INTRO's first shot, else 0 |
| +0x1c..+0x2f | | zeros, or export garbage such as 0x0016c472 / 0x0012f1d8 |
| +0x30 | u32 | 0x749182xx (garbage pointer) |
| +0x34 | u32 | 0x748f6bxx (garbage pointer) |
| +0x38 | i32 | −1 |

Notes on the shot record:
- The fields at +0x1c..+0x38 look like raw pointers saved by the exporter [INF: the values are typical Win32 DLL
  addresses].
- **Bug in the current OpenTDU2 parser:** it looks for a `"c_"` string and reads `+8` from it. That reads the camera
  *end time*, not the duration, and it misses hashed camera names such as `Cam_PREMIER_PLAN`.
- The `times[]` of the shot track are the global shot start times.
- Within a shot, camera local time = camStart + (t − times[i]).

**FX record (0x54)**:

| off | type | meaning |
|---|---|---|
| +0x00 | u32 | REFS index of an FX ref |
| +0x04 | u32 | flags: 0x37 for the music, 0x27 for positional sounds; meaning not decoded |
| +0x08 | f32 | 0 |
| +0x0c | f32 | duration |
| +0x10 | f32 | 0 |
| +0x14 | f32[16] | D3D row-major world matrix; translation in the last row |

---------------------------------------------------------------------------------------------------
## 3. .sce format ("SCE!") [DATA, verified on 37 files]

```
0x00 "SCE!"; u32 0x10; u32 0x104 (version); u32 0
0x10 empty chunk {u32 0; u32 0}
0x18 "DATA" {u32 size; body}      then "REFS" {u32 size; u32 n; entries}
REFS entry types: 0x0302 ANIM {u64 hash of the .anm name; name}, 0x0306 MORPH {u64; "…\female.fxa::TheDream_:<anim>"}
```

DATA body:

| off | type | meaning |
|---|---|---|
| +0x00 | f32 | scene duration |
| +0x04 | u32 | numObjects |
| +0x08 | u32 | numCameras |
| +0x0c | u32 | numActors |
| +0x10..+0x18 | 3×u32 | 0 |
| +0x1c | u32 | numRefs |
| +0x20 | u32 | ? |
| +0x24 | u32 | 0 |
| +0x28 | f32[16] | origin matrix (D3D row-major). Identity for the intro; translation (21120, 0, 20352) for press-start M_IBIZA_01, i.e. the sector origin |
| +0x68 | objects | |

**Object header (0x3c)**:

| off | type | meaning |
|---|---|---|
| +0x00 | u32 | object size (header + tracks) |
| +0x04 | u32 | header size = 0x3c |
| +0x08 | u32 | **class hash**: 0x2a043536 = camera, 0xd039fa46 = actor (PNJ, prop, car, target) |
| +0x0c | u32 | name hash (fold4 with uppercase folding, e.g. "DJ" = 0x4a44, "MAN_01" = 0x5f4e727d) |
| +0x10 | u32 | **number of tracks**. **This is NOT a type.** It is 3 for every camera (Position/Heading/Fov), 5 or 6 for actors, and 6 for CIN_CAR_01. Verified on all 93 objects. The `Type 3 = camera, 6 = car` reading in `title_scene.md` and `cinematic_scene.h` is wrong: use the class hash. |
| +0x14 | char[32] | name |
| +0x34 | u64 | entities.xmb `__Hc` of the matching entity (e.g. Dance_04_WOMAN_01 0x00fcd3bc715a4369); 0x6666666666666666 = none |

Tracks follow the common track layout of §2. Position/Scale tracks have kind 1, all others kind 0; flags = 1.
Cameras are baked at **50 Hz**: Cam_PREMIER_PLAN has 1455 keys over 29.08 s.

### 3.1 M_INTRO_generique.sce (35.36 s)

- 2 cameras: `Cam_PREMIER_PLAN_OLD` (unused) and **`Cam_PREMIER_PLAN`**.
  - Position/Heading: 1455 keys, t = 6.28 … 35.36.
  - Fov: 55°.
- 35 actors: dancers and props, each with 1 static key. Their Animation track points to REFS 0..22 (ANIM).
- Camera path [DATA]:

| t (scene) | position |
|---|---|
| 6.28 | (−1200, 2.9, −1181), over the open sea, looking 51° down |
| 6.28 → 26 | goes to (−157, −2.6, −249), levelling out to look toward the coast (the "low shot over turquoise water toward the beach") |
| 26 → 34 | slow drift |
| 34 → 35.36 | jumps into the house (−8.3, 36.9, −16.5) |

Local y: the house floor is at about 31. The ocean surface in this frame is not given in the .sce; it comes from
the world.

### 3.2 M_INTRO_Default.sce (20.4 s)

- 16 cameras: `c_tess_01`, `c_intro_01`…`c_intro_13`, `c_intro_11bis`, `c_intro_03_DJ`, `NO_CAMERA`.
- 36 actors: dancers, `c_intro_13_Target`, props.
- 53 REFS: ANIM and MORPH (FaceFX `TheDream_*`).

### 3.3 Animation track (10 dwords)

Field 0 is a u32 REFS index (ANIM). For example WOMAN_05 has idx 0 = `fi_dancestand1-w`. The other 9 floats look
like clip timing: start/end/blend times and the clip length. Observed values:
- `0,0,0,0,36.17,0,0,36.17,0`
- `0,0.2,0,0,98.12,0,0,97.92,0`
- 2-key example (Pool_WOMAN_01): `[9,0,0,5.06,0,5.06,5.06,0,5.06,ε]` and `[9,2.68,2.68,0,2.68,41.7,0,ε,39.02,…]`

Exact field semantics: **[ENC]** (CinScene.cpp). Best guess [INF]: f[5] = animation end, f[8] = end minus blend.

---------------------------------------------------------------------------------------------------
## 4. C_INTRO timeline (intro_lph_l3di_M_INTRO_TOTEM.cin, 52.62 s) [DATA]

Shots (t = global time, s):

| t | dur | scene | camera | cam time |
|---|---|---|---|---|
| 0.000 | 29.08 | generique | Cam_PREMIER_PLAN | 6.28 → 35.36 |
| 29.08 | 2.64 | Default | c_intro_01 | 0 → 2.64 |
| 31.72 | 1.12 | Default | c_intro_02 | 2.64 → 3.76 |
| 32.84 | 1.01 | Default | c_intro_03_DJ | 3.76 → 4.77 |
| 33.85 | 1.01 | Default | c_intro_04 | 4.82 → 5.83 |
| 34.86 | 1.61 | Default | c_intro_05 | 5.35 → 6.96 |
| 36.47 | 1.41 | Default | c_intro_10 | 6.09 → 7.50 |
| 37.88 | 0.85 | Default | c_intro_07 | 6.82 → 7.67 |
| 38.73 | 1.08 | Default | c_intro_08 | 7.60 → 8.68 |
| 39.81 | 1.40 | Default | c_intro_09 | 8.68 → 10.08 |
| 41.21 | 0.78 | Default | c_intro_03_DJ | 12.38 → 13.16 |
| 41.99 | 1.66 | Default | c_intro_11bis | 4.82 → 6.48 |
| 43.65 | 0.99 | Default | c_intro_06 | 6.55 → 7.54 |
| 44.64 | 1.26 | Default | c_intro_11 | 11.88 → 13.14 |
| 45.90 | 1.72 | Default | c_intro_12 | 15.04 → 16.76 |
| 47.62 | 3.30 | Default | c_intro_13 | 16.76 → 20.06 |
| 50.92 | 1.70 | Default | c_intro_13 | 20.068 → 20.408 |

Note on the last shot: it lasts 1.70 s but covers only 0.34 s of camera time. Either the last key is held, or the
playback rate is not 1 [INF].

Message tracks (`Message` = 0x73d8ccae; the value is a REFS index):

| track | events |
|---|---|
| zone | `zcde` at 0 (29.08 s), `zcne` at 29.08. Meaning **unknown**; handled by encrypted code. |
| shadows | `shad:PG` at 0; `PR`, `PR`, `PA`, `PM` at 29.08 / 31.72 / 32.84 / 33.85; `PR` at 37.88; `PA` at 38.73; `PR` at 41.99; `PM` at 43.65; `PR` at 44.64; `shad:AVATAR_CHOICE` at 47.62. These are shadow configs (DefaultShadowConfig is `CINE_DEFAULT`) [INF]. |
| weather | `weat:Intro` at 0 (preset in `islands/ibiza/fx/cinematic_weathers.xmb`); then on each cut `wmod:Intro_02`…`Intro_15` and `Intro_11bis` (in `cinematic_modifiers.xmb`), at the shot times above, plus Intro_15 at 48.18 |
| flash / sound | `flsh:generik1` at **5.200**, `flsh:generik2` at **11.754**, `flsh:generik3` at **19.050**, `flsh:generik5` at **24.352** (each lasts 4.8 s, generik5 7.8 s); `snd:timestop` at **45.919** |

FX tracks (`Fx`, sounds):

| t | Fx__Root | sound | wav | position |
|---|---|---|---|---|
| 0.000 | `FxC_INTRO` (0xb832a7b31ebe6ed5) | music | `c_intro_CINEMATIC.wav` (52.64 s) | identity matrix, flags 0x37 |
| 5.250 | `woosh01_00` | whoosh with "ATARI presents" | `woosh01_CINEMATIC.wav` (1.92 s) | (155.3, −235.4, −48.9) |
| 11.796 | `woosh01_01` | whoosh with "Eden Games production" | woosh01 | (127.4, −821.9, −1011) |
| 19.200 | `FxS_TDU2_LOGO` | logo hit | `s_tdu2_logo_CINEMATIC.wav` (5.19 s) | (340.6, −1001, −1290) |
| 45.919 | `snd:timestop` message → starts emitter `S_TimeStop` | | `s_timestop_CINEMATIC.wav` (10.28 s) | |

The listener (FUN_00da8040, event 8) dispatches these messages [CODE]:
- **`flsh:<x>`**: calls flashManager(FUN_00d7feb0)→vfunc+0xdc(`x`, "") on the intro movie, which plays label
  `generik1`/`2`/`3`/`5`.
  - For `generik5` it first sets `/:generik_date` (variant type 3) to localized string id 0x139b. The default text
    in the movie is "Ibiza, Sant Vincent. 18h15".
- **`snd:timestop`**: FUN_009bddf0 starts the `Entity__Fx` named `S_TimeStop`.
- **`snd:music`**: starts `S_WALKMODE_Music`, `_01`, `_02`, `_03`.
- Everything else (`weat:`, `wmod:`, `shad:`, `zc..`) goes to FUN_009be090 → FUN_00a91820 and then to encrypted code.

Other listener events:
- event 0 (start) of C_INTRO: stops `S_WALKMODE_Music` and `_01`…`_03` (FUN_009bdc80).
- event 7 (end) of `C_AVATAR`: chains `C_AV_01`; the avatar UI is `C_AV_xx` / `C_%dTO%d`.

### 4.1 Credits overlay (intro.vmf, frontend/hires/intro.bnk) [DATA]

The root timeline labels are at frames 36 `generik1`, 53 `generik2`, 65 `generik3`, 77 `generik4`, 91 `generik5`.
Each label's DoAction calls `<clip>.open` (gotoAndPlay "open") on a sprite placed at frame 36:

| label | sprite | content |
|---|---|---|
| generik1 | `generik_1` (sprite 62, 156 frames) | bitmaps 56/57 (ATARI logo + symbol) and edit-text `/:m_presents` (DBLoc hash 0x48a) → **"ATARI presents"** |
| generik2 | `generik_2` (sprite 71, 164 frames) | bitmaps 64/65 (Eden Games) with `/:m_An` and `/:m_Production` (0x48b) → "an EDEN GAMES production" |
| generik3 | `generik_3` (sprite 85) or, if `_root.IsCasino`, `cas_generik_3` | TDU2 logo bitmaps 73/76/80 (casino variant 88…99) |
| generik4 | `generik_4` | "Starring: /:PlayerName". **Not triggered by C_INTRO.** |
| generik5 | `generik_place` (247 frames) | `/:generik_date` ("Ibiza, Sant Vincent. 18h15") |

Frame 107 (`sex`) removes all of them and opens the sex choice popup.

The intro.vmf frame rate and stage size are printed at the top of `vmf_tree.txt`. The sprite internals (fades)
must be played by the flash player. The cinematic triggers them at the times in §4.

---------------------------------------------------------------------------------------------------
## 5. Audio

### 5.1 Engine [CODE / RTTI]

Eden's own engine, `edAudio`. RTTI found in .data at 0x00f919dc…0x00f92098:
- Classes: edAudioPC, edAudioMixer, edAudioSpatializer, edAudioSourceSpatial, edAudioStream, edAudioMemoryStream,
  edAudioChannelRouter.
- **Outputs: `edAudioOutputPCXAudio` (XAudio2) and `edAudioOutputPCDirectSound`** (DSOUND.dll is imported).
- **Codecs: `edAudioCodecWavPCM`, `edAudioCodecWavMS_ADPCM`, `edAudioCodecWavEx`, `edAudioCodecMP3`,
  `edAudioCodecVorbis`, `edAudioCodecSpeex`, `edAudioCodecForVideo{PCM,ADPCM,Vorbis}`.**
- Not FMOD, not XACT. Audio.cpr (512 bytes) is encrypted or compressed.
- Source files: `Sources\GSAudio.cpp`, `SC_SoundEmiter.cpp`, `FlashSounds.cpp`.

### 5.2 Files [DATA]

All intro sounds are inside `new_intro.bnk` (group 10, sub 6, ".wav"). They are ordinary RIFF WAVE files:
- `fmt `: wFormatTag 2 (**Microsoft ADPCM**), 2 channels, **22050 Hz**, avgBytes 22311, blockAlign 1024, 4 bits,
  cbSize 32, samplesPerBlock 1012, 7 standard coefficient pairs.
- `fact` gives the sample count.
- ffmpeg/ffprobe decode them as `adpcm_ms`.

| file | duration | used by |
|---|---|---|
| c_intro_CINEMATIC.wav | 52.64 s | C_INTRO music (FxC_INTRO) |
| woosh01_CINEMATIC.wav | 1.92 s | credits whoosh |
| s_tdu2_logo_CINEMATIC.wav | 5.19 s | TDU2 logo |
| s_timestop_CINEMATIC.wav | 10.28 s | snd:timestop (S_TimeStop) |
| s_timestart_CINEMATIC.wav | 1.61 s | S_TimeStart |
| c_avatar_CINEMATIC.wav | 7.38 s | C_AVATAR (Fx `C_AVATAR`) |
| s_walkmode_music_AMBIENCE_INTERNAL_SFX.wav | 29.53 s | S_WALKMODE_Music (party music, looped) |
| s_water_AMBIENCE_INTERNAL_SFX.wav | 7.29 s | FxS_WATER |
| (fade_freeride.bnk) s_toctoc_CINEMATIC.wav | 12.34 s | C_FADE_FREERIDE |

- Party voices are in `new_intro__<lang>.bnk` (sub 36+, `*_VOICE_STORYLINE.wav`).
- `Euro/Bnk/sound/musics/*.wav` and `radio/` are not used by the intro.
- The `.big`/`.map` archives are not involved: everything is in the banks.

### 5.3 Sound chain and .bfx

`Fx__Root{SourceData}` → `Fx__SourceData{Fx[0] = .bfx rsc (sub 31); SoundList[] = SoundNode hashes}` →
`Fx__Sound{SoundFile[0] rsc (type 10, sub 6) = .wav}`. Localized variants use `Fx__Sound__XX.LocalizedSoundFile`,
sub 36 (EN), 37 (FR), …

Single-sound `.bfx` (228 bytes) [DATA; field meanings INF]:

| off | type | meaning |
|---|---|---|
| +0x00 | u32 | 6 |
| +0x10 | f32[16] | identity matrix |
| +0x50 | u64 | node id |
| +0x58 | u32 | 0x8540076c |
| +0x78 | char[32] | name ("C_INTRO", "woosh01") |
| +0xa0 | u64 | **sound hash** (= Fx__Sound SoundFile, e.g. 0xc648329942d0f000 = c_intro_CINEMATIC.wav) |
| +0xa8 | f32 | 0 |
| +0xac | f32 | **volume**, linear: 1.0, woosh 0.501 = −6 dB |
| +0xb0 | f32 | 1.0 (pitch?) |
| +0xbc | f32 | 180 (cone in?) |
| +0xc0 | f32 | 180 (cone out?) |
| +0xc4 | f32 | 0.25 |
| +0xc8 | f32 | 4.0 |
| +0xcc | f32 | 40.0 (min/max distance?) |
| +0xd0 | u16 | 3 |
| +0xdc | u32 | 0x16 |
| +0xe0 | char[4] | `" DNS"` ('SND ' tag) |

Voice `.bfx` (2036 bytes): a `LSGL` language-switch node with 8 children, one SND node per language (`JP_…`,
`PL_…`, …).

---------------------------------------------------------------------------------------------------
## 6. Actors, avatars, animation

### 6.1 Characters [DATA+CODE]

- PNJs carry an avatar config (`ConfigName` → `avatar/config/pnj.bnk` `.avt`, e.g. `STO_Party_Woman02`), or a
  random avatar for the `WOMAN_xx`/`MAN_xx` names (§0).
- Bodies: `avatar/common_bodies.bnk` (group 17), meshes `man_body`, `woman_body`, `*_head_{occ,afrik,asia}`
  (3DD/3DG/2DM/XSB).
- Clothes are in `avatar/clothes/*/`. FaceFX data: `avatar/avatar_face_fx.bnk` and the intro `.fxe`.
- Shaders: TD_Avatar, TD_Skin2, TD_Clothe2, TD_Hair, TD_Eye, TD_Skinface (`Euro/Shaders`).
- Cinematic linkage: the .sce actor name must equal the entity `__Name`, and its +0x34 u64 is the entity `__Hc`.
  - The Animation track selects an ANIM REFS entry, which must be present in the bank (ANIM hash → `.anm`, sub 8)
    and in the Entity__Cinematic `additionalsAnims`.
  - The Morph track selects FaceFX animations.

### 6.2 .anm ("ANM!") [DATA, partial]

```
"ANM!" u32 0x10, f32 1.6, u32 0x12f(303) | u32 0 | u64 (0 or hash) | "DATA" {size}
DATA: u32 numTracks (65 = bones for a full avatar), f32 duration, f32 duration, u32[3]
track: {u16 kind (1, 3…), u16 0xffff, u32 size, u32 boneNameHash, u16 numKeys, u16 flags, f32 duration,
        u16 times[numKeys] (normalized 0..0xffff over duration), pad to 4,
        values (kind 3: f32 quaternion xyzw per key, then more data)}
"EOF!" trailer
```

Compressed value layouts for the other kinds are **not decoded**: the decoder is edAnim, partly at 0x7cb000, and
not followed further.

---------------------------------------------------------------------------------------------------
## 7. Runtime of a cinematic entity (Entity__Cinematic, class 'CINE') [CODE]

### 7.1 Field offsets

Loader FUN_00b406e0 (xmb entity → runtime fields):

| offset | field |
|---|---|
| +0x610 / +0x614 | CinProps[0] (flags) |
| +0x618 | CinEnd |
| +0x5a8 | StreamCenter |
| +0x5f0 | Znear |
| +0x5f4 | Zfar |
| +0xb70 | DefaultShadowConfig (8-byte id) |
| +0x5a0 | Micro |
| +0x5d0 | Ambiance_SoundEmiter1 |
| +0x5e0 | Ambiance_SoundEmiter2 |
| +0x620 | CinFile count |
| +0xb48 | additionalsAnims |
| +0x5f8 | AvatarsBlendDuration |

`CinCamera` and `TriggerZone` are **not read**: the strings do not exist in the exe.

### 7.2 CinProps bits

| bit | value | meaning |
|---|---|---|
| 1 | 0x2 | **loop**, copied to +0xb5b on load (FUN_00b3f5f0) |
| 2 | 0x4 | skippable / end-allowed; set by code for C_INTRO after the skip prompt, and for C_FIRST_INTRO_E |
| 4 | 0x10 | "no camera / no game-state takeover": skips the camera, shadow config and game-mode notifications |
| 5 | 0x20 | passed to FUN_00d80340(…,(p>>5)&1,…) |
| 6 | 0x40 | when clear, the cinematic sets DAT_00f80168+0x91c = 1 (HUD/flash layer flag) |
| 8 | 0x100 | saves/overrides DAT_01179620 = 0x1f |
| 11 | 0x800 | FUN_00b9a3e0 |

- C_INTRO: CinProps = 0, so it plays once.
- Press start: CinProps = 610 = 0x262 = bits 1, 5, 6, 9, so it **loops**.

### 7.3 Start (FUN_00b3ff00)

- Notifies the listener (event 10).
- If camera control is allowed (bit 4 clear) and the entity has a camera (+0x1e0), FUN_00b3eba0 applies:
  - **near = Znear, or 0.8 if Znear == −1.0; far = Zfar, or 40000.0 if −1.0**. The defaults are the globals
    0x00ffb664/0x00ffb668, whose static initializer at 0x00d777d0 (stores at 0xd78ef4 and 0xd7902e) sets 0.8 (0xedda90) and 40000.0 (0xedd278). No
    other readable code writes them; encrypted code might [ENC].
  - The values go to camera +0x370 (NearProjPlane) and +0x374 (FarProjPlane), and to the globals 0x01195f80/84.
  - It also applies the shadow config (0xfd50fc+0x28 = +0xb70), black stripes off (`/black_stripes`), and so on.
- duration = `*(float*)(*(+0xe4))` (the .cin total duration).
- time(+0x600) = startTime(+0x5fc); player(+0xe0)→vfunc+0x4c(time·…, 0) = **SetTime**.
- Notifies event 5.

### 7.4 Per-frame update (FUN_00b40350, `this->tick(dt)`)

```
if playing(bit2 of +0x60c) && !stopRequested(bit1):
    t += rate(+0xb34) * dt            (only while paused flag +0xb60 == 0)
    if t > duration − 0.001:
        if !loop(+0xb5b): player.SetTime(duration − 0.001); notify(7); stop(FUN_00b3fc40(1))
        else: t = (t <= duration) ? 0.001 : t − duration; restart(FUN_00b402f0(0)); player.SetTime(t, dt)
    else player.SetTime(t, rate*dt)
    if t >= duration − (+0xb30)/1000: notify(6)          (fade-out lead time in ms)
```

### 7.5 Stop and CinEnd (FUN_00b3fa70)

- If stopped while playing: **CinEnd 1 → SetTime(0); CinEnd 3 → SetTime(duration)**, i.e. jump to the final pose
  when skipped. Other values: nothing.
- Then notify 3 (end), restore the camera and so on.

### 7.6 Listener event ids

| id | meaning |
|---|---|
| 0 | start |
| 2 | stop |
| 3 | ended |
| 5 | started |
| 6 | near end (fade) |
| 7 | reached end |
| 8 | MSG string |
| 10 | about to start |
| 11 | ? |

FUN_00a91820 starts a screen fade of length +0xb30/1000 s on event 6.

### 7.7 Skip during C_INTRO (FUN_00da87f0 → FUN_00da7e70)

- While the current cinematic is `C_INTRO`, a timer at gm+0x1b8294 (only when ≥ 0) accumulates dt.
- At **≥ 25.0 s** the flash clip `skip` gets `open` (shows the skip prompt) and C_INTRO's CinProps |= 4 (skippable).
- At **≥ 35.0 s** it gets `close` and the timer = −1.
- The fourcc command `'skip'` (0x736b6970), FUN_00da91c0, sets the timer to −1 and returns 0x736b6901.
- What arms the timer (sets it ≥ 0) was not found in readable code [ENC].

### 7.8 Camera convention

Exact math **[ENC]**: CinScene/Cinematic.cpp, edCinCamState `POSITION_HEADING` / `RELATIVE_POSITION_TARGET_UP`.
Verified against the data [DATA]:

- Track Heading values are stored **x, y, z, w**.
- **Camera forward = R(q*)·(0,0,+1)**, where q* = conjugate, with the standard q·v·q⁻¹ rotation. Up = R(q*)·(0,1,0).
  The equivalent D3D left-handed camera-to-world rotation is R(q*) with +Z forward. So the current OpenTDU2
  `cameraWorld = T · R(conj q) · RotY(180°)` for a right-handed −Z view is **correct**.
- Evidence:
  - Press start: c_01, c_02, c_03 forward = (0.94, 0.09, −0.33), (0.99, 0.05, −0.16), (0.78, −0.03, −0.63) versus
    the direction to CIN_CAR_01 (0.78, −0.09, −0.62), (0.97, −0.18, −0.15), (0.77, −0.12, −0.62). Up is ≈ (0, 1, 0).
  - R(q)(0,0,±1) points away from the car or flips z.
  - Intro: the first frame looks 51° down at the sea and heads toward the coast, matching the user's frame.
- Position: world = origin(DATA+0x28) · local position.
- **Fov**: the .sce stores degrees (55/60/50/30/28).
  - The camera object stores radians: Entity__Camera `Fov = 1.0472`, and the default camera Fov is
    DAT_00f49b10 = 1.0471976.
  - FUN_008b39d0 (set fov): fov·0.5 → +0xec = f(half), +0xe4/+0xe8 = two trig values of half, +0xf0 = (+0xec)²,
    +0xf4 = g(half, +0xdc·(+0xec)).
  - [INF] f = tan, g = atan2 and +0xdc = aspect. Then fov is the **vertical** FOV and the horizontal half-angle is
    atan(aspect·tan(vfov/2)). The trig imports are behind the SecuROM IAT, so the functions cannot be named for sure.
- Near/far: cinematic default **0.8 / 40000** (§7.3). OpenTDU2's 0.5 / 12000 differs.
- Interpolation between keys: [ENC]. Keys are 50 Hz, so linear position plus nlerp/slerp is visually exact.

---------------------------------------------------------------------------------------------------
## 8. Ocean [CODE+DATA+SHADER]

- **Mesh**: not generated in readable code. It comes from the level banks:
  - `islands/ibiza/level/ibiza.bnk` → `ibiza-o.{3dd,3dg,2dm}` (whole-island far ocean, material "Ocean");
  - each `area-X-Y.bnk` → `area-X-Y-o.*` (area ocean tiles plus port/rock objects).
  - 25 banks contain the GUID [DATA, scan].
- **Material setup**: FUN_00985b50 and FUN_009927e3 walk loaded materials; for GUID 0x0cea0000_00000002 (TD_Ocean)
  they call **FUN_008be1e0**. That function:
  - creates the material parameter `AREASIZE` ('AREA','SIZE', float2, from area dims +0xf98/+0xf9c);
  - sets render states (+0x33=2, +0x3a=6, +0x38=5, +0x39=6, +0x31=1, +0x53=7, …);
  - sets 4 texture slots (+0x100…, stride 0x20) with sampler states (3/3/2, 1/1/2/2, 2/2/2/2, slot 3 = level object
    +0x670);
  - sets the material bucket from the level mode (0, 1, 2).
- **Reflection**: FUN_00c32810 creates `Ocean_RTRefl` 256×256 (format 2) and `Ocean_DBRefl` 256×256 depth (0x40).
  TD_Ocean's `reflMat` (REFLMAT) projects into it.
- **Normal map**: FUN_00dd6cf0 builds a `NoiseNMap` 256×256 render target:
  1. `NoizComp` pass (td_noisecompose, GUID facaa9ca_cabdbaaf);
  2. `NoizAss` pass (td_noiseassembly, facaa9ca_ca2dbaaf);
  3. `NoiseNMap` pass (**TD_ocean_nmap_from_hmap**, GUID 0cea0000_00fe0001), with `MATPARAM` = (1/w, 1/h) texel
     size (DAT_00f44c70 / RT size).
- **Shader TD_Ocean** (25 permutations):
  - VS constants: `__World_View_Proj`, `__World`, `__World_View`, `__DirLight_Dir_0`, `__CamPos_ObjSpace`,
    `__CamDir_ObjSpace`, `reflMat`, `matParam`, `ShiftWorld`.
  - PS: `__AmbLight_Ambient_0`, `__DirLight_Diffuse_0`, `matParam[4]`, samplers `samplerReflection` s0,
    `samplerNormalMap` s1, `samplerColorMap` s3.
  - Material symbols: REFLMAT, MATPARAM, AREASIZE, `..IFTWOR` [SHADER, otdu2_sob_tool]. The 2DM also carries a UV
    animation (`UVAA`/`MTPA`).
- **Weather `Intro` (cinematic_weathers.xmb) ocean values** [DATA]:

| parameter | value |
|---|---|
| ocean_NMapHR | (0.15, 1) |
| ocean_NMapLR | (0.015, 1) |
| ocean_NMapScaler | 0.13 |
| ocean_fresnel | (0.2565, 11.087, 0.7) |
| ocean_reflectionColor | (0.12, 0.23, 0.18) |
| ocean_specularPower | 50 |
| ocean_reflectionBluriness | 0.11 |
| ocean_attenuationBluriness | 0 |
| ocean_attenuationFresnel | 0 |
| ocean_specularColorAndCutOff | (0.78, 0.66, 0.80, 0) |
| sundirection | (0.613, −0.413, 0.483) |

  The weather parameter parser is FUN_00af30d0. The mapping of these parameters onto shader constants was not traced.

---------------------------------------------------------------------------------------------------
## 9. Press-start / title-screen cinematic (GMBootMenu) [CODE+DATA]

- **Bank**: FUN_009c0fc0 / FUN_009ca5a0 load `Scenarics` / `press_start` → `islands/<island>/scenarics/press_start.bnk`.
- **Ibiza bank** contains 4 cinematics:

| entity | .cin | weather |
|---|---|---|
| `C_01_3332` | M_IBIZA_01 | SS_Death_Valley |
| `C_02_3347` | M_IBIZA_02 | SS_Old_Town |
| `C_04_5067` | M_IBIZA_04 | SS_Chapelle |
| `C_05_4115` | M_IBIZA_05 | SS_Arche |

  All have Priority 1.5, CinProps 610, CinEnd 3, Znear/Zfar −1, and one scene each.
- **Choice** (FUN_009c6b20, called from the boot-menu enter FUN_009c7ea0):
  - count the CINE entities whose name does **not contain** `"C_LIFE"`;
  - `srand(seed)` with the seed from object 0x013677b0's vfunc+0xc, then `idx = rand() % count`;
  - the command-line switch **`FORCELOGINCINEINDEX n`** (handler FUN_00d68610 → DAT_00f76d14) overrides `idx`
    when 0 ≤ n < count;
  - take the idx-th such entity in entity-array order;
  - **parse the last 4 characters of its name as an integer**: "3332" → area (3,3), sector (3,2). These are the
    `local_70`/`local_68` outputs, used to stream the world around the car.
- **Start**: the entity is stored at gm+0x1ae310, and FUN_009c7ea0 sets entity+0x11e = 1 and entity+0x120 =
  (stream position − island offset). When the boot menu's vfunc+0x24 (world ready) returns true, the tick at
  0x9ca1b1 calls FUN_00b40180 once and clears the pointer.
- **Looping**: CinProps bit 1 → +0xb5b = 1 → FUN_00b40350 wraps time at 60 s and restarts. The cinematic **loops
  forever**. CinEnd 3 applies only when it is stopped (jump to the end pose).
- **Shots** [DATA] (M_IBIZA_01 .cin, 60 s): c_01 for 0–20, c_02 for 20–40, c_03 for 40–60.
  - Each record has scene 0, camT = 0 → 20 and fields +0x14/+0x18 = 0.
  - Cameras: Fov 60 / 60 / 50; positions keyed every ~1 s (20–21 keys); one Heading key, so a static orientation
    with slow translation.
  - Message track: weat:SS_Death_Valley for 0–20, wmod:_02 for 20–40, wmod:_03 for 40–60.
  - CIN_CAR_01 also has a `Message` track (ref 1, 77.72 s).
- **Cuts**: nothing in readable code restricts playback to one shot. Per the data the engine cuts c_01 → c_02 →
  c_03 every 20 s and changes the weather modifier with each cut. If the real game appears to show one stable
  camera, it is because the player normally presses start within the first 20 s (c_01 slowly translates about 2 m).
  The shot-switch code itself is [ENC].
- **No cinematic**: when no cinematic is found, the boot menu uses camera FUN_00ca0840(0) with Fov 1.0472 rad and
  a default position (FUN_009c7ea0 else branch).
- **Camera math**: identical to §7.8 (forward R(q*)·+Z, Fov in degrees → radians, near 0.8, far 40000).

---------------------------------------------------------------------------------------------------
## 10. Files needed for a faithful reimplementation

All of these are now in the container under `/mnt/user-data/uploads/TestDrive2/Euro/...`:

| file | content | status |
|---|---|---|
| `Bnk/scenarics/cinematics/intro/new_intro.bnk` | cinematics, scenes, house, anims, all intro sounds | present before |
| `Bnk/scenarics/cinematics/intro/fade_freeride.bnk` | | present before |
| `Bnk/frontend/hires/intro.bnk` | credits flash movie | present before |
| `Bnk/scenarics/cinematics/intro/new_intro__us.bnk` | localized party voices (EN); other languages `__fr/__ge/__it/__sp` on the Mac | **copied now** |
| `Bnk/avatar/config/pnj.bnk` | PNJ `.avt` configs, e.g. STO_Party_* | **copied now** |
| `Bnk/avatar/config/styles.bnk` | | present |
| `Bnk/avatar/common_bodies.bnk` | avatar body/head meshes and skeletons | **copied now** |
| `Bnk/Video/Bumper_ATARI.mkv` | AVI, H.264 + MS-ADPCM | **copied now**; `Bumper_EDEN_TDU.mkv` (12.9 MB) and `Teaser_TDU2.mkv` (93 MB) left on the Mac |
| `Bnk/islands/ibiza/fx/cinematic_weathers.xmb`, `cinematic_modifiers.xmb` | weather `Intro` and `Intro_xx` modifiers | present |
| `Bnk/islands/ibiza/level/ibiza.bnk` and `area-*/area-X-Y.bnk` | ocean meshes `*-o.*` | present |
| `Bnk/islands/ibiza/scenarics/press_start.bnk` | | present |
| `Shaders/TD_Ocean.sob`, `TD_ocean_nmap_from_hmap.sob`, `td_noisecompose.sob`, `td_noiseassembly.sob`, `TD_Avatar/TD_Skin2/TD_Clothe2/TD_Hair/TD_Eye.sob` | | present |

Still on the Mac only, needed for clothed avatars:
- `Euro/Bnk/avatar/clothes/*` (casino, common_clothes.bnk, default, eden, fashion, hair, helm, highlife, pnj,
  sportswear);
- `avatar/avatar_face_fx.bnk`;
- `common_bodies_light.bnk`.

No `.big`/`.map` entries are needed for the intro, and `Audio.cpr` is not needed.

## 11. Corrections to existing OpenTDU2 knowledge

1. `.sce` object +0x10 is the **track count**. The kind comes from the class hash at +0x08 (0x2a043536 camera,
   0xd039fa46 actor).
2. `.cin` shot records:
   - start at track `times[]`;
   - the camera name is a fold4 hash at +8, so ASCII only for names of 4 characters or fewer;
   - the duration is at +0, and the camera time range is +0xc..+0x10;
   - parse the track structure instead of scanning for "c_".
3. The `0x73d8ccae` track is the generic **Message** track (any MSG ref: weat/wmod/shad/flsh/snd/zc..), not only
   weather. Its value is a **REFS index**, not an index into a weather list.
4. Default cinematic near/far are 0.8 / 40000 (0.5 / 12000 is not in the data/code read).
5. The press-start cinematic loops (CinProps bit 1) and picks one of the 4 cinematics at random
   (FORCELOGINCINEINDEX to force one).
