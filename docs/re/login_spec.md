# TDU2 PC press-start screen ("boot menu") and new-game flow: reimplementation spec

Tags: [CODE] read from readable x86 · [DATA] read from game files · [INF] inferred (reason given) ·
[ENC] the code is in the SecuROM-encrypted range (< 0x6c1000) and was not read.

Decompiled code (Ghidra, cleaned only by grouping):
- `/tmp/claude-0/bootmenu_dec1.c`: GMLogin game mode (vtables, enter, tick, all state handlers, ctor).
- `/tmp/claude-0/bootmenu_dec2.c`: savegame check and popup system, NAT type, login helpers.
- `/tmp/claude-0/bootmenu_dec3.c`: flash manager, edFlash player viewport, screen ratio, camera aspect.
- `/tmp/claude-0/bootmenu_dec4.c`: input bit mask (press start), VIDEOBUMPER mode, mode loop, cmd handlers.

Tools written for this task:
- `/tmp/claude-0/py/vmf2.py`: .vmf dumper with decoded AVM1 actions, shape bounds and fills, and paths
  (`--defs`, `--noplace`, `<sprite id>...`, `--paths p<id>`).
- `/tmp/claude-0/py/dxt5png.py`: .2db (DXT5) to PNG.
- `/tmp/claude-0/bm/tool/bnk_tool`: copy of otdu2_bnk_tool with the 4096-element array cap removed. The
  stock tool truncates `db_*.cpr` (7047 lines) and `db_data.cpr` DBLoc (7047 rows) at 4096.

Data dumps: `/tmp/claude-0/bm/{l2_full.txt, gen_root.txt, loc_fr.tsv, loc_us.json, dbloc.tsv,
GamePC.dec, SystemPC.dec}`. Approximate composite of the rest frame:
`/tmp/claude-0/bootmenu_rest_169.png` (the 16:9 window) and `bootmenu_rest_full.png` (the full 1280x960 stage).
In both, a flat blue stands in for the 3D scene.

---------------------------------------------------------------------------------------------------
## 0. Correction: the press-start screen is GAME MODE **LOGIN (0x14)**, not BOOTMENU (0)

- `GamePC.cpr` (decrypted, [DATA]): `SetCurGameMode = 0 // GAME_MODE_BOOTMENU`. It also contains
  `AddFlashMovie "BOOT" 0` and `AddFlashMovie "LOG_IN2" 20 // GAME_MODE_LOGIN`. `boot.bnk` does not exist in
  `frontend/{hires,medres}` on the full install, so mode 0 has no movie.
- The mode loop FUN_00d51c80 [CODE] creates a game-mode object per mode with a switch. It has **no case 0**.
  Case `0x14` builds `FUN_009c8bd0` (size 0x1ae400), or `FUN_009bf860` (size 0x1ae480) on the casino build
  (`FUN_008d8d50`).
- FUN_009c8bd0 installs the vtables 0xf28278 / 0xf2833c / 0xf28344 / 0xf2834c / 0xf28374. The functions
  previously called "GMBootMenu" (FUN_009c7ea0 = enter, FUN_009c6b20 = cinematic choice) belong to this class.
  The strings next to the vtable are `"GMLogin::OnConnectionLostWarningClosed called."` and
  `"_Offline mode not allowed in BETA._"`. The class is **GMLogin** [CODE].
- `FlashSounds::PlayFrontEndMusic` (FUN_00d85280): mode 0x14 plays the music entry `"LOGIN"` [CODE].
- Boot order [CODE + INF]:
  1. FUN_00d514d7 sets the video list `DAT_00fabff4 = {Bumper_ATARI.mkv, Bumper_EDEN_TDU.mkv}`, count
     `DAT_00fabff0 = 2`, skippable `DAT_00f70eeb = 1`, and next mode `DAT_00f76ef0 = 0x14`.
  2. The VIDEOBUMPER mode (0xe, FUN_009ec5c0) plays the list.
  3. When the list ends it calls FUN_00d97c90(gameMgr, DAT_00f76ef0 = LOGIN).
  - The exact instruction that moves mode 0 to mode 0xe was not located [INF].

### 0.1 VIDEOBUMPER mode (0xe) [CODE: FUN_009ec350 enter, FUN_009ec1e0 tick, FUN_009ec150 exit]
- **enter**: index = 0, then open video[0] (FUN_00997860). Hide the MAIN movie (FUN_00d7fbe0).
- **tick** (`+0x25f4` = state):
  - **0**: wait until the video ends (FUN_00996f30), or until a press when skippable (`DAT_00f70eeb` and
    FUN_00d5eed0, see §2.1). Then stop the video (FUN_00996f80) and go to state 3.
  - **default**: state--. This counts 3 → 2 → 1, a 2-tick gap.
  - **1**: close the video and index++. If index < count, open the next video and go to state 0; else go to
    state −1.
  - **−1**: set the leave flag (+0x31) and FUN_00d97c90(next = 0x14). State becomes −2.
- The bumpers are skippable with the same inputs as "press start".
- **Attract mode**: FUN_00b9aa50 puts `Teaser_TDU2.mkv` in the list and switches to VIDEOBUMPER with next =
  LOGIN.
  - It is called from GMLogin state 1 when `DAT_0102583c` (`DEMO_MODE_TIMER`, default −1.0) is > 0 and that
    many seconds passed (`DAT_00fdb8cc − last`).
  - `GamePC.cpr` has no `DEMO_MODE_TIMER`, so attract mode is **off** [DATA].

---------------------------------------------------------------------------------------------------
## 1. GMLogin object [CODE]

Object = `this` (primary vtable 0xf28278). Interfaces:
- `this+4` → 0xf2833c.
- `this+8` → 0xf28344: the flash message handler FUN_009c9840.
- `this+0x90` → 0xf2834c: the GameMode interface. The engine calls slot 3 (0x9ca5a0, load banks), slot 4
  (0x9c7ea0, enter), slot 5 (0x9ca140, tick), slot 7 (0x9c93c0, exit) and slot 8 (0x9ca560, unload).
- `this+0x1adc80` → 0xf28374: the popup listener FUN_009c7010.

| field (from `this`) | meaning |
|---|---|
| +0x1adc8c / +0x1adc90 / +0x1adc94 | previous / current / requested state |
| +0x1adc98 | sub-state (reset to 0 on each state change) |
| +0x1adc9c | ms timer: `+= round(dt·1000)` every tick; reset to 0 only in the ctor |
| +0x1adca0 | next game mode, decided in state 21/22 |
| +0x1adca8 | controller / login-widget object (FUN_006cab00(0)) |
| +0x1adcac | "login done" flag (set by callback FUN_009c9750) |
| +0x1adcad | **`stok` received** (flash says the press-start validation animation ended) |
| +0x1adcb0 | connection dialog answer: `ncco` → 1, `deco` → 2 |
| +0x1adcb4 | LOG_IN2 player (`movieEntry+0x474`) |
| +0x1adcc0 | savegame-check-started flag |
| +0x1ae3a0 | chosen press-start cinematic entity (intro_spec §9) |

**Commit** (primary slot 18, FUN_009c68b0, after every state handler): if `next ≠ 0` then `prev = cur`,
`cur = next`, `next = 0`, `sub = 0`.

### 1.1 load (slot 3, FUN_009ca5a0)
- Init the weather/time systems.
- Set the callbacks `DAT_011fe934 = FUN_009c9750` (login OK), `DAT_011fe93c = FUN_009c7400`,
  `DAT_011fe940 = FUN_009c95b0`.
- Load bank `Scenarics` / `press_start` (hash 0x81b1f27c / 0x04e3f205) → `islands/<island>/scenarics/press_start.bnk`.

### 1.2 enter (slot 4, FUN_009c7ea0), in order
1. Hide/unload all movies of layers 0..4 (FUN_00d7fc80/FUN_00d7fbe0).
2. Choose and place the press-start cinematic (intro_spec §9). If there is none, use the default camera
   (Fov 1.0472).
3. Reset a long list of globals: `DAT_01025608+0x17be8 = 1`, `+0x17bfc = 8`, and others. Then
   FUN_00ad9cb0(1, …), FUN_00adba00(playerData), FUN_00d72f00(lang), FUN_00acf530, **FUN_00ac5c40** (reset the
   savegame popup state) and FUN_00ac22e0 (mark the 6 save slots unknown: `slot.info+0x10 = 0x80000000`).
4. Set `this+0x1adc8c..` to prev 0, cur **1**, next 0, sub 0, flags 0, `+0x1adcc0 = 1`.
5. Find the movies. Each must be in state 1 (loaded), and the player is `entry+0x474`:
   - `LOG_IN2` → +0x1adcb4.
   - `MAIN`: FUN_00a936e0 sets `/:WhatGameMode = 0` (int). The value is 0 for mode 0x14 and 3 by default.
   - `GENERAL`: `/:WhatGameMode` likewise.
   - `LOG_IN2`: `/:WhatGameMode`, then `vfunc 0xe0(1, 0)` = play.
6. Call FUN_00ae7710, then `this→slot44` (FUN_009c6820: LOG_IN2 play), then MAIN `vfunc 0xe0(1, "/")`.
7. Call FUN_00d8e200, then MAIN `/:PauseBack = false` (variant type 5 = bool).
8. Store `+0x1adcb8.. = DAT_00fdb8cc` (time). If the game-manager mode ≠ 0x25, clear `+0x130`.
9. **NAT**: `LOG_IN2./:NAT_msg = (FUN_00c1b7d0(&DAT_01001808) != 1)` (bool, §2.4).
10. If a pending invite exists (`DAT_00fd9538/c`), call FUN_00c036e0(…, 5000).

### 1.3 tick (slot 5, FUN_009ca140; `p = this+0x90`)
```
timer(this+0x1adc9c) += (int)round(dt*1000);  if (++DAT_01459fd4 > 59) *(u8*)(this+0xb370) = 1
FUN_00a91df0(); FUN_00a91600();                 // world / streaming (base class)
if (p->vfunc24() /*world ready*/ && pendingCine) { StartCinematic(pendingCine) /*FUN_00b40180*/; pendingCine = 0; }
if ((!leaving(this+0xc1) || cur == 23) && !SavePopupBusy() /*FUN_00ac57b0*/ && (!DAT_00fd952f || DAT_00fd9535))
    { stateHandler[cur](); commit(); }
```
State handler slots: 1→0x4c, 2→0x50, 3→0x54, 4→0x5c, 5→0x58, 6→0x60, 7→0x64, 8→0x68, 9→0x6c, 10→0x70,
11→0x74, 12→0x78, 13→0x7c, 14→0x80, 15→0x84, 16→0x88, 17→0x8c, 18→0x94, 19→0x90, 20→0x98, 21→0x9c,
22→0xa0, 23→0xa4, 24→0xa8, 25→FUN_009c6ef0, 26→FUN_009c7670, 27→FUN_009c9d70.

### 1.4 State machine [CODE] (offline/no-save path in **bold**)

| st | function | behaviour |
|---|---|---|
| **1** | FUN_009c7770 | sub0: wait `!(DAT_011fde35 & DAT_011fde30)` (loading-screen thread finished) → sub1. sub1: wait **timer > 3000 ms** → sub2. sub2: if **press start** (§2.1): clear `DAT_00ffde4c`, `+0x1adca8 = FUN_006cab00(0)`, `->vfunc58()`, FUN_00d6f3e0(), **`LOG_IN2.gotoLabel("next")`** (slot 45 FUN_009c67f0, label string 0xf25fb0 = "next"), clear `stok`, sub3. Otherwise attract-mode check (§0.1). sub3: wait `stok` → **next = 2** |
| **2** | FUN_009c6aa0 | if `!DAT_011fead9`: `r = FUN_00557c80()` [ENC] → +0x1adca4. If (savemgr profile id `+0x58/+0x5c` == 0) or `DAT_011fec79` or r == 0 or `DAT_00fd952c` or `DAT_00fd952e` → **25** (sign-in UI). Else → **4** |
| 3, 5, 12 | — | empty |
| **4** | FUN_009c73d0 | FUN_00add850() → **6** |
| **6** | FUN_009c7330 | if no save popup and `!DAT_00fd9534` and FUN_00557850(savemgr, id) [ENC]: if `DAT_00fd952d == 1` → 9. Else: savemgr→vfunc68(); savemgr→vfunc58()→vfunc14(0, 1, 0, 0); `+0x1adcc0 = 1`; **if FUN_00ac6440(this, listener)** (starts the async save check/load, §3) → **7** |
| **7** | FUN_009c9c10 | wait player data ready (`DAT_010017ec→vfunc84`) and **result `DAT_01001628 ≠ 0`** and `!this+0xc2`. Result 1 or 5 → **8**. 2 → FUN_009c98b0 (apply loaded options) and clear the `PTR_DAT_00f70ec7` byte → 8. 4 → FUN_009c98b0 → 9. 3 → 9. Then: if (savemgr→vfunc38(cur profile) false or player name empty) and `!DAT_011fec7b` → stay. Else FUN_00adf970() and **→ 2** (profile changed: re-check) |
| **8** | FUN_009c7c40 | FUN_00ad9cb0(1, …), FUN_00adba00, FUN_00d72f00(lang) → **9** |
| **9** | FUN_009c6d60 | flush/close the controller object, reset the widget. If `playerData+0x5e` (auto online login) and `!DAT_00fd50af` → 10 (online). Else → **14** |
| 10 | FUN_009c6a50 | proxy connection (`"[LOGIN] Proxy connection"`) → 11 |
| 11 | FUN_009c6a00 | wait proxy: OK → 26. Failure → 24 |
| 26 | FUN_009c7670 | `"[LOGIN] Signing In"` or `"[LOGIN] Create Account"` → 15, or 27 on error |
| 27 | FUN_009c9d70 | error popups (loc 0x8f3, 0x5fa, 0x5ff, 0x1f90, 0x1def `"No network connection! Switching to offline mode"`, 0x1df0/1/0x1cd7, Atari account errors 0x1d37..0x1d3d) |
| 15 → 16 → 17 → 19 → 18 | FUN_009c7b90 / 6950 / 6fd0 / 6930 | online session init (server list, `DAT_0101e9c0` service) |
| 24 | FUN_009c99e0 | popup `"_Connection Failure_"` (loc 0xa16) → retry / offline / quit |
| 13 | FUN_009c9b10 | offline with default name: copy `"TDU2"` into the widget name (+0x42), the player data (+0x60) and the profile (+0xc1) → 18 |
| **14** | FUN_009c9aa0 | offline: FUN_009c8d90 (copy the widget name into player data +0x60), copy the profile name into the widget, FUN_00ada270 → **18** |
| **18** | FUN_009c7640 | FUN_00ac2470(slots) (re-scan the 6 save slots) → `DAT_0102632c ? 22 : `**`20`** |
| **20** | FUN_009c7610 | FUN_00ac23a0(slots, `+0x1adcc0`, 0) (load the slots that are still unknown) → **21** |
| **21** | FUN_009c9180 | **decide the next game mode** (§3.4) → **23** |
| 22 | FUN_009c9030 | same decision, multiplayer/casino variant → 23 |
| **23** | FUN_009c6ce0 | when player data is ready and `!savemgr→vfunc60()`: once (`this+0xc1`), FUN_004831a0(&DAT_0132dff0) [ENC thunk] and **FUN_00d97c90(gameMgr, +0x1adca0)** = request the mode switch. Clear the `PTR_DAT_00f70ec7` byte |
| 25 | FUN_009c6ef0 | sub0: FUN_00adf970() = show the login/profile widget. sub1: login done → 4. Cancelled → 2 |

### 1.5 exit (slot 7, FUN_009c93c0)
- Stop the loading-screen video if it is active (FUN_00d54c80).
- Clear `DAT_00fd9538/c`. Call FUN_00c1f550.
- Set `/:PauseBack = true` on the current movie (FUN_00d7feb0).
- Clear bit 1 of `DAT_011fe908` and the login globals. Call FUN_00a930c0 and FUN_00be2340.

unload (slot 8, FUN_009ca560): FUN_00cb7610, FUN_00a93250, and unload the `Scenarics` bank if one was loaded.

### 1.6 Flash → game messages (FSCommand 4-char codes; FUN_009c9840, then the base handler FUN_00a92d00)

| fscommand | uint | GMLogin action |
|---|---|---|
| `stok` | 0x73746f6b | `+0x1adcad = 1` (state 1 sub3 → 2) |
| `ncco` | 0x6e63636f | `+0x1adcb0 = 1` (not-connected dialog: "valid") |
| `deco` | 0x6465636f | `+0x1adcb0 = 2` ("disconnect") |
| `pbak` | 0x7062616b | FUN_009c8dd0 = back/quit to offline LIVING (sets mode 0xd + next 0x14) |
| `lgls`, `lama`, `sndp:valid`, `widg:kmap:dflt`, `sbdd`, `dbxo` | — | base handler FUN_00a92d00 (not followed). `sndp:valid` is a sound cue [INF] |

---------------------------------------------------------------------------------------------------
## 2. Press start, NAT message, LOG_IN2 timeline

### 2.1 Press-start detection [CODE]
- FUN_00d5eed0 returns `Edge(9) || Edge(0) || Edge(1) || Edge(2) || Edge(3)`.
- FUN_00d5e450(bit) reads the mask pair at `DAT_00ffddfc` (struct of 6 dwords; the second set is used when the
  caller is not the main thread). It returns `(prev & bit) && !(cur & bit)`: the **edge fires on button
  release**.
- The mask is built each frame by FUN_00d5d4d0 (channels resolved by FUN_00d5d770):

| bit | source |
|---|---|
| 0 | keyboard `ESC` |
| 1 | keyboard `SPACE` |
| 2 | keyboard `ENTER` |
| 3 | mouse `BUTTON_0` (left) |
| 4..7 | mouse `BUTTON_1`, `BUTTON_2`, `WHEEL_UP`, `WHEEL_DOWN` |
| 8 | any channel of an input device with channel type 0 pressed |
| 9 | the same, on a device whose `+4 == 2` (gamepad/joystick class [INF]) |
| 10 | that device's `BUTTON_0` |

So **ESC, SPACE, ENTER, left mouse button, or any pad button** start the game. The press is accepted only in
state 1 sub2, which is ≥ 3000 ms after GMLogin was constructed and after the loading screen ended.

### 2.2 Variables the game sets in LOG_IN2

| variable | type | value |
|---|---|---|
| `/:WhatGameMode` | int (variant type 7) | 0 (mode LOGIN). Also set in MAIN and GENERAL |
| `/:NAT_msg` | bool (type 5) | `NATtype != 1` |
| `/:PauseBack` | bool | set on **MAIN**: false at enter, true at exit |
| `/:m_PressStart`, `/:m_NAT_msg_`, `/:m_Multi_profiles_`, `/:m_No_controller_` … | string | from `db_data.cpr` **DBLoc** (`_flash` var → `_hashcode` = loc line id), §2.5 |

### 2.3 LOG_IN2.vmf (log_in2.bnk) [DATA]
- Stage 1280×960, 30 fps, 444 frames.
- Every DoAction was decoded with vmf2.py. "fN" is the 0-based root frame.

| frames | content |
|---|---|
| f0 `init` | Defines `DBXO`, `DialogBox`, `PopUpNotConnected` and `SetBDD` (sets `db_main=1`, the others 0, then `FSCommand:sbdd`). `_root.fake = 0`. The test defaults (`NAT_msg=true`, French debug texts) are skipped because `!fake`. Places the **closed curtain**: band sprite ch31 at depth 3 (ty 173.4) and depth 1 (ty 653.4). Together they make the whole stage black. Also places the msg_multi_profiles/msg_no_controller sprites, a debug EditText and a 2-frame sprite (all removed at f38) |
| f1 | `Play` |
| f24 | `GotoLabel("press_start"); Play` → frames 1..24 = **0.8 s of black** |
| f25 `casino` | casino build branch, not reached |
| f38 `press_start` | Places: d64 invisible hit button ch32 (offscreen, `fake` → "next"); **d62 ch34 = 4:3 letterbox bars**; d60 `msg_NAT`; d40/d20 msg sprites; d18 `stuff_to_hide` (logo); **d15 ch51 = stripe overlay, alpha 0.75**; d5 ch55 glows (alpha 0); d3/d1 band sprites |
| f39..f68 | **Bands open** (ease-out, 30 frames = 1.0 s). Top band ty 173.4 → 35.4, bottom 653.4 → 906.1. Glows d5 fade in (alpha 0 → 1, scale 1.47 → 1.0, ty −75 → 108). `cart` (Press-start text, d13) placed at f39 with alpha 0 and x-scale 1.15, fading in to alpha 1 at f74 |
| stuff_to_hide (own timeline) | logo `popup` alpha 0 → 1 over 21 frames, then `Stop` at its label `stop` |
| f74 | `if (_root.NAT_msg) msg_NAT.gotoAndPlay("open")` |
| **f79** | **`Stop`; `FSCommand:lgls`** ← the press-start screen rests here |
| f80 `next` (game calls gotoLabel "next" on press) | `cart.gotoAndPlay("valid")`; `if NAT_msg msg_NAT.gotoAndPlay("close")`; `FSCommand:widg:kmap:dflt` |
| f86 | `stuff_to_hide.gotoAndPlay("hide")` (logo fades out over 17 frames) |
| f91..f118 | bands slide fully out (top to ty −309.6, bottom to 1136.4) and fade (alpha 1 → 0) |
| f112..f127 | stripe overlay d15 fades 0.75 → 0 |
| f118 | `if (_root.IsCasinoStandAlone) gotoAndPlay("goto_casmenu") else Play` |
| f127 | `Stop` → **the screen is now clear: only the 3D scene shows** (the save popup comes from GENERAL) |
| f128 `goto_casmenu` … f155 | casino branch: the bands close again |

**cart** sprite 58 (68 frames):
- `default` f0: fades in.
- f41: GotoLabel default (loop).
- `valid` f43: `FSCommand:sndp:valid`.
- f64: `Stop`, **`FSCommand:stok`**, `FSCommand:lama`.
- `stok` arrives **21 frames (0.70 s) after "next"**.

**msg_NAT** sprite 40 (74 frames):
- `default`: `Stop`. Defines `initText()`, which centres the icon `ic` left of the text:
  `ic._x = 492.2 − TxtLength/2 − 25`.
- `open` f9..f32: the msg clip moves up from y −35.5 to −80.5 and fades in. `Stop`.
- `close` f52..f73: fades out, then `GotoLabel default`.

### 2.4 NAT message logic [CODE, FUN_00c1b7d0]
- The type is cached in `obj+0x222ec`. The ctor (0xc1c583) initialises it to **1 = Open**, with "read" flag
  `+0x222ea = 0`.
- Until a read succeeds, each call does the following:
  - FUN_00d68b30 opens `HKLM\SOFTWARE\Wow6432Node\Atari\TDU2[\Steam]`. If that fails it opens
    `HKLM\SOFTWARE\Atari\TDU2[\Steam]`. `\Steam` is used when `DAT_00fd50ac`.
  - It queries the value **`NetworkNatType`** (RegQueryValueEx-like import; the IAT names are hidden by
    SecuROM [INF]).
- Parsing:
  - Prefix `"Strict"` → 3, `"Moderate"` → 2, `"Open"` → 1.
  - The full strings `Strict:UdpBlocked`, `Moderate:SymmetricUdpFirewall`, `Moderate:RestrictedCone`,
    `Moderate:PortRestrictedCone`, `Open:OpenInternet`, `Open:FullCone` and `Open:UPnPUsed` are also tested.
- **`NAT_msg` is shown iff type ≠ 1**, i.e. the registry value exists and is Strict or Moderate. A missing key
  means Open, so no message. The value is written outside the game, by the launcher's network test [INF].
- The flag is read once, at GMLogin enter. The message appears at f74 (2.47 s after `press_start`, about
  3.3 s after the movie starts).

### 2.5 Localized strings [DATA: db_us.cpr, db_data.cpr DBLoc]

| id | `_flash` var | English text |
|---|---|---|
| 938 | `m_PressStart` (common) | `Press START to play` |
| 7963 | `m_NAT_msg_` (common) | `To fully experience TDU2 online, you need to set your network settings to have an #color:BBD525#open NAT type#nocolor#.` |
| 7097 | — | `No §GAME_NAME§ progression data could be found on your HDD. To start a new game you must not have previous §GAME_NAME§ save data.\nIf you choose #color:5982BFFF#OK#nocolor# to start a new game, any previous §GAME_NAME§ save data will be deleted.\nChoose #color:5982BFFF#Retry#nocolor# to retry loading a save game.` |
| 7095 | — | `Savegame not found` (not used by this popup's code path) |
| 7096 | — | console variant ("storage device") |
| 219 | `m_Ok` | `OK` |
| 1839 | `m_Retry_min` | `Retry` |
| 338 / 217 / 487 | — | `Cancel` / `Yes` / `No` |
| 7061 / 7471 | `m_Multi_profiles_` / `m_No_controller_` | multi-pad / reconnect-pad warnings |

- `§GAME_NAME§` is replaced by `"TDU2"` (DAT_00f18f8c), or `"TDU2 Casino "` on the casino build.
- `#color:RRGGBB[AA]#…#nocolor#` is inline colour markup.
- The DBLoc flags (`_common`, `_main`, `_general`, …) match the `db_*` variables that `SetBDD` sends with
  `FSCommand:sbdd`. The game fills the `/:<_flash>` variables of the movie from them [INF from names].

---------------------------------------------------------------------------------------------------
## 3. Savegame detection, the "no progression data" popup, and the next mode

### 3.1 What is readable and what is not
- **[ENC]**: the save backend is class `edFileSaveGamePC` (RTTI, vtable 0xf3bcac; base `edFileSaveGameStrategyBase`
  0xf3bb94). All its methods are at 0x55xxxx, as is the save-manager object `DAT_00f86c6c` (vfuncs 0x30 =
  build the save folder path, 0x38, 0x58, 0x60, 0x68). These are encrypted.
- So the save **folder, file names and format check cannot be read**. Strings present but not referenced by
  readable code: `"savegame"`, `"PLAYERSAVE"`, `"savegame/SystemDefault"`.
- **[CODE]** `savegame/SystemDefault` = system options file:
  - path = `savemgr→vfunc30(buf, 3, 0)` + `"savegame/SystemDefault"`;
  - read: CreateFile-like, up to 0x400 bytes into `DAT_00faa658` (FUN_00acaa10);
  - write: FUN_00acb1e0.
- `Save/current.bin` (FUN_00c20120/FUN_00c202f0) is the **online/server** save transfer (FUN_006d6800, net
  resource with "waiting" UI). It is not the local detection.
- The local check runs as a worker job: `DAT_01001460` = save UI/job object, `DAT_01001790` = event, slots table
  `*DAT_01001618` = 6 slots × 0x34 bytes, slot info `+0x20`.
  - `DAT_0100177c` = current operation (3 = save, 4 = load).
  - `savemgr+0x44` (`[0x11]`) = device/result status.
  - The result for GMLogin is `DAT_01001628`. It is written by encrypted code or through the slots pointer
    (`DAT_01001618+0x10` = 0x1001628) [INF].

### 3.2 Popup request [CODE: FUN_00ac4c80 → FUN_00ac4660 → FUN_00ac5900 → FUN_00d86c00]
- FUN_00ac4c80 is the job-status → popup mapper. It switches on `savemgr[0x11]`:
  - 0/9: none.
  - **1 (no data) and op == 4 (load)**: `Popup(msg 0x1bb9 = 7097, buttons type 3, kind 1, params {"GAME_NAME" = "TDU2"})`.
  - 2: corrupt → 0x1c42/0x1c43.
  - 5: disk full → 0x1bc0/0x1d09 with SIZE/DRIVE_LETTER.
  - 7: 0x1bbb/0x1bbc.
  - default: 0x1bc4/0x1bc6/0x1bc8.
- FUN_00ac4660 stores the request in `DAT_01001460`: `+0x1e0` = button type, `+0x1e8` = message id,
  `+0x1ec` = kind, `+0x1f0..` = up to 3 × {name[0x20], value[0x40]}, `+0x1d2 = pending`.
- FUN_00ac5900 shows it when no other UI blocks it. It calls FUN_00d86c00 with type `+0x1e0`, message
  `+0x1e8`, kind `+0x1ec`, listener `+0x1d8` (= the GMLogin listener `this+0x1adc80`), and the parameter table
  (§GAME_NAME§ substitution, FUN_00999b00).
- **FUN_00d86c00** (GENERAL `avert` popup):
  - Button labels by type:
    - 0: A = `OK`(219).
    - 1: B = `Cancel`(338).
    - 2: OK / Cancel.
    - **3: A = `OK`(219), B = `Retry`(1839)**.
    - 4: OK / Yes(217).
    - 5: Yes / No(487).
  - Labels are written to `/:AvStateButtonA` / `/:AvStateButtonB`.
  - The text goes to `/:m_AvertText`.
  - The `/avert` clip label is chosen as: no B button → `"on"`; B present → `"on_choice"`, or **`"no_data"`**
    when the extra flag is set.
  - The `no_data` frame of GENERAL's avert has `RETRY` as button B, and its authoring text is the savegame text
    (French), so this popup uses **`no_data`** [DATA+INF].
- The popup has **no title** field. Only `/:m_AvertText` and the two button labels.
- Input: **A = MENU_VALIDATE / OK**, **B = Retry** [INF from the a_/b_ pad glyph sprites ch44/ch64].

GENERAL.vmf `avert` (char 515), placed at stage (65.4, 382.0), label `no_data` (f31..f40) [DATA]:

| element | stage rect / position |
|---|---|
| panel background (ch206 bitmap 205 218×48, scaled 2.973×3.707) | x 315.1..963.2, y 554.0..731.9 |
| message EditText `/:m_AvertText` (523.4×175.1) | from (433.9, 560.4) |
| button A label `/:AvStateButtonA` | (631.5, 743.3); pad glyph `a_` at (549.8, 735.4) |
| button B label `/:AvStateButtonB` ("RETRY") | (737.5, 743.3); glyph `b_` at (660.2, 735.5) |

### 3.3 OK / Retry [CODE: popup listener FUN_009c7010]
- The callback is `(button, owner, kind)` with button 0 = A (OK), 1 = B (Retry). When owner == `DAT_01001460`
  and **kind 1**:
  - **OK**: `DAT_0100177a = 0` ("do not retry"), FUN_00ac4530 (signals the worker event). The worker then
    continues the new-game path [ENC], so the old save data is reset by the backend as the text says. GMLogin
    gets result 1 → state 8 → … → 21.
  - **Retry**: FUN_009c6880 (`next = prev`, i.e. back to **state 6**, which re-runs the check, then 7), and
    `DAT_0100177a = 0`, FUN_00ac4530.
- Then, for both: `DAT_011fe91b = 0` and toggle the input-latch bits in `DAT_00ffdacc/ffdad4`.
- Other kinds: 2/7 (`0100177a = 1` = retry save), 3/4 (device select), 9/0, …
- A second listener branch (owner = GMLogin itself) handles its own dialogs: kind 0 → `+0x1adcb0` = 1/2;
  1/3 → `+0x1adc98++` (sub-state advance); 2 → `+0x1adc84 = 1`.

### 3.4 Next game mode (state 21, FUN_009c9180) [CODE]
```
mode = gameMgr->defaultMode(+0x124) ?: 9 /*LIVING*/;  FUN_008dbaa0(); FUN_00ada1e0(); setLang(); DAT_010017ea = 1;
if (DAT_00fd90b6 /* "play scenario intro" */) { islandMgr(DAT_01025608+0x9010)->vfunc8(0); mode = 0x20 /*AVATAR_CHOICE*/; }
else if (FUN_004831a0(&DAT_0132dff0) /*[ENC thunk] save loaded*/) { FUN_008d9580(); mode = gameMgr->mode(+0x128); }
else if (DAT_00fd8ff1) { place player at car-dealer spot; mode = 0xd /*ISLAND_CHANGE*/; }
savemgr profile slot: set +0x280/+0x284 = 0x154b987e_84c2fa36;  FUN_005578f0(...) [ENC];  FUN_00ac7b00() (save check/write);
next = 23;    // state 23 calls FUN_00d97c90(gameMgr, mode)
```
- **`DAT_00fd90b6`** is set by the config command **`SCENARIOTUTORIAL`** (handler 0xd6ad60):
  `flag = (arg == "ON")`, then `if (FUN_004831a0(&DAT_0132dff0)) flag = 0`. `GamePC.cpr` has
  `SCENARIOTUTORIAL = ON` [DATA].
- So with **no progression data → mode 0x20 AVATAR_CHOICE** (C_INTRO "The Dream", intro_spec §0). With a
  loaded progression the flag is cleared [INF: FUN_004831a0 is the same test in both places]. The mode then
  comes from the save/default, normally 9 LIVING (home) or island change 0xd.
- The GMLogin ctor also sets `DAT_01025608+0x9014 = DAT_00fd90b6 ? −1 : 11`.

---------------------------------------------------------------------------------------------------
## 4. Profiles / player name on PC [CODE + ENC]

- There is **no profile selection or creation menu** in GMLogin. When state 2 succeeds (the save manager has a
  current profile id `+0x58/+0x5c` and FUN_00557c80 [ENC] is OK), the flow goes straight press start → save
  check → popup.
- State 25 is the only UI branch. It shows the **login widget** (FUN_00adf970; movie `LOG_IN` with
  `/:input_key_01..04` = product key fields, `"[LOGIN] Signing In"` / `"Create Account"`, Atari account
  errors). It is the **online (Atari/TDU2 server) account** sign-in, reached only when there is no profile id or
  a login is forced.
- The save manager keeps 32 profile slots of 0x118 bytes at `+0x170` (64-bit id at +0x170/+0x174). Index
  `+0x50` = current, −2 = none. They are filled by encrypted code.
- Player name:
  - Offline: the name already in the login widget (+0x42, 0x30 chars) is copied to `playerData+0x60` (state 14).
  - Default name: `"TDU2"` (state 13).
  - The name is not taken from the Windows user name in readable code. The Steam registry variant
    `…\Atari\TDU2\Steam` exists. Games for Windows Live is not referenced (`edLive*` is Eden's own online
    layer) [INF].

---------------------------------------------------------------------------------------------------
## 5. Flash stage → screen, bands, 3D viewport

### 5.1 Viewport mapping [CODE]
- Each movie's edFlash player keeps a **normalized destination rect** at `player+0x220..0x22c` = {x0, x1, y0, y1}.
  - Default {0, 1, 0, 1} (FUN_0085d500).
  - Set by player vfunc 0x1c FUN_008424d0(x, y, w, h): `x0 = x, x1 = x + w, y0 = y, y1 = y + h`.
- The **whole stage** (`movie+0x1dc` width, `+0x1de` height, in twips; /20 = 1280×960) maps linearly onto it.
  Example: FUN_0085e4d0 (mouse) computes `stageX = Wtw·(mx − x0)/(x1 − x0)`.
- FUN_00d7faa0 (called when a movie finishes loading) chooses the rect from **`DAT_00fe4c78` = GameConfig+0x40
  = screen ratio**:

| ratio mode | rect (x, y, w, h) | visible stage area | meaning |
|---|---|---|---|
| 0 | (0, 0, 1, 1) | 1280×960 whole stage | 4:3 display |
| **1** | (0, −1/6, 1, 4/3) | **x 0..1280, y 120..840** | **16:9: fit width, crop 120 top/bottom** |
| 2 | (0, −0.125, 1, 1.25) | y 96..864 (1280×768, 5:3) | wide 16:10-ish |
| 3 | same as 1 | y 120..840 | |
| 4 | (1/3, −1/6, 1/3, 4/3) | centre third | triple-screen (3×16:9) |

- The rect is in render-target-normalized units, so the scale is uniform only when the backbuffer aspect matches
  the mode.
- GameConfig+0x40 is set by `SETSCREENRATIO` (config `SystemPC.cpr`: **`SetScreenRatio = RATIO_16_9`** [DATA]):
  - `RATIO_16_9` → 1.
  - Literal code reading [CODE]: `RATIO_4_3` → **2**, any other string (including `RATIO_16_10`) → 0. The
    compare chain at 0xd66858..0xd668e0 looks inverted for the last two.
  - The default in the GameConfig ctor (FUN_00d6a0b0) is 0.
- So on the PC defaults the movies show the **stage band y ∈ [120, 840] scaled ×(screenW/1280)**. On 1280×720
  that is 1:1 with y − 120. No side bars. The stage parts above 120 and below 840 are cropped.
- The 4:3 letterbox shapes are never visible in 16:9 mode:
  - LOG_IN2 **ch34/ch33** (depth 62, black, y 0..120 and 840..960);
  - GENERAL **`black_stripes`** (ch968: label `on` = ch278, black y 0..120 and 840..960 after ty 120; label
    `off` = hidden).
- The intro cinematic turns `black_stripes` on only when `DAT_00fe4c78 == 0`.
- Movie resolution folder (FUN_00d80460): `screenW ≤ 640` → `LowRes`. Else `round(screenW·9/16) > 720` →
  `HiRes`, else `MedRes`. 1280×720 therefore uses **MedRes**. medres/log_in2.bnk is byte-identical in size to
  hires (1,560,576 bytes) [DATA].

### 5.2 3D title scene [CODE]
- The 3D is rendered **full screen**. GMLogin has no sub-viewport code, and the bands are flash overlays.
- Camera aspect (FUN_00c9f680(refAspect = 16/9 (0xf4a868), fov)): `aspect = screenW/screenH`; if
  `aspect < 16/9` then `fov = fov·(16/9)/aspect`. Then `cam+0x2c0 = fov`, `+0x16c = aspect`,
  `+0x170 = 1/aspect`.
  - Narrower screens get a proportionally larger FOV value, scaled linearly, not through tan. On 16:9 or wider
    the FOV is unchanged.
  - GMLogin's no-cinematic camera sets the FOV directly (FUN_008b39d0(DAT_00f49b10)).

### 5.3 Title bands = LOG_IN2 sprites, not black_stripes [DATA]
- Shape **ch30** (inside band sprite ch31), x 0..1280, all black:
  - y −333.5..−213.4: linear gradient, alpha 0 (outer) → 1;
  - y −213.4..266.6: solid;
  - y 266.6..386.7: gradient, alpha 1 → 0 (outer).
  - Gradient matrices: b = ∓0.0733, c = 0.78125, ty = −5469 / 6533 twips.
- Shape **ch50** (inside ch51), stripe texture `fla_0049.2db` (2048×512 DXT5, used 1280×392). White diagonal
  stripes, alpha ≤ 37/255, fading to 0 at row ~350:
  - top copy at y −40..351.95;
  - bottom copy rotated 180° at y 407.95..799.9.
- Rest positions (f68..f79, held until "next"), in stage px, with 16:9 screen-y at 1280×720 = stage − 120:

| layer | depth | stage rect | on 1280×720 screen |
|---|---|---|---|
| top band, opaque black | d3, ty 35.4 | x 0..1280, y ≤ 302.0 | y 0..182.0 |
| top band fade (alpha 1→0) | d3 | y 302.0..422.1 | y 182.0..302.1 |
| bottom band fade (alpha 0→1) | d1, ty 906.1 | y 572.6..692.7 | y 452.6..572.7 |
| bottom band, opaque black | d1 | y ≥ 692.7 | y 572.7..720 |
| stripe overlay top (×0.75 alpha) | d15 ch51, ty 120 | y 80..471.95 | y −40..351.95 |
| stripe overlay bottom (rotated) | d15 | y 527.95..919.9 | y 407.95..799.9 |
| glows (fla_0052 370×145 white flare, alpha ≤ 50/255) | d5 ch55 at (−7.9, 108.0) | ≈ (0..370, 696.5..841.5) mirrored; (910..1280, 696.5..841.5); (−8..559, 120..342) ×−1.53; (792..1287, 120..314) ×(1.34, −1.34) | |
| TDU2 logo (fla_0041, 520×173) | d18 stuff_to_hide/popup/ch43 | x 379.6..899.5, y 155.3..328.25 | y 35.3..208.25 |
| "Press START to play" EditText (`/:m_PressStart`, box 705×34.7) | d13 cart at (545.1, 743.4) | x 286.5..991.5, y 717.5..752.2 (text centred, x ≈ 639) | y 597.5..632.2 |
| NAT message EditText (`/:m_NAT_msg_`, box 1030×26) | d60 msg_NAT → msg | x 148.4..1178.4, y 759.3..785.4; icon `ic` (fla_0035 114×114 ×0.4) left of the text | y 639.3..665.4 |

Draw order (low to high depth):
1. d1/d3 bands;
2. d5 glows;
3. d13 Press-start;
4. d15 stripes;
5. d18 logo;
6. d20/d40 (hidden pad messages);
7. d60 NAT;
8. d62 4:3 bars;
9. d64 invisible button.

The "ornate dark patterned band" = black band ch30 plus the 75%-alpha white stripe overlay ch50.

---------------------------------------------------------------------------------------------------
## 6. Reimplementation sketch (pseudo-C)

```c
// mode LOGIN (0x14), entered after the VIDEOBUMPER mode (2 skippable bumpers)
enter(): loadBank("press_start"); pickCine(); resetSaveUI();
         LOG_IN2.set("/:WhatGameMode",0); MAIN/GENERAL likewise; LOG_IN2.play(); MAIN.set("/:PauseBack",false);
         LOG_IN2.set("/:NAT_msg", ReadRegNat() != OPEN);  state=1; sub=0; (timer counts since ctor)
tick(dt): timer += round(dt*1000);
  1: if (sub==0 && !loadingScreen) sub=1; if (sub==1 && timer>3000) sub=2;
     if (sub==2 && Released(ESC|SPACE|ENTER|LMB|anyPadButton)) { LOG_IN2.gotoAndPlay("next"); sub=3; }
     if (sub==3 && fs("stok")) state=2;                      // ~0.7 s later
  2: state = haveProfile ? 4 : 25;  4: state=6;
  6: StartSaveCheck(listener); state=7;
  7: on popup(0x1bb9,"no_data",OK/Retry): OK -> result=NEW; Retry -> state=6;
     result NEW/OK -> 8 -> 9 -> 14(offline) -> 18 -> 20 -> 21
  21: next = (SCENARIOTUTORIAL && !progressionLoaded) ? AVATAR_CHOICE(0x20) : savedMode/LIVING(9);
  23: RequestMode(next);
draw: 3D full screen; flash rect per §5.1 (16:9: stage y 120..840 → screen).
```

---------------------------------------------------------------------------------------------------
## 7. Notes for OpenTDU2

- `gm_boot_menu.cpp` has a splash, profile select and profile create flow. None of that exists in the original.
  The original is: bumpers (videos) → LOG_IN2 timeline → savegame popup → AVATAR_CHOICE. LOG_IN (log_in.bnk) is
  only the online login widget.
- Use `log_in2` with the label sequence `init` → (auto) `press_start` → rest f79 → `next` on release of the start
  inputs, and wait for `stok` before continuing.
- **Shape path parser bug** [DATA]: Eden shape paths have **one extra byte after each path record**:
  `u8 line, u16 fill0, u16 fill1, i32 x, i32 y, u16 nEdges, nEdges × 16 bytes, u8 pad`. Verified on LOG_IN2
  ch30 (6 paths end exactly at the tag end) and on GENERAL ch278.
  `source/flash/flash_definitions.cpp::parseShape` advances 15 + 16·n with no pad, so every path after the
  first is misread.
- The `.vmf` header can carry an extra table: `if (d[0x12]) skip 4 + 4·count` before the tags (GENERAL.vmf).
- English strings need `db_us.cpr`. It was copied to `/mnt/user-data/uploads/TestDrive2/Euro/Bnk/database/db_us.cpr`.

---------------------------------------------------------------------------------------------------
## 8. Summary: facts vs unknowns

**Facts [CODE/DATA]**
- The press-start screen is game mode **LOGIN 0x14 (class GMLogin)**, preceded by the VIDEOBUMPER mode
  (Bumper_ATARI, Bumper_EDEN_TDU; skippable).
- It has a 27-state machine (§1.4).
- Press start = **release** of ESC/SPACE/ENTER/left mouse/any pad button, accepted ≥ 3 s after the mode starts.
  The game then calls `LOG_IN2.gotoLabel("next")` and waits for `FSCommand:stok` (0.7 s).
- NAT message ⇔ registry `HKLM\…\Atari\TDU2[\Steam]\NetworkNatType` exists and is not "Open…".
- Popup = GENERAL `avert`, label `no_data`, text loc **7097**, buttons **OK(219) / Retry(1839)** (type 3),
  `§GAME_NAME§ = TDU2`.
- OK → continue as a new game. Retry → back to state 6 (re-check).
- New game + `SCENARIOTUTORIAL = ON` → **AVATAR_CHOICE (0x20)**. No profile menu.
- Flash: the 1280×960 stage maps to a normalized rect chosen by the screen-ratio mode. PC default `RATIO_16_9` →
  stage y 120..840 fills the screen, width-fit, no side bars. The 3D is full screen; the camera FOV is widened
  when the aspect is below 16:9.
- Title bands are LOG_IN2 ch31/ch30 (+ ch51 stripes, ch55 glows). The rectangles are in §5.3.
- GENERAL `black_stripes` and LOG_IN2 ch33 are 4:3 letterbox bars (y 0..120 and 840..960). They are not
  visible at 16:9.

**Unknowns**
- [ENC] Save folder path, file names (`PLAYERSAVE`?), format and validity check, and the deletion on OK
  (edFileSaveGamePC).
- [ENC] FUN_00557c80 (profile check), FUN_004831a0 (progression-loaded test), and who writes `DAT_01001628`.
- [ENC] The real game-mode switch from 0 to 0xe.
- The base flash message handler (`lgls`, `lama`, `widg`, `sbdd`) was not followed.
- The exact semantics of the input device class `+4 == 2` (assumed pad).
- The literal SETSCREENRATIO mapping for `RATIO_4_3`/`RATIO_16_10` looks inverted. Whether a runtime
  auto-detection overrides it is unknown.
