# TD_Ocean (TDU2 sea) – complete rendering spec for OpenTDU2

Scope: the sea of the first 29 s of the intro (C_INTRO, weather `Intro`), but everything below is the generic
in-game ocean. Supersedes `intro_spec.md` §8 and §12.2 where they differ (corrections listed in §9).

Tags: **[CODE]** read from readable x86, **[DATA]** read from game files, **[SHADER]** from the .sob bytecode,
**[INF]** inferred (reason given), **[ENC]** behind SecuROM-encrypted code (unknown).

Files:
- `/tmp/claude-0/ocean_dec1.c`: noise normal-map pipeline (FUN_00dd65e0, dd67e0, dd68e0, dd6cf0, dd7020, dd7090,
  dd7550, NOISE_COMPOSITE/NOISE_ASSEMBLE_F creation `FUN_00d75b20`) with annotated raw asm.
- `/tmp/claude-0/ocean_dec2.c`: FUN_008be1e0 (raw asm), per-frame update FUN_00963010, constants FUN_00961710 (asm),
  mirror camera / REFLMAT FUN_00961ca0 (asm), clip plane FUN_00961bc0, views FUN_00963dc0, material push
  FUN_00982f60, colour-map source FUN_0098e450, bucket handler.
- `/tmp/claude-0/ocean_dec3.c`: matrix helpers (inverse FUN_00ddaab0, transpose FUN_00dd9d20, look-at FUN_00dd94c0),
  frustum test FUN_009620d0.
- `/tmp/claude-0/ocean_shaders_dis.txt`: all TD_Ocean / noise shaders, disassembled **with `_sat` and source
  modifiers** (the old `d3d9dis.py` drops `_sat`; the fixed copy is `/tmp/claude-0/oc/dis2.py`).

---------------------------------------------------------------------------------------------------------------------
## 0. Frame pipeline summary

```
per frame (CPU)
  FUN_00dd65e0(dt)           noise phase, NoizComp constants, NoiseNMap scaler          (§3.4)
  FUN_00963010               ocean manager update:
     h  = (DAT_00fe5028==1) ? -1.8 : 0                          water plane height     (§5.3)
     visible = FUN_009620d0  (frustum vs plane y=h)                                     (§4.6)
     if visible: FUN_00961ca0  mirror camera, clear colour, clip plane, REFLMAT          (§4)
     FUN_00961710(time)      weather -> VS/PS constant blocks                            (§2)
     FUN_00982f60 x 3 lists  copy constants + textures into every TD_Ocean material      (§2.5)
GPU, in render-layer order
  NOISE_COMP   td_noisecompose   NoiseSlice(32^3 L8)  -> NOISE_COMPOSITE 32x32 R16F       (§3)
  NOISE_ASS    td_noiseassembly  NOISE_COMPOSITE      -> NOISE_ASSEMBLE_F 256x256 R16F
  OCEAN_NMAP   TD_ocean_nmap_from_hmap NOISE_ASSEMBLE_F -> NoiseNMap 256x256 X8R8G8B8
  SEAREF_N     scene from the mirror camera            -> Ocean_RTRefl 256x256 X8R8G8B8 (+Ocean_DBRefl D24S8)
  ... main scene: TD_Ocean meshes (bucket 0x16) sample Ocean_RTRefl, NoiseNMap, level colour map
```

---------------------------------------------------------------------------------------------------------------------
## 1. Material runtime setup – FUN_008be1e0 [CODE]

Call: `FUN_008be1e0(levelObj, MAT *mat, nodeList)` with `EAX = manager` (it pushes the new parameter block into
`manager+0x00` vector and `mat` into `manager+0x10` vector; these two vectors are the "ocean material list" later
walked by FUN_00982f60). Called for every material whose GUID (+0x300) is `0x0cea0000_00000002` from:

| caller | level object | ocean meshes |
|---|---|---|
| FUN_008be540 ← FUN_008be5c0 (island manager; walks 3DD nodes `Area-%d-%d` / `Border-%d-%d`, `levelObj = [mgr+0xbd4]`) | island | `ibiza-o` |
| FUN_00985b50 / FUN_009927e3 (`[mgr+0x1bdc]`, `[mgr+0x314]`) | area / sector | `area-X-Y-o`, `sector-…-o` |

`levelObj+0x20` = **level kind**: 0 sector, 1 area, 2 island [CODE+SHADER: the kind is written into the permutation
switch MAT+0x330 and the three VS variants divide the colour UV by 768 / 6144 / 6144·AreaSize, see §1.5].

### 1.1 Parameter block (FUN_008ad580(block, 2, 0x0cea0000, desc[], n, 0))
desc[] = 16-byte entries {u64 symbol id, u8 vsFlag}; n = 4, or 5 when kind == 2:

| # | symbol | id (lo,hi) | stage | size |
|---|---|---|---|---|
| 0 | REFLMAT | 0x4c464552, 0x0054414d | VS | float4x4 |
| 1 | MATPARAM | 0x5054414d, 0x4d415241 | VS | float4 |
| 2 | MATPARAM | same | PS | float4[4] |
| 3 | SHIFTWORLD (`..IFTWOR`) | 0x46498c9f, 0x524f5754 | VS | float4 |
| 4 | AREASIZE (kind 2 only) | 0x41455241, 0x455a4953 | VS | float2 |

The 2DM already declares REFLMAT/MATPARAM(VS)/AREASIZE/MATPARAM(PS) with flag 1 = VS, 0 = PS [DATA ibiza-o.2dm].
AREASIZE (kind 2) = `(float)levelObj+0xf98, (float)levelObj+0xf9c` (uint → float), set once here. Those two ints are
copied from the globals `DAT_01024a8c/DAT_01024a90` (FUN_0098e060) = the WorldConfig object at 0x1024a50 (static
default 11 × 9 [CODE 0xaee9d1]), i.e. worldconfig.cpr `NbColumns` / `NbRows` (Ibiza 7 × 6, Hawai and Casino 11 × 9
[DATA]). The same pair gives the TD_HeightMap island AREASIZE = (16 / NbColumns, 16 / NbRows) (FUN_0098cc90).

### 1.2 Fixed material fields
| offset | value | meaning |
|---|---|---|
| +0xb0..+0xbc, +0xd0..+0xdc | 1.0 (DAT_00edf668) | D3DMATERIAL Diffuse and Specular colours = (1,1,1,1). Ambient (+0xc0) keeps the 2DM value (1,1,1,1) [DATA]. So `__AmbLight_Ambient_0 = sun_ambient`, `__DirLight_Diffuse_0 = sun_diffuse` [INF: engine multiplies light × material colour, as OpenTDU2 already does]. |
| +0x310..+0x33f (spec) | all 0, except spec[3] = 1, spec[0x20] (+0x330) = kind, +0x331 = 0, +0x31b = 0 | permutation switches |
| +0x31 | 1 | depth write ON |
| +0x33 | 2 | cull = D3DCULL_CW (same as the 2DM byte) |
| +0x38/+0x39/+0x3a | 5 / 6 / 6 | Src SRCALPHA, Dst INVSRCALPHA, BlendOp 6 = **no blending** ⇒ opaque |
| +0x40 | 0 | (z func default) |
| +0x50,+0x51,+0x52,+0x53 | 0,0,2,7 | stencil block (enable 0) [INF: stencil fields; same defaults as FUN_00d70e20] |
| +0x54,+0x58 | 0xffffffff | stencil masks [INF] |
| +0x5c | `1 << [DAT_00fd95c8+0x143c]` | stencil ref = the ocean manager's stencil bit [INF]; stencil is disabled (+0x50 = 0) so ignore |
| +0x20 (bucket) | 0x16 | from the TD_Ocean handler FUN_00d70ca0 `return 0x16` [CODE]; opaque, not in the blend buckets |

### 1.3 Texture slots (+0x100 + 0x20·i; +0xfc = 4 slots)
Slot layout: +0x00 u64 type, +0x08 u64 hash (−1/−1 = runtime texture), +0x10 texture ptr, +0x16/+0x17/+0x18/+0x19 sampler bytes.

| slot / sampler | texture | set by | +0x16 | +0x17 | +0x18 | +0x19 |
|---|---|---|---|---|---|---|
| 0 `samplerReflection` | **Ocean_RTRefl** texture (`DAT_011c95f0+0x48`) | FUN_00982f60 every frame | 2 | (0) | 3 | 3 |
| 1 `samplerNormalMap` | **NoiseNMap** texture (`DAT_011c52b0+0x48`) | FUN_00982f60 every frame | 2 | 2 | 1 | 1 |
| 2 (unused by PS) | none | – | 2 | 2 | 2 | 2 |
| 3 `samplerColorMap` | `levelObj+0x670` | FUN_008be1e0 | 2 | 2 | 2 | 2 |

**levelObj+0x670 = the level's terrain colour map** [CODE FUN_0098e450]: the loader looks up the level's
TD_HeightMap material (GUID 948a357330c13b48, slot 0 type `COLOR`) and stores its slot-0 texture in +0x670 (slot-1
`SHADOW` texture in +0x674). For the island it also builds the name `"%s-C"` from the level name (DAT_00fe5008) and
compares the hash to flag +0x330. So:

| kind | colour map | file [DATA] | 2DM COLOR hash |
|---|---|---|---|
| island | `ibiza-c` | `islands/ibiza/level/ibiza.bnk` → `maps/ibiza-c.2db` | 00432d415a494249 (`IBIZA-C` folded) |
| area | `area-X-Y-c` | `area-X-Y.bnk` → `maps/area-X-Y-c.2db` | e.g. 312d352d4145956e for area-5-1 |
| sector | `sector-X-Y-SX-SY-c` | `sector-….bnk` → `sector-…-c.2db` | e.g. 785a827c8b707680 for 5-1-7-0 |

Sampler bytes: raw values only [CODE]; the D3D mapping is not resolvable from readable code (the device wrapper is
reached through vtables). Recommended Vulkan: reflection = clamp + linear, NoiseNMap = **repeat** + linear (it is
tiled by world-space UVs ×0.015 / ×0.15 so it must wrap), colour map = clamp + linear [INF]. (2DM files use
+0x18/+0x19 = 2 for every real texture and 1 for empty slots, +0x16/+0x17 = 0.)

### 1.4 What the 2DM gives [DATA]
OCEAN material in `ibiza-o/area-X-Y-o/sector-…-o.2dm`: no textures, parameters all 0, UVAA/MTPA empty, states
`01 01 00 02 00 00 00 02 | 02 01 06` (depth on, CW cull, opaque). Everything is runtime.

### 1.5 Shader permutations [SHADER]
`TD_Ocean.sob` profile 2 (VS) has 4 binaries, profile 6 (PS) 2:

| binary | use | colour-map UV (TC3.xy) |
|---|---|---|
| vs_0 | kind 0 sector | `v0.xz / 768` |
| vs_1 | kind 1 area | `v0.xz / 6144` |
| vs_2 | kind 2 island (has `AreaSize` c80, ShiftWorld moves to c81) | `v0.xz / (768 · 8 · AreaSize.xy)` |
| vs_3 + ps_1 | z-prepass / FullscreenDepthTarget variant (writes motion RG + encoded depth BA, see postfx_spec gViewPos) | – |
| ps_0 | colour pass | – |

So the ocean also writes the screen depth buffer (haze/post work on it). Which of the 25 item ids maps to which
kind is not encoded in the dump (spec[] empty); select by kind as above [INF, by the UV divisors that exactly match
the mesh extents in §5].

---------------------------------------------------------------------------------------------------------------------
## 2. Runtime constants – exactly [CODE FUN_00961710 + FUN_00982f60, SHADER]

Weather values are fetched with `FUN_0041d8a0(id_lo, id_hi)` on the current weather block
(`DAT_0117a3a8 + DAT_0117a320·0x44`), value floats at +0x18.. . Ids (GetIdentifier64bit):

| parameter | id lo, hi | consumer |
|---|---|---|
| ocean_NMapLR | 0x93919390, 0x4d4e5f4e | FUN_00961710 |
| ocean_NMapHR | 0x938d9390, 0x4d4e5f4e | FUN_00961710 |
| ocean_specularPower | 0x8deccbeb, 0x9fa3b18f | FUN_00961710 (also d7c84d, 926xxx) |
| Skydome_sunIntensity | 0xe6fbe9f9, 0xad8aa19d | FUN_00961710 |
| ocean_fresnel | 0x86939694, 0x52465f9a | FUN_00961710 |
| ocean_specularColorAndCutOff | 0x14322434, 0xf4d9f5dd | FUN_00961710 |
| ocean_attenuationBluriness | 0xebdf1d44, 0xe8d8fce1 | FUN_00961710 |
| ocean_attenuationFresnel | 0xdbe5cef1, 0xefcf01e2 | FUN_00961710 |
| ocean_reflectionBluriness | 0xd6dfdb2a, 0xe6e6f6eb | FUN_00961710 |
| ocean_reflectionColor | 0xd3d6ded8, 0x93a1a8f4 | FUN_00961ca0 (reflection clear colour) |
| ocean_NMapScaler | 0x84989390, 0x9f93ab8f | FUN_00dd65e0 (NoiseNMap pass) |

(The same ids are also read at 0x9264ae–0x926a27, another water renderer not used by TD_Ocean.)

### 2.1 VS `matParam` (c79) — manager+0x12f0
```
matParam = ( ocean_NMapLR.x, ocean_NMapHR.x, 0, 0 )
```
VS: `TC0 = (World·p).xzxz + ShiftWorld.xzxz) * matParam.xxyy` → TC0.xy = LR UV, TC0.zw = HR UV (per world metre).

### 2.2 PS `matParam[4]` (c11..c14) — manager+0x1300
```
mp0 = ( ocean_specularPower / Skydome_sunIntensity^2 , 2*ocean_NMapLR.y , 2*ocean_NMapHR.y , 0 )
mp1 = ( F.x*F.z , (1-F.x)*F.z , F.y , 0 )              F = ocean_fresnel
mp2 = ( ocean_specularColorAndCutOff.xyz , 0 )         (.w copied then forced to 0 by FUN_00982f60; unused)
mp3 = ( ocean_attenuationBluriness , ocean_attenuationFresnel , 0.001 (DAT_00ecb0c4) , ocean_reflectionBluriness )
```
(only the `.x` of each float1 weather value is used.)

### 2.3 `ShiftWorld` (c80, c81 in vs_2)
`= DAT_01367a10` (or `DAT_01178130` when `DAT_0117810c`) = the floating origin [CODE]. `World·p + ShiftWorld`
is the absolute world position, so the normal map is continuous across origin shifts. In OpenTDU2 (no floating
origin) use `ShiftWorld = 0` and absolute world positions.

### 2.4 `reflMat` (c75..c78) — manager+0x12b0, see §4.4.

### 2.5 Distribution (FUN_00982f60) [CODE]
For every (param block, material) pair in a list: memcpy REFLMAT (64 B), VS MATPARAM (16 B), PS MATPARAM (64 B),
ShiftWorld (16 B); then PS mp0.w = 0, VS mp.w = 0, PS mp2.w = 0; then slot0 tex = Ocean_RTRefl, slot1 tex =
NoiseNMap. Lists: `DAT_01024b88+0x27b2c8` (island), 4 entries at `+0x11b0` stride 0x14c0 (state 1) and 16 entries at
`+0x1b0` stride 0x269c0 (state 1/3) [INF: area and sector caches].

### 2.6 Intro values [DATA cinematic_weathers.xmb `Intro`; no Intro_xx modifier touches ocean_* or Skydome_sunIntensity]
| constant | value |
|---|---|
| VS matParam | (0.015, 0.15, 0, 0) |
| mp0 | (50 / 0.5² = **200**, 2, 2, 0) |
| mp1 | (0.179564, 0.520436, 11.087, 0) |
| mp2 | (0.78, 0.66, 0.80, 0) |
| mp3 | (0, 0, 0.001, 0.11) |
| NoiseNMap scaler | 0.13 |
| reflection clear colour | (0.12, 0.23, 0.18) |
| Ambient c9 / Diffuse c10 | sun_ambient / sun_diffuse of the active modifier (base Intro: (0.3137,0.3921,0.549) / (2.4314,1.8196,1.1137); Intro_02…15: (0.502,0.5804,0.7686) / (2.0549,1.5216,0.9568)) |
| L (TC5 = −DirLight_Dir) | from `sundirection` (base (0.613,−0.413,0.483); Intro_xx override, e.g. Intro_02 (−0.1826,−0.3087,−0.3217)) |

### 2.7 TD_Ocean shaders, exact (with saturates) [SHADER]
VS (vs_0; c60 WVP, c64 World, c68 WorldView, c72 DirLight_Dir, c73 CamPos_obj, c74 CamDir_obj, c75 reflMat,
c79 matParam, c80 ShiftWorld):
```
p      = v0.xyz + (0,-0.01,0)  (w=1)
Wp     = World·p
TC0    = (Wp.xzxz + ShiftWorld.xzxz) * matParam.xxyy
TC1.xyz= World3x3 · (CamPos_obj - p)          (vector to eye, world)
TC1.w  = (WorldView·p).z * (-1/224)            (view depth / 224)
TC2    = reflMat · (Wp.x,Wp.y,Wp.z,1)
TC3.xy = v0.xz / 768 (vs_0) | /6144 (vs_1) | /(6144*AreaSize) (vs_2)     (object space!)
TC3.z  = dot(CamDir_obj, World row y)          (= world camera direction .y)
TC5    = -DirLight_Dir
```
PS (ps_0; c9 Ambient, c10 Diffuse, c11..c14 mp0..mp3; s0 refl, s1 nmap, s3 colour):
```
h   = sat(0.02 * TC1.y)                  // eye height above the vertex / 50 m
a   = (1 - h*h*(3-2h)) * 0.7 + TC1.w
a   = sat((a - 1.2) * 1.6666666)
A   = a*a*(3-2a) * 0.38 * pow((1 - TC3.z^2) * 1.05, 5.5)
flat= sat(2A) ;  att = sat(A)
base= mp3.y * att + mp1.x
n   = (tex(s1,TC0.xy).xyz-0.5)*mp0.y*2 + (tex(s1,TC0.zw).xyz-0.5)*mp0.z*2
n.xz *= exp(-mp3.z)
nn  = normalize(n) ;  N = normalize(nn + flat*((0,1,0) - nn))
uvR = sat( (TC2.x/TC2.w + exp(-mp3.x)*mp3.w*N.x ,  TC2.y/TC2.w) )
R   = tex(s0, uvR).xyz
C   = tex(s3, sat(TC3.xy + 0.01*N.xz)).xyz
L   = normalize(TC5) ;  light = Ambient + max(L.y,0)*(Diffuse - Ambient)
V   = normalize(TC1.xyz)
F   = sat( base + mp1.y * pow(1 - sat(dot(V,N)), mp1.z) )
col = lerp(C*light, R, F)
r   = reflect(-V, N) ;  col += pow(max(dot(r,L),0), mp0.x) * mp2.xyz
out = (col, 1)
```
Notes: reflection distortion is horizontal only (N.x); `exp` in the bytecode is exp2 applied to x·(−1/ln2).

---------------------------------------------------------------------------------------------------------------------
## 3. Noise normal-map pipeline [CODE + SHADER]

Static object `0x011c5280` (initialised by FUN_00dd67e0): +0x0c enabled, +0x0d "nmap pass enabled"
(`DAT_011c528d`, set to 1 by FUN_00963f90 when the reflection manager initialises), +0x1c NoiseSlice volume,
+0x30 NoiseNMap RT (`DAT_011c52b0`), +0x40 NoizComp quad, +0x8d0 NoizAss quad, +0x1180 NoiseNMap quad,
+0x1164 NoizComp mat, +0x116c NoizComp PS MATPARAM ptr, +0x1174 NoizAss mat, +0x1a14 NoiseNMap mat,
+0x1a1c NoiseNMap VS MATPARAM, +0x1a20 NoiseNMap PS MATPARAM, +0x1a24 phase t, +0x1a28 speed (= 1.0).
Init order FUN_00dd7550: FUN_00dd68e0 (volume) → FUN_00dd6cf0 (RT + materials) → FUN_00dd7090 (quads).

### 3.1 NoiseSlice volume (FUN_00dd68e0)
- `FUN_00e29360(32, 32, 32, fmt 0x27, flags 0xc00, "NoiseSlice")`: 32³, engine format 0x27 → D3DFMT table at
  0xf7321c (stride 8) entry 0x27 = 0x32 = **D3DFMT_L8** (1 byte/voxel; the fill writes bytes) [CODE + INF on the
  repeated table half].
- Fill: RNG state on the stack initialised to **0x42424242**; for idx = 0 … 32767 (x fastest, then y, then z):
  `buf[idx] = (uint8)trunc( FUN_005abe00(&state) * 255.0 )` (FPU control word forced to chop) [CODE].
  FUN_005abe00 is **[ENC]**. [INF] It is a float-[0,1) LCG: another caller seeds the same state with
  `seed*0x19660d + 0x3c6ef35f` (Numerical Recipes LCG), so the likely generator is
  `state = state*1664525 + 1013904223; return asfloat(0x3f800000 | state>>9) - 1.0`. The exact bit pattern cannot
  be confirmed; any uniform white noise gives the same look.
- Smoothing into the locked texture (per z slice, 2D, wrap in x and y, **no z neighbours**):
  ```
  out[z][y][x] = ( 8*in[z][y][x]
                 + in[z][y][x-1] + in[z][y][x+1]
                 + in[z][y-1][x] + in[z][y+1][x]
                 + in[z][y-1][x-1] + in[z][y-1][x+1]
                 + in[z][y+1][x-1] + in[z][y+1][x+1] ) >> 4        (indices & 31)
  ```
  (corrects §12.2 "periodic 3D kernel").

### 3.2 Render targets
| RT | size | format | created by |
|---|---|---|---|
| NOISE_COMPOSITE | 32×32 | eng 0x2d = **R16F** if `DAT_00fd97fd & 1`, else eng 0x30 = R32F | FUN_00d75b20 → renderer+0x7a4 |
| NOISE_ASSEMBLE_F | 256×256 | same as above | FUN_00d75b20 → renderer+0x7bc |
| NoiseNMap | 256×256 | eng 2 = **X8R8G8B8** (flags 0x10) | FUN_00dd6cf0 → obj+0x30 |
| Ocean_RTRefl | 256×256 | eng 2 = X8R8G8B8 | FUN_00c32810 |
| Ocean_DBRefl | 256×256 | eng 0x40 = D24S8 | FUN_00c32810 |

(engine→D3D format table at 0xf7321c [CODE]: 2 = X8R8G8B8, 3 = A8R8G8B8, 0x11/0x2d = R16F, 0x14/0x30 = R32F.)
NoizComp draws into NOISE_COMPOSITE and NoizAss into NOISE_ASSEMBLE_F [INF: NoizAss samples `*DAT_00ffb698+0x48`,
NoiseNMap samples `*DAT_00ffb6c4+0x48` (runtime pointers), and the quads' half-texel offsets are 1/64 and 1/512,
i.e. 32- and 256-wide targets]. These are the first three render layers NOISE_COMP, NOISE_ASS, OCEAN_NMAP.

### 3.3 Passes (materials FUN_00dd6cf0, quads FUN_00dd7090, draw FUN_00dd7020)
All three: MAT+0x30 = 3, +0x31 = 2 (depth write off), +0x3a = 6 (no blending), +0x35 = 1, +0x40 = 0, one texture slot; NoiseNMap also +0x33 = 3.

| pass | shader (GUID) | input (slot 0, +0x116/117/118/119) | quad pos (D3D half-texel) |
|---|---|---|---|
| NoizComp | td_noisecompose (facaa9ca_cabdbaaf) | NoiseSlice volume (1,1,2,2) | [−1/64, 63/64]², uv 0..1 |
| NoizAss | td_noiseassembly (facaa9ca_ca2dbaaf) | NOISE_COMPOSITE (2,1,1,1) | [−1/512, 511/512]², uv 0..1 |
| NoiseNMap | TD_ocean_nmap_from_hmap (0cea0000_00fe0001), cull 3 | NOISE_ASSEMBLE_F (1,1,1,1) | shift −0.5/256, uv 0..1 |

FUN_00dd7020 (per frame): draw NoizComp, draw NoizAss, clear `*DAT_00ffb6f0` (5 dwords); if +0x0d: store the
NoiseNMap RT in it and draw NoiseNMap. In Vulkan: full-screen triangles at pixel centres, no offsets.

Shaders [SHADER]:
```
NoizComp:  out = tex3D(S,(uv,mp0.x)).x*mp1.x + tex3D(S,(uv,mp0.y)).x*mp1.y + tex3D(S,(uv,mp0.z)).x*mp1.z   (all channels)
NoizAss:   acc=0; amp=1; for i in 0..3 { acc += amp*tex(C, uv*2^i).x; amp *= 0.5 }   out = acc * 0.5333333 (= 8/15)
           -> needs REPEAT sampling of NOISE_COMPOSITE
NoiseNMap: d = matParamVS.xy = (1/256, 1/256); s = matParamPS.x = ocean_NMapScaler
           hL=h(u-d,v) hR=h(u+d,v) hD=h(u,v-d) hU=h(u,v+d)
           N = normalize( -s*(hR-hL)*2d , 4d*d , -s*(hU-hD)*2d )      ( ∝ (-s*dh/du, 1, -s*dh/dv) )
           out = (N*0.5+0.5, 0)
```
Texture u ↔ world x, v ↔ world z (TC0 = world.xz·scale).

### 3.4 Per-frame NoizComp constants (FUN_00dd65e0(dt)) [CODE]
```
t += speed*dt            (speed = obj+0x1a28 = 1.0, only written by the initialiser)
if (t >= 32) t -= 32
s0 = fmod(floor(t), 32); s1 = fmod(s0+1, 32); s2 = fmod(s1+1, 32); f = fmod(t, 1)
w0 = trig(pi*(2+f)/3)^2 / 1.5 ; w1 = trig(pi*(1+f)/3)^2 / 1.5 ; w2 = trig(pi*f/3)^2 / 1.5
mp0.xyz = (s0, s1, s2) / 32      (volume z coordinates; .w untouched = 0)
mp1.xyz = (w0, w1, w2)           (.w untouched = 0)
NoiseNMap PS matParam.x = ocean_NMapScaler   (only if obj+0x0d)
```
`trig` = FUN_006c8d38 **[ENC]**; [INF] it is `sin`: with sin the weights are continuous across the slice wrap
(w0(f=1)=0, w1(1)=w2(0)…), and sin² of three angles π/3 apart sums to 3/2 so the weights sum to 1; with cos they
would jump. floor/fmod are CRT imports (0x7c7f66, 0xe2dc12). The z coordinate s/32 lies on a slice boundary, so with
linear z filtering each tap averages slices s−1 and s (reproduce by sampling the same way, volume repeat in z).
Period: 32 s.

---------------------------------------------------------------------------------------------------------------------
## 4. Reflection (Ocean_RTRefl) [CODE unless noted]

### 4.1 Objects (FUN_00963dc0, manager = param, static RT holder 0x011c95e0)
- FUN_00c32810: Ocean_RTRefl / Ocean_DBRefl (§3.2) at 0x11c95f0 / 0x11c95f4. RT sets: mgr+0x1284 = {RTRefl,
  DBRefl}, mgr+0x1298 = {RTRefl} (FUN_00961600).
- View **"SEAREF_N"** at mgr+0x880 (FUN_008b2f60 flags 0x40808010): camera mgr+0x4d0, viewport/clear block mgr+0x850,
  RT set mgr+0x1284. Inserted into the scene list DAT_013685d0.
- View **"SEAREF_2D"** at mgr+0xf90 (flags 0x40808040) on RT set mgr+0x1298 with a generic PFX Blur node
  (FUN_008b2a60) at mgr+0x1280 whose enable byte (+8) is set to **0** ⇒ no blur.
- SEAREF_N draws the material buckets of mask `(M0|M1) & 0xfd3fffe7 | 0x100000`, high dword 0x80000
  (M = 64-bit bucket masks from FUN_00d70dd0) = buckets **{0,1,2,5,6,7,8,11–21,27,29,51}**. Excluded: 3, 4, 22
  (TD_Ocean itself), 23/24 (TD_WaterRiver / backwash), 25 and every car/alpha bucket (0x2b…). Includes 5 (TD_HeightMap
  terrain), 7 (road decals), 17 (opaque, default), 18 (alpha-tested, e.g. vegetation), 21.
- Sky: the dome/cloud handlers are encrypted so their buckets are unknown **[ENC]**. [INF] the sky is not drawn
  into the reflection: the RT is cleared to `ocean_reflectionColor` every frame, which only makes sense as the
  stand-in for the sky, and matches the dark teal reflections in the reference shots.

### 4.2 Clear colour (FUN_00961ca0)
`ocean_reflectionColor` (4 floats at weather+0x18) → FUN_004ef360 [ENC, float4→D3DCOLOR] → mgr+0x860 = clear
colour of SEAREF_N. Intro: (0.12, 0.23, 0.18).

### 4.3 Mirror camera (FUN_00961ca0)
Main camera object `[DAT_00ffc088]`: world matrix rows at +0x50 (x), +0x60 (y/up), +0x70 (z), +0x80 (position);
FOV at +0x2c0.
```
h       = (DAT_00fe5028 == 1) ? -1.8 : 0         (water height; 1 = Hawaii, Ibiza → 0)
fov'    = main fov (FUN_008b39d0)
dir'    = (-m70.x, +m70.y, -m70.z)   = mirror of the forward vector (-z)      [INF on the -z convention]
up'     = (m60.x, m60.y, m60.z)      (main up, NOT mirrored)
pos'    = (m80.x, 2h - m80.y, m80.z)
World'  = FUN_00dd94c0(dir', up', pos')   (look-at basis via cross products; mgr+0x520)
View'   = inverse(World')                 (FUN_00ddba70; mgr+0x4e0)
```
A proper rotation (no handedness flip) ⇒ **no cull-mode change** needed.

### 4.4 Clip plane and REFLMAT
- FUN_00961bc0: plane `P = (0, 1, 0, -h)`; `M = View' · Proj` (Proj = global 0x1195c90 [INF: the main projection]);
  clip-space plane = `P · transpose(inverse(M))` (FUN_00ddaab0 = 4×4 Gauss-Jordan inverse, FUN_00dd9d20 =
  transpose); stored in SEAREF_N view +0x310 and enabled with view+0x350 |= 4 (user clip plane, keeps y ≥ h).
  This is a user clip plane, **not** an oblique projection.
- REFLMAT:
  ```
  T = | 0.5   0    0   0 |
      | 0   -0.5   0   0 |
      | 0     0   0.5  0 |
      | 0.5  0.5   0   1 |      (row-vector convention)
  REFLMAT = transpose( View' · Proj · T )       → mgr+0x12b0, also to the water mats' MATREFL (+0x1344/+0x1348)
  ```
  TC2 = REFLMAT·world; uv = TC2.xy/TC2.w (D3D: y down, origin top-left). In Vulkan keep the same matrix if the RT
  is rendered with a y-flipped viewport, otherwise use +0.5 for the y scale.
- The RT is square (256²) but rendered with the main projection; REFLMAT uses the same projection so the mapping is
  consistent [INF].

### 4.5 Order
SEAREF_N is a scene of the list DAT_013685d0, so it renders before the main scene of the frame [INF: render lists are
recorded and executed before the main layers]. Whether reflection rendering is enabled per frame:
`mgr+0x18 = FUN_009620d0(...)` (or forced by bit 20 of `[DAT_01195bf0+0x300]`), then FUN_00961ca0 only when non-zero.

### 4.6 FUN_009620d0
Unprojects the 8 corners NDC (±1.1, ±1.1, z) with the inverse view-projection (mgr+0x80) and tests them against the
plane y = h → reflection only when the (slightly enlarged) frustum crosses the water plane [CODE, partly: the matrix
helpers 0x424460/0x41f060 are encrypted].

---------------------------------------------------------------------------------------------------------------------
## 5. Meshes and LOD [DATA + CODE]

### 5.1 Geometry (all position-only vertices, 12 B, **y = 0 exactly**)
| file | content |
|---|---|
| `ibiza.bnk` → `ibiza-o.3dg` | one 6144² tile per area (x 0…43008, z 0…36864 = 7×6 areas), hierarchy nodes `AREA-x-y`, plus `BORDER-0/1/2` meshes: 4 big quads and rings out to (−98304 … 141312) × (−98304 … 135168) — the open sea to the horizon |
| `area-X-Y.bnk` → `area-X-Y-o.3dg` | 768² tiles (sector cells) in area-local coordinates 0…6144, only for sea sectors |
| `sector-…bnk` → `sector-…-o.3dg` | one 768² quad (6 vertices) in sector-local coordinates 0…768 |

### 5.2 Selection
- The island manager FUN_008be5c0 resolves the nodes `Area-%d-%d` (stored at mgr+0xba8[y·nx+x]) and
  `Border-%d-%d` (mgr+0xbb0…) [CODE]; the code that hides an island `Area-x-y` node when the area bank is loaded
  is not in readable code **[ENC]**. [INF] Standard 3-level swap: draw the island ocean for areas not loaded, the
  area tiles for areas loaded whose sectors are not, the sector tiles where sectors are loaded; the borders always.
  Coplanar duplicates would only differ by the colour map used, so overlapping is benign if you draw finer levels
  last with LEQUAL.
- [CODE FUN_00992890 / FUN_00992820] the area -o scene is split per sector: dummy nodes `Sector-%d-%d-%d-%d` (and,
  on odd rows, the previous area's `Sector-(x-1)-y-7-sy`) stored in a per sector table of the area; each holds that
  sector's 768 m ocean tile and its far objects (see ocean_fix_spec §3).
- First intro shot: sector tiles of 5-1-x-0/1 and 5-0-6/7-7 (see intro_spec §12.2).
- [DATA] In `ibiza-o.3dd` the `AREA-x-y` nodes are dummies (no object); each has one child (`OCEAN`, `OCEAO`, …)
  carrying the 6144² tile, so hiding an island tile means hiding the items under `AREA-x-y`. A tile left under a finer
  level z-fights with it (same plane): blocky turquoise/dark bands on TBDR GPUs (fixed, see
  /tmp/claude-0/spec_ocean_fix.md).
- Shore foam: TD_backwash `Ecume` (sector-5-0-6-7, 5-1-6-1) animated by `ecume_0.uva` (__UVA_0, 8 s loop) from the
  same sector bank [DATA, SHADER].

### 5.3 Height
Mesh y = 0 [DATA]; VS lowers it by 0.01 m; reflection plane h = 0 on Ibiza, −1.8 when DAT_00fe5028 == 1 (Hawaii,
FUN_0098e450 compares the level name with "Hawai") [CODE]. Ibiza intro: y = 0.

---------------------------------------------------------------------------------------------------------------------
## 6. Other water materials (for completeness)
FUN_00963910 / FUN_00963670 set up two other materials (ids at 0x11c97cc / 0x11c97e8, alpha-blended 5/6/1) with
symbols MATREFL, ATTENU, TRSC_0..2, ..PARAM; FUN_00961710 scrolls their TRSC offsets by `rate·DAT_00fdb8e4`
(time) and FUN_00961600 binds Ocean_RTRefl to their slot 3. They are not TD_Ocean [INF: TD_Water family].

---------------------------------------------------------------------------------------------------------------------
## 7. Intro weather (cinematic_weathers.xmb "Intro") [DATA]
| parameter | value |
|---|---|
| ocean_NMapHR | (0.15, 1) |
| ocean_NMapLR | (0.015, 1) |
| ocean_NMapScaler | 0.13 |
| ocean_fresnel | (0.25652, 11.087, 0.7) |
| ocean_reflectionColor | (0.12, 0.23, 0.18) |
| ocean_specularPower | 50 |
| Skydome_sunIntensity | 0.5 |
| ocean_reflectionBluriness | 0.11 |
| ocean_attenuationBluriness | 0 |
| ocean_attenuationFresnel | 0 |
| ocean_specularColorAndCutOff | (0.78, 0.66, 0.80, 0) |
| sun_ambient / sun_diffuse / sundirection | see §2.6 |

cinematic_modifiers.xmb: 30 modifiers with weather_name "Intro" (Intro_02…Intro_15, two each): one sets only
`sundirection`, the other sun_shadow_opacity=1, photoshop_crappymode_depth_oofdepth_blend=(0,20,1.2),
sun_ambient=(0.502,0.5804,0.7686), sun_diffuse=(2.0549,1.5216,0.9568), fog_max=0.5,
Skydome_betaMieMultiplier=0.0457, sunflare_halo_size=0.8, sunflare_halo_color=(0.89,0.455,0.231,1).
**No modifier changes an ocean_* value or Skydome_sunIntensity.**

---------------------------------------------------------------------------------------------------------------------
## 8. Reimplementation checklist (Vulkan)
1. Create NoiseSlice 32³ R8_UNORM (white noise ×255 trunc, then the 2D 3×3 [1 1 1;1 8 1;1 1 1]/16 per slice, wrap).
2. Each frame: t = (t + dt) mod 32; compute mp0/mp1 (§3.4, sin). Render NoizComp → 32² R16F (repeat sampler on the
   volume, linear), NoizAss → 256² R16F (repeat, 4 octaves ×8/15), NoiseNMap → 256² RGBA8 with s = NMapScaler.
3. Mirror camera (§4.3), user clip plane y ≥ h (gl_ClipDistance = dot(worldPos,(0,1,0,−h))), render terrain/roads/
   opaque/alpha-tested objects (no sky, no water, no cars) into a 256² RGBA8 target cleared to ocean_reflectionColor.
4. REFLMAT = transpose(View'·Proj·T) (§4.4).
5. Draw ocean meshes (§5) with TD_Ocean ps_0 / vs_k, constants §2.1–2.3, opaque, depth write, CW cull, also into the
   depth/gViewPos prepass (vs_3/ps_1).

---------------------------------------------------------------------------------------------------------------------
## 9. Corrections to intro_spec §8 / §12.2
- PS slot mapping (old table [INF]) replaced by code facts §2.2. In particular: mp0.x = specularPower / sunIntensity²
  (200 in the intro, not 50); mp0.y/z = 2·NMapLR.y / 2·NMapHR.y (LR pairs with TC0.xy); mp1 = (F.x·F.z, (1−F.x)·F.z,
  F.y); mp3 = (attenuationBluriness, attenuationFresnel, 0.001, reflectionBluriness). The old table put NMapScaler in
  mp3.w and reflectionBluriness in mp3.x; NMapScaler is only used by the NoiseNMap pass, and the reflection offset is
  exp(−attenuationBluriness)·reflectionBluriness·N.x.
- ocean_reflectionColor = clear colour of the reflection RT [CODE] (was [INF]).
- Several `_sat` modifiers were missing in the old PS listing (h, a, flat, att, uvR, colour UV, dot(V,N), F).
- NoiseSlice smoothing is 2D per slice, not 3D. NoiseSlice is L8; NoiseNMap/RTRefl are X8R8G8B8 (eng 2), not
  A8R8G8B8.
- NoizComp time animation is in readable code (FUN_00dd65e0), not [ENC] (only sin/RNG are).
- Reflection: user clip plane (not oblique projection); mirror camera keeps the unmirrored up vector.

---------------------------------------------------------------------------------------------------------------------
## 10. Summary: facts vs unknowns
**Facts [CODE/DATA/SHADER]:** material setup (states, slots, params, bucket 0x16, kind 0/1/2 ↔ sector/area/island
VS variants), colour map = level heightmap COLOR texture (`<level>-c.2db`), every ocean constant and its weather
source, ShiftWorld = floating origin, full PS/VS math with saturates, noise volume size/format/seed/smoothing, noise RT
sizes/formats, noise shaders, per-frame slice/weight animation (1 slice/s, 32 s loop), NoiseNMap scaler, reflection
RT/views/clear colour, mirror camera construction, clip plane, REFLMAT, reflection bucket mask, mesh layout (y = 0),
water height rule (0 / −1.8), Intro values (no modifier touches the ocean).

**Inferred [INF]:** sampler-byte meanings (use repeat for NoiseNMap), RNG = NR LCG float, trig = sin, sky not drawn in
the reflection, Proj of REFLMAT = main projection, NoizComp/NoizAss target assignment (strongly supported by quad
half-texel offsets), LOD swap policy (finest loaded level wins; the node tables are [CODE], their per frame use [ENC]).

**Unknown [ENC]:** exact RNG output (bit-exact noise), the D3DCOLOR conversion of the clear colour, buckets of the
dome/clouds, the per-frame show/hide of island `Area-x-y` / area / sector ocean nodes.
