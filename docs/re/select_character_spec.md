# TDU2 "Select your character" (AVATAR_CHOICE after C_INTRO): reimplementation spec

Tags: [CODE] read from readable x86 (>= 0x6c1000) · [DATA] read from game files · [INF] inferred (reason given) ·
[ENC] behind SecuROM-encrypted code (< 0x6c1000), not read.

Sources:
- Decompiled code: `/tmp/claude-0/selchar_dec1.c` and `/tmp/claude-0/selchar_dec2.c`, plus the earlier
  `/tmp/claude-0/intro/range_da7.c`. Raw disassembly: `/tmp/claude-0/text.asm` (FUN_00da8d90 at line 2397537).
- Data: `Euro/Bnk/scenarics/cinematics/intro/new_intro.bnk`, already extracted in `/tmp/claude-0/intro/x/`.
  The entity dump is `/tmp/claude-0/intro/entities.txt` and the cinematic dump is `/tmp/claude-0/intro/cinematics.txt`.
- Flash: `Euro/Bnk/frontend/hires/intro.bnk` → `intro.vmf`, already extracted in `/tmp/claude-0/intro/fe/`.
- Tools:
  - **`/tmp/claude-0/py/cindump2.py`** is `cindump.py` with the track-offset bug fixed (§2.1).
  - `scedump.py` and `sceanim.py` dump scenes and their Animation tracks.
  - `vmf2.py` dumps the flash movie.
  - A DefineButton2 action decoder (BUTTONCONDACTION with key codes) is in
    `/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad/btn.py`
    (usage: `python3 -I btn.py intro.vmf`).

---------------------------------------------------------------------------------------------------
## 0. Summary of the whole sequence

```
C_INTRO (52.62 s)
  45.919  snd:timestop  -> S_TimeStop emitter (s_timestop_CINEMATIC.wav, 10.28 s)          [DATA+CODE]
  47.62   shad:AVATAR_CHOICE, shot c_intro_13 (rate 1)                                      [DATA]
  50.92   last shot: 1.70 s of real time cover 0.34 s of scene time  => rate 0.2 ("slow motion")  [DATA]
  52.62   end (listener event 7)  -> FUN_00da8040                                            [CODE]
            gm.index = 0
            play C_AV_01         (main cinematic: camera on candidate 1, loops every 1 s)
            play C_AVATAR        (background: dancers + slow music, CinProps|=0x10 no camera, loop,
                                  1200 s shot mapped to scene 0..240 s => rate 0.2)
            flash INTRO.gotoAndPlay("avatar") -> pop_avatar "open" ("Select your character" UI)
CHOICE loop
  right (flash nextAvatar) -> FSCommand "nxav" -> C_<i>TO<i+1> (0.5 s rail move) -> end -> C_AV_0<i+1>
  left  (flash previousAvatar) -> "prav"     -> C_<i>TO<i-1> (0.5 s rewind rail) -> end -> C_AV_0<i-1>
  /:IsTransition = 1 while the current cinematic is not C_AV_xx; flash ignores input while != 0
VALIDATE (Enter, or click on the bar) -> flash close anim (14 frames = 0.467 s) -> FSCommand "crav"
  S_TimeStart (s_timestart_CINEMATIC.wav 1.61 s); gm.selected = index
  FUN_00da8d90: stop C_AVATAR; start C_WALKMODE (background, loop, rate 1);
    release the six candidate avatars; build the player avatar from DBPNJ "AVT_<Male|Female><''|_Afrik|_Asia>_Intro";
    /:PlayerName; play C_FIESTA_INTRO_0<selected+1> (6.37 s)
C_FIESTA_INTRO_0N: 1.5 s fade from white [INF]; flsh:generik4 ("Starring <PlayerName>") at ~0.5 s;
  snd:music at 1.0 s (the party music restarts at normal speed); shot 1 on "Avatar" (the player)
  doing fi_selectavatar-m/-w; shot 2: Tess waves (fi_hello_tess)
  end (event 7) -> walk mode: camera from entity cam_entree_0N, flash label "wm_home", cursor on.
WALK MODE in the house -> trigger volume "Tess" -> C_TESS (9.22 s) -> end: tutorial "TUTO_GO_TO_THE_GARAGE",
  objective text "Go to the garage" (loc 5108)
```

---------------------------------------------------------------------------------------------------
## 1. Game-mode object (AVATAR_CHOICE, vtable 0x00f29d60) [CODE]

### 1.1 Vtables

Secondary vtables:
- 0x00f29dcc: {0x4d7760, FUN_00da7f40 (/:IsCasinoSaveData)}
- 0x00f29dd4: {0x4d7770, **FUN_00da91c0 = FSCommand handler**}, with `this` = gm+8
- 0x00f29ddc, with `this` = gm+0x90:
  - slot 3 FUN_00da9690
  - slot 4 FUN_00da8770 (/:IsWalkmode=false)
  - **slot 5 FUN_00da87f0 = per-frame tick**
  - slot 7 **FUN_00da9560 = exit**

Main vtable 0x00f29d60:

| slot | function | role |
|---|---|---|
| 1 | **FUN_00da8040** | cinematic listener (`entity+0x590` = gm; called as vfunc+4(event, cine, msg)) |
| 16 | FUN_00da7fb0 | |
| 18 | **FUN_00da92e0** | mode start (intro_spec §0) |

Constructor/reset: FUN_00da8a40.

### 1.2 Fields (dword index → byte offset in gm)

| field | offset | init | meaning |
|---|---|---|---|
| `gm[0x6e09c]` | +0x1b8270 | −1 | **current candidate index** (0..5). Set to 0 at the end of C_INTRO; −1 after validation |
| `gm[0x6e09d]` | +0x1b8274 | 0 | **current "main" cinematic** (C_INTRO, C_AV_xx, C_xTOy, C_FIESTA_INTRO_xx, C_TESS) |
| `gm[0x6e09e]` | +0x1b8278 | 0 | **background cinematic** (C_AVATAR, then C_WALKMODE) |
| `gm[0x6e09f]` | +0x1b827c | 0 | Tess_Wintory avatar instance (found at the end of FIESTA) |
| `gm[0x6e0a0]` | +0x1b8280 | 0 | byte: C_TESS already started |
| `gm[0x6e0a1]` | +0x1b8284 | −1 | **selected candidate index** (0..5), set by "crav" |
| `gm[0x6e0a2]` | +0x1b8288 | 0 | entity of class 0x56454c55 named "Tess" (trigger volume for C_TESS) |
| `gm[0x6e0a4]` | +0x1b8290 | 0 | casino-import avatar index |
| +0x1b8294 / +0x1b8298 | | | C_INTRO skip timer / skip prompt shown (intro_spec §7.7) |
| `DAT_013e272c` | | | 0 at mode start, 1 at the end of C_INTRO (consumer not traced) |

### 1.3 Helper FUN_00da7fd0(name, bStopPrev) = "play main cinematic" [CODE]

```
cine = FindEntity(world, 'CINE', name); if (!cine) return
if (gm.mainCine) { if (bStopPrev) Stop(gm.mainCine, 1) /*FUN_00b3fc40*/ ; gm.mainCine = 0 }
cine.listener(+0x590) = gm; Start(cine, startTime = 0.0) /*FUN_00b402a0*/; gm.mainCine = cine
```

A forced stop (FUN_00b3fc40(1)) notifies events 2 and 3 only. Only a natural end of a non-looping cinematic
notifies **event 7** (intro_spec §7.4), and event 7 is what chains the next step.

### 1.4 Per-frame tick FUN_00da87f0 [CODE]

1. If mainCine == C_INTRO: run the skip timer FUN_00da7e70 (intro_spec §7.7).
2. **If gm.index ≥ 0 and mainCine ≠ 0: FUN_00da7d30** sets the flash variable `/:IsTransition` (movie "INTRO",
   variant type 7 = number):
   - first to 1;
   - then to 0 if `strstr(mainCine.name, "C_AV_")`.
   So **IsTransition = 0 while a C_AV_xx loop is current, and 1 during a C_xTOy transition**.
3. Tess trigger, when all of the following hold:
   - !gm.tessStarted, gm.tessAvatar and gm.tessTrigger are set;
   - player state `*(DAT_00fd51c0+0x420)+0x2e8 == 9` (walk mode [INF]);
   - not (DAT_00fd960c && byte +0x345);
   - FUN_00b40e50(0, 0) is true. This is a point-in-volume test with box, sphere and cylinder shapes, cases 0..5.

   Then FUN_00da7fd0("C_TESS", 1), tessStarted = 1, flash `cursor` hidden, and mainCine.CinProps |= 4.
4. Base update FUN_009bde30, then FUN_00a4ee30.

### 1.5 Cinematic listener FUN_00da8040(event, cine, msg) [CODE]

| event | cinematic | action |
|---|---|---|
| 0 (start) | C_INTRO | stop emitters S_WALKMODE_Music, _01, _02, _03 (FUN_009bdc80) |
| 7 (end) | **C_INTRO** | Runs the following:<br>`DAT_013e272c = 1`<br>`FUN_00da7fd0("C_AV_01", 0)`<br>`gm.index = 0`<br>`a = Find('CINE', "C_AVATAR")`<br>`FUN_00b3f7a0(a)`<br>`a.CinProps(+0x610) \|= 0x10` (no camera / no game takeover)<br>`a.loop(+0xb5b) = 1`<br>`a.listener = gm`<br>`Start(a, 0)`<br>`gm.bgCine = a`<br>`flash.gotoAndPlay("avatar")` (vfunc 0xdc on the intro movie) |
| 7 | name contains "C_FIESTA_INTRO" | see the walk-mode bullets below the table |
| 7 | "C_TESS" | Runs the following:<br>FUN_0094fdb0<br>flash `cursor` visible<br>`DAT_0117a038 \|= 0x100`<br>FUN_00afbba0(0)<br>FUN_00d61790(loc 0x13f4 = 5108 "Go to the garage")<br>tutorial `TUTO_GO_TO_THE_GARAGE` (FUN_00b09f50 → FUN_00b0a090) |
| 7 | name contains `"C_%dTO"` with %d = gm.index+1 | `sscanf(name, "C_%dTO%d", &from, &to)`; `gm.index = to−1`; `FUN_00da7fd0("C_AV_%02d" % to, 0)` |
| 8 (MSG) | | `flsh:` → intro movie label (generik5 also sets /:generik_date); `snd:timestop` → S_TimeStop; `snd:music` → start the 4 S_WALKMODE_Music emitters; anything else (weat/wmod/shad/zc..) → FUN_009be090 → FUN_00a91820 |

When C_FIESTA_INTRO ends (event 7, walk-mode start):
- It looks up `Tess_Wintory` in the avatar list (DAT_00fd9004) and stores it in gm[0x6e09f].
- It calls gm vfunc 0x50 [INF: enters walk mode].
- For every entity of class 0x5f43414d ('_CAM') named `"cam_entree_%02d" % (gm.selected+1)`, it copies the entity
  matrix (+0x10) into gm+0x1adc90 (`gm+0x6b724*4`) via FUN_00950060 and FUN_009517c0. [INF: this is the walk-mode
  camera start pose]
- If the INTRO flash movie is ready (state +0x28c == 1), it plays `flash.gotoAndPlay("wm_home")`.
- Then `DAT_0117a038 |= 0x100`, FUN_00afbba0(0) and FUN_00afa050().

### 1.6 FSCommand handler FUN_00da91c0(this = gm+8, …, uint fourcc) [CODE]

The fourcc packs the 4 command characters big-endian: "skip" = 0x736b6970.

| command | fourcc | action |
|---|---|---|
| **`nxav`** (next) | 0x6e786176 | Runs the following:<br>if index > 4: return 0 (no wrap)<br>`sprintf(s, "C_%dTO%d", index+1, (index+1)%6+1)`<br>`FUN_00da7fd0(s, 1)` |
| **`prav`** (previous) | 0x70726176 | Runs the following:<br>if index < 1: return 0<br>`sprintf(s, "C_%dTO%d", index+1, (index−1)%6+1)` (= index)<br>`FUN_00da7fd0(s, 1)` |
| **`crav`** (validate) | 0x63726176 | Runs the following:<br>start Entity__Fx `"S_TimeStart"` (FUN_009bddf0)<br>`gm.selected = gm.index`<br>`FUN_00da8d90(gm, gm.index)`<br>`gm.index = −1` |
| `gain` | 0x6761696e | `(DAT_01025608+0x9010)->vfunc+8(1)`. This is a walk-mode command from the root `action` function, not part of the choice. |
| `skip` | 0x736b6970 | skip timer = −1 (intro_spec §7.7) |
| other | | base FUN_009be530 |

Note: gm.index changes only when the transition ends (event 7). The flash `num_avatar` changes as soon as the key
is pressed. Both stay consistent because the flash refuses input while `/:IsTransition != 0`.

### 1.7 Validation FUN_00da8d90(gm, sel) [CODE, from the disassembly at 0xda8d90..0xda91ae]

```
if gm.bgCine: Stop(gm.bgCine, 1)                                     // C_AVATAR stops (and its music loop)
w = Find('CINE', "C_WALKMODE"); if w: FUN_00b3f7a0(w); w.CinProps |= 0x10; w.loop = 1; w.listener = gm;
                                     Start(w, 0.0); gm.bgCine = w
FUN_00da8b20(): for each avatar instance in DAT_00fd9004 whose name starts with 'W' or 'M' and whose id64
                (+0x220/+0x224) != player id (0x8193c347, 0x9d785ed9): release (FUN_00b84530)
                -> the six candidates (WOMAN_04..06, MAN_01..03) disappear
movie = flash "INTRO"
sex = movie.GetVariable("/:WhatSex", 32)          // "male" / "female"
eth = movie.GetVariable("/:WhatEthnicGrp", 32)    // "caucasian" / "african" / "asiatic"
isFemale = sex[0]=='f'
e = (eth[1]=='a') ? 0 : (eth[1]=='s') ? 1 : 2     // 'c[a]ucasian'=0, 'a[s]iatic'=1, 'a[f]rican' (and default)=2
sexStr = isFemale ? "Female" : "Male";  ethStr = {0:"", 1:"_Asia", 2:"_Afrik"}[e]
name = sprintf("AVT_%s%s_Intro", sexStr, ethStr)    // e.g. "AVT_Female_Asia_Intro"
FUN_00cbd680(&cfg); FUN_00cb5ee0(0x11ae180, &cfg, name)   // DBPNJ lookup by _configid -> FUN_00cb5bd0: .avt + clothes
FUN_00445920(src=&cfg, dst=profile+0x30)            [ENC] copy into the profile's avatar configuration
FUN_00acee30(0)
pa = FUN_00b83860(DAT_00fd9004)  // player avatar instance (id 0x8193c347/0x9d785ed9)
FUN_00b85cb0(pa); FUN_00b8baf0(pa, profile+0x30, 1, 1, 1)  // rebuild the player avatar from the profile config
if DAT_00fd95e0: FUN_0094fae0()
movie.SetVariable("/:PlayerName", string(profile+0xc1))     // variant type 3 (string)
if casinoImport(DAT_00fd954a): ... CLOTH_SET_WON ... (casino-avatar clothes, partly [ENC])
s = sprintf("C_FIESTA_INTRO_%02d", gm.selected+1); c = Find('CINE', s)
if c: { if gm.mainCine: Stop(gm.mainCine, 1); gm.mainCine = 0 } c.listener = gm; Start(c, 0.0); gm.mainCine = c
```

### 1.8 Start and exit [CODE]

- Start (FUN_00da92e0) is in intro_spec §0. In addition:
  - it finds the entity of class 0x56454c55 named "Tess" (→ gm[0x6e0a2]);
  - it sets `DAT_013e272c = 0`;
  - it sets `/:IsDreaming = true` on the "MAIN" movie.
- Exit (FUN_00da9560):
  - `DAT_0117a038 |= 0x100`;
  - Stop(mainCine, 1);
  - `DAT_011f8cc4 = 0`;
  - `/:IsDreaming = false`;
  - FUN_009be6c0.

---------------------------------------------------------------------------------------------------
## 2. Cinematics of the choice (new_intro.bnk) [DATA]

### 2.1 .cin parser correction

- Tracks start at **DATA+0x3c + 8·numScenes** (numScenes = DATA+0x0c), not at the fixed 0x4c.
- 0x4c only works for C_INTRO and FIESTA, which have 2 scenes. With 1 scene (C_AVATAR, C_AV_xx, C_xTOy,
  C_WALKMODE, C_TESS) the tracks start at 0x44.
- `/tmp/claude-0/py/cindump2.py` contains the fix.

### 2.2 Entities and files

All entities share these values [DATA]:
- orient_pos_scale: rot (−0.613733, 0.004198, 0.032286, 0.788842), pos (−28.6699, 17.6492, 21.316);
- Priority 1.5, CinEnd 3, Znear/Zfar −1, `DefaultShadowConfig = "CINE_DEFAULT"`;
- .sce origin matrix = identity.

| entity | CinProps | .cin | .sce | duration | shot(s): camera, camT | message tracks |
|---|---|---|---|---|---|---|
| **C_AVATAR** | 114 = 0x72 (bits 1 loop, 4 no-camera, 5, 6) | intro_lph_l3di_M_AVATAR_TOTEM | M_AVATAR_avatar (362.1 s, 41 objects) | **1200 s** | 1 shot 1200 s, cam `NO_CAMERA` (0x95a49cd0), **camT 0 → 240** | FX: ref 0 = Fx__Root `FxS_TimeStart` (0x7ea0ae6d6719781a) → SourceData "C_AVATAR" → **c_avatar_CINEMATIC.wav (7.38 s)**, duration FLT_MAX, flags 0x27 |
| **C_AV_01** | 2 (loop) | M_AV_01 | M_AV_01_Choice (1.0 s) | 1.0 s | `c_av_01`, 0 → 1 | `weat:Avatar`, `zcvn` |
| **C_AV_0N** (N = 2..6) | 2 (loop) | M_AV_0N | M_AV_0N_Choice (all 6 files are identical, md5 58b4873f…) | 1.0 s | `c_av_0N`, 0 → 1 | `zcvn` |
| **C_kTO(k+1)** | 0 | M_kTO(k+1) | M_kTO(k+1)_Choice | **0.5 s** | `c_choice_rail` (0x36273a9d), camT [0.5(k−1), 0.5k] | wmod (table §2.4), `zcvn` |
| **C_kTO(k−1)** | 0 | M_kTO(k−1) | M_kTO(k−1)_Choice | **0.5 s** | `c_choice_rewind` (0x448fa89a), camT [0.5(6−k), 0.5(7−k)] | wmod (table §2.4), `zcvn` |
| **C_FIESTA_INTRO_0N** | 0 | M_FIESTA_INTRO_0N | `…fiesta_intro` (scene 0, 6.32 s) + `…fiesta_intro_0N` (scene 1, 3.52 s) | **6.37 s** | see the shot list below | `flsh:generik4` at 0.505/0.538/0.549/0.544/0.555/0.555 (N = 1..6); `wmod:Avatar_Avatar` at 0; `snd:music` at 1.0; `zcvn` at 0 |
| **C_WALKMODE** | 0 (the code forces 0x10 + loop) | M_WALKMODE | M_WALKMODE_walkmode (30 objects) | 240 s | `NO_CAMERA`, 0 → 240 (rate 1) | |
| C_TESS | | M_TESS | M_TESS_Tess | 9.22 s | 4 shots on 2 cameras | `zcne` |

C_FIESTA_INTRO_0N shots:
1. scene 1, `c_fiesta_intro`, 0 → 3.53 s. The record has **+0x14 = 1, +0x18 = 1.5, +0x1c..+0x24 = (1, 1, 1)**.
2. scene 0, `c_fiesta_02`, camT 3.48 → 6.32, duration 2.84 s.

Transitions that exist:
- forward: 1TO2, 2TO3, 3TO4, 4TO5, 5TO6;
- backward: 2TO1, 3TO2, 4TO3, 5TO4, 6TO5.
- There is **no 6TO1 or 1TO6**: the list does not wrap, which matches the code (§1.6).

### 2.3 Shot-time mapping and the slow motion

Shot record fields are in intro_spec §2.

**Scene time inside a shot** [DATA + INF]:

```
sceneTime = camStart + (t − shotStart) · (camEnd − camStart) / shotDuration
```

The rate `(camEnd−camStart)/shotDuration` is 1 for almost every shot. The exceptions are exactly the "time stop"
moments:

| shot | duration | camT range | rate |
|---|---|---|---|
| C_INTRO last shot (50.92 s, c_intro_13) | 1.70 s | 20.068 → 20.408 | **0.2** |
| C_AVATAR | 1200 s | 0 → 240 | **0.2** |
| C_WALKMODE (after validation) | 240 s | 0 → 240 | 1.0 |

- The C_AVATAR scene content fits this mapping. Every Animation key spans scene 0 → ~241–259 s, every Morph track
  ends at 237.7–240.8 s, and the rail cameras span 0 → 240 s. A scene time running in 0..240 s matches; a 1200 s
  real-time span would not.
- **So the slow motion of the dancers and the candidates is the scene being sampled at 0.2× real time** (C_AVATAR
  loops after 1200 s real time = 240 s scene time).
- The engine code that does this mapping is in CinScene.cpp [ENC]. The formula is [INF], from the three
  consistent cases above.
- OpenTDU2 `IntroCinematic::updatePnjs()` currently uses `CameraStart + (time − Start)`, without the rate. This
  must change.
- **Candidates are slowed as well, by the data**: the six candidates are ordinary actors of the same C_AVATAR
  scene, with no different track. If the real game showed the focused candidate dancing at full speed, nothing
  readable explains it. Check this in-game before adding a special case.

Other shot-record fields:
- +0x14/+0x18 (+0x1c..+0x24) [INF]: a start transition (type 1 = fade, duration, RGB colour).
  - C_INTRO's first shot: (1, 4.0), with the colour fields at 0 or garbage → a 4 s fade in from black.
  - C_FIESTA_INTRO's first shot: (1, 1.5, colour 1, 1, 1) → **a 1.5 s fade from white** as time restarts.
  - The values are consistent, but the code is [ENC].
- Camera keys:
  - rail/rewind: positions at 50 Hz (51 keys/s), headings 7–15 keys/s;
  - C_AV cameras: one key each;
  - Fov **30°** for every choice camera, 35° for the c_fiesta cameras;
  - near/far 0.8 / 40000 (intro_spec §7.3).
  - Use linear position plus slerp/nlerp, as for C_INTRO.

### 2.4 Weather during the choice [DATA]

- `weat:Avatar` (cinematic_weathers.xmb) is set by C_AV_01. Its values:
  - sundirection (0.4783, −0.3087, −0.2043);
  - sun_diffuse (2.055, 1.522, 0.957), sun_ambient (0.502, 0.580, 0.769), sun_specular (1.914, 1.443, 1.082);
  - photoshop_bloom_intensity 0.15;
  - **photoshop_crappymode_enable 1 with oofdepth_blend (0, 20, 1.2)** = the depth-of-field blur of the background;
  - softlight 0.25 / 1.5, overlay 0.6, Skydome_haze 0.7;
  - avatar_sun_* as dumped by `weather.py`.
- Each transition applies a `wmod:` modifier (cinematic_modifiers.xmb) that **only changes `sundirection`**.
  The resulting light per candidate position is:

| position (num_avatar) | modifier | sundirection |
|---|---|---|
| 1 | (base Avatar) / Avatar_Avatar (via 2TO1) | (0.4783, −0.3087, −0.2043) |
| 2 | Avatar_01 (1TO2, 3TO2) | (0.82608, −0.66086, 0.1087) |
| 3 | Avatar_02 (2TO3, 4TO3 has no wmod) | (0.29566, −0.73044, 0.25218) |
| 4 | Avatar_02 (3TO4, 5TO4) | (0.29566, −0.73044, 0.25218) |
| 5 | Avatar_03 (4TO5, 6TO5) | (0.7, −0.84348, 0.3) |
| 6 | Avatar_04 (5TO6) | (−0.09566, −0.4, 0.30434) |

- The modifier is applied when the 0.5 s transition starts and lasts until the next one. C_AV_02..06 carry only
  `zcvn`.
- `zcvn` / `zcne` are zone/streaming messages handled by encrypted code [ENC]. They can be ignored in the house.

### 2.5 Cameras (scene space; the identity scene origin is then mapped by the cinematic entity transform as in C_INTRO) [DATA]

| camera | position | Heading quaternion (x, y, z, w) | target actor (`c_av_0N_Target`) |
|---|---|---|---|
| c_av_01 | (1.383, 32.73, 7.171) | (−0.05028, 0.783, −0.05998, 0.6171) | (−0.5092, 32.42, 6.716) |
| c_av_02 | (2.804, 32.06, 6.288) | (0.0316, 0.8479, 0.04218, 0.5275) | (0.8001, 32.30, 5.300) |
| c_av_03 | (3.341, 32.72, 5.342) | (0.0237, −0.9253, 0.08111, −0.3697) | (2.147, 32.42, 4.089) |
| c_av_04 | (4.087, 32.65, 2.502) | (−0.01573, 0.8295, −0.05521, 0.5556) | (2.530, 32.47, 1.865) |
| c_av_05 | (4.295, 32.03, 0.05796) | (0.0415, 0.7268, 0.04131, 0.6844) | (1.729, 32.33, −0.09689) |
| c_av_06 | (2.808, 32.44, −1.030) | (0.03879, −0.9971, 0.01375, −0.06349) | (2.654, 32.40, −2.250) |

- `c_choice_rail` is a 2.5 s path c_av_01 → c_av_06, keyed in the M_kTO(k+1)_Choice scenes (each file holds only
  its 0.5 s segment, with the global rail times). Its end points equal the c_av positions exactly.
- `c_choice_rewind` is the reverse path c_av_06 → c_av_01, in the same layout.
- The `_Target` actors and `c_choice_*_Target` are path helpers. The cameras have full Heading keys, so use the
  Heading track [INF: POSITION_HEADING mode].
- M_AVATAR_avatar.sce also contains rail/rewind versions spread over 240 s, but C_AVATAR's shot uses `NO_CAMERA`
  and CinProps bit 4, so they are never viewed.
- `NO_CAMERA` sits at (−19.44, 57.23, 0.0498), Fov 25, and is unused.

---------------------------------------------------------------------------------------------------
## 3. The six candidates [DATA + CODE]

The order is fixed by the flash `setAvatarConfig` (§4.2). The DBPNJ configs are in avatar_spec §3.1:
`<_configid>` → `.avt` from Avatar/CONFIG/PNJ.bnk plus the DBPNJ clothes. The positions are the C_AVATAR .sce
actors (one static key; floor y = 31.11). The Heading is a pure yaw.

| num_avatar | C_AV / camera | entity (C_AVATAR actor) | /:WhatSex, /:WhatEthnicGrp | ConfigName (= player config name built in §1.7) | .avt | position | yaw | C_AVATAR anim (loop) |
|---|---|---|---|---|---|---|---|---|
| 1 | C_AV_01 | **MAN_01** | male, caucasian | AVT_Male_Intro | AVT_Male | (−0.8472, 31.11, 7.496) | −79.2° | fi_dancestand1-m (1.72 s) |
| 2 | C_AV_02 | **WOMAN_04** | female, caucasian | AVT_Female_Intro | AVT_Female | (1.427, 31.11, 4.729) | −45.2° | fi_dancestand3-w (1.84 s) |
| 3 | C_AV_03 | **MAN_02** | male, african | AVT_Male_Afrik_Intro | AVT_Male_Afrik | (1.743, 31.11, 4.515) | −65.7° | fi_dancestand2-m (2.88 s) |
| 4 | C_AV_04 | **WOMAN_05** | female, african | AVT_Female_Afrik_Intro | AVT_Female_Afrik | (2.656, 31.11, 1.283) | −78.7° | fi_dancestand1-w (1.92 s) |
| 5 | C_AV_05 | **MAN_03** | male, asiatic | AVT_Male_Asia_Intro | AVT_Male_Asia | (2.063, 31.11, 0.2135) | −92.5° | fi_dancestand3-m (1.88 s) |
| 6 | C_AV_06 | **WOMAN_06** | female, asiatic | AVT_Female_Asia_Intro | AVT_Female_Asia | (3.214, 31.11, −3.098) | −19.9° | fi_dancestand2-w (3.40 s) |

How the entity mapping is established:
- [DATA] sex and ethnicity come from the flash. The entity ConfigName comes from entities.xmb.
- [INF, geometry] each c_av_0N_Target lies 0.45–1.0 m from exactly one candidate, and that candidate is the one
  with the matching config.
- The code itself never maps index → entity: the player avatar is rebuilt from the sex/ethnicity strings.

Candidate details:
- Each candidate also has a FaceFX `Morph` track (0x70726fb5, 34–144 keys up to ~240 s).
- The other 32 actors of M_AVATAR_avatar.sce are the party PNJs:
  - Dance_*, Pool_*, DJ, Tess_Wintory (co_stand-w);
  - the props CF_Platine_01/02 and CF_Bouee_01/02;
  - and the helpers.
- Their animations are dumped by `sceanim.py M_AVATAR_avatar.sce`. Every one plays from scene 0 to its f4 end
  (~241–259 s), looping the clip.
- After validation, C_WALKMODE has the same party (29 actors, rate 1) **without** WOMAN_0x/MAN_0x.

---------------------------------------------------------------------------------------------------
## 4. Flash UI: intro.vmf (frontend/hires/intro.bnk, stage 1280×960, 30 fps) [DATA]

### 4.1 Opening

At the end of C_INTRO the code plays label **`avatar`** (root frame 1).

Root frame-1 action:
```
Play(); if (_root.IsCasinoSaveData == true) pop_avert.gotoAndPlay("open") else pop_avatar.gotoAndPlay("open")
```
- Root frame 16 calls `Stop`.
- The `pop_avert` popup is the Casino Online warning (`/:m_CasinoAvertSD`). Set `IsCasinoSaveData` to false; it is
  set by FUN_00da7f40 from `DAT_00fd954a`.

`pop_avatar` = sprite 24 (62 frames), placed at (42.2, 578.0), depth 4.

Children:

| depth | char | name | content |
|---|---|---|---|
| d1 | sprite 10 | | EditText `/:m_SelectCharacter_` = **"Select your character"** (DBLoc 1011) |
| d3 | sprite 11 | `arr_r` | arrow; labels `loop` (bounce anim f0–f21), `stop` (f22), `locked` (f29: greyed, mul 0.39, alpha 0.75) |
| d5 | sprite 11 | `arr_l` | same, mirrored (sx −1) |
| d7 | sprite 17 | `logo_casino` | casino logo (labels open/close) |
| d10 | sprite 19 | | validate-button glyph, picked by `/:WhatPlatform`: 0 → `x360`; 1 → `x360` if `_root.IsPC_X360` else `pc`; 2 → `ps3`; 3 → `ps3_jap` |

Timeline:
- f9 `open`: Play; `_root.Clip = this`; `arr_l.gotoAndStop("locked")`, `arr_r.gotoAndPlay("loop")`;
  **`num_avatar = 1`**; `setArrows()`; `setAvatarConfig()`.
- f9–f21: fade-in. The title slides 20 px up (y 119.7 → 99.7, alpha 0 → 1 over f14–f21). The arrows slide in
  (x 949 → 875 and 277 → 317, alpha 0 → 1 over f10–f16). The glyph fades in over f20–f26.
- f26 `avatar_stop`: Stop. Places the 4 buttons:

| depth | button | position | events |
|---|---|---|---|
| d14 | button 20 | (277.9, 130.5) | left arrow click → `previousAvatar()` |
| d16 | button 21 | (−158.6, 3.5) | off-screen **key catcher** |
| d18 | button 22 | (865.4, 130.5) | right arrow click → `nextAvatar()` |
| d20 | button 23 | (333.3, 83.2), scaled 11.2×1.9 (the title bar) | click → `A()` (validate) |

- f42 `close`: Play. The fade-out runs f43–f51.
- f56 action: `GotoLabel('default')`; **`_root.num_avatar = num_avatar`**; **`FSCommand("crav")`**.
  So the validation reaches the game **14 frames = 0.467 s** after `A()`.

### 4.2 Sprite-24 functions (frame 0 DoAction) [DATA, decoded AVM1]

- `A()`: `gotoAndPlay("close")`; `FSCommand("sndp:valid")`.
- `setArrows()`, from `num_avatar`:
  - 1: `arr_l` locked, `arr_r` loop;
  - 2–5: both loop;
  - 6: `arr_l` loop, `arr_r` locked.
- `setAvatarConfig()`:

  | num_avatar | /:WhatSex | /:WhatEthnicGrp |
  |---|---|---|
  | 1 | male | caucasian |
  | 2 | female | caucasian |
  | 3 | male | african |
  | 4 | female | african |
  | 5 | male | asiatic |
  | 6 | female | asiatic |

  Then the casino-logo open/close is driven by `_root.IsLogoOpened`, `_root.IsCasinoSaveData` and
  `_root.CasinoAvatar == num_avatar` (casino import only).
- `nextAvatar()`:
  ```
  if _root.IsTransition == 0: if num_avatar < 6: num_avatar++; setArrows(); setAvatarConfig();
                                                 FSCommand("nxav"); FSCommand("sndp:mopen")
                                  else FSCommand("sndp:stop")
  ```
- `previousAvatar()`: the same with `num_avatar > 1`, `num_avatar--` and `FSCommand("prav")`.

### 4.3 Input (button 21 BUTTONCONDACTION key codes) [DATA]

| cond | SWF key | action |
|---|---|---|
| 0x1a00 | key 13 = **Enter** | `A()` (validate) |
| 0x0200 | key 1 = **Left arrow** | `previousAvatar()` |
| 0x0400 | key 2 = **Right arrow** | `nextAvatar()` |

- The mouse works through buttons 20, 22 and 23 (OverDownToOverUp = release).
- Pad buttons have to reach the flash as these SWF key codes. The engine's pad → flash-key mapping was not traced
  [INF]: D-pad/stick left/right → 1/2, A/Cross → 13.
- `sndp:<name>` are flash-UI sounds: `mopen` on change, `valid` on validate, `stop` when blocked at either end.
  They come from FlashSounds.cpp; the sound bank mapping was not traced.

### 4.4 Variables written by the game

| variable | value | type |
|---|---|---|
| `/:IsTransition` | 0 or 1, every frame (§1.4) | 7, number |
| `/:IsDreaming` | true at start, false at exit | bool |
| `/:IsCasinoSaveData` | | bool |
| `/:CasinoAvatar` | `gm[0x6e0a4]+1` | number |
| `/:PlayerName` | at validation, for generik4 "Starring" | string |

Read back by the game at validation: `/:WhatSex`, `/:WhatEthnicGrp` (32-char buffers).

---------------------------------------------------------------------------------------------------
## 5. Audio: what "slow motion music" is [DATA + CODE]

| when | sound | how |
|---|---|---|
| C_INTRO start (event 0) | the 4 party-music emitters `S_WALKMODE_Music`, `_01`, `_02`, `_03` (`s_walkmode_music_AMBIENCE_INTERNAL_SFX.wav` 29.53 s, looped, positional in the house) are **stopped** | FUN_009bdc80 |
| C_INTRO 45.919 s | `snd:timestop` → `S_TimeStop` → `s_timestop_CINEMATIC.wav` (10.28 s, runs until ~56.2 s, i.e. ~3.6 s into the choice) | FUN_009bddf0 |
| C_AVATAR (whole choice) | Fx track: `FxS_TimeStart` → SourceData "C_AVATAR" → **`c_avatar_CINEMATIC.wav`** (7.38 s, MS-ADPCM 22050 Hz stereo); Fx duration FLT_MAX | |
| validation ("crav") | `S_TimeStart` emitter → `s_timestart_CINEMATIC.wav` (1.61 s) | FUN_009bddf0 |
| validation | C_AVATAR stopped → the c_avatar loop stops [INF: its Fx ends with the cinematic] | |
| C_FIESTA_INTRO +1.0 s | `snd:music` → the 4 S_WALKMODE_Music emitters restart: **party music at normal speed** | |

The C_AVATAR loop:
- **Loops**: bfx `intro_64FCDE6C7ABEB641.bfx` +0x5c = 0xffffffff. That field is −1 exactly for the looping sounds
  (C_AVATAR, S_WATER, S_WALKMODE_Music) and 0 for the one-shots [INF: loop count].
- Volume 1.0.

**No pitch or rate change is applied in data or readable code**:
- every bfx has +0xb0 = 1.0;
- no audio-rate code is referenced by the mode.

The slowed music is therefore **a dedicated asset**: the s_timestop "time stop" effect, then the c_avatar loop, then
the s_timestart effect. Measured with the scratch analysis: c_avatar has a spectral centroid of 2151 Hz versus
3257 Hz for the party loop, so it is darker or filtered, but it is not a plain 0.2× resample of the party loop.
How Eden made it is unknown [INF].

Flags and placement:
- The C_AVATAR Fx record has flags 0x27 and a matrix whose translation is (−15.37, −23.54, 3.74) scene-local.
- The C_INTRO music had 0x37. The positional woosh had 0x27.
- Meaning of bit 0x10: not decoded. To be safe, **play the c_avatar loop non-positional (2D) at volume 1** [INF];
  at that position with the bfx distances (4 / 40) it would be nearly inaudible.

---------------------------------------------------------------------------------------------------
## 6. After validation: C_FIESTA_INTRO_0N, then walk mode [DATA + CODE]

### 6.1 Scene 1 (`M_FIESTA_INTRO_0N_fiesta_intro_0N`, 3.52 s)

- Camera `c_fiesta_intro` (Fov 35, 42–51 position keys, 1 Heading key) dollies toward the actor **`Avatar`**
  (name hash 0x7461e8a2; no entity hash = 0x6666…).
- `Avatar` is the **player avatar** [INF: the only actor without an entity, and FUN_00da8d90 has just rebuilt the
  player avatar].
- Avatar positions (scene space, y 31.11), each next to the chosen candidate's spot:

| N | position | yaw quaternion (0, y, 0, w) |
|---|---|---|
| 1 | (−1.345, 6.705) | (0, −0.6666, 0, 0.7454) |
| 2 | (0.658, 4.624) | (0, −0.2035, 0, 0.9791) |
| 3 | (1.877, 3.840) | (0, −0.6994, 0, 0.7147) |
| 4 | (2.339, 1.660) | (0, −0.6965, 0, 0.7176) |
| 5 | (1.728, 0.195) | (0, −0.6666, 0, 0.7454) |
| 6 | (2.599, −3.270) | (0, −0.2118, 0, 0.9773) |

- Animation `fi_selectavatar-m` (5.9 s), except N = 4, which references `co_stand-m`.
- Gender is resolved through the Entity__Cinematic **`additionalsStyleAnims`**:
  - the base `additionalsAnims[i]` is replaced by `anims[i]` of the avatar's style `"m"` or `"w"`;
  - for example index 12 is `fi_selectavatar-m` → `fi_selectavatar-w` (5.4 s), and index 1 is `co_stand-m` →
    `co_stand-w`.
  - The selector code is not readable [ENC/INF].
- FaceFX uses `additionalsStyleFxe` (`intro-w.fxe` for w, `thedream-m.fxe`).

### 6.2 Scene 0, the shot at 3.53 s

- Camera `c_fiesta_02`, (5.57, 32.97, 6.017) → (6.165, 32.9, 6.216), Fov 35.
- `Tess_Wintory` at (10.33, 31.41, 6.623) plays `fi_hello_tess` (4.24 s) from scene time 3.14.

### 6.3 Overlays and walk-mode start

- `flsh:generik4` at ~0.5–0.56 s → intro.vmf label `generik4` = "Starring" (DBLoc 4053) + `/:PlayerName`.
- `wmod:Avatar_Avatar` at 0, and the 1.5 s white fade (§2.3).
- At the end, walk mode starts (§1.5):
  - camera pose from Entity__Camera `cam_entree_0N`; these are at eye height y = 32.59, i.e. ~1.48 m above the
    floor, next to the chosen spot;
  - flash `wm_home` (opens `cursor.cursor_anim`, `_root.Input = true`);
  - background C_WALKMODE (rate 1).
- Next step: C_TESS, with the "Go to the garage" tutorial (§1.5). The switch to FIRST_WM (garage.bnk) happens later
  and is outside this spec.

| cam_entree | position | rotation (x, y, z, w) |
|---|---|---|
| 01 | (−0.318, 32.591, 6.979) | (0, −0.7009, 0, 0.7133) |
| 02 | (0.927, 32.591, 5.645) | (0, −0.5025, 0, 0.8646) |
| 03 | (2.315, 32.591, 4.067) | (0, −0.5679, 0, 0.8231) |
| 04 | (2.374, 32.591, 1.904) | (0.00196, −0.6214, −0.00155, 0.7835) |
| 05 | (2.333, 32.591, −0.500) | (−0.00423, −0.5335, 0.00267, 0.8458) |
| 06 | (2.079, 32.591, −1.826) | (0.00241, −0.2629, −0.00066, 0.9648) |

---------------------------------------------------------------------------------------------------
## 7. Timings (exact)

| item | value | source |
|---|---|---|
| slow-motion start | C_INTRO t = 50.92 s (rate 0.2 shot); time-stop sound at 45.919 s | DATA |
| choice background rate | 0.2 (240 s scene in 1200 s real time; loops) | DATA |
| C_AV_xx loop | 1.0 s (static camera) | DATA |
| transition | 0.5 s per step, real time (camera rail at rate 1) | DATA |
| input lock | while the current cinematic is not C_AV_xx (`/:IsTransition`) | CODE+DATA |
| UI fade-in | f9 → f26 = 17 frames = 0.567 s at 30 fps | DATA |
| validate → crav | f42 → f56 = 14 frames = 0.467 s | DATA |
| S_TimeStart | 1.61 s | DATA |
| C_FIESTA_INTRO | 6.37 s (shot 1 3.53 s, shot 2 2.84 s); fade from white 1.5 s [INF]; generik4 at ~0.5 s; music at 1.0 s | DATA |

---------------------------------------------------------------------------------------------------
## 8. Implementation plan for OpenTDU2

Do not modify the sources from this spec task. This is the plan to apply.

### 8.1 Cinematic runtime (game/cinematic)

1. **Fix the .cin track offset**: tracks start at `0x3c + 8·numScenes` (§2.1). Today the 1-scene files
   (C_AVATAR, C_AV_xx, C_xTOy, WALKMODE, TESS) misparse.
2. **Shot-time mapping with rate**: in `Cinematic::getShot`/`getCamera`/actor sampling, use
   `sceneTime = CameraStart + (t − Start) · (CameraEnd − CameraStart) / Duration`. This alone gives the slow motion
   at the end of C_INTRO and during C_AVATAR. Update `IntroCinematic::updatePnjs()` accordingly.
3. Build a `CinematicPlayer` instance class (one per running cinematic) with:
   - time; loop (CinProps bit 1, or forced); noCamera (bit 4);
   - events {Start = 0, End = 7};
   - `stop(bJumpToEnd)` (CinEnd 3 → final pose; no event 7);
   - message/FX dispatch over [prev, cur), with loop wrap: `t > dur−0.001` → `t −= dur` (intro_spec §7.4).
   Two may run at once: **main** (provides the camera, weather/wmod, flash and snd messages) and **background**
   (noCamera: provides actors and its FX only).
4. Shot record +0x14/+0x18/+0x1c: fade type, duration and colour (fade-in from black 4 s for C_INTRO, from white
   1.5 s for FIESTA).
5. Style anims: when an actor is bound to an avatar with style `m`/`w`, replace ANIM ref hashes found in
   `additionalsAnims[i]` with `additionalsStyleAnims[style].anims[i]`. The `.anm` files are already in
   new_intro.bnk; fi_selectavatar-m/-w are present.

### 8.2 GMAvatarChoice state machine (mirrors FUN_00da8040 / FUN_00da91c0 / FUN_00da8d90)

```
enum State { Intro, Choice, Fiesta, WalkMode };
int index = -1, selected = -1; Player main, bg;

enter: as now (C_INTRO in main); stop party music emitters; flash INTRO: /:IsDreaming = true,
       /:IsCasinoSaveData = false, /:WhatPlatform = 1, _root.IsPC_X360 per input device.
Intro, on main End(7): index = 0; main = C_AV_01 (loop); bg = C_AVATAR (loop, noCamera);
       flash.gotoLabel("", "avatar", true); state = Choice
Choice, each tick: flash /:IsTransition = (main.name starts with "C_AV_") ? 0 : 1
       keys -> flash key events: Left -> SWF 1, Right -> 2, Enter/A -> 13 (button 21 conditions);
       mouse -> buttons 20/22/23
FSCommand "nxav": if index <= 4: playMain("C_%dTO%d", index+1, (index+1)%6+1, stopPrev = true)
FSCommand "prav": if index >= 1: playMain("C_%dTO%d", index+1, index)
main End(7) of "C_<a>TO<b>": index = b-1; playMain("C_AV_%02d", b)
FSCommand "crav": play S_TimeStart; selected = index; validate(); index = -1; state = Fiesta
validate(): bg.stop(); bg = C_WALKMODE (loop, noCamera); hide the 6 candidates (WOMAN_04..06, MAN_01..03);
       cfgName = "AVT_" + (WhatSex[0]=='f' ? "Female":"Male") + {caucasian:"", asiatic:"_Asia", african:"_Afrik"}
                 + "_Intro"; build player avatar from DBPNJ(cfgName) (EdenAvatarLibrary, as for PNJs);
       store into profile; flash /:PlayerName = profile name; main = C_FIESTA_INTRO_%02d(selected+1)
       and bind the scene actor "Avatar" to the player avatar (style m/w).
Fiesta, main End(7): walk-mode stub: camera = cam_entree_%02d(selected+1); flash "wm_home";
       (for now: go to the next mode or stay in a free-look walk camera).
```

### 8.3 Flash player (source/flash)

1. **Button key events**: add `FlashPlayer::sendKey(uint8_t swfKey)`. It walks the visible DefineButton2
   instances, parses the BUTTONCONDACTION list (u16 size, u16 cond, actions), and runs the records whose
   `cond >> 9 == key`. Also dispatch the mouse transition bits (bit 3 OverDownToOverUp = click) for buttons 20/22/23.
   The decoder logic is in `btn.py`.
2. Make sure the FSCommand callback receives `nxav`, `prav`, `crav`, `sndp:*`.
3. Variable types: `/:IsTransition` as a number (7), so that `_root.IsTransition == 0` holds.
4. Fallback if button support is late: call the sprite functions directly (`pop_avatar.nextAvatar()`,
   `previousAvatar()`, `A()`) through a new `callFunction(path, name)` API. Keep the exact flash logic (the
   num_avatar bounds, `IsTransition`) rather than reimplementing it in C++.
5. Draw intro.vmf over the 3D view with the root at label `avatar`. The bitmaps come from intro.2dm and the
   fla_*.2db pages.

### 8.4 Actors, audio and weather

- **Actors**: the existing `loadPnjs()` loads the PNJs of the intro bank (including WOMAN_04..06 and MAN_01..03
  via DBPNJ `AVT_*_Intro`). Sample them from **bg** (C_AVATAR, rate 0.2) during the choice, and from C_WALKMODE
  after validation, with no candidates. Sample the "Avatar" actor from **main** during FIESTA.
- **Audio** (CinematicSounds):
  - C_AVATAR's Fx (c_avatar loop, 2D, loop until the cinematic stops);
  - S_TimeStop at `snd:timestop`;
  - S_TimeStart at "crav";
  - `snd:music` → S_WALKMODE_Music + _01/_02/_03 (looped, positional at their Entity__Fx positions);
  - optional `sndp:` UI sounds.
- **Weather**: C_AV_01 `weat:Avatar`; per transition `wmod:` (sun direction only, §2.4). `findModified` already
  exists. With the DOF (photoshop_crappymode) enabled, the background blurs as in the original.

### 8.5 Tests

- Render at C_INTRO t = 51 s: the dancers should move at 1/5 speed.
- Press right 5×: the camera passes through the 6 c_av positions in 0.5 s each; the right arrow becomes locked at 6;
  `/:IsTransition` blocks input mid-move.
- Validate on 4: the config name must be `AVT_Female_Afrik_Intro`; FIESTA_INTRO_04 plays; "Starring <name>"
  appears ~0.54 s in.

---------------------------------------------------------------------------------------------------
## 9. Open points (not readable / not traced)

- The CinScene shot/scene time mapping code and the camera interpolation are [ENC]. The rate formula is [INF] from
  the data (§2.3).
- Whether the focused candidate is exempt from the 0.2 slow motion: no data or code supports an exemption (§2.3).
- `zcvn` / `zcne` messages [ENC].
- The style-anim substitution code [ENC/INF].
- FUN_00445920 (config copy into the profile) [ENC].
- The pad → SWF key mapping of edFlash, and the `sndp:` sound bank, were not traced.
- The meaning of FX flag bit 0x10 (0x37 vs 0x27).
