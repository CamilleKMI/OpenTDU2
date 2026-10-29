# TDU2 `.pmi` instance placement files: specification

Scope: the `.pmi` resources in the island sector banks (`Euro/Bnk/islands/ibiza/level/area-X/area-X-Y/sector-X-Y-SX-SY.bnk`),
how their records resolve to models, their coordinate space, and which banks must be loaded for the press-start view.

Tags: [CODE] read from readable x86, [DATA] checked on files, [SHADER], [INF] inferred (reason given), [ENC] the code is encrypted (SecuROM), so the behaviour is unknown.

Tools and files:
- Dumper: `/tmp/claude-0/py/pmi.py` (run it with `python3 -I`; see its docstring).
- Decompiled code: `/tmp/claude-0/pmi_dec.c` (loaders), `pmi_dec2.c` (record decode, actor creation), `pmi_dec3.c` (matrix helpers, which call encrypted code), `pmi_dec4.c` (model lookup, LOD helpers), `pmi_dec5.c` (model instance init `0xda0440`, F loader), `pmi_dec6.c`.
- Scratch scripts: `/tmp/claude-0/pmi/scr/*.py` (survey, bbox/rotation validation `val2.py`, near-camera stats `near.py`).
- Validation set: 17 sectors of area 3-3 (`/tmp/claude-0/pmi/x/3-3-*`): 1-1,1-2,1-3,2-0..2-4,3-0..3-4,4-1..4-3,5-2. They hold 53,245 static instances and 143 actor instances.

---------------------------------------------------------------------------------------------------------------------

## 0. The five PMI files of a sector

Each sector bank holds up to five PMI files. The table lists the bank group/subtype, the loader that reads each file, and its content.

| file | bank group/sub | loader | content |
|---|---|---|---|
| `Sector-…-V.pmi` | 5 / **10** | `FUN_00988d10` (kind 0) | vegetation: palms, agaves, olive trees… Static library models. [CODE]+[DATA] |
| `Sector-…-O.pmi` | 5 / **13** | `FUN_00988d10` (kind 1, also the default) | objects: **white houses `HE_*`/`HT_*`**, walls `SF_WAL*`, lamps `SF_LIGH*`, bushes, rocks, benches… Static library models. [CODE]+[DATA] |
| `Sector-…-S.pmi` | 5 / **50** | `FUN_00988d10` (kind 2) | skinned static models: guard rails `SF_GLISS` (metal, 20 m, 7 bones) and `SF_GLISO` (wooden, 24 m, 8 bones), each record with its own skin matrices (§1.5). Drawn by OpenTDU2. [CODE]+[DATA] |
| `Sector-…-A.pmi` | 5 / **14** | `FUN_009879e0` → `FUN_009613e0` | **actors**: road signs (`RS_Sp50_act` = the "50" sign, Stop, Yield, direction signs…) from `worldactors.bnk`. [CODE]+[DATA] |
| `Sector-…-F.pmi` | 5 / **24** | `FUN_00c38f40` → `FUN_00c38d40` | **island ambient effects**: MOB hashcode = a Fx__SourceData of `fx/vfx_env.bnk`, type 5, at most 30 records per sector, started within the TriggerDistance of the camera (`particles_spec.md` 7). Empty (464 bytes) in most sectors; 105 sectors of the install place some (Ibiza 31, Hawai 491 records). [CODE]+[DATA] |

In `FUN_00988d10` the subtype comes from the instance-list object (`obj+8`): 0 → 0x0a, 1 → 0x0d, 2 → 0x32, anything else → 0x0d. The resource getter is
`FUN_00d8aea0(edx=group 5, ecx=subtype, bank, &size)` [CODE 0x988d4c..0x988d7e]. `FUN_00986760` builds three such lists at
`sector+0x28d0`, `+0xa010` and `+0x11750` (stride 0x7740). A fourth list at `+0x18e90` is filled by `FUN_00988730`, which did not decompile. [CODE]

---------------------------------------------------------------------------------------------------------------------

## 1. File format

All values are little endian. The file uses the common Eden RenderFile container: a 16-byte header, then sections `{u32 type; u16 vMaj; u16 vMin; u32 size; u32 dataSize}`.

### 1.1 Header and `MOBA` section

| off | type | value / meaning | tag |
|---|---|---|---|
| 0x00 | u16,u16 | version 2,1. The loaders require `(maj*10+min)*100` ≥ 2000. `0x9613e0` uses ≥2000; `0x988d10` uses `==0x180 \|\| 2000..2100`; `0xc38d40` uses 2000..2100 | [CODE]+[DATA] |
| 0x04 | u32 | flags = 0 | [DATA] |
| 0x08 | u32 | file size | [DATA] |
| 0x0c | char[4] | `".PMI"` (0x494d502e is checked) | [CODE] |
| 0x10 | section | `"MOBA"`, version 0.3, size = rest of the file, dataSize = 0x1c0 | [DATA] |
| 0x20 | u32 | **numMOB**: number of `MOB.` child sections | [CODE] |
| 0x24 | f32 | **heightMin** (Y of the lowest instance) | [CODE] |
| 0x28 | f32 | **heightMax**. An empty file stores 0 / 1024 | [CODE]+[DATA] |
| 0x2c | u32 | 0xcdcdcdcd padding | [DATA] |
| 0x30 | u8[409] | **cellCount[cell]**: instances per quadtree cell (see 1.4). The sum equals the instance count. Used to size the per-cell lists (`0x9613e0`, `0x987d60`) | [CODE]+[DATA] |
| 0x1c9 | | 0xcd padding up to 0x1d0 | [DATA] |
| 0x1d0 | | numMOB × `MOB.` sections, one after the other | [CODE] |

### 1.2 `MOB.` section: one per referenced model

| off (from section start) | type | meaning | tag |
|---|---|---|---|
| 0x00 | char[4] | `"MOB."` | [DATA] |
| 0x04 | u16,u16 | version 3.0 | [DATA] |
| 0x08 | u32 | size = 0x20 + count × stride | [DATA] |
| 0x0c | u32 | dataSize = size | [DATA] |
| 0x10 | u64 | **model hashcode**: a library node hash (V/O/S) or an actor entity hash (A). See §2 | [CODE] (`MOB+0x10/0x14` are copied as the key in `0xc38d40`; for V/O/S, `0x98ac80` → `FUN_008b9100(lo,hi)`) |
| 0x18 | u16 | **count** of records | [CODE] |
| 0x1a | u16 | **extraSize**: per-record payload bytes | [CODE] |
| 0x1c | u16 | unknown (0x21..0x4b5, not read by any readable loader; not the vertex or index count) | [DATA] |
| 0x1e | u8 | **type**: 1 = static model, 2 = actor, 3 = skinned static model (bone palette in the payload), 4 = actor with a payload (sign text). 0 = skipped | [CODE]+[DATA] |
| 0x1f | u8 | 0 | [DATA] |
| 0x20 | | count × record. **stride = 0x20 + ((extraSize + 15) & ~15)** (`(ext+0x2f)&~0xf` in `0x9613e0`/`0xc38d40`) | [CODE] |

`0x988d10` skips types 0 and 2 (actors are handled by the A loader). Its pre-pass sizes types 0/2 as `count*0x20 + roundup16(ext)`, but no type-0/2 MOB carries a payload in the data. [CODE]

### 1.3 Record: 32 bytes, 16 × u16

| word | byte | decode | meaning | tag |
|---|---|---|---|---|
| w0 | 0x00 | `x = w0 * 0.0156252384 - 128` | position X, sector-local, range [-128, 896) | [CODE] 0xf4ae38, 0xf49e08 |
| w1 | 0x02 | `y = heightMin + w1 * (heightMax-heightMin)/65535` | position Y (height, world absolute) | [CODE] (`fVar2-fVar3)/65535.0` 0xf49830) |
| w2 | 0x04 | `z = w2 * 0.0156252384 - 128` | position Z, sector-local | [CODE] |
| w3..w5 | 0x06 | `s = w * 6.1036088e-5` (= 4/65535) | scale X,Y,Z (0x4000 = 1.0) | [CODE] 0xf4ae30 |
| w6 | 0x0c | `a = w * 9.5875264e-5` (= 2π/65535) | rotation about **X** (rad) | [CODE] 0xf4ae40 |
| w7 | 0x0e | same | rotation about **Y** (yaw) | [CODE] |
| w8 | 0x10 | same | rotation about **Z** | [CODE] |
| w9 | 0x12 | bits 0..8 = **cell** (quadtree leaf, <409); bits 9..15 = **level** of the loose quadtree cell that contains the whole bbox | culling | [CODE] `&0x1ff`, `>>9` (0x98930b) + [DATA] |
| w10..w12 | 0x14 | `v = w * 0.0625009537 - 1024` | AABB min (x,y,z), relative to the instance position, in the rotated and scaled frame | [CODE] 0xf4ae28, 0xf4add0 + [DATA] |
| w13..w15 | 0x1a | same | AABB max | [CODE] |
| payload | 0x20 | extraSize bytes, padded to 16 | type 3: bone matrices; type 4: sign text | [CODE]+[DATA] |

Transform, from `FUN_00988250` and `FUN_00960f60` [CODE]. These functions build the matrix through `FUN_00ddbbb0` (scale),
`FUN_00ddbd60` (rot X: writes elements 1/2), `FUN_00ddbc40` (rot Z: writes elements 0/1), `FUN_00ddbcd0` (rot Y: writes elements 0/2) and `FUN_00ddb740` (out = A·B):

```
M = Scale(s) · RotX(w6) · RotZ(w8) · RotY(w7)      (D3D row-vector convention: v_world = v_model · M)
M.row3 = (sectorOrigin.x + x, y, sectorOrigin.z + z, 1)
```

The rotation helpers call encrypted code [ENC], so the sign convention was checked on data instead:

- The D3DX convention with positive angles is used. RotX = [[1,0,0],[0,c,s],[0,-s,c]], RotY = [[c,0,-s],[0,1,0],[s,0,c]], RotZ = [[c,s,0],[-s,c,0],[0,0,1]].
- Test: the stored AABB (w10..w15) was compared with the model's 3DG vertex AABB transformed by M, on 1,826 non-vegetation instances. With positive angles the median error is **0.08 m** (bbox quantization is 1/16 m) and 89% of instances are within 0.2 m. With negated angles the median error is 0.47 m. (`/tmp/claude-0/pmi/scr/val2.py`) [DATA]
- Walls, houses and rocks all match. Vegetation is larger in the file because of the billboard leaves expanded by TD_Vegetation[SPR]. SF_GLISS differs because it is skinned (its box holds the skinned mesh, §1.5). [DATA]/[INF]
- Order x→z→y comes from the code; the data cannot tell it apart from z→x→y because tilts are only a few degrees. [CODE]

Note: the exe contains the exporter string "### PMI Model Error, matrix has been scaled, IT IS FORBIDEN ###" (0xf07078). It has no code reference. [DATA]

### 1.4 Cells: loose quadtree per sector

[CODE `FUN_00987dd0`, tables 0xf2c798 = {0,324,388,404,408}, 0xf2c7a4 = {18,8,4,2,1}]

| level | cell index range | grid | cell size |
|---|---|---|---|
| 0 | 0..323 | 18×18: 16×16 over [0,768) plus a one-cell border ring covering [-48,0) and [768,816) | 48 m |
| 1 | 324..387 | 8×8 | 96 m |
| 2 | 388..403 | 4×4 | 192 m |
| 3 | 404..407 | 2×2 | 384 m |
| 4 | 408 | 1 | 768 m |

- `leafCell = (floor(z/48)+1)*18 + floor(x/48)+1`. This matches 98.7% of records. [DATA]
- `level = w9>>9`: the smallest level L at which the bbox (pos+box) stays inside one 48·2^L cell. This matches 95%. [DATA]

`FUN_00987dd0(cell, L)` converts a cell to its ancestor at level L:

```c
s = level(cell); if (s > L) return cell;                 // already coarser
local = cell - off[s]; row = local / w[s]; col = local % w[s];
if (s == 0) { row -= 1; col -= 1; }                      // drop the border (u16 wrap -> out of range)
return (row >> (L-s)) * w[L] + (col >> (L-s)) + off[L];
```

`FUN_00988d10` registers every instance twice:
- in its leaf cell (`w9 & 0x1ff`, array at stack 0x26a4);
- in the ancestor cell `FUN_00987dd0(cell, max(1, w9>>9))` (array at stack 0x764).

Both lists are capped at **4000 instances** per list per sector. Results ≥ 409 are ignored. The consumers (`0x4dd1c0`, `0x4dc850`, `0x4dc9b0`, `0x4dcd80`) are [ENC]. [CODE]

### 1.5 Payloads

- **Type 3** (S.pmi guard rails): one D3D row-major float4x4 **skin matrix per skeleton bone**: `SF_GLISS` extraSize 448 = 7 matrices,
  `SF_GLISO` extraSize 512 = 8 matrices. Ibiza 2,936 rail records (2,381 GLISS, 555 GLISO), Hawai 18,780, Casino 13. [DATA]
  - Loader, kind 2 (S list) of `0x988d10` [CODE]: stores the payload pointer at `modelInstance+0x78`, sets flag `0x200`, calls
    `FUN_004dc650` [ENC], then writes `25.0` (`DAT_00edd53c`) to `modelInstance+0x70` and to `+0x2c` of the object of every LOD
    entry (`+0x58..+0x5c`, stride 0xc). `+0x70` is the bounding radius slot `in_EAX[0x1c]` of `FUN_00da0440` (default 1.0),
    so the rail gets a fixed 25 m radius for its deformed mesh [CODE; "radius" is INF from `0xda0440`].
  - Kind 2 also calls `FUN_009884c0`: it maps the model hash through a 3-entry table `{u64 from, u64 to}` at `0x11f7728`
    (`FUN_00c31620`), instantiates the `to` model (`FUN_008b9100`), stores it at `+0x60` with flag `0x10000`, and the loader gives it
    the same flag `0x200` and payload. The table is zeroed at init (`0xc33f78`) and filled by `FUN_00c316f0`, which has no readable
    caller, so which model this companion is stays unknown [CODE / ENC]. OpenTDU2 does not draw it.
  - The palette upload to the shader was not found in readable code (the reader of `+0x78` is after `FUN_004dc650` [ENC]).
  - Kind 2 does not clear `bVar3`, so every rail is registered in the cell lists (kind 0 only registers models with ≥ 2 LODs).
    The per frame S pass is `FUN_00981f40` [CODE]: gated by byte `[[sector+0x1bdc]+0xeb1]`, it walks the visible cells
    (`{u16 cell, f32 cellD2}` at `sector+0x1be0..[+0x28a8]`) and their S entries (`sector+0x11aa0 + cell·0x40`, `{f32 maxDist2, inst*, ?}`,
    count at `sector+0x1176c + cell·2`) and adds instances while `cellD2 <= maxDist2 · (DAT_00fd50a8 + DAT_00f7f5f8)` (= max(f, 1)),
    the same rule as the V/O pass `FUN_00985290` (pmi_streaming_spec.md §4). `maxDist2` is written by `FUN_004dd1c0` [ENC], so the
    rail draw distance is unknown. The library node has one LOD with switch value 0. OpenTDU2 draws the rails of every loaded
    sector (no distance limit) [INF].
- Geometry [DATA, `library.3dg`]: one PRIM per rail, TD_Ground material `SF_gliss_Sk` / `SF_Gliso_Sk` (2DM spec 9 =
  S_USESKINNING = 1), BONE palette = identity (0..6 / 0..7), stream 0 = position, UBYTE4 blend indices, one FLOAT1 weight
  (the 4th weight is implicit). The rail runs along model −Z from 0 to −20 m (GLISS) / −24 m (GLISO); bone 0 holds the start
  section (z 0..−1.2), bone 1 the end cap (z = −20 / −24), bones 2..6(7) the posts and the beam every 4 m, blended pairwise.
- Skinned TD_Ground vertex shader (`TD_Ground_vs_1`, skin=1) [SHADER]: `a0 = 3·blendIndices`,
  `p' = Σ_k w_k (p·c[148+a0.k], p·c[149+a0.k], p·c[150+a0.k])` with `w = (v.x, v.y, v.z, 1−x−y−z)`, then
  `oPos = p'·__World_View_Proj (c60..c63)`. So the skin matrix is applied before the instance matrix:
  **`v_world = v · Bone[blendIndex] · M`** (D3D row vectors, M = the record matrix of §1.3).
- Validation [DATA, `/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad/scr/seam.py`]: with
  `v · Bone · M` the end cap of each rail lands on the start section of the next one. Median gap 8 mm (3-3-3-5: 123/131 rails
  < 5 cm, 3-3-1-0: 137/141), 17 mm on the wooden hairpins of 0-3-4-7 (24/32, bones up to 85° from bone 0). The others are chain
  ends. With the bone rotation transposed the median gap is 7 cm to 16 m; without bones it is 20 m. The record AABB holds the
  skinned mesh (it is larger by ~2.6 m on x and ~6.5 m on z, an exporter margin [DATA]).
- The bones carry a −90° X rotation that cancels the instance's ~90° X rotation (the rail model is exported lying along −Z).
- Collision: rails have no `Library.dhk` model (no SF_GLIS* entry). Their collision is in the sector static `.shk`: `hkpCapsuleShape`
  bodies, radius 0.25, one capsule per one or two rails (half length 9 to 21 m). In 3-3-3-5, 68 of the 76 capsules lie within
  0.22 m (median) of the drawn rail vertices [DATA, `capchk.py`]. OpenTDU2's VehicleWorldCollision already loads them.
- **Type 4** (A.pmi direction and distance signs, extraSize 19/55/64): sign text. [DATA, decoded]
  - byte 0 = layout code (0x03, 0x1f, 0x22, 0x23…).
  - The rest is text: 0 = separator, 0x01..0x1a = 'A'..'Z', 0x1b..0x24 = '0'..'9', 0x2b..0x44 = 'a'..'z', 0x47 = 'ñ', 0x50 = 'À'. Codes 0x27, 0x29, 0x48 are unknown.
  - Example: "SANT MIQUEL 20 Hort Nou 4 Barda de Maians 7".
  - Some records end with leftover memory bytes from the exporter (`b8693400a8633400eb…`).
  - The A loader passes the payload to the actor through `vtbl+0x58(record+0x20)` [CODE `0x960f60`]. `0x988d10` also stores `record+0x20` at `inst[0x15]` when type==4.

### 1.6 Validation

[DATA] All 85 PMI files of the 17 sectors parse exactly: the section sizes match, the cell sums match the counts, and parsing ends at the end of the file.

---------------------------------------------------------------------------------------------------------------------

## 2. Resolving a referenced model

### 2.1 V / O / S: library models in `commonworld.bnk`

- The MOB hashcode is the **instance (node) hashcode of a root model node of `library.3dd`**. That is the INSA `HASH` entry, which equals the HIEA entry and `GetIdentifier64bit(name)`, e.g. `V_PALM_A`, `HE_AA_A`, `SF_WALA`.
  - All 118 distinct static hashes of the 17 sectors resolve, each to exactly one library node.
  - Every such node is a leaf with an object and identity rotation. [DATA]
  - At runtime: `FUN_0098ac80` → `FUN_008b9100(hashLo, hashHi)` → `FUN_00da06e0`/`FUN_00da0440`, which builds a model instance from the registered INS. descriptor. [CODE]
- **Ignore the library node's translation.** `library.3dd` is a gallery scene (`SCENE01` → `OBJECT/…/TREE/V_PALM_A` at T=(-17.8,0,0)…). The PMI matrix replaces the node transform: the geometry AABB only matches the stored bbox with the geometry left at its own origin. [DATA]
- The INS. payload of a library node (0x9c bytes, section size 0xb0) [DATA] + [CODE `FUN_00da0440`]:

| off | type | meaning |
|---|---|---|
| 0x00 | f32[16] | node matrix (gallery placement; ignore it for PMI) |
| 0x44 | u32 | pMaterialBank (MBNK, materials in `library.2dm`) |
| 0x48 | u32 | 0xffffffff |
| 0x4c | u16 | 0xcdcd |
| 0x4e | u16 | **numLOD** (0..3; 428 nodes have 2) |
| 0x50 + k·0x14 | {u32 pObjectHashEntry; f32 lodDistance; u32 pad[3]} | **LOD k**: an OBJA entry, whose hash is the 3DG geometry hash, plus the switch value |

- `FUN_00da0440` copies each LOD `{object, dist × 3.160494 (0xf4aa28 = (16/9)²)}` into the instance.
  - `lodDistance` looks like a **squared distance**. Example √values: walls 37/73 m, houses `HE_AA_A` 109/271 m, palms 90/144 m. [INF from the values]
  - The comparison and the LOD factor are decoded in `pmi_streaming_spec.md` §3; LOD meshes and impostors in `lod_impostor_spec.md`.
  - Beyond the last LOD the instance is not drawn [INF].
  - The `3dd.h` comment "+0x50 pObjectHashEntry" in OpenTDU2 is LOD 0.
- **LOD objects and materials** [DATA]:
  - LOD0 is the full mesh; its PRIM MaterialIndex values cover materials 0..n-1 of the MBNK.
  - LOD1 is a light mesh whose single PRIM uses the **last** MBNK material:
    - **TD_HardwareInstance** (GUID cd453201027c8aba) for buildings, walls, lamps, signs, cars, pools…;
    - **TD_Impostor** for trees: a 4-vertex impostor quad, e.g. `V_PALM_A` LOD1 = object `415f4d4c8a7da7b5`.
  - So TD_HardwareInstance is the low LOD material, not per-instance data. There are no separate "_low" banks for library models; the `exterior/*_low.bnk` files belong to the exterior buildings system, not to PMI.
- **TD_HardwareInstance shader** [SHADER `TD_HardwareInstance_vs_0..3`]:
  - Inputs: position, normal, optional color/texcoord (no per-instance stream). Constants: `__World_View_Proj__float4x4` (c60), `__DirLight_Dir_0`, `__AmbLight_Ambient_0`, `__DirLight_Diffuse_0`.
  - It computes Gouraud N·L lighting; the PS optionally samples `gColorTexSampler`. It has **no `__Instance_Color`**; that constant only exists in TD_Ground/TD_ShadowBox (PS) and TD_CloudMesh/TD_Clouds/TD_Lamp/TD_Sprite/td_checkpoint (VS).
  - The PMI record has no color field.
- Material handling per instance in `0x98ac80` / `0x988d10` (GUIDs at MAT+0x300) [CODE]:
  - TD_Lamp (c913741cbe17490d) or TD_Sprite (fcdaa96bdeadbeef; also sets mat+0x335=1) → instance flag 0x100, model flags |= 0x30 (night lamps).
  - TD_LightBeam / TD_Gouraud → counter at +0x58.
  - TD_LightBeam → `0x986200`; TD_Ground → `0x986120`.
  - TD_Building with mat+0x312 and +0x313 == 1 → mat+0xfc = 8, texture slot = `DAT_011c9f24+0x48`, mat+0x332 = 1 (inferred to be a night/emissive window texture).
- Instance bbox: when the record has one, `0x988d10` copies `box + worldPos` into the instance (`0x98ac80` params). [CODE]

### 2.2 A: actors in `islands/ibiza/scenarics/packs/worldactors.bnk`

(The bank is only on the Mac install. It has been copied to `/mnt/user-data/uploads/TestDrive2/Euro/Bnk/islands/ibiza/scenarics/packs/worldactors.bnk`.)

- The MOB hashcode is `Entity__Actor.__Hc` in `entities.xmb` (bank type 8/11). Examples:
  - `c5c4f170f07bf9c2` = **"RS_Sp50_act"**
  - `6dc224712fc0478b` = RS_Stop_act, `85ebbe795c17fe5a` = RS_Yield_act, `95085b370f809543` = RS_MedTR_act, `a572b78c2dee51b0` = RS_PrioRd_act, `514c45067fda22e8` = RS_Sp90_act
  - `eb2298d44ac82629` = RS_DirL_D_act (direction sign).
  - All 31 distinct A hashes of the 17 sectors resolve. [DATA]
  - `__Hc` is not the 8-char fold hash.
  - At runtime the entity lookup `FUN_0048fd00` is [ENC].
- The entity's `mesh[]` holds `{rsc u64, rscType 0, rscSubType}`: 0 = 3DD, 1 = 3DG, 2 = 2DM, 3 = 2DB textures.
  - `rsc` maps to a bank entry through `hashcodes.xmb` (8/10): `CmdSaveFileInfoArray[] {Type, SubType, FileIndices[], FileReferences[] (sorted rsc hashes)}`, where FileIndices[i] is the bank entry index of FileReferences[i].
  - Example: RS_Sp50 3DD rsc `36b4c61448602a6f` → entry 291 = `rs_sp50_FBC78DBF0E4AA6BB.3DD`. [DATA]
  - Simpler alternative: the actor 3DD root node hash = `GetIdentifier64bit(__Name)` (`RS_Sp50_act` → `5f30355053b39693`). [DATA]
- Unlike the library, **the actor's own 3DD hierarchy must be applied** under the PMI matrix. Example: `RS_Sp50_act` → child → `RS_SP50` (T = 0, 1.42, 0); the mesh spans y −1.75..1.39, so the pole stands on the ground. [DATA]
  - The entity `orient_pos_scale` is an export-scene placement; ignoring it is [INF].
- Actor records always have a degenerate bbox (all 0x3fff). [DATA]
- Actor creation [CODE `FUN_009613e0` / `FUN_00960f60`]:
  - It runs only if the sector's actor pool exists (`+0x24b2c`) and the computed memory (`FUN_00960ce0`: Σ count·(classSize+0x20, or +0x250 for physics) + 0x19000) ≤ **614,400 bytes** (`DAT_00f76630`). Otherwise **no actor of that sector is created**.
  - The per-class enable byte is at `param+class[+0x84]`.
  - The world position is `sectorOrigin + local` (`param_6`, from the ints at `sector+0xfb0/+0xfb4`), w = 1.0.
  - The matrix is the same as 1.3, set through `vtbl+0x74`; the payload goes through `vtbl+0x58`.
  - The actor is linked into its leaf cell list (`w9 & 0x1ff`).
- `SetInstanciatePMIActorsLow/Medium/High : %d, %d` (quality settings, two ints each) and `STREAMING_ACTORSPMI_SIZE` gate actor instancing. Their values are in `Quality_Settings.cpr`, which is encrypted/packed (not XMB) and was not decoded.

---------------------------------------------------------------------------------------------------------------------

## 3. Coordinate space

- Positions are **sector-local**, in the same frame as the sector's `sector-…-o.3dd` (whose nodes have T = (348, 45, 592)…) and the terrain. [CODE+DATA]
- x and z cover [-128, 896): objects may overhang the 768 m sector by up to 128 m. Y is absolute.
- World position = `sectorOrigin + (x, y, z)`, with
  `sectorOrigin = ((8*ax+sx)*768 + (sy odd ? 384 : 0), 0, (8*ay+sy)*768)`. The A loader adds the sector origin explicitly [CODE `0x960f60`]. Origins match `EdenWorld::getSectorOrigin`, e.g. 3-3-3-3 → (21120, 20736).
- Raw D3D (left-handed) game space, the same as the sector .3dd files. OpenTDU2's `EdenModel` parses world banks with `bMirrorX = false`, so the PMI matrix can be used as is, combined with the unmirrored library 3DG.
- At runtime the engine subtracts a floating origin (`DAT_01367a10`/`DAT_01178130`) [CODE `0x986760`].

Press-start camera (21260, 35, 20155) is in sector 3-3-3-2 (origin 20736, 19968) at local (524, 187), near its south edge.

---------------------------------------------------------------------------------------------------------------------

## 4. Banks to load for the press-start view

| bank | contains | where |
|---|---|---|
| `islands/ibiza/level/commonworld.bnk` | `library.3dd/.3dg/.2dm` + 2DB textures for all V/O/S models: houses, walls, palms, lamps, guard rails… | upload (59,326,464 bytes, same as the Mac file) |
| `islands/ibiza/level/area-3/area-3-3/sector-3-3-{2,3,4}-{1,2,3}.bnk` (the 9 sectors OpenTDU2 already loads) | `-V/-O/-S/-A/-F.pmi` | upload (all of area 3-3 is present) |
| `islands/ibiza/scenarics/packs/worldactors.bnk` | actor entities (`entities.xmb`, `hashcodes.xmb`) and the **road signs** (`rs_sp50_*`, `rs_stop_*`…, 161 3DD/3DG, 223 2DM, 47 2DB) | **Mac only; copied** to the upload path above |

Instances within 250 m of the camera [DATA, `/tmp/claude-0/pmi/scr/near.py`]:

| sector | O | V | A |
|---|---|---|---|
| 3-3-3-2 | 1112 | 54 | 18 |
| 3-3-3-1 | 43 | – | 6 |
| 3-3-2-1 | 3 | – | – |
| 3-3-4-2 | 1 | – | – |

- **Signs (A)**: RS_Sp50_act ×9 (e.g. at world (21302.7, 32.95, 20162.1) and (21221.7, 34.66, 20153.1), yaw 265° / 100°), RS_Yield ×5, RS_Stop ×2 (21269.3, 34.0, 20166.1), RS_PrioRd ×2, RS_MedTR ×2, RS_Sp90 ×2, RS_DirL_D/RS_DirR_D.
- **Houses (O)**: HE_AM_A/B ×6 each, HE_AM_C ×3, HE_AA_A ×2, HE_AA_B/C, HE_AC_A/B/C, HT_AD_A/B. Example: HE_AM_A at (21240.0, 36.06, 20193.3) yaw 93.8°.
- **Other O**: walls SF_WAL* (~200), two bush models (870), V_PALM_B/F, agaves, benches, lamps.
- **V**: V_PALM_A/C/D/E ×54.

Notes:
- The `exterior/*.bnk` files (dealers, real-estate houses, `*_low` variants) are **not referenced by any PMI** in these sectors. They are placed by another system and are not needed for these instances.
- `ibiza.bnk` holds no PMI.
- A full hex neighbourhood of 3-3-3-2 would be 2-2, 4-2, and 2-1, 3-1, 2-3, 3-3 (odd rows are shifted by +384). The 3×3 set OpenTDU2 loads covers it.

---------------------------------------------------------------------------------------------------------------------

## 5. Exe code (readable part)

| addr | role |
|---|---|
| `0x00986760` | sector instance setup: calls `0x988d10` ×3 (V/O/S lists at +0x28d0/+0xa010/+0x11750), then `0x988730` (+0x18e90) |
| `0x00988d10` | **static PMI loader** (V/O/S): version check, two cell histograms (409), per record decode → `0x98ac80` (model lookup and instance) → cell registration via `0x987dd0`; type 3/4 payload handling; limit 4000 |
| `0x009884c0` | S list companion model: hash remap (`0xc31620`, table `0x11f7728`) → `0x8b9100`, stored at instance `+0x60`, flag `0x10000` |
| `0x00988250` | record → matrix (S·Rx·Rz·Ry + T), bbox (vec4 min/max, w=0), color = 0xffffffff |
| `0x0098ac80` | model instance creation: `FUN_008b9100(hash)`, copies the bbox, scans materials for Lamp/Sprite/LightBeam/Gouraud |
| `0x0098a870` | static instance matrix rebuild: Scale · RotY(yaw) with a lookup `0x97ef20` |
| `0x00987dd0` | quadtree cell → ancestor at level L (tables 0xf2c798/0xf2c7a4) |
| `0x00987d60` | per-cell list setup from cellCount[409] |
| `0x009879e0` | sector load: A.pmi (5/14) → `0x9613e0`, also 5/5 and 5/0x15 resources |
| `0x009613e0` | **actor PMI loader**: per-cell lists (409 × 8 bytes at +0xcc8), memory budget `0x960ce0` ≤ 614,400, `0x960f60` per record |
| `0x00960f60` | actor creation: world matrix, `vtbl+0x74` set matrix, `vtbl+0x58(payload)` |
| `0x00960ce0` | actor memory estimate |
| `0x00c38f40` / `0x00c38d40` | F.pmi loader (≤30 entries, hash list `0xb34a40`) |
| `0x00da0440` | model instance init from an INS. descriptor: LOD table (+0x4e count, +0x50 stride 0x14, distance × 3.160494) |
| `0x00acb670`, `0x00acc990`, `0x00acd7f0` | quality config parsers (`SetInstanciatePMIActors*`, `MAP_LOD_*`) |
| `0x00ddbbb0`, `0x00ddbd60`, `0x00ddbc40`, `0x00ddbcd0`, `0x00ddb740` | matrix helpers (scale, rotX/Z/Y, multiply); their bodies call [ENC] code |
| `0x0048fd00`, `0x004dd*`, `0x004dc*` | actor entity lookup and cell-list consumers: [ENC] |

Constants: 0xf4ae38 = 0.0156252384, 0xf49e08 = 128.0, 0xf4ae30 = 6.1036088e-5, 0xf4ae40 = 9.5875264e-5, 0xf4ae28 = 0.0625009537,
0xf4add0 = 1024.0, 0xf49830 = 65535.0, 0xf4aa28 = 3.160494, 0xedf668 = 1.0f, 0xf76630 = 614400, 0xedd53c = 25.0 (rail bounding radius).

---------------------------------------------------------------------------------------------------------------------

## 6. Rendering recipe for OpenTDU2

For each loaded sector, for each of the V/O/S/A files:

1. Parse the file (§1).
2. For V/O/S records: find the library node by hash. For LOD0, draw its object with the node's MBNK materials (`library.2dm`) and the matrix of §1.3, ignoring the node transform.
   - Optional LOD: switch to LOD1 (the TD_HardwareInstance or TD_Impostor material) beyond √lodDistance[0], and hide beyond √lodDistance[1] (both scaled by an unknown factor) [INF].
   - S records (type 3): skin with the record payload, `v · Bone[i] · M` (§1.5); draw distance [ENC] (§1.5), OpenTDU2 draws them over the loaded sectors.
3. For A records: entity by `__Hc` → `__Name` → 3DD/3DG/2DM of `worldactors.bnk` (via `hashcodes.xmb` rsc, or root node = `GetIdentifier64bit(__Name)`). Draw the actor's node hierarchy under the PMI matrix. Type 4 carries sign text, which can be ignored for geometry.
