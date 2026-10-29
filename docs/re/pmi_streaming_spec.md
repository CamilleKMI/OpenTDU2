# TDU2 PMI instance visibility: LOD selection, quality factors, streaming, fade — and why OpenTDU2 pops in

Scope: how TestDrive2.exe decides which static PMI instances (V/O/S library models) are drawn and with which LOD,
which config values feed it, how sectors are streamed during the intro cinematic, and the exact differences with
OpenTDU2 `EdenWorld::loadInstances / drawInstances / loadAround` that make instances appear late.

Tags: [CODE] readable x86 (>= 0x6c1000), [DATA] files, [INF] inferred (reason given), [ENC] reached only from / implemented in
SecuROM-encrypted code (not analysed).

Files:
- Decompiled: `/tmp/claude-0/pmi_streaming_dec1.c` (LOD selectors, cell pass, impostors, cubemap pass),
  `pmi_streaming_dec2.c` (FOV LOD scale, cell distance, generic LOD distance, quality level selection),
  `pmi_streaming_dec3.c` (default quality level, settings apply/save, impostor helpers).
- Decoded config: `/tmp/claude-0/pss/q.txt` (Quality_Settings.cpr, decrypted with `bnk_tool decrypt`, same as OpenTDU2's cpr loader).
- Scripts: `/tmp/claude-0/pss/lodstat.py` (library LOD tables), `/tmp/claude-0/pss/popin.py` (pop-in simulation on the intro path,
  14 sectors of areas 5-0/5-1 extracted in `/tmp/claude-0/pss/sec/`).
- Ghidra copy used: `/tmp/claude-0/pss/gh`, helper `/tmp/claude-0/pss/dec.sh`.

---------------------------------------------------------------------------------------------------------------------

## 1. Config values

### 1.1 Handlers and globals [CODE: handler bodies at the addresses below, registered by FUN_00d6b5b0]

| key | handler | effect |
|---|---|---|
| `SET_GLOBAL_LOD_FACTOR a b` | 0xd6a2f0 | `DAT_00f7f5f8 = a` (**LOD factor**); `DAT_00fd50a8 = a < 1.0 ? 1.0 - a : 0.0`; `DAT_00f7f5fc = b` (**impostor mul**); settings+0x244 = a; if a>0 and a != DAT_00ffb670: DAT_00ffb670 = a and listeners FUN_00d72960 |
| `MAP_LOD_SELECTION_SCALE v` | 0xd69060 | `DAT_00f75f04 = v`, settings+0x24c |
| `MAP_LOD_ALWAYS_VISIBLE_RADIUS v` | 0xd69030 | `DAT_00f75f08 = v`, settings+0x250 |
| `MASSINSTANCE_VIS_MAX n` | 0xd648c0 | `DAT_00f76624 = n` (u16 at settings+0x23a) |
| `MASSINSTANCE_VIS_MAX_CUBEMAP n r` | 0xd68d10 | `DAT_00f76628 = n`, `DAT_00f76620 = r` (min pseudo-pixel radius) |
| `SetDefNbSectors n` / `SETMINNBSECTORS n` | 0xd683d0 / 0xd683b0 | byte `[this+0x8c]+0x6c` = clamp(n,1,16) (also settings+0x238) / `+0x6d` = n |
| `CAR_LOD_SCALE v` | 0xd64890 | `DAT_00f7e524` (cars only) |

Static defaults in .data [DATA]: f7f5f8 = 1.0, f7f5fc = 1.0, f75f04 = 1.0, f75f08 = 5.0, f76624 = 1000, f76628 = 1000, f76620 = 25.0.

### 1.2 Values in `Quality_Settings.cpr` ([ENVIRONMENT_QUALITY_LEVEL_n]) [DATA]

| level | SET_GLOBAL_LOD_FACTOR | MAP_LOD_SELECTION_SCALE | MAP_LOD_ALWAYS_VISIBLE_RADIUS | MASSINSTANCE_VIS_MAX | SetDefNbSectors / MIN | SET_Z_FAR |
|---|---|---|---|---|---|---|
| 0 (low) | 0.5 1.0 | 0.5 | 10.0 | 800 | 4 / 4 | 40000 |
| **1 (default)** | **1.0 1.0** | 1.0 | 7.0 | 1000 | 5 / 5 | 40000 |
| 2 | 2.0 1.0 | 1.0 | 7.0 | 4000 | 9 / 9 | 40000 |
| 3 (max) | 4.0 1.0 | 1.0 | 7.0 | 4000 | 16 / 16 | 40000 |

`[CAR_QUALITY_LEVEL_n]`: MASSINSTANCE_VIS_MAX_CUBEMAP = 1000 25 / 1000 25 / 4000 5 / 4000 1. SystemPC.cpr `[PMI]`:
SetNbMaxInstance 64000, SetNbMaxHierNode 20000; `[Memory]` STREAMING_ACTORSPMI_SIZE 716800. GamePC.cpr has no LOD/streaming key.
`ALPHAFADEINSPEED` (→ DAT_00f765d8) is a grass key and is absent from the file.

### 1.3 Which level is active [CODE]

- FUN_00d6f770(cat, level): builds `"%s_QUALITY_LEVEL_%d"` with STREAMING / ENVIRONMENT / GRAPHICS / CAR / FX / AUDIO and executes that section.
- FUN_00d6f950: if no saved settings (`FUN_00acaa10() == 0`, file `savegame/SystemDefault`) **all six categories are set to level 1**;
  otherwise the `QUALITY_SETTINGS_LOD` array of the saved profile gives the level per category.
- FUN_00d700d0 (boot) uses `DAT_00fe4c50` (ENVIRONMENT), initialised to −1 → level 0, until the profile is applied (FUN_00acabc0 copies saved
  values; note it writes the saved LOD factor to DAT_00ffb670, the listeners then refresh f7f5f8).
- The Mac install contains no `savegame` folder, so the user's actual level is not known from the files. A user running the game on
  "High/Max" uses level 2/3 [INF].

---------------------------------------------------------------------------------------------------------------------

## 2. Model instance LOD table [CODE FUN_00da0440 + DATA library.3dd]

- Model instance: `+0x08` flags, `+0x58/+0x5c` begin/end of a vector of 12-byte LOD entries stored at `+0xc0`:
  `{u32 object, u32 0, f32 dist2}` with `dist2 = INS.lod[k].lodDistance × 3.160494` (0xf4aa28, a double ≈ (16/9)²).
- `+0x70` = bounding sphere radius of LOD0's geometry (1.0 when none); `+0xf0` = INS last-LOD padding × 3.160494 (all 1111 LOD records
  have padding 0 → 0) [DATA].
- Flag `0x100` ("has LOD switching") is set when numLOD ≥ 2 **or** LOD0 distance ≠ 0. Without it the model always uses LOD0 with no
  distance limit. Library: 171 models with 1 LOD, 428 with 2, 28 with 3; 9 one-LOD models have distance 0 [DATA].
- `dist2` is a **squared distance** (all comparisons below are against squared metres) [CODE].
- `+0x54` multiplies the squared camera distance in the selectors; it is not written in readable init code (FUN_00447c60 is [ENC]).
  Treat it as 1.0 [INF: no readable writer, neutral factor].

Typical draw ranges (√(last dist2), metres) of the models within 500 m of the intro house [DATA, popin.py]:
bushes `#..BUS..` / V_BUSH_* 129–144 (1 LOD), V_CHEN_B 144, walls SF_WALHX 130, benches SF_BNC_F 98, SF_AFLWA 57, lamps SF_LIGHE 161,
olive trees V_OLIV_* 208–257, pines V_PPIN_C/D 304, houses HE_AA_A 482.

---------------------------------------------------------------------------------------------------------------------

## 3. Per-instance LOD selection [CODE FUN_00989d10, FUN_00989f50, FUN_00df6e60]

`FUN_00989d10(out, scale)` (PMI static instance; ESI = PMI instance, `[ESI] = model instance`, `ESI+0x30` = world position vec4 (w = 1),
EAX = viewpoint with position at `+0x80` (vec4)):

```c
f  = DAT_00f7f5f8;                         // SET_GLOBAL_LOD_FACTOR[0]
fx = DAT_00fd50a8;                         // f < 1 ? 1 - f : 0
if (!(model->flags & 0x100)) { lod = 0; return visible; }          // no LOD switching
d2 = |view.pos4 - inst.pos4|^2 * scale * model[+0x54];
for (i = 0; i < n; i++) {
    k = (n - i <= 1) ? 1.0 : 0.0;          // 1 only for the LAST LOD
    F = k * fx + f;                         // = f for LOD 0..n-2, = max(f, 1) for the last LOD
    if (d2 < lod[i].dist2 * F) { lod = i; return visible; }
}
if (DAT_01195bf0[0x2e8] == 3 && MAP_LOD_ALWAYS_VISIBLE_RADIUS < model->radius)   // mode 3 = map [INF]
    { lod = n - 1; return visible; }
return NOT visible;                         // beyond the last LOD: not drawn
```

- `FUN_00989f50` is the same with a visibility test `FUN_008b4c60` first (result 2 → out flag +0xa), and "none" encoded as LOD 4.
- `FUN_00df6e60` (generic edRender LOD, used for any model with flag 0x100; also the 1-LOD library models, which the PMI loader does not
  put into the cell lists, §4) counts the exceeded thresholds with the same `F` rule; all exceeded → 4 (hidden) unless map mode.
  Its distance is `FUN_00df6d70`: `cam[+0x180] * DAT_01367c0c * |cam.pos4 - model.pos4|^2 / model[+0x50]^2`.
- So: **the low LODs (TD_HardwareInstance meshes, TD_Impostor quads) are drawn between lod[0] and the last threshold, and nothing is drawn
  beyond `last.dist2 × max(f, 1)`.** Intermediate switches use `× f` (with f < 1 they come earlier, the last one never earlier than ×1).
- Callers of 0x989d10 / 0x989f50 are not in readable code (no direct call or pointer) [ENC]; the `scale` argument is therefore unknown.
  In the readable cell pass the same role is played by `*(DAT_00fd51c0+0x220) * DAT_01367c0c` (§4); its writer is not readable.
  Treat it as 1.0 for wide cameras [INF, §5].

Effect of the factor on visibility range: **range × √max(f,1)** → level 0/1: ×1, level 2: ×1.414, level 3: ×2.

---------------------------------------------------------------------------------------------------------------------

## 4. Cell (quadtree) pass and caps [CODE]

Per sector, every frame (`FUN_009875e0` → `FUN_00982a70` → `FUN_0098b540`, then `FUN_00985290` ×2 (V, O lists) and `FUN_00981f40` (S list)):

1. `FUN_0098b540(sector, p = *(DAT_00fd51c0+0x220), out…)`: for each of the 409 cells, 2D distance from the viewpoint (x,z) to the cell
   square: `dx = max(|cellCenter.x - cam.x| - half, 0)` (same for z), `half` = level-0 half size, doubled per level
   (thresholds 0x144/0x184/0x194/0x198/0x199). `cellD2 = (dx²+dz²) · p · DAT_01367c0c / max(f·b, 1)²`
   (`b` = SET_GLOBAL_LOD_FACTOR[1] = 1.0). Cells flagged empty go to the back of the list.
2. `FUN_00985290`: for each listed cell, walk its instance entries `{f32 maxDist2, inst*, flags}` (registered by the loader in the
   ancestor cell of level max(1, w9>>9), filled by [ENC] FUN_004dd1c0); add instances while `cellD2 <= maxDist2 · (fx + f)` (= × max(f,1));
   the walk stops at the first failure, so the lists are sorted by decreasing maxDist2 [INF from the early-out].
   In mode 3 (map) every instance is added. Entries with flag bit 0 and a light pointer call FUN_00ddde20.
3. `FUN_009871c0`: visible mass instances per list are capped to `MASSINSTANCE_VIS_MAX` (1000 at level 1) [CODE `min(count, DAT_00f76624)`].
4. Loader (FUN_00988d10, [CODE] pmi_dec.c): an instance is put into the cell lists only when its model has **≥ 2 LODs**
   (`bVar3 = false` when numLOD < 2), max 4000 per list per sector. One-LOD models go through another path (FUN_004dd470, [ENC]),
   whose LOD test is the generic one (§3) [INF].

The cell pass is conservative (2D distance to a ≥ 96 m cell box ≤ 3D distance to the instance), so the per-instance test of §3 decides.

Cube-map pass `FUN_00986300`: an instance is drawn in the environment cube only if `radius / dist × sector[+0x24b64] > DAT_00f76620`
(25 pseudo-pixels at levels 0/1, 5 at 2, 1 at 3), max `DAT_00f76628` per face.

Impostor path `FUN_009969e0` (TD_Impostor GUID 0x83078865caca336d): drawn when
`dist²/inst[0x18]² · (DAT_01367c08² · cam[+0x5ac]²) <= lod[n-2].dist2 · f · DAT_00f7663c (1.0)` and count ≤ MASSINSTANCE_VIS_MAX,
with a blend parameter `(d - T·0.33)/(T - T·0.33)` (DAT_00f76638 = 0.33) passed to FUN_00996820 [CODE]. Otherwise the material is queued
by FUN_00996300.

---------------------------------------------------------------------------------------------------------------------

## 5. FOV / screen-size scaling [CODE FUN_00d72540, caller FUN_00ca24f0]

Called every frame with `p = [DAT_00ffc088]+0x17c` (main camera field; its meaning is not resolved, the writer is not readable):

```c
if (DAT_00ffb680) {                         // set to 1 at init (0xd791fb)
    p = clamp(p, 0.1 /*ffb67c*/, 0.4 /*ffb678*/);          // init values 0xede03c, 0xede018
    bias = clamp(DAT_00ffb674, 1e-5, 1.0);                 // init 1.0 (0xd791f3; 0xa4ef92/0xa4efc5 also write 1.0)
    s = (1/bias) * (((0.4 - p) / 0.3) * 1.5 + 1.0);        // 1.5 = DAT_00f49a60
    if (s < 1) s = 1;
    DAT_01367c08 = s;  DAT_01367c0c = s * s;               // multiplies the squared distances
}
```

- For `p ≥ 0.4` the scale is exactly 1. Whatever `+0x17c` is (FOV in radians, half-FOV, or tan(half-FOV)), every intro camera
  (Fov 50–60°: 0.87–1.05 rad, half 0.44–0.52, tan 0.47–0.58) gives p ≥ 0.4, so **the intro uses scale 1** [INF, robust to the 3 readings].
- `cam[+0x180]` (generic path) and `*(DAT_00fd51c0+0x220)` (cell pass) are further per-camera terms whose writers are not readable [ENC].
- `MAP_LOD_SELECTION_SCALE` (f75f04) has no readable consumer apart from the settings copy (0xaca818/0xacac72) [CODE]; it is a map-view
  setting. `MAP_LOD_ALWAYS_VISIBLE_RADIUS` is only used in mode 3 (§3) — **neither affects the intro**.

---------------------------------------------------------------------------------------------------------------------

## 6. Fade-in

- No time-based fade-in of PMI instances exists in readable code: no per-instance alpha/timer is written by the selectors or the cell pass,
  the PMI colour is constant 0xffffffff (FUN_00988250) and TD_HardwareInstance has no instance colour [CODE+SHADER].
- `FUN_0098a560` computes a **distance** fade `t = clamp((D - L)/((F+L)/2 - L), 0, 1)` with `L = last.dist2·f`, `F = model+0xf0` (= 0 in all
  data → `t` goes 1 → 0 between L/2 and L) and stores `t·param` in model+0x8c; it has no readable caller [ENC]. This is a fade-out at the far
  end, not a fade-in.
- `FUN_009875e0` writes one global value `min(DAT_00ffb5f0 + DAT_00ffb5f4, 1)` into model+0x80..0x8c of the sector light lists (+0x28b8);
  it is not a visibility fade.
- Impostors have the 0.33 → 1.0 blend of §4.

So the game shows objects at their LOD distance with no delay; there is nothing to emulate beyond the distances.

---------------------------------------------------------------------------------------------------------------------

## 7. Streaming during the intro

- `C_INTRO.StreamCenter = 0x0000000000000000` (no entity) in `new_intro.bnk/entities.xmb` [DATA]; FUN_00b3f190 only resolves the reference
  [CODE]. So there is no fixed stream centre for this cinematic; streaming follows the viewpoint [INF].
- Sector count: `SetDefNbSectors` = 5 at level 1, 16 at level 3 (byte `[obj+0x8c]+0x6c`). The consumer of that byte is not readable [ENC], so
  the exact shape (count vs ring) is unknown; values 4/5/9/16 read as counts (9 = 3×3, 16 = 4×4) [INF].
- Intro path [DATA intro_spec §3.1 + house EPH_L3DI_5067I (36382.3, 5.9, 5729.8), heading 2.1903]: camera from (36118, 7392) at scene t 6.28 to
  (36271, 6002) at t 26 and the house at t 34. Rows 6..10, columns 45..48.
- OpenTDU2: `initialize(house, 1)` loads the 3×3 around the house, then `loadAround(camera, 1)` adds the 3×3 around the start → **14 sectors from the
  first frame** (log: "14 sectors loaded around (36118, 7392)"), covering the whole flight. `sector-5-1-7-1` does not exist in the game either
  (sea) [DATA, Mac listing]. **Streaming is not the cause of the intro pop-in.**

---------------------------------------------------------------------------------------------------------------------

## 8. Pop-in measured on the intro path [DATA, `/tmp/claude-0/pss/popin.py`]

Rule of §3 (`d² < last.dist2·max(f,1)`, pivot distance, scale 1) on the 27,473 V/O instances of the 14 loaded sectors, camera sampled every 0.1 s:

| factor | visible at the first frame | appear during the flight | appear within 250 m of the house |
|---|---|---|---|
| f = 1 (level 1 = OpenTDU2 today) | 215 | 753 | 456 |
| f = 2 (level 2) | 286 | 1151 | 577 |
| f = 4 (level 3) | 507 | 1816 | 684 |

With f = 1, the house surroundings pop in during the last ~10 s at 130–300 m. With f = 4 the same objects already exist at 260–600 m (houses at 964 m),
where they are a few pixels tall. That matches "already there" in the game at High/Max quality.

---------------------------------------------------------------------------------------------------------------------

## 9. Differences with OpenTDU2 and fixes

OpenTDU2 today (`source/world/eden_world.cpp`): `distance = sqrt(lods.back() * 3.160494)` (2000 m if none), draws LOD0 when
`|pos - cam|² <= distance²`, then AABB frustum test; actors fixed 300 m; `loadAround(camera, 1)` plus the house at init.

| # | difference | effect | fix |
|---|---|---|---|
| 1 | **SET_GLOBAL_LOD_FACTOR not applied** (OpenTDU2 = f 1.0, i.e. level 1, the no-savegame default) | at levels 2/3 the game shows every instance ×1.41 / ×2 farther; **this is the pop-in the user sees** [CODE+DATA] | read `[ENVIRONMENT_QUALITY_LEVEL_n]` from Quality_Settings.cpr (OpenTDU2 already decrypts it for shadows), with n from a setting / `-envquality n` (default 1 like the game, 3 for "max"); then `maxD2 = lods.back()*3.160494f * std::max(f, 1.0f)` |
| 2 | last LOD factor rule | with f < 1 (level 0) the game still keeps ×1 for the last LOD; intermediate switches use ×f | use `F = (k == n-1) ? max(f,1) : f` when LOD switching is implemented |
| 3 | ~~only LOD0 is loaded and drawn up to the last threshold~~ **done** | LOD 1 / 2 meshes and the TD_Impostor quads are drawn (`source/world/eden_world_lod.cpp`, `docs/re/lod_impostor_spec.md`) | — |
| 4 | distance metric | game: `|cam.pos4 - inst.pos4|² · scale · model[+0x54]` with pivot position, scale = 1 for the intro cameras (§5) | none: OpenTDU2's pivot distance is equivalent [INF] |
| 5 | 1-LOD models with distance 0 | game: flag 0x100 clear → never distance culled; OpenTDU2 culls at 2000 m | use `FLT_MAX` when `lods.size() == 1 && lods[0] == 0` (9 library models, none near the intro house) |
| 6 | MASSINSTANCE_VIS_MAX cap (1000 / 4000 per list) | game draws fewer, not later | none needed |
| 7 | streaming radius | with f = 4 draw ranges reach ~960 m (houses); `loadAround(camera, 1)` guarantees only ≥ 768 m to the first unloaded sector | when f > 1 use `sectorRadius = 2` (or `ceil(maxRange/768)`); the game's level 3 streams 16 sectors |
| 8 | fade | the game has no time-based fade-in (§6) | none |
| 9 | environment cube (`setMinInstanceRatio(0.02)`) | game: `radius/dist·k > 25 px` (levels 0/1), 5 px (level 2), 1 px (level 3) | optional; does not affect the main view |

Minimal patch for the main symptom (in `EdenWorld::loadInstances`, with `lodFactor` = SET_GLOBAL_LOD_FACTOR[0] of the chosen level):

```cpp
const float lastFactor = std::max( lodFactor, 1.0f );          // DAT_00fd50a8 + DAT_00f7f5f8 for the last LOD
if ( !lods.empty() && lods.back() > 0.0f ) {
    distance = std::sqrt( lods.back() * 3.160494f * lastFactor );
} else if ( lods.size() == 1 ) {
    distance = FLT_MAX;                                         // flag 0x100 clear: no LOD switching, never culled
}
```

and in `IntroCinematic::update`: `view.getWorld().loadAround( camera.Position, lodFactor > 1.0f ? 2 : 1 );`.

---------------------------------------------------------------------------------------------------------------------

## 10. Open points

- Callers of FUN_00989d10 / 0x989f50 / 0x98a560 and the writers of `cam+0x17c`, `cam+0x180`, `[DAT_00fd51c0]+0x220`, model `+0x54` are [ENC]/not found.
  They are assumed neutral (1.0) for the intro. If the game still shows objects farther than ×√f, the first suspect is `cam+0x180`.
- The user's real quality level is in the Windows profile (`savegame/SystemDefault`, `QUALITY_SETTINGS_LOD`), not in the Mac install folder.
- The actor LOD (A.pmi road signs): readable two-level selectors FUN_0098a2e0 / 0x98a410 use fixed squared thresholds 1000 and 21000
  (31.6 m / 145 m before scaling) with an alternate model at `inst[0x18]`; their binding to actors is [ENC]. OpenTDU2's 300 m is not derived from code.
