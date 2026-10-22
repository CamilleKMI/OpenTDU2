# TDU2 intro house EPH_L3DI_5067I: pool water and house interior — reimplementation spec

Tags: **[CODE]** read from readable x86 (≥ 0x6c1000) · **[DATA]** read from game files · **[SHADER]** from .sob bytecode ·
**[INF]** inferred (reason given) · **[ENC]** behind SecuROM-encrypted code, not read.

Work files (all under `/tmp/claude-0/pool/`):
- decompiles: `w1.c` FUN_00d7df50 (registers projective-map materials), `w3.c` FUN_00d7e950 (GSReflection bank registration),
  `w4.c` (FUN_00dd6070/60c0/63e0, FUN_00d7e4b0), `w5.c` (GSReflection vtable methods, frame render FUN_00d7eea0,
  FUN_00d7de80), `w6.c` (init FUN_00d7cc30, FUN_00d7c030 per-frame texture bind), `w7.c`/`w8.c`/`w9.c` (NIGHTLMAP
  weather drivers), `w10.c` (Entity__Light loader), `w2.c` (SKYSPHEREMAP / CUBEMAP render-target passes).
- shader dumps: `sob/TD_Water/*` (dxso+spv), `d_TD_Water.txt` (all 192 permutations, `-dump -all`), `sob/b` (TD_Building).
- tools: `tools/dd3.py` (3DD hierarchy dump), `tools/g3prim.py` (3DG primitives and bounds).
- extracted banks: `mt/` meeting_tess, `ga/` garage, `gph/` interior/gph_l3di, `low/` eph_l3di_low. These are new
  copies; `/tmp/claude-0/intro/{eph,lph,x}` already held eph_l3di, lph_l3di and new_intro.
- Staged from the Mac into the container, read-only: `scenarics/cinematics/intro/{meeting_tess,garage}.bnk`,
  `interior/{gph_l3di,lph_l3d}.bnk`, `islands/ibiza/exterior/eph_l3di_low.bnk`.
- OpenTDU2 screenshots (current state): `/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad/pool35.png`,
  `pool46.png` (the pool is drawn **flat white**); verbose log `/tmp/claude-0/poolv.log`.

---------------------------------------------------------------------------------------------------
## 0. Summary

| question | answer |
|---|---|
| pool water shader | **TD_Water** (`TD_Water.sob`, GUID `0x0123000000000000`). It is **not** TD_Ocean and **not** TD_Pool (that family does not exist). [DATA+SHADER] |
| pool water mesh | Material `caustic` = material index 34 of the main exterior object `EPH_L3DI` in `islands/ibiza/exterior/eph_l3di.bnk` → `eph_l3di_sc_C44A679762D76AA8.{3DD,3DG,2DM}`. It is one quad: 4 vertices, y = 31.02, x −12.18…1.24, z −9.58…4.04 (house frame, before the X mirror). [DATA] |
| pool basin | Separate object `POOL` under nodes `D_REFLECT → REFRACT → POOL` in `eph_l3di_59E1C536C6C6FAD9.3DD`. Materials `Caust_01` and `pierre_02` (TD_Building); y 29.43…31.03. [DATA] |
| runtime setup | The **GSReflection** module (object at `0x011f8ca0`, vtable `0xee727c`) does three things: it registers the TD_Water materials, it builds a *refraction projector* from the `REFRACT` node, renders the basin into the "REFLECT" render target every frame, and binds that target to sampler **s4 `gProjectiveMapSampler`**. [CODE] |
| why OpenTDU2 is white | (1) s4 (refraction, 85 % weight) is unbound → white 2D fallback; (2) the CUBEMAP 2DB is a 6-face cube, but OpenTDU2 uploads it as a 2D face-0 texture → white cube fallback in s3; (3) `__UVA_*` is identity → the normal maps do not scroll; (4) the material handler/bucket is wrong (minor). [CODE OpenTDU2 + DATA + SHADER] |
| interior source | In the intro: **new_intro.bnk's own copy**: Scenery `D_SCENE` = `intro_lph_l3di_349E1852FA421941` plus `Entity__Actor` furniture plus `Entity__Actor__Mesh D_REFLECT` = `lph_l3di_59E1…` (indoor pond basin and a mirror plane). In normal play: `interior/lph_l3di.bnk` (same frame) and `interior/gph_l3di.bnk` (garage, own frame). [DATA] |
| interior lighting | Baked lightmaps. COLOR slot = `lm_eph_l3d` (day), slot 8 = `lm_eph_l3d_night`, blended by the material parameter **NIGHTLMAP**. The game overwrites NIGHTLMAP every frame from the weather: `spot_interior_lightmap_day_night` = (0, 1) for interiors, `spot_exterior_lightmap_day_night` = (1, 0) in weather Intro for the exterior. Reflections use static cube 2DBs (`env_lph_l3d` 256², `cubemap` 64²). [CODE+DATA+SHADER] |

---------------------------------------------------------------------------------------------------
## 1. Pool water: data

### 1.1 Exterior bank `islands/ibiza/exterior/eph_l3di.bnk` entities (entities.xmb) [DATA]

| entity | class (IDClass) | mesh (3DD) | placement (orient_pos_scale) |
|---|---|---|---|
| Scenery `EPH_L3DI` | Scenery | `eph_l3di_sc_C44A679762D76AA8` (whole exterior, pool water quad) | house frame |
| `Door_garage` | Entity__Actor | `eph_l3di_14F68374D895E85A` (roll-up door; mesh authored at its own origin, y −2.6…0, z ±3) | **pos (27.686, 2.6, −3.5315)**, rot identity |
| `CF_Key` | Entity__Actor | `cf_khous_575F…` (key ring, skinned, used by removal cinematics) | pos (0.072, 0, 0) |
| `D_REFLECT` | **Entity__Actor__Mesh** (`'MESH'` = 0x4d455348) | `eph_l3di_59E1C536C6C6FAD9` | identity |
| `interior_01` | Entity__Actor__Interior (`'INTE'`) | `eph_l3di_990064580DE70AD6` (garage interior LOD, y 0…2.6) | identity |
| `Interior` | Entity__Actor__Interior | `eph_l3di_4D594F8203D580EB` (living-room LOD seen through the windows, `Mat_Lod_int` + `lm_lph_l3d_lod`, bounds x 9.25…24.45, y 31.04…36, z −8.13…13.44) | identity |
| `Evt_Box_`, `Spot_Detection` | Entity__Volume | — | — |
| 9 cinematics | C_VTOUR_E, C_REMOVAL, C_OUTRO, C_INTRO_E_1/2, **C_OPEN / C_CLOSE** (garage gate), C_FIRST_OUTRO, C_INTRO_SHORT | | |

`eph_l3di_59E1…3DD` hierarchy: `D_REFLECT` (hash `98 5f 52 45 46 4c 45 43` = fold of "D_REFLECT") → `REFRACT` (no
geometry, identity) → `POOL` (object `POOL`, materials `Caust_01`, `pierre_02`). [DATA, tools/dd3.py]

Other variants of the bank: `eph_l3di_low.bnk` (one LOD 3DD `eph_l3di_low_sc_A08677E6C12EC429`) and `eph_l3di__xx.bnk`
(localized, 22 KB) [DATA].

### 1.2 Material `caustic` (TD_Water) [DATA eph_l3di_sc_C44A…2DM]

| field | value |
|---|---|
| states (+0x30…) | `01 01 00 02 00 01 00 7f 02 01 06`: depth test on, depth write on, cull 2, blend op 6 = **opaque** |
| spec | 0:2 1:3 3:1 8:3 10:1 12:1 13:1 14:1 32:1 35:1. S_COORDTEXTURE = 3, S_USEREFLECTION = 1, S_UVMATRIXMASK = 3, S_NBPRELIGHT = 1, U1 = 1 (color map), **U4 = 1 (projective map)** |
| slot 0 NORMAL0 | `caustic_water.2db` (128², 2D) |
| slot 1 NORMAL1 | `caustic_water.2db` |
| slot 2 COLOR | `nuancier_01.2db` (colour swatch, picked by TC2) |
| slot 3 REFLECTION (fold `a193464c45435449`) | hash `0050414d45425543` = "CUBEMAP" → `cubemap.2db` (**cube**, 64², 6 faces, 5 mips, DXT1) |
| slot 4 REFRACTION (fold `a193465241435449`) | hash 0 (runtime) |
| SPECPARM | (200, 5) |
| MATPARAM | (1, 0) |
| BIASCOEF (biasCoeff) | (0.03, 0.15) |
| WATERFAL | (4, 0.5); used only by the U5 permutations |
| vertex streams | pos, normal, **D3DCOLOR**, 3 UV sets, tangent, binormal [DATA 3DG sem `1:0 1:b 1:2 3:c 1:17 1:b`] |

UV animations [DATA]:
- The 2DM `UVAA` section lists two `UVA.` entries. Their ids `0xfd398b99263a1775` and `0x7009d837_00035fb2` equal the
  ANM header ids of `caustic_0.UVA` and `caustic_Normal1_1.UVA`.
- File name convention [INF from all bank files]: `<material>[_<SlotName>]_<slotIndex>.UVA`.

Indoor pond `Oasis_02` (in `intro_lph_l3di_349E…` and `lph_l3di_sc_349E…`) [DATA]:
- Same family. Spec has no prelight. REFLECTION = `env_lph_l3d.2db` (cube 256², 7 mips).
- SPECPARM (60, 1). UVAs: `Oasis_02_0.UVA` (20 s) and `Oasis_02_Normal1_1.UVA` (18 s).
- Basin: `bassin` (TD_Building), under `D_REFLECT → REFRACT → POOL` of `lph_l3di_59E1…`, at x 20.38…24.38,
  y 31.38–31.44, z 4.65…8.76.

### 1.3 Permutation [SHADER + CODE OpenTDU2 log]

- With S_NBTEXTURES = 5 (NORMAL0, NORMAL1, COLOR, REFLECTION, REFRACTION), the material switches match
  **item `b9624b65fa73b46a` = vs 46 / ps 14** exactly. OpenTDU2 already selects it, with the log line
  `S_NBTEXTURES(shader 5 mat 4)`.
- vs 46 has `dyn[0e 0f]` = `__UVA_0` and `__UVA_1`.
- Oasis_02 → item `521895c4950cac8d` (vs 30 / ps 14).
- U4 = 0 variants exist: ps 12/13 (no s4), e.g. `5f3bb15fae4013e3` = vs 46 / ps 12 for `caustic`.
- Switch meaning in TD_Water (from the 192 items): NBTEX = 2 + U1 + REFL + U4; U1 = s2 colour map; REFL = s3
  (**cube**, D3D sampler type 3); **U4 = s4 projective map**; U5 = WATERFAL constant.

PS 14 samplers: s0 gNormal0, s1 gNormal1, s2 gColor, s3 gReflect (CUBE), s4 gProjectiveMap (2D), s15 shadow.

PS 14 constants:

| register | content |
|---|---|
| c9–c12 | `__World` |
| c13 | Amb |
| c14 | DirDiffuse |
| c15 | DirSpecular |
| c16 | SpecParm |
| c17 | MatParam |
| c18 | biasCoeff |
| c6 | RenderTargetSize |
| c7 | StereoUvMadd |

VS 42/46 constants:

| register | content |
|---|---|
| c60 | WVP |
| c64 | WorldView |
| c68 | CamPos_ObjSpace |
| c69 | DirLight_Dir_0 |
| c70+ | `__UVA_1` |

Exact PS 14 math [SHADER, `/tmp/claude-0/oc/dis2.py`]:
```
n1 = 2*(tex(s1, uvN1).agb - .5); n0 = 2*(tex(s0, uvN0).agb - .5)   // DXT5nm-style (a,g) normals
N  = normalize(normalize(n0) + normalize(n1))            (tangent space; v5/v6/v7 = TBN from VS)
off = N.xy * biasCoeff.xy                                 // (0.03, 0.15)
s   = screenUV = projPos.xy/projPos.w (VS: (x+w)/2, (w-y)/2)
a   = frac(s*0.5000012+0.5)*2π-π  ;  d = s + off*sin(a)   // distortion fades to 0 at screen edges
R0  = tex(s4, s*c7.xy+c7.zw); R1 = tex(s4, d*c7.xy+c7.zw)
refr = lerp(R0.rgb, R1.rgb, R1.a)                         // alpha of the refraction RT = "valid underwater pixel"
refl = texCUBE(s3, world reflect(-V,N)) * MatParam.x * fade,  fade = 1 - sat((|viewPos|-200)*0.02)
lit  = tex(s2, TC2).rgb * lerp(Amb, Diff, sat(N·L)) * vcolor + refl
col  = lerp(refr, lit, biasCoeff.y)                       // 85 % refraction, 15 % surface
col += pow(max(R·L,1e-4), SpecParm.x) * Spec * vcolor * SpecParm.y
alpha = tex(s2).a ; then the usual Eden s15 shadow/HDR tail
```
VS: `uvN0 = TC0 * __UVA_0`, `uvN1 = TC1 * __UVA_1` (c70.xyw/c71.xyw rows dotted with (u, v, 1)), `uvColor = TC2`.

### 1.4 UVA format (ANM! with a MAT! track) [DATA, decoded from 7 files]

```
"ANM!" u32 0x10, f32 1.6, u32 0xce, u64 id (= the 2DM UVA. reference), "DATA" u32 size,
  u32 numTracks(1), f32 duration, f32 duration, u32[3] 0, u32 0x1f, u32 0x64,
"MAT!" u32 1, u32 0, u32 0, u32 0,
  4 channels, each: { u32 n; f32 length; f32 times[n]; f32 values[n] }
     ch0 = U offset, ch1 = V offset, ch2 = U scale, ch3 = V scale
```

Proof:
- `artWorks_09_0.UVA`: ch0/ch1 have 128 keys stepping by 0.125 (an 8×8 flipbook), ch2 = ch3 = 0.125.
- `caustic_0` / `caustic_Normal1_1`: ch0 goes from 0 to **1.0 over 10 s**; V = 0; scale = 1.
- `Oasis_02_0`: 0 → 1 over 20 s. `Oasis_02_Normal1_1`: 0 → 1 over 18 s.
- `Water_falls_Detail_2`: 0 → −4 over 10 s.

Matrix [INF from the VS use]: `c70 = (su, 0, 0, offU)`, `c71 = (0, sv, 0, offV)`, i.e. `u' = su·u + offU`.
Interpolation: linear, looping on the ANM duration [INF].

### 1.5 Handler / bucket [CODE 0x00d70cf0]

TD_Water: `K(0x1b)=0; return (M(0x31)==2) ? 0x2e : 0x19`.
- 0x19 = **WATER** (layer table 0xf7f604); 0x2e = WATER_FULLALPHA.
- TD_WaterRiver (0x00d70cb0) and 0123210123210123 (0x00d70cd0, `K(0x18)&=7`) return 0x18 when M(0x31) == 2,
  else 0x17 (WATER_RIVER_FULLALPHA / WATER_RIVER).
- OpenTDU2 maps all three to NoFog/UVMask3 → default bucket → 0x11 OPAQUE.

---------------------------------------------------------------------------------------------------
## 2. Pool water: runtime (GSReflection) [CODE]

### 2.1 Enable per game mode

`FUN_00d7c120(bool)` sets `mgr+0x26`. FUN_00a93640 reads the table at **0x00f817e0** `{mode, enable}`:
- enabled (1): SHOP, CARSHOWCASE, GARAGE, **LIVING**, GOODIES, GAMBLING_HALL, AUCTION_HALL, DRESSING, CINVIEWER,
  ESTATES, CLUB, LOBBY, DRIVING_SCHOOL, SCREEN_SHOTS, HAIRDRESSER, PHOTOGRAPH, SPOT_VIEWER, CASINO, STICKERS, CINE,
  **AVATAR_CHOICE (0x20, the intro)**, SURGERY, AIRPORT, CAR_WASH, **FIRST_WM (0x23)**;
- disabled (0): BOOTMENU, **FREERIDE**, MAP, MISSION, EDITOR, TESTCAR, ISLAND_CHANGE, …

### 2.2 Material registration — FUN_00d7df50 (from FUN_00d7e950)

Table at **0x00f817c0**, entries `{guid lo, guid hi, specByte, slot}`:
- `{TD_Furnitures d5013ed0db79afaf, 7, 6}`: spec[0x26] (U7) == 1 → slot 6;
- `{TD_Water 0x0123000000000000, 4, 4}`: spec[0x23] (U4) == 1 → slot 4.

For each loaded material (at most 100 entries):
- slot hash = −1 (runtime);
- push `{MAT, specByte, slot, enabled = 0, bank id}` to `mgr+0x178`;
- push the SPECPARM pointer to `mgr+0x188`. FUN_00d7c7f0 rewrites `SpecParm.x = ocean_specularPower / x²`; the second
  factor is x87 code that cannot be read reliably [CODE partial].

TD_Water only, when the global `DAT_011c9f24` exists:
- slot 3 (+0x168) := `DAT_011c9f24 + 0x48`;
- U4 := 0, slot 4 := 0, slot count = 16;
- the shader is selected again (FUN_008a8950).

`DAT_011c9f24` is the **"SKYSPHEREMAP"** 64×64 format-3 2D render target [CODE FUN_00c32ac0, pass object 0x11c9f14,
created with depth `FUN_008b73b0(0x40,0x40,0x40,…,"SKYSPHEREMAP")`].
- Every TD_Water PS declares s3 as a **CUBE** sampler [SHADER].
- So the original binds a 2D texture to a cube sampler. Under D3D9 the result is undefined (typically 0) [INF].
- This is the one point where faithfully copying the original is ill-defined. Recommendation: keep the bank cube
  `cubemap`, as the material authoring intends, and add an option for a black reflection when comparing against
  captures [INF].

### 2.3 Enabling a registered material — FUN_00d7de80(1), called from FUN_00d7e4b0

- `spec[specByte] = 1` (U4 back on), slot hash = −1, slot ptr = 0;
- sampler bytes `+0x116 = +0x117 = 2` (address mode → clamp [INF]);
- shader selected again.

Disabling does the opposite. Reset/uninstall: FUN_00d7e7d0 and FUN_00d7e8b0.

### 2.4 Projectors — FUN_00d7e950(mgr, bank)

`bank` is the level/scene object; registration happens for the spot exterior (FUN_00981b60 → 0x981d27), for
cinematic banks (0x9be350/0x9be3cc) and for interiors (0xa7c69a, …). Steps:
1. FUN_00d7df50 (above). If `mgr+0x26 == 0` the function stops here: the water uses PS 12/13 without refraction,
   and the reflection slot gets SKYSPHEREMAP.
2. `isEPH = strstr(bankName(+0x10), "EPH") || strstr(…, "ECAS")`.
3. `ent = FUN_00b34af0(bank, 'MESH')` = the Entity__Actor__Mesh **D_REFLECT**.
4. Walk its scene nodes (`ent+0xf8` → `+0x14` list).
   - A node **with** geometry is added to the current projector's object list (FUN_00dd63e0, at most 0x41).
   - A node **without** geometry creates a projector (0xa0 bytes, at most 10 in `mgr+0x14`), with
     `n = node+0x20..0x28` and `d = n · node+0x40..0x48`. These are the node's Y axis and position [INF].
   - If the node name contains **"REFRACT"**: FUN_00dd6070: plane (n, d) at +0x80/+0x90, view matrix +0x40 =
     identity (FUN_0041d9e0), `+0x94 = 1` (refraction).
   - Otherwise: FUN_00dd60c0 builds the **mirror matrix** `M = I − 2nnᵀ`, translation `2·d·n`, at +0x40…+0x7c, and
     sets `mgr+0x25 = 1`. If the node hash equals `ent+0x190` / `ent+0x198` and `!isEPH`: `+0x95 = 1`
     (actor-reflection layer; at most 2, views `mgr+0x124` / `mgr+0x128`).
5. For the intro data:
   - `eph_l3di`: one refraction projector (`REFRACT`) with objects {POOL}.
   - new_intro's `lph_l3di_59E1`: `REFRACT` → {POOL `bassin`}, plus **`GROUND2` = mirror projector** (at
     12.29, 31.57, −5.94) → {PLANE_01 artwork}.

### 2.5 Per frame — FUN_00d7eea0 (vtable slot 5) and FUN_00d7c030

Preconditions: `mgr+0x1bc` (registered material count) ≠ 0, enabled, at least one projector.

Views:
- `mgr+0x120 = DAT_00ffbef8`, `+0x124 = DAT_00ffbf24`, `+0x128 = DAT_00ffbf50` (stride 0x2c).
- By order of the layer table these are **REFLECT 2D**, **REFLECT FOR ACTORS 1ST LAYER**, **2ND LAYER** [INF].
- Their aspect is set to screen w/h (`DAT_01367cf0/cf4`) in FUN_00d7cc30.
- All three layers render into the RT set **renderer+0x10880** [CODE FUN_00d73170 layer table: entry = {RTset, flags,
  name, …}; flags 0x40800000 / 0x40800001 / 0x40800001].
- Order: after `HEADLIGHTS BLIT`, before `NEAR_OPAQUE`.

Refraction projector (+0x94):
- The projector's objects go into DAT_00ffbdf0 (6 entries before REFLECT 2D = ZPREPASS view [INF]) and into the
  REFLECT 2D view.
- **Actors** (avatars from DAT_00fd9004 via vfunc +0x60/+0x4c, cars from mgr+0x1a0) are added only when
  `mgr+0x24 == 1`.
- `mgr+0x24` is set to 1 only by the cinematic hooks for `"C_JACCUZZI"` (0xa41f47) and name `"C_A4"` (0x9b4978).
  In C_INTRO it is 0, so **the pool refraction holds only the basin**.

Mirror projectors (+0x95): the scene is drawn with the mirror camera (FUN_00dd6300); at most 2.

Texture bind — FUN_00d7c030, every frame: for each enabled registered material,
`slot[k].hash/ptr = *(view(mgr+0x124)+0x34)` (its RT colour texture), else `DAT_01459dc8+0x18`.

Not found in readable code: the RT size/format and the clear colour [ENC]. The shader samples it with screen UV ×
StereoUvMadd, so it must be **screen-sized**; its **alpha must be 0 where nothing was drawn** for the `lerp(R0, R1, R1.a)`
edge guard [SHADER→INF].

Draw order: TD_Water is in bucket WATER (0x19), after all opaque, avatar and building buckets [CODE 0xd70cf0 + layer
order]. Its depth write hides submerged avatar parts, so the swimmers' legs are not visible in C_INTRO [INF].

---------------------------------------------------------------------------------------------------
## 3. Why OpenTDU2 draws it wrong (verified on screenshots pool35/pool46: flat white water)

| # | cause | where |
|---|---|---|
| 1 | s4 `gProjectiveMapSampler` has no texture → `pickTexture` returns **white 2D**; it weighs 85 % | `eden_renderer.cpp` ~1073–1101 |
| 2 | `cubemap.2db`/`env_lph_l3d.2db`/`cubemap01`/`cubecarb` have **Depth = 6, Flags 0xC4** (2D textures: Depth 1, 0x88). TextureCache treats Depth > 1 as a volume and uploads **face 0 as 2D, 1 mip** → the cube sampler gets **white cube**. This affects every reflective interior/exterior material (glass, frames, TV, pool). | `texture_cache.cpp` 173–176, `2db.cpp` getMipSize·Depth |
| 3 | `__UVA_*` written as identity → caustic normals static | `eden_renderer.cpp:921` |
| 4 | no GSReflection: no refraction RT and no planar mirrors (living-room floor `Mat_Floor1_09`, TD_Furnitures U7 → slot 6) | — |
| 5 | TD_Water / WaterRiver / UVMask3 handlers return 0x39 → OPAQUE instead of 0x19/0x2e/0x17/0x18 | `eden_material_handlers.cpp` NoFog/UVMask3 |
| 6 | NIGHTLMAP stays at its 2DM default (0.44, 1.0) instead of the weather value → the exterior gets 56 % night lightmap plus night emissive in daylight; the interior gets 44 % sun | §5 |

2DB cube layout [DATA, verified by checking that mip 1 matches a downsampled mip 0 on env_lph_l3d: error 1.9 for
face-major against 60 for mip-major]:
- texels are **face-major**: face 0 holds all of its mips, then face 1, …;
- 6 faces in D3D order +X, −X, +Y, −Y, +Z, −Z [INF, standard D3D].

With the OpenTDU2 X mirror [INF, geometric]: new[+X] = flipH(old[−X]), new[−X] = flipH(old[+X]), and the other four
faces = flipH(old). The shader computes R in the mirrored world, so this keeps the look of the original.

---------------------------------------------------------------------------------------------------
## 4. House interior: banks, placement, entities

### 4.1 House identity

DBHouse[58] [DATA db_data]:

| field | value |
|---|---|
| hashcode | 0x6c |
| housename_hc | 0x1989 |
| level | 3 |
| nbcarslots | 6 |
| realtor | ERE_B_3336I |
| spotname | **EPH_L3DI_5067I** |
| windowedgarage | 0 |

The twin house EPH_L3DI_3346I (DBHouse[69]) uses the same banks.

`islands/ibiza/level/spots.bin` record, 0x110 bytes [DATA]:

| off | content |
|---|---|
| +0x00 | u64 id |
| +0x08 | char[8] `"EPH_L3DI"` (bank base name) |
| +0x10 | position (36382.32, 5.9198, 5729.77) |
| +0x20 | heading 2.1903 |
| +0x28 | second point (36329.80, 5.213, 5743.80) |
| +0x34 | u8[4] **(5, 0, 6, 7)** = area / sector → `Sector-5-0-6-7` |
| +0x38 | heading2 −1.3049 |
| +0x40 | u16 list 0x8b, 0x8c, 0x8d, 0x9d, 0x9e, 0x9f, 0xb0, 0xffff… |
| +0x80 | OBB 1: axes (47.57, 0, 66.63) / (0, 39.89, 0) / (−55.32, 0, 39.50), centre (36359.45, 24.37, 5735.49) |
| +0xc0 | OBB 2: centre (36357.55, 8.05, 5718.62) |

Meaning of the second point, the u16 list and the OBBs [INF/unknown]: the second point is probably the car/garage
entrance and the OBBs the property zones.

Bank naming [DATA, from the file names]:

| part | bank |
|---|---|
| exterior | `islands/ibiza/exterior/eph_l3di.bnk` (+ `_low`, + `__<lang>`) |
| living interior | **`interior/lph_l3di.bnk`** |
| garage | **`interior/gph_l3di.bnk`** |
| furniture | `interior/myhouse/<DBFurniture._bank>.bnk` (e.g. tabdinej, beanbags…) |

- The path builder was not found in readable code [ENC].
- Rule [INF]: E→L / E→G prefix.
- `lph_l3d.bnk` / `gph_l3d.bnk` are the non-"i" variants (L3D vs L3DI).

### 4.2 Frames [DATA]

- The **living interior (`lph_l3di`) and the intro's interior use the house frame**. Interior nodes are at y ≈ 31–36,
  and the exterior "Interior" LOD bounds coincide with the LPH_L3DI geometry.
- World [CODE in OpenTDU2, confirmed by the data]: `world = local · RotY(heading) · T(spot position)`.
- The **garage `gph_l3di` / `garage.bnk` is a separate space**: GPH_L3D at x −23…14, y 0…4, z 6…24. It does not match
  the exterior garage LOD at x 12…27, y 0…2.6, z −6.5…−0.5. Its placement is [ENC]; render it in its own frame.

### 4.3 What the intro (new_intro.bnk) contains for the interior [DATA]

| entity | class | mesh |
|---|---|---|
| D_SCENE | Scenery | `intro_lph_l3di_349E1852FA421941` (identity). Main mesh `LPH_L3DI` (52 materials), COLLISION boxes, `TV`→`GLASS`, `LIGHTS`, `FLARE` (halo), `PC_MEDIA` |
| D_REFLECT | **Entity__Actor__Mesh** | `lph_l3di_59E1…` (REFRACT→POOL `bassin`; GROUND2 mirror plane → PLANE_01 `Artworks_01`) — **OpenTDU2 does not load it** (it only walks ArrayOf_Entity__Actor) |
| furniture | Entity__Actor | `intro_lph_l3di_objects_*`: TabRecE, sofaL_A, Sofa1P_A, TabDineC, ChaiSalD(_01…_05), ChaiBarD(_01,_02) |
| HOLOGRAMS | Entity__Actor | `intro_lph_l3di_71E5…`: the LIVING-mode icons (T_Sprite_Garage/Ingame/Diary, Pulse_DarkBlue UVA). Visibility in C_INTRO is unknown [ENC] |
| party props | Entity__Actor | cf_mix, cf_speak, cf_plat, cf_tabl, cf_buoy, pc_media |
| lights | 55 Entity__Light | 23 `RT_Int_*` (isInterior 1) + 32 `RT_Ext_*` |
| cameras | 7 Entity__Camera | cam_entree_01…06 (≈ x 0…2.4, y 32.6: terrace→living entrance), NO_CAMERA |
| volumes | — | `Garage` FpSelectable (20.24, 33.11, 3.21; OfType 2, Type 5), `Tess` Volume (9.25, 32.68, 6.71), `lookAt_Tess` (7.58, 32.78, 6.31) |

**Placement rule for actors** [DATA + INF]:
- Furniture 3DDs have their **root node at the absolute house position**: e.g. TABRECE root (15.68, 31.56, −2.78)
  equals the entity pos (15.684, 31.564, −2.778); SOFAL_A root = entity transform including the 180° yaw.
- cf_* props have identity roots.
- The rule that fits both cases: **actor world = sceneOrigin · entityTRS · rootLocal⁻¹ · nodeWorld**. The entity
  transform replaces the root node.
- OpenTDU2 currently draws `sceneOrigin · entityTRS · nodeWorld`, which **doubles the offset** for all 14 furniture
  pieces (e.g. TabRecE at (31.4, 63.1, −5.6)).

### 4.4 Interior bank `interior/lph_l3di.bnk` (normal LIVING mode) [DATA lph_ent.txt]

| entity | content |
|---|---|
| Scenery D_SCENE | `lph_l3di_sc_349E…` (same object hash as the intro copy) |
| Entity__FirstPerson `First_Person` | StartCamera 0xb79edc61b68be1de (= `cam_entree`, 23.56, 32.78, 6.84) |
| 13 Entity__MyHouseObject | furniture slots `{pos, rot, Type (DBFurniture._type: 1 chair, 4/8 sofa, 9 dining table, 11 low table, 15 bar stool), Group, DefaultColor}`. Positions equal the intro furniture. Models come from `interior/myhouse/*.bnk`. Colour materials are `_MHA_/_MHB_/_MHC_xx` (string `_mh%c_01` in the MyHouse module 0x90b…), and walls/floors `MAT_WALL1_`, `MAT_FLOOR1_`. |
| 6 FpSelectable | ExitDoor (24.51, 32.31, 6.84), Media (Type 57, "td_media"), **Garage** (20.31, 32.84, 3.33 → `cam_entree_garage` 19.52, 33.65, 2.94), PlugGame, Dressing (→ cam_entree_dressing), Diary |
| actors | `LIGHT_ACTOR_ Cat_01`, `HOLOGRAMS` |
| Entity__Actor__Mesh | `D_REFLECT` |
| lights | 55 Entity__Light |
| cameras | 3 |
| cinematics | 9: C_REALTOR, C_LIFE(_CAT), C_INTRO, C_INTRO_FROM_GARAGE / _DRESSING, C_MYLIFE, C_DIARY_L, C_REMOVAL |

Scenario banks:
- `garage.bnk` (FIRST_WM): `garage_int_*` and `gph_l3d_*` (GPH_L3D, `D_REFLECT`, BANNER), garage fiesta `.bas` / `.trk`,
  C_ENTER_CAR / C_OUTRO / C_VOICE_xx, and the waterfall UVA.
- `meeting_tess.bnk`: `garage_int_*`, phone, ferrari key, M_MEETING_TESS (the wake-up scene, outside this house).

### 4.5 Doors and glass [DATA]

- **Door_garage**: exterior roll-up door actor (§1.1).
  - It must be placed at its entity transform; OpenTDU2 currently draws it at the house origin.
  - `M_OPEN_gate.sce` / `M_CLOSE_gate.sce` (C_OPEN / C_CLOSE) key its Position / Rotation / Scale / Animation /
    Visibility (1 key each).
- The interior exit and garage doors are FpSelectable volumes with no animated mesh; the doors are part of the
  LPH_L3DI mesh.
- Glass:
  - TD_Building `windows_03/04`, `glass_01/03/04`. States `05 06 01` (SRCALPHA / INVSRCALPHA, add) or `05 02 01`
    (windows_04: SRCALPHA / ONE).
  - REFLECTION = `env_lph_l3d` (cube); depth write state 2.
  - The TD_Building handler gives them bucket 0x21 BUILDING_ALPHA (0x22 when additive) [CODE 0xd71030].
- `TV`→`GLASS` (windows_04) is a separate node.
- Exterior glass: `Glass`/`windows` TD_Building (05 06 01) with `cubemap`.

### 4.6 Exterior interior-LODs

The exterior bank's `interior_01` and `Interior` actors (`eph_l3di_9900…`, `eph_l3di_4D59…`) are the
views-through-windows LODs.
- When the real interior is loaded (intro D_SCENE or lph_l3di), they **coincide with it** and must be hidden.
- The hiding mechanism is [ENC]; the class 'INTE' is clearly dedicated to this [INF].
- OpenTDU2 loads every `eph_l3di*` 3DD and therefore draws both.

---------------------------------------------------------------------------------------------------
## 5. Interior and exterior lighting

### 5.1 Material slots and samplers [DATA + SHADER]

TD_Building slots and samplers:

| slot | content | sampler |
|---|---|---|
| 0 | COLOR (= **lightmap** `lm_eph_l3d` / `lm_eph_l3di`) | s0 gColor |
| 1 | DETAIL (albedo) | s1 gDetail |
| 2 | GLOSS | s2 gGloss |
| 3 | REFLECTION (cube) | s3 gReflect (**CUBE**) |
| 4 | DETAIL2 | s4 gDetail2 |
| 5 | NORMAL | s5 gNormal |
| 6 | WINDOWMAP | s6 gWindowMap |
| 7 | SKYENVMAP | (no sampler) |
| 8 | NIGHTLIGHTMAP (`lm_eph_l3d_night`) | s8 gNightLightMap |

TD_Furnitures (house walls and floors `WhiteWall_01`, `Mat_Wall1_38`, `Mat_Floor1_09`, `brick_01`, `Wall_red`…): same
lightmap pair in COLOR / slot 8. Furniture objects use COLOR = baked occlusion `occ_*` and DETAIL1 = albedo.

### 5.2 NIGHTLMAP from the weather [CODE]

The NIGHTLMAP parameter (fold `9e494748544c4d41`, PS constant `nightlmap`) is **overwritten every frame from the
weather**:

| scope | functions | materials | value |
|---|---|---|---|
| exterior spot | FUN_00981b60 → FUN_00981870 collects; FUN_00981600 → FUN_00980e50 writes each frame | TD_Building with spec[37] (U6) == 1, at most 63 | `(spot_exterior_lightmap_day_night.x, .y, 0, 0)` |
| interior (house manager) | FUN_00a4d7e0 collects; FUN_00a4ee30 writes each frame | TD_Building, TD_Furnitures and d5013ed0dcacafaf, at most 64 | `spot_interior_lightmap_day_night.xy` |
| MyHouse module | FUN_0090b370 | furniture / MyHouse materials | the interior value |

Side effects:
- Sprite materials of GUID fcdaa96bdeadbeef (e.g. `T_Sprite_Diary2`) get param `0xa58487f1_97909ca3[+8] = 1 − x`.
- Nodes using GUID c913741cbe17490d (halos) get node flags |= 0x30 and node+0x8c = weather.y [CODE FUN_00980d30 /
  FUN_00980e50].

Values [DATA]:
- Intro weather: interior **(0, 1)**, exterior **(1, 0)**, `outside_street_lightmap` 0.
- The 24 game weathers vary the interior value per sub-block: mostly (0, 1), some (0.1, 1) and (0.3, 0.6). The
  exterior value is (0, 1) at night and (1, 0) by day.
- `cinematic_modifiers.xmb` contains 6 `spot_interior_lightmap_day_night` entries: resolve them through the existing
  weather/modifier lookup.

PS use (TD_Building ps_100, [SHADER]):
```
lighting = lerp(nightLM.rgb, sunLit, nightlmap.x)          // sunLit = lerp(Amb, Diff, N·L·shadow)
color    = base * lighting + spec + nightlmap.y * nightLM.rgb * nightLM.a    (emissive lamps)
```
- Interior with (0, 1) = 100 % baked lightmap plus lamp emissive; the sun reaches it only when a weather sets
  x > 0.
- Exterior in the Intro = sun only.

### 5.3 Realtime lights

Entity__Light fields (loader FUN_00def230) [CODE]:

| offset | field |
|---|---|
| +0xe0 | Rot |
| +0xf0 | Pos |
| +0x100 | Ambient |
| +0x110 | Diffuse |
| +0x120 | Specular |
| +0x130 | Intensity |
| +0x134 | DistanceDropOff |
| +0x138 | DistanceCutOff |
| +0x140 / +0x13c | AngularDropOff / AngularCutOff (read order) |
| +0x144 | isInterior (bool, default 1) |

Light data [DATA]:
- RT_Int: Diffuse (0.757, 0.678, 0.553), Ambient (0.388, 0.380, 0.380), Intensity 0.45–0.65.
- RT_Ext: Diffuse (0.659, 0.624, 0.565), Intensity 0.4.
- All lights: drop-off 10, cut-off 20, angles 0.314 / 0.873; y = 36.17.

Consumers [ENC] (not reached from readable code):
- No building or furniture PS has spot-light constants. Only 4 TD_Avatar VS take `__SpotLight_Pos_0`.
- These lights are therefore for avatars and props [INF].

### 5.4 Env cubes [DATA]

| cube | size / mips | used by |
|---|---|---|
| `env_lph_l3d` | 256², 7 mips | interior |
| `cubemap` | 64², 5 mips | exterior/pool |
| `cubemap01`, `cubecarb` | 64² | furniture |

All are static; no runtime cube is involved for houses [DATA: 2DM hashes resolve to bank files].

---------------------------------------------------------------------------------------------------
## 6. Implementation plan for OpenTDU2

**P1 — Cube 2DB** (fixes the white reflections everywhere)
1. `Texture2DB`: if `Depth == 6 && (Flags & 0x40)`, it is a cube.
2. Faces are face-major; per-face size = Σ mips.
3. Create `VK_IMAGE_VIEW_TYPE_CUBE` (6 layers, all mips).
4. Apply the X-mirror remap of §3 (swap ±X and flip all faces horizontally; DXT1 can be flipped per block by
   reversing the 2-bit index columns and the block order).
5. `pickTexture` then succeeds for s3.

**P2 — UVA**
1. Parse `*.UVA` (§1.4) when loading a bank.
2. Map each one to (material, slot) through the 2DM `UVAA`/`UVA.` id, or failing that the file name.
3. Each frame write `__UVA_k` = rows `(su,0,0,offU)`, `(0,sv,0,offV)` with `t = time mod duration` and linear keys.
4. This also animates the HOLOGRAMS pulses (Pulse_DarkBlue), the garage waterfalls and the artwork flipbook.

**P3 — Handlers**
- TD_Water: `K(0x1b)=0; bucket = M(0x31)==2 ? 0x2e : 0x19`.
- TD_WaterRiver and backwash: `M(0x31)==2 ? 0x18 : 0x17`.
- 0123210123210123: `K(0x18)&=7` plus the same 0x17/0x18 rule.
- Draw bucket 0x19 after the opaque and avatar buckets.

**P4 — GSReflection** (new `EdenPlanarReflections`, owned by the scene view)
1. Per game mode, enable it with the 0xf817e0 table: on for AVATAR_CHOICE, FIRST_WM, LIVING…; off for FREERIDE.
2. On bank registration (exterior spot bank, intro bank, interior bank):
   - TD_Water with spec[35] = 1 and TD_Furnitures with spec[38] = 1: mark the slot (4 / 6) as runtime "projective",
     with clamp-linear sampling.
   - If disabled: U4 → 0 and slot 4 empty (PS 12/13 permutations).
3. Build projectors from the bank's Entity__Actor__Mesh `D_REFLECT` scene:
   - a node without geometry is a plane, with n = world Y axis, d = n · node position (D3D space; mirror X for OpenTDU2);
   - a name containing "REFRACT" makes it a refraction projector, otherwise a mirror (`M = I − 2nnᵀ`, `t = 2dn`);
   - the following geometry nodes become that projector's object list.
4. Each frame, before the main pass:
   1. Allocate one screen-sized RGBA8 RT plus depth (shared, like renderer+0x10880). Clear it to (0,0,0,0) [INF].
   2. For each refraction projector, render its objects with the main camera (plus a clip plane n·p ≤ d if wanted;
      the REFRACT plane at house-local y = 0 clips nothing [INF]). Avatars only while the actors flag is set (only for
      cinematics `C_JACCUZZI` / `C_A4`).
   3. Mirrors: render the mirror camera into the same RT. The original uses at most 2 "actor" layers and skips actor
      layers for EPH/ECAS banks.
   4. Bind the RT to slot 4 (TD_Water) and slot 6 (TD_Furnitures U7: `Mat_Floor1_09`, `metal_simple_01` in the intro
      interior).
5. Keep drawing the D_REFLECT objects (POOL / bassin) in the main pass too.
6. Reflection slot 3 of TD_Water: keep the bank cube (deviation from the original SKYSPHEREMAP-on-cube; §2.2).
   Optional flag: black.

**P5 — Weather to material parameters**
1. Each frame, overwrite NIGHTLMAP = (x, y, 0, 0):
   - exterior spot TD_Building with spec[37] = 1 ← `spot_exterior_lightmap_day_night`;
   - interior / intro-interior TD_Building, TD_Furnitures and d5013ed0dcacafaf ← `spot_interior_lightmap_day_night`.
2. Sprite materials of GUID fcdaa96bdeadbeef: param `[+8] = 1 − x`.
3. Halo nodes (GUID c913741cbe17490d): intensity = y.

**P6 — Intro placement** (`intro_cinematic.cpp`)
1. Load the exterior by entity:
   - Scenery → identity;
   - Entity__Actor (Door_garage, CF_Key) → entity TRS;
   - Entity__Actor__Mesh D_REFLECT → identity plus a projector;
   - **skip Entity__Actor__Interior** (`interior_01`, `Interior`) while an interior is present.
   Do this instead of "all eph_l3di* 3DD".
2. Load new_intro's **Entity__Actor__Mesh** (`D_REFLECT`, lph_l3di_59E1).
3. Actor transform = `sceneOrigin · entityTRS · rootLocal⁻¹ · nodeWorld` (fixes the 14 furniture pieces).
4. Decide on HOLOGRAMS (LIVING-mode UI): hide by default [INF].

**P7 — Later LIVING / FIRST_WM**
1. Load `interior/lph_l3di.bnk` in the house frame.
2. Place MyHouseObject slots from `interior/myhouse/<bank>.bnk` (DBFurniture `_type` / `_bank`; colours `_mh%c_01`,
   `MAT_WALL1_` / `MAT_FLOOR1_`).
3. Use the FirstPerson start camera and the FpSelectable volumes (`Garage` → `cam_entree_garage`).
4. Load `interior/gph_l3di.bnk` / `garage.bnk` in its own frame.

Expected result for the pool after P1–P4: a turquoise refracted basin (`tilespool1_c` plus the `caustic1_d` detail
from the day lightmap), 15 % `nuancier_01` tint, a cube reflection, a sharp sun glint (pow 200 × 5), and gently
scrolling distortion (U +0.1 UV/s on both normal layers).

---------------------------------------------------------------------------------------------------
## 7. Open points

1. Refraction RT size, format and clear colour; the RT object at renderer+0x10880 [ENC].
2. What D3D9 returned for the SKYSPHEREMAP (2D) bound to TD_Water's cube sampler [hardware-dependent].
3. The mechanism that hides Entity__Actor__Interior LODs [ENC].
4. The exterior→interior bank name builder [ENC]; the rule E→L / E→G is [INF].
5. Whether the clip plane is used for refraction projectors (it is stored at projector +0x80/+0x90; the consumer is
   [ENC]).
6. Exact Entity__Light consumers [ENC].
