# Inline key/button icons in flash text (`#command:` / `#bitmap:`)

Tags: [CODE] readable exe code (VA >= 0x6c1000), [DATA] game files, [INF] inference, [ENC] SecuROM (not opened).

## 1. Resolving `#command:ACTION#`: FUN_00d87cc0 [CODE]

The resolver is the global DAT_013e281c, vfunc +4. It walks the current key map at DAT_010017f4: count at +0x768c, entries of 0x180 bytes starting at +0xc.

The entry layout comes from the key map loader at 0xac9b26..0xac9c9a [CODE]:

| Offset | Field |
|---|---|
| +0x00 | ActionName (0x20) |
| +0x20 | ChannelName (0x20) |
| +0x40 | ChannelNameAND (0x20) |
| +0x60 | ChannelLocName (0x104) |
| +0x164 | DevInstanceHID |
| +0x178 | DevType |
| +0x179 | DevSubType |
| +0x17a | DevIndice |

- No key map entries → NULL: the markup is dropped.
- `LEFT_PAD`: if all 4 `AVT_MOVE_*` entries have DevSubType == 3 → `#bitmap:x360_LS#`. Otherwise the keyboard form, using the first AVT_MOVE_* entry's ChannelLocName.
- `RIGHT_PAD`: if all 4 `AVT_LOOK_*` entries have DevSubType == 3 → `#bitmap:x360_RS#`. Otherwise `[loc 0x5af]` in the keyboard form.
- Every other action (0xd87cf1..0xd87d73, 0xd8818d..0xd8828a):
  - The first entry whose name equals the action is used. The comparison is `call [0xe4141c]`.
  - If that entry's **DevSubType == 3** (X360 pad) → `"%s"` (0xeabc60) of the pad table (§2).
  - Otherwise → **`#color:ff6600#[%s]#endcolor#`** (0xee61f4), filled with the entry's **ChannelLocName**.
- Correction to garage_car_spec §1.4:
  - `[0xe4141c]` is an import (IAT) slot. It is filled at runtime; the static value 0x00b5f76c points at int3 padding.
  - It is called everywhere as `f(str, const) == 0`. So it is a CRT string comparison [INF], not encrypted code.
  - The general branch is therefore readable [CODE].

### PC keyboard result [CODE+DATA]

Default_KB.xmb has DevType = DevSubType = 0 for all 76 entries. With the keyboard, the original therefore shows the orange text `[4 (PAVE NUM.)]` / `[HAUT]`, and no icon. The icons in the reference screenshots only appear for key map entries bound to an X360 pad.

## 2. Pad channel → bitmap name: FUN_00d877b0 [CODE]

| Channel | Bitmap |
|---|---|
| BUTTON_0..11 | x360_A, B, X, Y, LB, RB, LT, **RT**, Start, Back, LS_clic, RS_clic |
| STICK_0_LEFT / RIGHT | x360_LS |
| STICK_0_UP | x360_LS_Up |
| STICK_0_DOWN | x360_LS_Down |
| STICK_1_* | x360_RS |
| LEFT / RIGHT / UP / DOWN | x360_DPad_Left / _Right / _Up / _Down |
| anything else | "" (0xf35018) |

- The output is wrapped as `#bitmap:<name>#`.
- Screenshots: ACCESSORIES_LEFT → DPad_Left; CAR_ACCEL → RT (BUTTON_7).
- The default pad key map is not in the data: only Default_KB.xmb ships. The action-name strings at 0xeec23c.. have no readable references [ENC].

## 3. Which device [CODE]

- The choice is per key map entry (DevSubType == 3), i.e. the controller configured in the options. It is not "last used".
- FUN_0093d620 sets profile option +0x45 `IsX360PadIconsUsed` = (selected device type == 3), through FUN_00acaaf0.
  - The same call sets the flash bool `/:IsPC_X360`. It is re-sent on FSCommand `sbdd`.
  - The movies use it for their own prompt clips.
  - Some `$$bitmaps$$` sprites (e.g. x360_LS) also jump to a frame label `x360|pc|ps3|ps3_jap` from `/:WhatPlatform` [DATA].
- Tutorial text: FUN_00d62a90 [CODE].
  - DBTutoMess `_messpc_hc` is used unless the current device name is "Microsoft X360 Pad"; `_mess_hc` is the fallback.
  - `_messpc_hc` is 0 in all 77 rows [DATA], so it is always `_mess_hc`.

## 4. The bitmap registry `$$bitmaps$$` [CODE+DATA]

- FUN_0085ad40 (place): a sprite instance named `$$bitmaps$$` gets flag +0x44.
- Objects placed into it go through FUN_00dd8830 and are added to the global map DAT_013e2720, keyed by instance name.
- FUN_00dd8830 checks that the first display-list object is a shape whose first fill is a bitmap (0x40..0x43, bitmap id +0x34) of type 0x7e.
- Stored values:
  - scale x = |column 0| of the instance matrix (FUN_0084e340)
  - scale y = |column 1| of the instance matrix (FUN_0084e310)
  - the bitmap object, with +0xa width, +0xc height, +0x10..+0x1c UV rect, +0x20 texture
- Entries are removed on movie unload: FUN_0085d500 → FUN_00dd8770.
- GENERAL.vmf root f0 places ch612 `$$bitmaps$$` [DATA]. It holds 21 x360_* sprites and 21 PS3_* sprites.
  - The icons are 45x45 px (x360_LS: 76x45), with fill matrix 20 tw/px and placement scale 1.
  - There are no PC keyboard icons.

## 5. Layout and drawing [CODE]

**Parsing (FUN_00849dd0, read-only fields only).**
- `#bitmap:name#` (name ≤ 0x7f characters) → FUN_00849cc0.
- At most 0x20 bitmaps per layout (+0x988; table +0x788, 16 bytes each: glyph index, sx, sy, bitmap).
- The pushed record has code = −n and **advance W = ftol((Ascent − Descent) · w / 20.0 · sx)**. The 20.0 is at 0xf49840.
- A name that is not in the registry adds nothing.

**Rect (FUN_00848c90).**
- H = ftol((Ascent − Descent) · h / 20.0 · sy)
- x0 = pen, x1 = pen + W
- bottom = baseline + (Ascent + Descent + H) · 0.5 − Ascent (0.5 at 0xf49748)
- top = bottom − H
- The icon is centred on the [baseline − Ascent, baseline + Descent] box.

**Units.** Ascent and Descent are layout +0x34 / +0x38, in font units (1024/em). Everything is scaled by FontHeight/1024.

**Drawing.** FUN_0083e390 draws the bitmaps after the glyph batch, one quad each with the bitmap's texture/UV, white through the object's colour transform [INF on the colour bytes].

**Worked example.** TutText is font 1 "Berlin Sans FB", height 360 tw, asc 914, desc 208. For a 45 px icon: W = H = 1588 units ≈ 1.55 em ≈ 27.9 px.

## 6. OpenTDU2 implementation

**source/flash/flash_player.h/.cpp**
- `TextBitmap` registry (DAT_013e2720), filled in `placeObject` for children of `$$bitmaps$$` (`registerTextBitmap`). It is purged on `unload`.
- `#bitmap:` parsing, sizes and placement exactly as in §5. The quads are drawn after the glyphs.
- `PadChannelMarkup(channel)` (§2) and `KeyboardChannelMarkup(locName)` helpers for the resolver.
- Test aid `-padcommand ACTION=CHANNEL[,...]` stands for a key map entry with DevSubType 3, because no pad key map ships. Without it, nothing changes: keyboard form.
- `-logtext` also prints the font height, ascent and descent.

**source/input/gs_input.h/.cpp**
- Additive `getLastDevice()` (keyboard/mouse vs pad).
- [INF] It is a convenience. The original uses the configured controller (§3).

**Not changed:** `ResolveCommand` (now `ResolveGameCommand` in `source/input/game_key_map.cpp`). It still emits the keyboard form, which is correct for Default_KB. With a pad key map, it should emit `FlashPlayer::PadChannelMarkup(entry.ChannelName)` when DevSubType == 3.

## 7. OpenTDU2 user binding: main-row 4 for ACCESSORIES_LEFT (not game data)

User request (Mac keyboard without numpad): the door action ACCESSORIES_LEFT (Default_KB `PAD_4`,
"4 (PAVE NUM.)") also answers the **main-row 4** key. This is an OpenTDU2 binding, not from the game files.

- `kUserBindings` in `source/input/game_key_map.cpp` (moved from `gm_first_walk_mode.cpp`; the accessory
  bindings 8 / 6 / 2 of `docs/re/accessories_spec.md` §2 are there too): `{ACCESSORIES_LEFT, ChannelName "4", ChannelLocName "4"}`, put
  before the Default_KB entries of the action. The resolver takes the first entry (§1), so the tip reads
  **`[4]`**; numpad 4 (`PAD_4`) still opens the door.
- Names as the game writes them for that key [DATA]:
  - Channel-name table in `.data` at **0x00f73b08**: 32-byte names indexed by DirectInput key code (0 "Unamed",
    1 "ESC", **2..0xb "1".."0"**, 0x10 "A", 0x11 "Z" (AZERTY), …, 0x4b "PAD_4", …, 0x52 "PAD_0"). DIK_4 = 5 → "4".
    No readable code references the table [INF: used by encrypted code].
  - Default_KB: RADIO_CHANNEL / RADIO_VOL_LESS / RADIO_VOL_MORE = channels "1" / "2" / "3" with ChannelLocName
    "1" / "2" / "3". The ChannelLocName strings are not in the loc database (they look like the French Windows key
    names: "PAVE NUM.", "RET.ARR", "PG.PREC") [INF].
  - No Default_KB action uses the main-row "4".
- SDL scancodes are key positions (like DIK codes): `SDL_SCANCODE_4` is the main-row 4 key whatever the layout.
- Test aid: autoplay `key=<SDL scancode name>` (e.g. `key=4`, `key=Keypad 4`) injects a raw key press.

