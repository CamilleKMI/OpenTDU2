# GENERAL `/avert` "no progression data" popup on PC: spec

Tags: [CODE] = readable exe code (TestDrive2.exe, nothing below 0x6c1000 touched). [DATA] = GENERAL.vmf
(Euro/Bnk/FrontEnd/HiRes/general.bnk, extracted copy at /tmp/claude-0/gen/GENERAL.vmf). [INF] = inference.
Coordinates are stage px (1280×960) unless stated. Screen mapping (bootmenu_spec §5, ratio mode 1, 16:9):
`screenX = stageX·W/1280`, `screenY = (stageY − 120)·H/720`. For 1456×825 that is ×1.1375 and ×1.14583.

---------------------------------------------------------------------------------------------------------
## 1. PC button glyphs (green check / red cross)

### 1.1 Glyph sprites [DATA]
The pad glyphs in avert `no_data` are **ch44** (button A, unnamed at d23) and **ch64** (button B, unnamed at d33).
Both are 34-frame sprites with labels `default`(f0), `x360`(f8), `pc`(f15), `ps3`(f21), `ps3_jap`(f26). Each label
frame has a `Stop` and swaps the bitmap shape at depth 5. The swap is a PlaceObject2 "move + character" with no
matrix, so the matrix stays identity:

| sprite | f0 / `x360` (d5) | **`pc` (f15, d5)** | `ps3` (f21) | `ps3_jap` (f26) |
|---|---|---|---|---|
| ch44 (A) | ch2800 = bmp2524 45×45 (Xbox green **A**) | **ch2808 = bmp2807 45×45: green CHECKMARK** | ch2805 = bmp2584 (PS cross) | ch2806 = bmp2593 (PS circle) |
| ch64 (B) | ch2796 = bmp2795 45×45 (Xbox red **B**) | **ch2798: fill 2 = bmp2797 45×45, red X (cross)**, fill matrix sx −20 / tx 900 tw (mirrored, the X is symmetric) | ch2794 = bmp2593 | ch2793 = bmp2584 |

The bitmaps are in page_001.2db (2048×1024 DXT5):
- bmp2807 at uv(0.364, 0.747) = px (745, 765).
- bmp2797 at uv(0.340, 0.841) = px (696, 861).
- bmp2524 at uv(0.364, 0.794).
- bmp2795 at uv(0.340, 0.888).

I checked them visually: 2807 is the green check, 2797 is the red X, 2524 is "A", 2795 is "B".

Both sprites also hold `pc_btn` at d1: ch2804 for A and ch2792 for B, at t(−0.3, −0.2). This is the mouse-hover glow
sprite. At its `default` frame it shows only ch2801/ch2789, a fill `#9900cc` with **alpha 0x00**, so it is
invisible. Its `over`/`loop`/`out` labels are a glow masked by d1 with clip = 3. The avert buttons have no rollover
actions, so the glow is never triggered here.

### 1.2 Selection ActionScript [DATA]
In ch44, the f0 DoAction defines `function setFrame()`. The `default` DoAction (also in f0) then runs
`setFrame(); stop();`. In ch64 the same code is inline in the f0 DoAction, which has no Stop of its own: the
`x360` frame's Stop catches it.
```
r0 = /:WhatPlatform
if (r0 === 0)       gotoLabel("x360")
else if (r0 === 1)  { if (_root.IsPC_X360 == true) gotoLabel("x360"); else gotoLabel("pc"); }
else if (r0 === 2)  gotoLabel("ps3")
else if (r0 === 3)  ch44: (_parent._parent._parent.isEMP === true ? "ps3_jap" : "ps3")   ch64: "ps3" / "ps3_jap" likewise
else                (nothing; ch44 stays on f0 = Xbox A; ch64 plays on to f8 "x360" = Xbox B)
```
`===` is StrictEquals (0x66). So WhatPlatform must be a **number**. This code runs only once, when the glyph
sprite is placed (avert f40). Set the variable **before** `/avert` reaches f40.

### 1.3 What the game sets [CODE]
- **`/:WhatPlatform = 1`** (variant type 7, int 1). There are two sites:
  - **FUN_00d8d930 @0xd8dbce..0xd8dbfd**: `mov [esp+0x20],7 ; mov [esp+0x28],1 ; push 0xee5ca4 "/:WhatPlatform" ; call movie->vfunc 0xc8 (SetVariable)`.
    It runs unconditionally after the db_* loop and FUN_00d8d2d0. The same function also sets `/:isTrial`,
    `/:HasCasino`, `/:IsCasino` and others.
    - FUN_00d8d930 is called by **FUN_00d8e200** for **GENERAL** (0xee4c74, @0xd8e26e) and **MAIN** (0xee4c90,
      @0xd8e29e). FUN_00d8e200 is called in GMLogin enter step 7 (bootmenu_spec §1.2).
    - FUN_00d8d930 also has callers at 0x93dad9, 0x99c90c, 0x9c7309 and 0x9c8880.
  - FUN_009bf000 @0x9bf0be..0x9bf0dc: the same (type 7, value 1) on the movie at `this+0x1ae400` (boot/menu movie).
- **`/:IsPC_X360`** (bool, variant type 5) is set by **FUN_00acaaf0(bool)**:
  - It stores `playerData(DAT_010017f0)+0x45 = b` and sets `/:IsPC_X360` on GENERAL (@0xacab4f) and MAIN (@0xacaba9).
  - Callers:
    - **FUN_0093d620** (controller-config apply): `b = (selectedController->type(+8) == 3)`, i.e. an
      Xbox 360 pad. Keyboard mapping "Default_KB.xmb" is used when `+4 == 0`.
    - **FUN_009c98b0** (apply loaded options): `b = playerData+0x45`.
    - FUN_0099c490 (`sbdd` FSCommand handler @0x99c8d6) also sets it on the calling movie from playerData+0x45.
- So on PC with keyboard/mouse, WhatPlatform = 1 and IsPC_X360 = false or undefined. `Equals2(undefined, true)` is
  false, so both glyphs go to **`pc`**: green check and red X. [CODE+DATA]
- In the no-save path FUN_009c98b0 does not run, so IsPC_X360 stays undefined there. That still gives `pc`. [INF]

**Our bug**: OpenTDU2 never sets `/:WhatPlatform` (grep: no occurrence in source/). So r0 is undefined, ch44
stays on f0 (Xbox A) and ch64 falls through to f8 `x360` (Xbox B).
- **Fix**: when GENERAL and MAIN are loaded, or in GMLogin enter (FUN_00d8e200), call
  `setVariable("/:WhatPlatform", Number 1)`.
- Optionally also set `/:IsPC_X360 = false` (true only if the configured controller is an X360 pad).

---------------------------------------------------------------------------------------------------------
## 2. Layout of `/avert` label `no_data` [DATA]

### 2.1 How the clip is shown [CODE/DATA]
- FUN_00d86c00 with button type 3 and the extra flag set calls `/avert` vfunc 0xdc("no_data"), i.e. gotoAndPlay.
- `/avert` = **ch515** at root **d1246, matrix t(65.4, 382.0), scale 1**, placed at root f0.
- No root ActionScript moves it. Only `external_avert` (ch447) and `avert2` (ch245) are scripted.
- **Stage offset of every avert-local coordinate = (+65.4, +382.0).**

### 2.2 Open animation (f31 `no_data` → f40 Stop) [DATA]
- **f31** action: `Play; FSCommand:gein; isLittlePopup = 0` (no trace).
- **Panel d1 = ch459** (36-frame sprite, stopped at its f0):

  | frame | t | s | alpha |
  |---|---|---|---|
  | 31 | (326.6, 194.7) | 0.742 | 0 |
  | 32 | (289.6, 181.1) | 0.853 | 0.328 |
  | 33 | (259.9, 170.2) | 0.942 | 0.594 |
  | 34 | (237.3, 162.0) | 1.010 | 0.793 |
  | 35 | (222.1, 156.3) | 1.056 | 0.930 |
  | 36 | (213.9, 153.4) | 1.080 | 1 |
  | 37 | (220.6, 155.8) | 1.060 | |
  | 38 | (227.3, 158.2) | 1.040 | |
  | 39 | (233.9, 160.7) | 1.020 | |
  | **40** | **(240.6, 163.2)** | **1** | |

- **"?" icon d37 = ch396** (bmp395 114×114):

  | frame | t | s | alpha |
  |---|---|---|---|
  | 36 | (234.8, 167.1) | 0.544 | 0 |
  | 37 | (242.3, 171.1) | 0.726 | 0.398 |
  | 38 | (248.1, 174.1) | 0.863 | 0.699 |
  | 39 | (251.9, 176.1) | 0.954 | 0.898 |
  | **40** | **(253.8, 177.1)** | **1** | cx cleared |

- Everything else appears at f40: text, labels, glyphs, ch206 and the buttons. **f40: Stop.**
- **Close**: avert button actions → `close_no_data` (f61..f71: FSCommand `geou`, panel and icon shrink and fade),
  then f71 `gotoLabel default`.

### 2.3 Resting frame f40, full display list (depth order back→front) [DATA]
Columns: matrix in avert space → stage rect → 1280×720 rect → 1456×825 rect.

| depth | char | what | matrix | stage rect | 1456×825 |
|---|---|---|---|---|---|
| d1 | ch459 sprite (f0) | **panel frame** (9-slice: corners ch31/bmp2494 25×25, edges ch33/bmp2492 ×24.657, sides ch29/bmp2501 ×7.76) + black 65% fill ch458 (`#000000a6`, 8.75..659.55 × 8..238.85) + radial gradients ch457 (d17) and ch27 (d15). **d10 ch458 is a MASK (clip=16) for d11..d16, i.e. the ch27 gradient at d15.** Local size 0..666.5 × 0..244 | t(240.6,163.2) s1 | 306.0..972.5 × 545.2..789.2 | 348.1..1106.2 × 487.2..766.8 (real ≈ 350..1097 × 494..760; the corners have transparent margins) |
| d19 | ch484 Button | invisible hit area → **OK** | t(495.4,375.0) s(3.264,1.232) | 560.8..666.9 × 737.0..777.0 | 637.9..758.6 × 707.0..752.8 |
| d21 | ch483 Button | invisible hit area → **Retry** | t(603.2,375.0) s(4.233,1.232) | 668.6..806.2 × 737.0..777.0 | 760.5..917.0 × 707.0..752.8 |
| d23 | **ch44** | glyph A (→ `pc`: green check) | t(484.4,353.4) s(1,1.002) | 549.8..594.8 × 735.4..780.5 | 625.4..676.6 × 705.1..756.8 |
| d27 | ch482 EditText `/:AvStateButtonA` | "OK" | t(566.1,361.3) s(1,1.002) | box 596.2..665.5 × 741.3..774.3; **text origin x 598.2, top 743.3** | text x 680.5, top 714.2 |
| d28 | ch481 Button | off-screen key catcher | t(−149.5,380.1) | x −84 (off-stage) | — |
| d30 | ch206 (bmp205 218×48) | inner dark vignette (opaque at top centre, fades out) | t(249.7,172.0) s(2.973,3.707) | 315.1..963.2 × 554.0..731.9 | 358.4..1095.7 × 497.3..701.2 |
| d31 | ch480 EditText `/:m_AvertText` | message | t(368.5,178.4) s1 | box 431.9..957.3 × 558.4..735.5; **text origin (433.9, 560.4)** | text origin (493.6, 504.6) |
| d32 | ch479 EditText `/:AvStateButtonB` | "Retry" | t(672.1,361.3) s(1,1.002) | box 702.2..952.4 × 741.3..771.7; **text origin x 704.2, top 743.3** | text x 801.0, top 714.2 |
| d33 | **ch64** | glyph B (→ `pc`: red X) | t(594.8,353.5) | 660.2..705.2 × 735.5..780.5 | 751.0..802.2 × 705.3..756.8 |
| d37 | ch396 (bmp395) | "?" icon | t(253.8,177.1) | 319.2..433.2 × 559.1..673.1 | 363.1..492.8 × 503.1..633.8 |

Notes on the glyph and label row:
- The row runs from check (549.8) to the end of "Retry" (≈ 704 + ≈ 55 = 759). Its centre is stage ≈ 654, which
  is screen ≈ 744. This matches the real "centred around x ≈ 745".
- Button rows are vertically centred: glyph 735.4..780.5 vs text cap height around 743..767.

### 2.4 EditText definitions [DATA] (DefineEditText flags parsed)

| char | bounds (local px) | flags | font / size | colour | layout |
|---|---|---|---|---|---|
| **ch480** `/:m_AvertText` | x −2..523.4, y −2..175.1 | HasText, **WordWrap, Multiline**, ReadOnly, HasColor, HasFont, HasLayout, NoSelect, UseOutlines. **No Border, no AutoSize, no HTML** | font 1, **20 px** | `#ffffffff` | align 0 (left), margins 0, indent 0, **leading −3 px** |
| **ch482** `/:AvStateButtonA` | x −35.3..33.95, y −2..30.95 | HasText, ReadOnly, HasColor, HasFont, HasLayout, NoSelect, UseOutlines. **No wrap, no Border** | font 1, **24 px** | white | left, leading +2 |
| **ch479** `/:AvStateButtonB` | x −35.3..214.85, y −2..28.3 | as ch482 but **without NoSelect** (f2 = 0x21). No Border | font 1, 24 px | white | left, leading +2 |

- Inner text width of ch480 = 525.4 − 4 (2 px gutters) = **521.4 px** (stage). Text origin = (xmin + 2, ymin + 2).
- The authoring text in ch480 is the French disk-full message. The game overwrites it through `/:m_AvertText`.
- Inline colours come from loc strings: `#color:5982BF#OK#nocolor#` → **#5982BF** for "OK"/"Retry".
- **No ActionScript in avert repositions or resizes anything.** There is no initText, no TxtLength, no
  `_x`/`_y`/`_width` assignment and no textHeight use.
- The only AS in the clip is: Stop/Play, FSCommand gein/geou, `isLittlePopup`, and (in other labels)
  `time_out._visible` (on_choice f20, /:IsAvertTimeOut), `erase._visible` (missing_content, /:IsErase), and
  `cancel`/`valid` `_visible` (msg, /:IsCancel, /:NoValidButton).
- Layout is purely the static f40 placements above.

### 2.5 Input [DATA]
- **ch484** (left hit area, over the check + "OK"): OverDownToOverUp (release) → `FSCommand:avye`;
  `gotoLabel close_no_data`; Play; `FSCommand:sndp:valid`.
- **ch483** (right hit area, over the X + "Retry"): release → `FSCommand:avno`; `close_no_data`; Play;
  `FSCommand:sndp:cancel`.
- **ch481** (off-screen) key conditions: **key 13 (Enter) → avye (OK)**, **key 8 (Backspace) → avno (Retry)**.
- Note the button order: d19/ch484 is A/OK and d21/ch483 is B/Retry.

---------------------------------------------------------------------------------------------------------
## 3. The dark rectangles behind "OK" / "Retry"

They are the **Up-state graphics of the hit buttons ch484 and ch483**.
- Every one of the 104 standard GENERAL buttons has the records: st1 (Up) = ch38, st2 (Over) = ch39, st8 (Hit) = ch40.
  - ch38 = rect 0..32.5 × −16.25..16.25, fill **#00CCFF alpha 0x39 (22%)**.
  - ch39 = #990000 alpha 0x50.
  - ch40 = #FF9900.
- Scaled by the matrices above, ch38 gives the teal-dark boxes at stage 560.8..666.9 and 668.6..806.2 ×
  737..777. These are exactly the rectangles in our render (lg_popup.ppm, 720p y 617..657).
- It is not EditText Border (no Border bit in ch479/ch482), not a mask and not an AS `_visible`.

**[CODE] Eden never draws button characters:**
- FUN_00858e00 is the character-instance factory. Tag 0x22 → FUN_00841f10 builds the button object, vtable
  0xf3ce14.
  - The constructor walks the button records and **only uses the record with state bit 8 (Hit)**. It copies that
    shape's bounds (`obj+0x98..` from shape+0x10..0x1c) to build the hit rectangle.
  - It creates no display children for the Up, Over or Down states.
- The display-object vtable **slot 5 is draw**:
  - Shape (vtable 0xf3cfc4) slot 5 = FUN_0083d300, which calls renderer->vfunc 0x20 with the mesh.
  - Morph/text-like objects (0xf3cf60) slot 5 = FUN_0083d600.
  - **Button slot 5 = FUN_00840c80, which is a bare `ret`.**
- So in TDU2, DefineButton2 records are only used for mouse hit-testing, plus their condition actions (key
  presses, release).

**Fix**: in `FlashPlayer::drawInstance`, `case CT_Button` should draw nothing. Keep the Hit-record bounds for mouse
picking.

---------------------------------------------------------------------------------------------------------
## 4. Renderer review: OpenTDU2 source/flash

The reference render is /tmp/claude-0/lg_popup.ppm (1280×720).
- Its panel is ≈ x 310..965, y 432..664, text at x ≈ 432, y ≈ 445, and the "?" icon ≈ 327..425 × 447..545.
- Real, converted from 1456×825 to 720p (×0.879, ×0.873): panel 307.7..964.3 × 431..663, text origin 429/441,
  icon 321..420 × 449..545.
- **So the panel, icon and text placement already match within about 2 px.** An apparent "smaller panel lower"
  usually means the two images were compared at different scales.
- The expected values are in §2.3. If a newer build differs, check that the 16:9 mapping
  `y' = (y − 120)·H/720, x' = x·W/1280` is applied.

Concrete issues:
1. **Button states drawn** (flash_player.cpp `drawInstance`, `case CT_Button`). It draws the records with
   `States & 0x01`, i.e. ch38 at 22% cyan. These are the "dark rectangles". Eden draws nothing for buttons (§3).
2. **`/:WhatPlatform` never set**. This is the cause of the Xbox A/B glyphs (§1.3). Set Number 1 on GENERAL and MAIN
   before showing the popup. Set `IsPC_X360` (bool) from the controller config if you want x360 glyphs with a pad.
   Our StrictEquals already compares number vs number; undefined vs number gives false, which is correct.
3. **ClipDepth masks ignored** (`drawClip`: `maskEnd` is assigned but unused, `(void)maskEnd`).
   - Mask shapes are skipped, but the masked depths (ClipDepth range) are drawn **unclipped**.
   - Here: the ch459 panel's d15 gradient ch27 spans local 1.9..661.4 × 3.8..238.8. Its mask ch458 (rounded,
     8.75..659.55 × 8..238.85) should clip it. Unclipped, it spills about 7 px into the top-left frame corner.
   - The pc_btn hover glow (clip = 3) is affected too (not active here).
   - [CODE] Eden stores the clip depth: FUN_0085a720 place-flag 0x40 → `*(u16*)(obj+10)`.
   - Implement stencil or scissor masking for depths in (maskDepth, ClipDepth].
4. **EditText flags**: parsing reads WordWrap (flags1 & 0x40) only.
   - Multiline (0x20), Border (f2 & 0x08), AutoSize (0x40) and HTML (0x02) are not used.
   - None of them cause the visible difference here: no field has Border or AutoSize.
   - Single-line fields (ch482/ch479, no Multiline) must not break on '\r'. This is harmless for "OK"/"Retry".
5. **Line wrapping**: ch480 inner width is 521.4 stage px at 20 px font 1, leading −3.
   - The real game wraps the English text into 5 lines ("…previous TDU2 save data." on one line); ours makes 6.
   - So our `measureText` for font 1 at 20 px is a few % too wide. Possible causes: glyph advances, a space
     width, or em vs. ascent scaling of `FontHeight`.
   - Check that the font size equals the EditText height (20 px = em) and that the advances come from the Eden
     font data. [INF]
6. **Frame actions order**: ch44's f0 has two DoActions (define `setFrame`, then call it + Stop). They must run in
   tag order with the target = the glyph clip, so GotoLabel acts on ch44. Our pending-action queue keeps the order
   and `callFunction(…, pClip, …)` uses the calling clip. That is OK once WhatPlatform is set.
   - ch64 has no Stop at f0. With WhatPlatform unset it plays to f8 (`x360`). With 1 it jumps to f15 (`pc`) and
     stops there.
7. **Sequence timing**: the glyph AS runs only when ch44/ch64 are placed (f40). WhatPlatform must be set before
   `gotoLabel("/avert","no_data")` plays through f40. Setting it after the popup opened has no effect.
