# TDU2 distant objects: library LODs, tree impostors, area far objects — spec

Scope: how TestDrive2.exe draws the static `.pmi` instances (library models of `commonworld.bnk`) far from the camera:
the LOD meshes, the tree impostors (TD_Impostor), the mesh to impostor transition, and what OpenTDU2 does
(`source/world/eden_world_lod.cpp`). Distances, quality factors and the LOD selector are in `pmi_streaming_spec.md`
§1–§5 (not repeated); the PMI format in `pmi_spec.md`.

Tags: [CODE] readable x86 (>= 0x6c1000), [DATA] game files, [SHADER] .sob bytecode, [INF] inferred (reason given),
[ENC] SecuROM-encrypted code (not analysed).

Files: decompiled `scratchpad/lod1.c` (FUN_00986e90, FUN_00991560, FUN_00996820…), `lod2.c` (impostor manager
FUN_00995f30, FUN_00996470, FUN_00996720…); calibration `scratchpad/cal/dumptree.cpp` (LOD object dump with the
OpenTDU2 parsers), `scratchpad/cal/cal.py` (silhouette vs atlas cell); scan `scratchpad/scr/lods.py` (LOD table
statistics). Scratchpad = `/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad`.

---------------------------------------------------------------------------------------------------------------------

## 1. LOD data of the library [DATA, library.3dd / .3dg / .2dm]

INS. payload `+0x4e numLOD`, `+0x50 + k·0x14 {u32 pObjectHashEntry; f32 lodDistance; pad[3]}`: LOD k is an OBJA
entry (its hash = the 3DG geometry hash), LOD 0 is the node object. 627 library models with an object:

| LODs | last LOD material | models | example (√(dist·3.160494) per LOD, m) |
|---|---|---|---|
| 1 | — (TD_Vegetation, TD_Building…) | 171 | bushes 129–160 (9 have distance 0: never culled) |
| 2 | TD_HardwareInstance | 213 | walls 37 / 73, houses `HE_AA_A` 109 / 271 |
| 2 | TD_Building | 131 | |
| 2 | **TD_Impostor** | 27 | `V_OLIV_A` 160 / 208, `V_CHEN_A` 257 / 353, `V_FIG_A` 209 / 305 |
| 3 | **TD_Impostor** | 15 | `V_HPIN_*` (pines) 28.8 / 115 / **1038**, carob trees 28.8 / 98 / 1730 |
| 2 / 3 | TD_pubneon 24, TD_Ground 15, TD_Sprite 11, TD_HardwareInstance (3) 7, TD_Building (3) 6, TD_Shop 3, TD_Lamp 3, TD_Water 1 | | |

- The LOD objects use the same MBNK as LOD 0; a low LOD primitive uses the **last** MBNK material
  (e.g. V_HPIN_A LOD 0 materials 0/1/2, LOD 1 1/3/2, LOD 2 4) [DATA].
- No LOD node exists in the sector `-o` scenes (one TD_Road node, no LOD) or in the area `-o` scenes (TD_HardwareInstance
  far objects with one LOD at distance 0 = always drawn) [DATA area-2-2, sector-2-2-1-7].
- Impostor LOD object = 4-vertex triangle strip (PRIM type 4), one stream {float3 position, float2 uv} (stride 20), in
  the model plane x = 0: `(0, top, -w) uv (0,0)`, `(0, bottom, -w) (0,1)`, `(0, top, +w) (1/(N+1), 0)`,
  `(0, bottom, +w) (1/(N+1), 1)`; V_HPIN_A: w = 12.92, y = -2.26 … 23.59, u = 0 … 0.0625 [DATA].
- TD_Impostor materials (42): one COLOR texture `<tree>_h-i.2db` (DXT5 atlas, one row of N+1 cells: V_HPIN_A 2048 x 128
  = 16 cells of 128 x 128, V_OLIV_A 256 x 64 = 8 cells), parameter **NbImpostorSide** (sym `0x54539494969c949d`) = N =
  15 (pines, carob, figs), 7 (olive, oak, almond, orange, peach…), 3 (3 materials). Cells 0 … N-1 are side views, cell N
  is a **top view** (the last cell of every atlas is the canopy seen from above) [DATA, atlases decoded with
  `/tmp/claude-0/py/dxt5png.py`]. States: alpha ref 0xb2, no blending [DATA].
- Parameter block parsing: 48 library materials (all TD_Impostor among them) have a float4x4 parameter (descriptor size
  0x40); the values are packed by descriptor size and end exactly at the MAT. section end [DATA: all 432 library materials with parameters].
  OpenTDU2's `Render2DM` stopped at the first 0x40 descriptor and read every value of those materials from the wrong
  offset (fixed: `source/render/2dm.cpp`). The 6 other ones are TD_HardwareInstance LOD materials (`SF_Barrier_Lod`,
  `Obj_L`, `Obj_L_AdPanel`, `Obj_L_AdPanel_P`, `Obj_L_Ferry`, `Obj_VL_Building`), not drawn before this work.

---------------------------------------------------------------------------------------------------------------------

## 2. Runtime [CODE]

### 2.1 Instance setup, FUN_00989b50 (called by the PMI loader FUN_00988d10 for every record)

```
inst+0x5c = rand() / 32767.0                       // import 0xe41458 = msvcrt rand, 0xf49a08 = 32767.0
inst+0xbc..0xc8 = 0; inst+0xcc = inst+0xd0 = 0
if (numLOD > 1) {
    inst+0xbc..0xc8 = model+0x64..0x70
    mat = material of the LAST LOD object (FUN_004dc590 [ENC] -> MBNK index -> model material table)
    inst+0xcc = mat
    if mat is TD_Sprite / TD_Impostor / TD_VegetationSPR: mat+0x33 (cull mode) = 3
    model flags |= 0x30
    if mat is TD_Impostor (0x83078865caca336d):
        mat+0x35 = 0 (alpha test on), mat+0x31 = 1 (depth write), word mat+0x20 = 0x1d
        inst+0xd0 = &mat.NbImpostorSide              // FUN_0042a780(mat, 0x969c949d, 0x54539494)
}
```
The instance scale `inst+0x60` = max(scale x, y, z) of the record (0x9890c2..0x9890f2), `inst+0x64` = rotation Y.

TD_Impostor material handler 0x00d717e0 (table 0x00f8fdf0): `M(0x3a) = 6`, keys `0x25 = 1`, `0x1a = 0x14 = 0x1b = 0`
(no fog), MAT `+0xfc = 2` texture slots, slot 1 = runtime texture (`+0x128 = 1`, `+0x130` = a global texture of the
table 0x11c70cc), bucket 0x39.

### 2.2 Mesh or impostor, FUN_009969e0 (per instance per frame; `ecx` = impostor manager DAT_00fd9584)

Callers: FUN_00986e90 (sector cell lists, two loops: instances accepted as meshes are written to the draw list,
the others only tested) and the thunk 0x98ac40 (no readable caller [ENC]).
```
D = |inst.pos - mgr.cam|^2 / inst.scale^2 * mgr[+0x978c0]     // +0x978c0 = DAT_01367c08^2 * cam[+0x5ac]^2 (FUN_00996720)
T = lod[n-2].dist2 * SET_GLOBAL_LOD_FACTOR                      // second to last LOD threshold
inst+0xb0 = atan2(dz, dx)  (d = inst - camera, CRT _CIatan2 0xe2db84 on st1 = dz, st0 = dx)
inst+0xb4 = dx^2 + dz^2,  inst+0xb8 = dy^2
if (D <= T && !mgr[+0x978a9] && count <= MASSINSTANCE_VIS_MAX) {
    b = (D - 0.33 T) / (T - 0.33 T)                                 // 0.33 = DAT_00f76638, 1.0 = DAT_00f7663c
    a = clamp(1 - 0.99 b, 0, 1)                                     // 0.99 = 0xf4ae50
    FUN_00996820(renderer+0x430, ..., NbImpostorSide or 15, a, ...) // matrix / block -> model +0x10..+0x3c [ENC helpers]
    return 1;                                                       // mesh drawn
}
if (inst.material(+0xcc) is TD_Impostor) FUN_00996300(mgr+0x8a4, inst)   // queue impostor
return 0;
```

### 2.3 Impostor batches

- FUN_00996300: 1024 batches of 0x25c bytes `{material, ?, int(NbImpostorSide) or 1, inst*[146], DAT_00f76634, count}`,
  keyed by material and DAT_00f76634 (a value FUN_00991560 accumulates with the frame time up to 10.0 after the world is
  loaded, −1 before).
- FUN_00996470 (draw): at most **20000** impostors per frame; per batch `FUN_008bcba0(3, count·6, 1)` (6 vertices per
  impostor) then per instance **FUN_004d9200** (or FUN_004d8e20 with 100.0 when `mgr+0x978a8`, the cube map mode set by
  FUN_00a3d820) — **the vertex builder is [ENC]**.
- Debug counters string "Impostor Instances;Visible Impostor Instances;" (FUN_00cc2360).

### 2.4 Light: impostor_ambient

- `game_weathers.xmb` key `impostor_ambient` (float3, 0.1 by day, 0.0039 at night, 0 at some hours) [DATA].
- FUN_00d769e0 writes it to the renderer +0x45c0; FUN_00981600 (spot exterior light set) puts `AMBI` (sun ambient) in
  ambient light 0 and `impostor_ambient` in ambient light 1 [CODE]. The consumer of +0x45c0 is not readable [ENC].

---------------------------------------------------------------------------------------------------------------------

## 3. Shaders [SHADER]

TD_Impostor (8 items; item ids = HashSpec of the switches, found by enumeration):

| items | switches | vs / ps | use |
|---|---|---|---|
| 5 a09f…, 2 5743… | S_NBTEXTURES 1, COORD 1 (LOD 0/1) | vs0 / ps0 | colour |
| 7 f2b3…, 0 056f… | + S_PASS_Z | vs2/3 / ps2/3 | depth / velocity |
| 4 8cc0…, 3 7b1c… | S_NBTEXTURES 2 | vs1 / ps1 | colour with dissolve |
| 6 deec…, 1 2930… | S_NBTEXTURES 2 + S_PASS_Z | vs4/5 / ps4/5 | |

- VS: **world space input** (`oPos = (v0.xyz, 1) · ViewProj`, no world matrix), texcoord passed through, colour =
  `__AmbLight_Ambient_0 + __AmbLight_Ambient_1`. `NbImpostorSide` is declared but unused: the cell is chosen on the CPU.
- PS0: `texkill(tex.a - __Alpha_Ref)`, `oC0 = tex · (amb0 + amb1)` — no sun light, no fog.
- Dissolve (vs1/ps1): threshold `t = (v - 1 - 0.2 (sin z + 1)) · (0.4 + 0.15 (sin 2z + 1))` (z = world Z, v = texcoord),
  pixel kept if `dissolveTex(vPos / 64) <= t`. Since v ≤ 1, t ≤ 0: the whole quad is discarded for any unsigned dissolve
  texture.
- TD_Vegetation (the tree meshes): `__Lod_Dist` is used only as `.z`: PS `colour = lerp(lit, amb0 + amb1, z)` (the
  impostor colour), VS wind amplitude × `(1 - z)`. No other library shader declares `__Lod_Dist`.

---------------------------------------------------------------------------------------------------------------------

## 4. Impostor quad and atlas cell [INF + DATA check]

FUN_004d9200 is encrypted, so the quad is reconstructed from the data:

- The atlas cells are views around the vertical axis; the quad template has the cell aspect (V_HPIN_A 25.8 x 25.8 m
  for 128 x 128 px) and the LOD 0 canopy extent (sprite centres ±8 m + sprite size) [DATA].
- View to cell mapping, checked by rendering the LOD 0 silhouette (bark / branch triangles + leaf sprite centres as discs)
  from N directions and correlating with every atlas cell (`cal/cal.py`): with the view direction
  `f = (cos φ, 0, sin φ)` (camera to tree, model frame) and screen right `r = (-sin φ, 0, cos φ)` (u grows to the right),
  the best cell is **k = φ · N / 2π** (offset 0): V_FIG_A 10/15 views exact and 2 at ±1, V_FIG_C 10 exact + 3 at ±1,
  V_HPIN_A 6 exact + 9 at ±1 (15/15 within one cell); the mirrored right vector gives k = φ N / 2π + N/2 with lower scores. Olive and oak (7 sides, 32 px cells)
  are too symmetric to decide.
- OpenTDU2: θ = atan2(dz, dx) of (instance − camera) (the angle the game stores at +0xb0), model angle θ + yaw
  (v_world = v_model · RotY(yaw), yaw = atan2(−M02, M00)), k = round((θ + yaw) N / 2π) mod N, u = k / (N + 1);
  vertices `P + r_world · z · max(sx, sz) + (0, y · sy, 0)`, r_world = (−sin θ, 0, cos θ), z / y / v from the template,
  same strip order (triangles 0 1 2, 2 1 3), cull mode 3 as set by FUN_00989b50.
- Not done [ENC]: the top view cell N (dy² at +0xb8 is stored, presumably for it), tilting, the per batch value
  DAT_00f76634, the per instance random +0x5c.

---------------------------------------------------------------------------------------------------------------------

## 5. OpenTDU2 implementation (`source/world/eden_world_lod.cpp`)

- `Render3DD`: `SceneNode::LodObjects` (object of each LOD). `EdenModel::setLodLevel(k)`: loads the LOD k object of
  every node; `EdenWorld` keeps `libraryLodModels[0/1]` = LOD 1 / LOD 2 of library.3dd (525 / 28 items).
- Per instance (`drawLibraryInstance`): LOD = FUN_00989d10 rule (§3 of pmi_streaming_spec); models whose last LOD is
  TD_Impostor use the FUN_009969e0 test: impostor when `d²/s² > dist2[n-2]·f`, otherwise the mesh LOD (≤ n-2).
  Meshes between 0.33 T and T get `__Lod_Dist = (0, 1, 1 - a, 1)` [INF: the game passes `a` through encrypted code;
  TD_Vegetation's only input for it is `.z`] and `__AmbLight_Ambient_1 = impostor_ambient`.
- Impostors: queued per material, written to the frame's transient vertex buffer and drawn once per material at the end
  of `EdenWorld::draw` (≤ 20000 per frame), with `__AmbLight_Ambient_1 = impostor_ambient` [INF: light set binding ENC].
- TD_Impostor handler added (0x00d717e0); the runtime slot 1 is cleared so the plain S_NBTEXTURES 1 permutation is used
  (§3: the dissolve one discards everything) [INF].
- Instances beyond the last LOD distance are culled as before (`loadInstances`); one-LOD models with distance 0: FLT_MAX.
- Options: `-nolods` (LOD 0 up to the last distance, the previous behaviour), `-noimpostors` (mesh instead of impostor),
  `OTDU_FORCE_IMPOSTORS=1` (impostors at every distance, calibration), `OTDU_LOD_STATS=1` (per frame counts).
- Area far objects (`area-x-y-o`, TD_HardwareInstance, one LOD at distance 0) were already drawn and hidden per
  `Sector-…` group while the sector is loaded (ocean_spec 5.2); they have no LOD table to apply.

### 5.1 Results (World Viewer, 960 x 540, software Vulkan, LOD factor 4 = environment level 3)

| view | before (`-nolods`) | after |
|---|---|---|
| pine forest, sectors 2-2-x-5..2-3-x-1, `-campos 13824,250,18048 -camyaw 0 -campitch -10` | 16403 LOD 0 tree draws | 2 LOD 0 + 46 LOD 1 (faded) meshes + 16355 impostors in ~15 batch draws |
| frame cost (same view, 40 frames) | 3.5 s / frame | 1.45 s / frame |
| hills `-campos 12600,180,17000 -camyaw 30 -campitch -4` | 17847 LOD 0 | 83 LOD 0 + 13 LOD 1 (67 faded) + 17751 impostors |
| Ibiza town area 3-3 `-campos 21120,250,19500 -campitch -15` | 4293 LOD 0 | 1468 LOD 0 + 63 LOD 1 (TD_HardwareInstance houses) + 2762 impostors |
| FREERIDE start (house, 9 sectors) | 283 LOD 0 | 271 LOD 0 (9 faded) + 12 impostors |

Screenshots: `scratchpad/lod/cmp_forest.png`, `cmp_hills.png`, `cmp_town.png` (top before, bottom after),
`fr.png` / `fr_nolods.png`.
