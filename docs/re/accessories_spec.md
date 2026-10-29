# Car accessories while driving: "Accessoires" menu, roof, windows, blinkers

Tags: [CODE] readable exe code (VA >= 0x6c1000), [DATA] game files, [INF] inference, [ENC] SecuROM range (not read).
Reference: the original game while driving shows the HUD "Accessoires" radial at the top left (roof on top, the
left / right blinkers on the sides, the window at the bottom, a steering wheel in the centre).

## 1. Input actions [DATA + CODE]

Action table at **0x00f7f108** (16-byte entries `{name*, action id, option loc id, 0}`) [DATA]; the actions are
queried by id with FUN_00d5e9f0 (pressed this frame) / FUN_00d5eab0 (held) [CODE].

| Action | id | Default_KB.xmb ChannelName / ChannelLocName | option loc id |
|---|---|---|---|
| CAR_WINDOWS | 0x16 | `W` / "W" | 4309 |
| CAR_RIGHT_BLINKER | 0x17 | `PAD_0` / "0 (PAVE NUM.)" | 4269 |
| CAR_LEFT_BLINKER | 0x18 | `RCTRL` / "CTRL DROITE" | 4268 |
| ACCESSORIES_LEFT | 0x34 | `PAD_4` / "4 (PAVE NUM.)" | 7644 |
| ACCESSORIES_RIGHT | 0x35 | `PAD_6` / "6 (PAVE NUM.)" | 7645 |
| ACCESSORIES_UP | 0x36 | `PAD_8` / "8 (PAVE NUM.)" | 7642 |
| ACCESSORIES_DOWN | 0x37 | `PAD_2` / "2 (PAVE NUM.)" | 7643 |

There is no separate "open the accessories" action: **ACCESSORIES_LEFT opens the menu** (§3).

Channel names are the game's DirectInput key names: table **0x00f73b08**, 32-byte names indexed by the DIK code
(French AZERTY labels of the key positions: 0x10 "A", 0x11 "Z", 0x1e "Q", 0x27 "M", 0x2c "W", 0x9d "RCTRL", ...)
[DATA]. A DIK code is a key position, like an SDL scancode: `W` = DIK 0x2c = `SDL_SCANCODE_Z` (the key labelled W
on an AZERTY keyboard, Z on a QWERTY one). OpenTDU2 maps every channel this way (`input/game_key_map.cpp`).

## 2. OpenTDU2 key bindings for keyboards without a numpad (Mac) — not game data

User request: the accessories must work on a Mac keyboard (no numpad, usually no right Ctrl). OpenTDU2 adds
main-row digits (same digits as the numpad keys; channel / loc names like the game's own main-row entries
RADIO_CHANNEL / RADIO_VOL_LESS / RADIO_VOL_MORE = "1" / "2" / "3"), in `kUserBindings` of
`source/input/game_key_map.cpp`, put before the Default_KB entries (the `#command:` tip shows them); **the
Default_KB keys keep working**:

| Action | OpenTDU2 key | When |
|---|---|---|
| ACCESSORIES_LEFT | main-row **4** | always (also the car door in FIRST_WM, `text_icons_spec.md` §7) |
| ACCESSORIES_UP | main-row **8** | only while the accessories menu is open |
| ACCESSORIES_RIGHT | main-row **6** | only while the accessories menu is open |
| ACCESSORIES_DOWN | main-row **2** | only while the accessories menu is open (main-row 2 stays RADIO_VOL_LESS otherwise) |

CAR_WINDOWS (`W` channel = the key labelled W on AZERTY / Z on QWERTY) works on a Mac as is. The direct blinker
keys (right Ctrl, numpad 0) usually do not exist on a Mac: use the menu (4 then 4 / 6).

## 3. Menu logic: FUN_00929be0 (per frame) [CODE]

Object fields: +0x14 HUD movie, **+0x18 menu** (0 none, 1 accessories `HUD/ig_menu`, 2 radio `HUD/radio`),
**+0x1c auto close timer**.

1. FUN_00928c70 (menus allowed: HUD movie present, no pause / popup / cinematic / special mode flags, +0x10 set)
   false → FUN_00929100 (close the open menu: goto "close" on `HUD/ig_menu` or `HUD/radio`, menu = 0).
2. up / down / left / right = ACCESSORIES_UP / DOWN / LEFT / RIGHT pressed.
3. menu 0: RIGHT → FUN_009284e0 (radio: "open" on HUD/radio, menu 2, timer 3.0 s DAT_00f75f14); else LEFT →
   **FUN_00928c10**: FUN_00928840, goto **"open"** on `HUD/ig_menu`, HUD/radio vfunc +0x100(2.0) [INF: not
   identified], **menu = 1, timer = 4.0 s** (DAT_00f75f18).
4. menu 1: **FUN_009293f0(up, down, left, right)**, first match only:
   * up → FUN_00928da0 (roof, §4) — done: FUN_00928e70(0) = `ValidIGM("hood")`;
   * down → FUN_00928b70 (windows, §5) — done: `ValidIGM("wind")`, menu = 0;
   * left → FUN_00928a20 (left blinker, §6) — done: `ValidIGM("clil")`;
   * right → FUN_00928950 (right blinker) — done: `ValidIGM("clir")`;
   * refused: timer = 4.0 s again and FUN_009291c0 = FUN_00d84a70("wrong", DAT_00f80168 + 0x1f88, 0, 0) (the
     "wrong" cue of the frontend element list; what it plays is not in readable code [INF]).
   FUN_00928e70 sets menu = 0 after the call (the movie closes itself). When FUN_00ad9690 (vehicle category
   0x12 / 0x13 / 0x16 = bike) is true the labels are "taun" / "clbl" / "clbr" / "hell" instead.
5. menu 2: FUN_009297c0 (radio, not implemented in OpenTDU2).
6. No accessory key pressed this frame → **FUN_00929b60**: CAR_WINDOWS → FUN_00928b70 (refused: "wrong"),
   CAR_LEFT_BLINKER → FUN_00928a20, CAR_RIGHT_BLINKER → FUN_00928950, radio keys (FUN_00929710), MAP.
7. FUN_009292c0 (BACK_ON_TRACK hold, `HUD/BOT`), then the auto close: if menu ≠ 0 and timer > 0: timer -= dt,
   ≤ 0 → FUN_00929100.

No speed / gear condition anywhere in this path: the roof, windows and blinkers work while driving [CODE].

### 3.1 HUD flags: FUN_00928840 [CODE]

Root variables of HUD.vmf before "open": `/:HasHood` (int, car vfunc +0x34 = FUN_00c66250: the Hardtop animation
player has its clip `\HardTop.anm`, or the car has the HARDTOP node, slot 39 car +0x15b0), `/:HasWindows`
(FUN_009287b0: DBCarPhysicsData record +0x45c **_windows_type ≠ 3**), `/:HasTurnSignals` (FUN_00928720: record
+0x1f7 **_hasturnsignals ≠ 0**). Field offsets: 0x470-byte rows, fields in name order (_hasdoors +0x1f4,
_haspowersteering, _hasshortgear, _hasturnsignals +0x1f7; serialized at 364..367) [DATA]. `/:IsBike` is set by
the game from the same category test as FUN_00ad9690 (e.g. 0x00a7676a) [CODE].

California (DBCarPhysicsData[228], `_id_car` 387): `_car_body` 4, `_windows_type` 2, `_hasturnsignals` 1,
`_vehiclecategory` 3, Hardtop.anm present → all three accessories available [DATA].

### 3.2 HUD/ig_menu (HUD.vmf sprite 1075, placed as `ig_menu` in the HUD clip) [DATA]

* Frame 0 functions: `ValidIGM(valid_icon)`: `valid_icon` gotoLabel "valid" + play, `valid_icon/icon` and
  `valid_icon/valid_txt` goto the label, `valid_icon/icon/lock` hidden, then gotoLabel **"close"** + play.
  `SetIcons()`: `IG_Title = m_Accessories` (loc "Accessoires" / "Accessories"); car set (IsBike false):
  `icon_1` = "hood" (top), `icon_2` = "clir" (right), `icon_3` = "wind" (bottom), `icon_4` = "clil" (left);
  `icon_n/lock` visible when `!HasHood` / `!HasTurnSignals` / `!HasWindows` / `!HasTurnSignals`.
* Labels: `default` f0 (stop, hidden icons), **`open` f10** (`FSCommand:sndp:ig_open`, SetIcons(), the four
  arcs and the icons spread from the centre over f14..f23, `ig_center` (steering wheel) "open"), `stop` f28,
  **`close` f37** (`ig_center` "close", `FSCommand:sndp:ig_close`), back to `default` at f54.
* Icon sprite 1050: labels `clil` f0, `clir` f7 (the clil shape mirrored), `hood` f14, `wind` f21, `taun` f27,
  `hell` f36, `clbl` f42, `clbr` f49; each has a `lock` child. Texts (sprite 1059): `/:m_IG_Hood`, `/:m_Windows_`,
  `/:m_IG_Clil`, `/:m_IG_Clir`, ...
* `sndp:*` are flash-UI sound cues; their handler is not in readable code [INF] (not played by OpenTDU2).

## 4. Roof (hardtop) [CODE]

* Command byte **player +0x3b5** (player = DAT_010257e8 +0x79bc). FUN_00928da0: needs a car (vfunc +0x2c == 0)
  with a hood (vfunc +0x34) and the roof **not moving** (FUN_00c662b0 ≠ 2); -1 → 1; 0 → 1 if the roof fraction
  (FUN_00c66a80) ≤ 0.5 else -1; 1 → -1 (1 = open, -1 = close). FUN_00be48a0: network message only.
* FUN_00bbc650 copies it every frame into the car state +0x1e4; FUN_00c3fcc0 (car spawn / first freeride)
  resets +0x3b5 = +0x3a8 = 0 after opening the roof.
* Car tick FUN_00c67c60: +0x1e4 > 0 → FUN_00c668f0 (player mode 0 = forward, only when not moving), fraction
  > 0.99 (DAT_00f49ff8) → +0x1e8 = 1, +0x1e4 = 0; < 0 → FUN_00c668c0 (mode 1 = backward), fraction < 0.01
  (DAT_00ede410) → +0x1e8 = -1; 0 → +0x1e8 > 0: FUN_00c66920(1.0) (t = 0.99999 × duration, mode 2),
  < 0: FUN_00c66920(0).
* Hardtop player (car +0xb8d0, california_spec.md §2): speed +0x8d4 = **1.0** (FUN_00c68c80), FUN_00c4ac00:
  t ± dt, mode 2 once FUN_00c4a770 ≠ 2 (2 while 0 < t < end − 0.001). Hardtop.anm of the California lasts
  **17.0 s** [DATA] → the roof opens / closes in 17 s. Fraction = t / duration clamped (FUN_00c4ab50).
* Cars with only a detachable HARDTOP node (no clip): `HasHood` is set, the roof fraction is car +0x2ae0 moved
  by 0.5 / s (FUN_00c4aa50) [CODE]; OpenTDU2 animates only the Hardtop.anm roofs [INF: no such car tested].

## 5. Side windows [CODE]

* Command byte **player +0x3a8**. FUN_00928b70: needs `_windows_type ≠ 3` and a car; -1 → 1; 0 → 1 if the left
  window lowering (car +0xa780) > 0.5 else -1; 1 → -1 (1 = raise, -1 = lower).
* Car tick FUN_00c67c60 with +0x1e0 (copy of +0x3a8): > 0 → FUN_004514f0(−dt × 0.333) / FUN_004514a0 (same
  delta) [ENC: taken as both windows += delta, INF] until +0xa780 < 0.01 → +0x1ea = 1; < 0 → +dt × 0.333 until
  > 0.99 → +0x1ea = -1; 0 → +0x1ea > 0: FUN_00c66150(0) / FUN_00c66110(0), < 0: (1.0). Rate DAT_00f4b170 =
  **0.333 / s** (double) → a full travel takes **3.0 s**.
* FUN_00c66150 / FUN_00c66110 clamp to [0, 1] into car +0xa780 (left) / +0xa784 (right), dirty flag +0xa693;
  FUN_00c674a0 places WND_L1 / WND_R1 between their key poses with FUN_00451560 [ENC].
* Key poses (FUN_00c6a610): under DOOR_L the nodes named `WND_` + "L1" / "L2", under DOOR_R "R1" / "R2",
  decomposed by FUN_009544e0 into car +0xa6b0 / +0xa6e0 (left) and +0xa720 / +0xa750 (right); no windows for
  the car id 999. 0 = WND_x1 rest (up), 1 = WND_x2 (down). California: WND_L2 is 0.39 m lower than WND_L1 and
  tilted by ~16° about Z [DATA]. OpenTDU2 blends the two poses linearly [INF].
* At the first freeride the roof is open and the windows are down (FUN_00c3fcc0: FUN_00c66150 / FUN_00c66110(1.0)).

## 6. Blinkers [CODE]

* **player +0x3b2 left / +0x3b3 right**. FUN_00928a20 (left): needs `_hasturnsignals` and a car or a bike;
  left off → left on, right off; left on → left off (FUN_00be4720: network message). FUN_00928950: the same
  for the right one. No hazard mode in the readable code.
* FUN_00bbc650 copies them to the car state +0x1db / +0x1dc; **FUN_00c57ef0** (car light update): timer car
  +0x1f10 = -1 when both off, set to 0 when one is switched on; timer = fmod(timer + dt, 1.0); lamps lit while
  timer < **0.6** (DAT_00ef2754) → period **1.0 s, 60 % on**; the left / right lamp intensities are zeroed unless
  that side is on, then the lamp setters (DAT_01230528 / DAT_01230530 + vehicle kind × 200).
* No blinker sound and no HUD / gauge indicator in the readable code or in GAUGES.vmf / DASHBRD.vmf [DATA + CODE].

## 7. OpenTDU2

* `source/input/game_key_map.{h,cpp}`: Default_KB key map + OpenTDU2 bindings (§2), `#command:` resolver,
  channel → SDL scancode by key position (§1); used by FIRST_WM and FREERIDE.
* `source/game/freeride/car_accessories.{h,cpp}`: §3 (menu 1 only; the radio menu 2 is not implemented),
  §4 / §5 commands and car tick, blinker state → `CinematicCar::setIndicator` (car lights engineer's API; blink
  phase and lamps are in the car lights code).
* `FreeRideHud::openAccessoryMenu / validAccessory / closeAccessoryMenu`: §3.1 / §3.2 on `/HUD/ig_menu`.
* `CinematicCar::setHardtopTime` (resamples the HANIM_S pose), `setWindowLowering` (WND_x1 between its rest and
  WND_x2, on top of the door rotation).
* Test aids: `-fixeddt <ms>` with `-fixedtimestep` (e.g. 100 ms steps: the 17 s roof in 170 frames),
  `-chaseorbit <deg>` (chase camera turned about the car, side views), `-startstopped`; autoplay `key=4`,
  `key=Keypad 4`, `key=Z` (CAR_WINDOWS).
* Verified (FREERIDE, California, autoplay): menu open (main-row 4 and numpad 4), roof close / open (17 s, trunk
  lid motion of Hardtop.anm), windows up / down from the menu (numpad 2, main-row 2) and with CAR_WINDOWS (Z
  position), left / right blinkers from the menu (4 / 6), main-row 2 ignored when the menu is closed.
