# TDU2 Eden flash: DefineEditText layout and Eden font metrics (PC TestDrive2.exe)

Tags: [CODE] = read from readable x86 (0x83xxxx..0x86xxxx only, nothing below 0x6c1000 decrypted) ·
[DATA] = read from GENERAL.vmf / MAIN.vmf · [INF] = inferred (reason given) · [ENC] = called code is SecuROM-encrypted.

Decompiled code: `/tmp/claude-0/edittext_dec1.c` (factory, ctor, draw, layout, line breaker, parser).
Tools: `/tmp/claude-0/et/fontdump.py`, `/tmp/claude-0/et/fontrest.py` (DefineFontEden header, advances, bounds),
`/tmp/claude-0/et/sim.py` (reimplementation of the exe layout, used for §6).

## 0. Summary of what is wrong in OpenTDU2

| item | exe | OpenTDU2 now | effect on ch480 |
|---|---|---|---|
| wrap width | `XMax − XMin` (rect shifted +40 tw, **not shrunk**) | `XMax − XMin − 80` | **this causes the 6th line** (line 2 needs 523.83 px, OpenTDU2 allows only 521.4) |
| line pitch | `(1024 + font.Leading + trunc(edit.Leading·1024/H))` units | `(Ascent+Descent+Leading)/1024·H + edit.Leading` | 18.926 px vs 20.83 px |
| glyph quad x0 | `pen − OffsetX` | `pen + OffsetX` | glyphs drawn 2·\|OffsetX\| units too far left (≈0.4–1.7 px at 20 px) |
| units per em | 1024, `s = FontHeight/1024` twips per unit | 1024 | OK |
| advances | `(short)glyph.Advance`, no letter spacing, **no kerning** | same | OK |
| wrap trigger | Multiline flag (0x20). WordWrap (0x40) is ignored | WordWrap | — |

## 1. Call chain [CODE]

- Tag handler table (filled at 0x846b41..0x846c8d, slot = code·4+4): 0x25 → 0x56ab30, 0x7d → 0x56ad90.
  Both factories are [ENC]. The EditText definition ctor is 0x83fa20 (vtable 0xf3cf0c) → parser **FUN_0083f6c0**.
- Character factory FUN_00858e00: type 0x25 → alloc 0x84 → FUN_0083df50 (object vtable 0xf3cf30).
  Vtable slot 5 = draw **FUN_0083e390**.
- Draw → FUN_0083dfe0 (layout setup) → FUN_00849a70 (metrics) → FUN_0084a520 → FUN_0084a460 / FUN_00849dd0
  (string → glyph records) → **FUN_00849730** (line breaking, calls FUN_008492b0 per line) → FUN_00848ef0
  (alignment). Then the draw emits quads with FUN_0083de00.

## 2. DefineEditText parse (FUN_0083f6c0) [CODE]

Definition object fields (all little-endian reads from the tag stream, **flags are a plain u16 LE**):

| def off | content |
|---|---|
| +0x08 | u16 character id |
| +0x0c,+0x10,+0x14,+0x18 | rect XMin, XMax, YMin, YMax (int twips, SWF bit RECT via FUN_00decb50), **then each += 0x28 (40 tw = 2 px)** |
| +0x1c | u16 flags: bit0 HasFont, bit1 HasMaxLength, bit2 HasColor, bit3 ReadOnly, bit4 Password, bit5 Multiline, bit6 WordWrap, bit7 HasText, bit8 UseOutlines, bit9 HTML, bit10 WasStatic, bit11 Border, bit12 NoSelect, bit13 HasLayout |
| +0x1e, +0x20 | u16 font id, u16 **font height (twips)** (if bit0) |
| +0x22 | RGBA colour (if bit2, else 0xffffffff) |
| +0x26 | max length (if bit1, else 0xffff) |
| +0x28 | align (u8 → int): 0 left, 1 right, 2 center, 3 justify (if bit13) |
| +0x2c, +0x2e, +0x30 | u16 left margin, right margin, indent |
| +0x32 | s16 leading (twips) |

All four rect values get +40. So the box is moved 2 px right and down, but **its width and height stay the same**.

What the flags do in the layout (FUN_0083dfe0, FUN_00849730, FUN_00849dd0) [CODE]:
- bit0 HasFont: required for drawing at all.
- bit5 **Multiline** → layout+0x60. This flag alone turns line breaking on. bit6 WordWrap is never tested.
- bit3 ReadOnly → layout+0x98d. It enables `#color:…#`, `#endcolor#`, `#nocolor#`, `#underline#`, `#nounderline#`,
  `#bitmap:…#` and `#command:…#` markup (strings at 0x849e7b..0x84a06f). It also enables the "..." ellipsis on overflow.
- bit4 Password → layout+0x98c (every glyph is replaced by '*').
- bit7 HasText: there is an HTML-ish canonicalisation path ("TempString"/"TempStringCanonic").

ch480 [DATA]: rect tw (−40, 10468, −40, 3502), flags 0x31ed (HasFont, Color, ReadOnly, Multiline, WordWrap, HasText,
UseOutlines, NoSelect, HasLayout), font 1, height 400 tw (20 px), align 0, margins 0/0, indent 0, leading −60 tw.

## 3. Font metrics and scale (FUN_0083dfe0, FUN_00849a70) [CODE]

- `s = FontHeight · 0.0009765625` (= H/1024; constant at 0xf4a5a8). This is twips per font unit. **1024 units/em is confirmed.**
  Layout runs in integer font units. The draw matrix is scale (s, s), and glyph quads are in font units.
- The rect is scaled by (1/s, 1/s): `FUN_0056c790(&rect, 1/s, 1/s)`. That routine is [ENC], so whether it truncates or rounds
  is unknown. For ch480 it does not matter (see §6).
- layout+0x30 = H, +0x34 = font **Ascent**, +0x38 Descent, +0x3c font **Leading**. These come from the EditText's own
  DefineFontEden (font+0x2c/+0x2e/+0x30). [INF] The order mirrors the file's ascent/descent/leading triple.
- Glyph font fallback: if the flag at 0x13e279d is set and the font name equals the configured name at 0x13e2758, glyphs come
  from the global font at 0x13e2798. The setup at 0x85ee08 copies "Berlin Sans FB", then "MAIN". Metrics still come from the
  own font. [INF] The global font is MAIN.vmf font 116. GENERAL font 1 has 0 textured glyphs [DATA]. Glyph lookup order
  (FUN_00849dd0): own font, then the fallback font. A glyph found in neither is **not emitted (no advance)**.
- layout+0x50 **space advance** = `(short)glyph(' ').Advance` (default 80 if there is no glyph). layout+0x54 = advance of '.'
  (or space/3), used for the ellipsis.
- layout+0x58 **line pitch (units)** = `idiv(edit.Leading << 10, H) + 0x400 + font.Leading`. This is a C integer division,
  so it truncates toward 0.
  - ch480: `trunc(−61440/400) = −153` → `−153 + 1024 + 98 = 969` units = 378.52 tw = **18.926 px**.
  - Note: font Ascent and Descent are not part of the pitch. For Berlin Sans, 914+208 = 1122 = 1024+98, so the value matches
    by coincidence.
- Tab stop = 4 · space advance (global 0x13e2740 = 4, set at 0x85f0e6).

## 4. Glyph records and line breaking (FUN_00849dd0, FUN_00849730, FUN_008492b0) [CODE]

Record, 0x18 bytes: `{int code, int adv, int x, int y, Glyph* g, u8 fromFallback}`. `adv = (short)g+0x1e`.
' ', '\t', '\n' and '\r' get records with adv 0 and no glyph. **No kerning, no letter spacing, no rounding** beyond integer
font units.

Box in units: `left = (XMin+40)/s`, `right = (XMax+40)/s`, `top = (YMin+40)/s`, `bottom = (YMax+40)/s`.

```
pen = left; y = top + indent(def+0x30!) + ascent        // first baseline (the indent is added vertically: Eden quirk)
lineStart = 0; lastSpace = none
for each record i:
  if '\n' or '\r': rec.x = pen; newline(i)
  after = pen + (space ? spaceAdv : tab ? ((pen-left)/(4*spaceAdv)+1)*4*spaceAdv /*no +left*/ : rec.adv)
  if after > right:                     // strict
     if !Multiline: truncate here (ReadOnly → "..." via FUN_00849520) and stop
     if space/tab:  rec.x = after; newline(i)                        // the space ends the line
     elif i == lineStart: rec.x = pen; newline(i)                     // a lone glyph wider than the box stays
     elif lastSpace >= lineStart: newline(lastSpace); i = lastSpace+1 // word wrap, re-layout the word on the next line
     else: newline(i-1); i stays                                      // no space in the line: break before this glyph
  else: rec.x = pen; rec.y = y; pen = after   (space: lastSpace = i)
newline(end): store line [lineStart,end]; pen = left; y += pitch; lineStart = end+1;
              continue only if (y < bottom && Multiline); otherwise drop all later records
              (ReadOnly: FUN_00849520 drops glyphs until x + 3*dotAdv <= right, then appends "...")
```

So lines whose next baseline is not above `bottom` are not drawn.

Alignment (FUN_00848ef0, from the last record of each line):
- `shift = right − rec[end].x − rec[end].adv`.
- align 1 (right): add `shift`. align 2 (center): add `shift/2` (int). align 3 (justify): spread `shift` over the spaces,
  except on the last line and on lines that end with '\n'.
- A space that ends the line by word wrap keeps x = its pen-before position, so it does not count. A space that itself
  overflowed has x = pen after it.

## 5. Glyph quads (FUN_0083de00, draw FUN_0083e390) [CODE]

Glyph object: +0x14 u16 Width, +0x16 u16 Height, +0x18 s16 OffsetX, +0x1a s16 OffsetY, +0x1e s16 Advance, +4..+0x10 UVs.
[INF] The layout mirrors the 26-byte file record: OffsetX at file+22, OffsetY at file+24.

```
x0 = rec.x − OffsetX      y0 = rec.y − OffsetY      x1 = x0 + Width      y1 = y0 + Height   (font units, ×s → twips)
```

[DATA] check: with `pen − OffsetX`, every glyph image starts 6–9 units left of its ink bound. Examples: 'N' ox −43 → image at
+43, ink xmin 52. '!' ox −52 → +52, ink 59. 'A' ox 2 → −2, ink 5. With OpenTDU2's `pen + OffsetX` the left padding would
be −34 for 'N' and 111 for '!'.

Other draw details:
- Some passes inset the quad by `0.1·0.5·W` and `0.1·0.5·H` on each side (flag from FUN_0084b4f0; purpose unknown). It does
  not affect layout.
- When 0x13e272c is set, the object translation is snapped to whole screen pixels (`round(t/px + 0.5)`). The glyph scale is
  unchanged.

## 6. GENERAL ch480 check with the real font data [DATA]+[CODE]

GENERAL font 1 = MAIN font 116 'Berlin Sans FB': Ascent 914, Descent 208, Leading 98. The advance tables are identical, and
there is no kerning table (the bounds table ends exactly at the tag end).
Advances used: space 256, N 735, o 524, T 526, D 697, U 676, 2 497, p 578, r 341, g 533, e 494, s 306, i 226, n 536,
d 570, a 568, c 412, u 565, l 226, b 568, f 301, y 488, H 733, . 220, t 350, m 859, w 747, v 518, h 533, I 243, O 743,
K 611, ',' 220, R 609, C 607 (full list: `/tmp/claude-0/et/main_font116.txt`).

Box units: left 0, right `10508/0.390625 = 26900.48` (26900 with truncation), top 0, bottom 9067. Result of `sim.py`:

| line | baseline (local px) | pen width (units → px) | quad span px | text |
|---|---|---|---|---|
| 1 | 17.852 | 25210 → 492.38 | 491.21 | No TDU2 progression data could be found on your HDD. To |
| 2 | 36.777 | 26820 → **523.83** | 522.89 | start a new game you must not have previous TDU2 save data. |
| 3 | 55.703 | 26574 → 519.02 | 517.91 | If you choose OK to start a new game, any previous TDU2 save |
| 4 | 74.629 | 8729 → 170.49 | 169.55 | data will be deleted. |
| 5 | 93.555 | 18261 → 356.66 | 355.65 | Choose Retry to retry loading a save game. |

- **5 lines**, pitch **18.926 px**. Line 1 + " start" = 27381 > 26900. Line 2 = 26820 ≤ 26900 (80 units = 1.6 px of slack,
  so truncation vs rounding of the rect does not matter).
- With OpenTDU2's 521.4 px (26695 units), line 2 overflows. That gives 6 lines, which is the observed bug.
- Stage positions: text origin (433.9, 560.4) (avert_pc_spec). Baselines at stage y 578.25, 597.18, 616.10, 635.03, 653.96.
  The pen starts at x 433.9, i.e. XMin+2 px.
- [INF] Measured pitch ≈18.75 vs 18.926 from the code: 0.7 px over 4 pitches, which is within screenshot measurement error.
- [INF] The quoted "line 1 ≈1.5% wider" is **not** explained by the code. The pen widths are the same in both (25210 units).
  The OffsetX sign bug makes OpenTDU2's quad span 25214 vs 25150 units: +1.25 px, about +0.25%, plus a 1.7 px left shift of
  'N'. The rest is probably screenshot scale or measurement.

## 7. Patch description (do not apply blindly; OpenTDU2 sources were not modified)

### render/font.cpp (`createFromFlash`) and font.h

1. Quad X: `glyph.X0 = -source.OffsetX / 1024.f; glyph.X1 = glyph.X0 + source.Width / 1024.f;`.
   Y stays as it is: `Y0 = -OffsetY/1024`, `Y1 = Y0 + Height/1024`.
2. Advance = `source.Advance / 1024.f` exactly. Drop the `Width + 2·OffsetX + 0.03` guess. The exe has no letter spacing:
   a glyph without a table entry still gets its (short) field, and a code not in any font is skipped with advance 0.
3. Add `emLeading = desc.Leading / 1024.f`. Keep `ascent = desc.Ascent/1024`. For EditText use
   `pitchEm = 1 + emLeading`, not `(Ascent+Descent+Leading)/1024`. If other text users rely on `lineHeight`, add a new
   accessor (e.g. `getEditTextPitchEm()`) rather than changing `lineHeight`.
4. `spaceAdvance` = ' ' advance/1024 (default 80/1024). Add `dotAdvance` = '.' advance (default space/3) for the ellipsis.
5. The metrics (ascent, leading) must come from the EditText's own DefineFontEden (GENERAL font 1). The glyphs and the
   space/dot advances come from the fallback "Berlin Sans FB" font in MAIN. Both have the same values for this font.

### flash/flash_definitions.cpp (`parseEditText`)

- Keep the flags as a u16 LE. Store `bMultiline = flags & 0x20` (wrap switch), `bReadOnly = flags & 0x08`
  (markup and ellipsis), `bPassword = flags & 0x10`.
- Parse LeftMargin, RightMargin and Indent: `u16` at layout+1, +3, +5. The leading is at +7 (already read).

### flash/flash_player.cpp (`drawEditText`)

Work in integer font units, then convert with `s = FontHeight/1024` (twips per unit):

```cpp
const float s = text.FontHeight / 1024.0f;
const int left   = int((text.XMin + 40) / s), right  = int((text.XMax + 40) / s);   // NO -80
const int top    = int((text.YMin + 40) / s), bottom = int((text.YMax + 40) / s);
const int ascU   = ownFont.Ascent;                                                    // 914
const int pitchU = int(text.Leading * 1024 / text.FontHeight) /*trunc*/ + 1024 + ownFont.Leading;  // 969
int pen = left, y = top + text.Indent + ascU;
// per character (UTF-8 decoded, after #markup# removal):
//   adv = glyph ? glyph.Advance : (space ? spaceAdvU : tab ? tabStop : skip)
//   if (pen + adv > right && text.bMultiline): apply the break rules of section 4 (break at the last space, else
//     before the char, a lone first char stays); new line: pen = left; y += pitchU; stop when !(y < bottom).
//   no Multiline: stop at the overflow (ReadOnly → back off until pen + 3*dotAdvU <= right, then add "...").
// emit quad: x0 = pen - g.OffsetX, y0 = y - g.OffsetY, x1 = x0 + g.Width, y1 = y0 + g.Height  (all *s → twips)
// alignment: shift = right - lineEndPen (word-wrap trailing space excluded); center = shift/2 (int); justify per section 4.
```

This replaces the current `boxLeft = XMin+40`, `boxWidth = XMax−XMin−80`,
`baseline += lineHeight*size + Leading` and the `measureText(candidate) > boxWidth` word test. The greedy per-character
rule gives the same breaks as a word test that includes the space before the word, but it also covers character breaks
inside long words.
Expected for ch480: 5 lines, baselines 17.852 + 18.926·k px below YMin+2 px, line 2 = 523.83 px in a 525.4 px box.
