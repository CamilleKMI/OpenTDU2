# Avatar skin parameters and body-zone hiding (TDU2 PC): RE spec for OpenTDU2

Scope: (1) how the avatar skin look is built from the `.avt` configuration (tone, freckles, age, colours) and bound
to the TD_Skin2 / TD_Skinface / TD_Avatar materials; (2) how body zones are hidden under clothes, and why the intro
DJ shows a hole in his chest in OpenTDU2.

Tags: **[CODE]** readable x86 (VA >= 0x6c1000), **[DATA]** game files, **[SHADER]** .sob bytecode, **[EXP]** experiment
with a scratch build of OpenTDU2 (sources untouched), **[INF]** inference (reason given), **[ENC]** caller/callee in the
SecuROM range, not read, **[UNK]** unknown.

Artefacts:
- Decompiled code for this spec: `/tmp/claude-0/skinzones_dec1.c` (FUN_00cab7e0, cabad0, cabfa0, cac1b0, cad7c0,
  cb6860, cb6cb0, cbe1a0, cbe3a9, cab140, cace20, cb6f20, cacf90).
- Images: `/tmp/claude-0/av/skinzones/`
  - `dj_current.png` and `dj_current_crop.png`: OpenTDU2 as it is now, at intro t = 33 s (shot `c_intro_03_DJ`).
  - `dj_cull3_none.png`: the same frame with Eden cull state 3 treated as "no culling" (the fix).
  - `dj_exp_nozonehide.png`, `dj_exp_clothes_nocull.png`, `dj_exp_noshirt.png`: diagnostic runs.
  - `man_body_zones_front.png`: zone map of man_body.3dg.
  - `m_g_channels_body_head.png`: R/G/B channels of m_body_g and m_head_g.
- Tools: `/tmp/claude-0/py/meshrender.py` (bind-pose or CPU-skinned z-buffer render of body zones and clothes) and
  `/tmp/claude-0/py/dec2db.py` (DXT1/RGBA8 .2db decoder).

---------------------------------------------------------------------------------------------------
## 0. Summary

1. **There is no runtime skin texture composition.**
   - No compositing shader exists. All 118 families in `Euro/Shaders/*.sob` were enumerated, and none composes skin
     [DATA].
   - The skin is drawn directly by TD_Skin2 (body zones) and TD_Skinface (head) with the 2DM textures.
   - Freckles, age, skin tone and colours are **material constants**, written per avatar from the `.avt` values
     [CODE + SHADER]:

   | value | how it reaches the shader |
   |---|---|
   | freckle | `COEFFS.y`; the shader blends `DCLCLR` over the albedo with the freckle mask `_G.b` |
   | age | `SPECPARM.w`; weight of the wrinkle normal map `_HEAD_N2.(g,a)` (head only) |
   | skin tone | the ethnic 2DM variant: LUT texture `COLOR_WHITE` / `COLOR_ASIA` / `COLOR_BLACK`, plus `SKINCLR` and `DCLCLR` |
   | `ScatteringCoeff` | `COEFFS.z`, the LUT row |
   | `DiffWrap` | `COEFFS.x` |
   | SkinAmbiant, SkinDiffuse, SkinSpecular | the material colour block, which multiplies the lights |
   | spec | `SPECPARM = (SpecCoeff × 0.5, SpecPow, 0.1, age)` |

   The `AVT_F` / `AVT_B` layers that avatar_spec §6.3 took for a skin composite are camera layers, not a texture
   compositor (§1.6).
2. **Zone hiding** [CODE]:
   - Every zone starts visible.
   - Then, for **only** the garments in slots **top (1), bottom (2), gloves (3), shoes (4) and dress (7)**, each
     zone is ANDed with that garment's DBClothe flag (1 = keep, 0 = hide).
   - Hair, glasses, helmet, beard, accessories and belt are ignored.

   OpenTDU2 applies every worn slot except files whose name contains "NO_". For the 31 intro PNJ the result is
   identical. It differs for 4 non-intro items (§2.5).
3. **The DJ hole is not a zone bug.**
   - The DJ's hidden-zone set is the same under the game rule and the OpenTDU2 rule.
   - The hole comes from the shirt material `M_PN_T_Shirt_SoundOperator`, whose 2DM cull state (MAT+0x33) is **3**.
     OpenTDU2 maps 3 to D3DCULL_CCW, which culls the shirt's outer faces.
   - Through the culled front you see the hidden chest zones, then out through the neck or armhole of the sleeveless
     shirt to the sky.
   - Eden cull state 3 means **two-sided** [DATA + EXP, §2.6]. Mapping 3 to "no culling" removes the hole, and the
     speaker logo on the chest appears (`dj_cull3_none.png`).

---------------------------------------------------------------------------------------------------
## 1. Skin

### 1.1 Materials and textures of the skin [DATA: common_bodies.bnk 2DM]

Every BodyID (FUN_00cb43e0, avatar_spec §3.2) uses the **same** albedo, normal and gloss maps per sex. Only the LUT
and two colours change with the ethnic variant.

| material | family (GUID) | slot 0 SKIN | 1 NORMAL | 2 DETAIL | 3 LUT | 4 GlossDetailCut | 5 N2 | 6 / 7 MASK1 / MASK2 |
|---|---|---|---|---|---|---|---|---|
| 19 man / 21 woman zones (`TOP_*`, `BOTTOM_*`, `BASE`) | TD_Skin2 d5013ed0dbcdcdcd | M/W_BODY_C | M/W_BODY_N | NOISE | COLOR_WHITE (occ), COLOR_ASIA (asia), COLOR_BLACK (afrik) | M/W_BODY_G | – | – |
| OCC/ASIA/AFRIK_HEAD | TD_Skinface d5013ed0dbcddbca | M/W_HEAD_C | M/W_HEAD_N | NOISE | as above | M/W_HEAD_G | M/W_HEAD_N2 | M/W_HEAD_B1, B2 |

2DM parameters (man_body; the woman is the same with UVSCALE (40, 48)):

| parameter | value |
|---|---|
| UVSCALE | (48, 40) |
| SPECPARM | (0.3, 3, 0.7, 0) |
| COEFFS | (0.6, 0, 1, 0) |
| SKINCLR occ | (1, .863, .867) |
| SKINCLR asia | (.996, .906, .820) |
| SKINCLR afrik | (1, .776, .894) |
| DCLCLR occ | (.431, .176, .039) |
| DCLCLR asia | (.431, .302, 0) |
| DCLCLR afrik | (.278, .031, .031) |

Heads:
- SKINCLR is (.91, .28, .24), the lip/ear red.
- SPECPARM is (0.3, 3, 0.7) for occ, (0.35, 4, 0) for asia and (0.25, 4, 0.3) for afrik.
- COEFF1 and COEFF2 are (0, 0, 0, 1).

Texture contents [DATA, decoded with `dec2db.py`]:

| texture | format | content |
|---|---|---|
| `*_C` | DXT1 1024² (head 512²) | albedo |
| `*_G` | DXT1 | R = specular mask; G = "SSS" mask (lips, nipples, ears; tints the light toward SKINCLR and removes detail); **B = freckle speckle mask** (shoulders, chest and back on the body; nose and cheeks on the head); see `m_g_channels_body_head.png` |
| `COLOR_*` | RGBA8 256×4, 4176 bytes | spectral-scattering LUT; u = wrapped N·L × shadow, v = row (COEFFS.z) |
| NOISE | 256² | detail normal, tiled by UVSCALE |
| `*_HEAD_N2` | – | xy channels (R, B) = expression normal, weighted by MASK1·COEFF1 + MASK2·COEFF2 (FaceFX); zw channels (G, A) = age wrinkle normal, weighted by SPECPARM.w |

### 1.2 Shader formulas [SHADER: TD_Skin2 ps_4 / TD_Skinface ps_4; other permutations add shadow-slice variants]

Constants:
- c9 = `__AmbLight_Ambient_0`, c10 = `__DirLight_Diffuse_0`, c11 = `__DirLight_Specular_0`.
- c12 = specParm, c13 = coeffs, c14 = skinclr, c15 = dclclr.
- Skinface adds c16 = facialAnimCoef1 and c17 = facialAnimCoef2.

Samplers: s0 SKIN, s1 NORMAL, s2 DETAIL, s3 LUT, s4 G, s5 N2, s6 MASK1, s7 MASK2, s15 shadow.

```
G   = tex(s4, uv0)
Nd  = (tex(s2, uv1).wy - 0.5, -1) * (1 - G.g)                  detail normal (uv1 = uv0 * UVSCALE, VS)
N   = normalize(normalize(2*tex(s1,uv0).wy - 1 ...) + Nd)       body (TD_Skin2)
      head: N += specParm.w * nrm(2*N2.(g,a) - 1)               AGE wrinkles
              + (dot(MASK1,c16) + dot(MASK2,c17)) * nrm(2*N2.(r,b) - 1)     expressions
wrap = (dot(N,L) + coeffs.x) / (1 + coeffs.x)
      + dot(V,-L) * specParm.z * (1 - dot(N,V))                 back-light / rim term
s    = lerp(1, shadowRT.x, v2.w)                                real-time shadow factor
k    = v4 + v3.w * (1 - v4)                                     per-vertex light scales (VS, CARINTEG/ATTEFACT)
lut  = tex(s3, float2(wrap * s, coeffs.z))                      SKIN TONE: LUT row = COEFFS.z
lum(c) = dot(c, (.299,.587,.114));  T(c) = lerp(c, lum(c)*skinclr.rgb, G.g)
L    = lerp( T(k.x*c9) * lut, T(k.y*c10) * lut, wrap * s )      ambient -> sun
spec = pow(max(s * dot(V, reflect(-L,N)), 1e-4), specParm.y) * T(k.z*c11) * G.r * specParm.x
alb  = lerp(tex(s0,uv0).rgb, dclclr.rgb, G.b * coeffs.y)       FRECKLES
out.rgb = (alb * L + spec) * (1 + exposure(shadowRT.yzw)) ; out.a = 1
```

So:
- **freckles** = COEFFS.y × G.b toward DCLCLR (body and head);
- **age** = SPECPARM.w (head only; TD_Skin2 never reads c12.w);
- **tone** = LUT texture × row COEFFS.z, plus SKINCLR in the G.g areas.

### 1.3 Per-avatar values written into the skin materials [CODE]

Avatar block filled by **FUN_00cace20** (called by FUN_00cb6f20(avatar, cfg), which is called by FUN_00b8b5xx at
b8b694 and FUN_00ac18d0 at ac1917, right after the BodyID setup FUN_00cb4970 and the SkinID setup FUN_00cb6f80):

| avatar offset | content |
|---|---|
| +0x2620 | SkinAmbiant RGB |
| +0x2630 | SkinDiffuse RGB |
| +0x2640 | SkinSpecular RGB |
| +0x2650 | (SpecCoeff, SpecPow, RIMCoeff) |
| +0x2660 | (DiffWrap, ScatteringCoeff) |
| +0x26a0..+0x26e4 | a copy of the same block |

FUN_00cace20 then calls FUN_00448be0 [ENC].

Material callback **FUN_00cbe2a0** (EAX = MAT, EDI = the +0x2620 block):
- It runs only for GUID d5013ed0dbcdcdcd (TD_Skin2) or d5013ed0dbcddbca (TD_Skinface).
- Its EDI offsets match the +0x2620 block exactly. Its caller is [ENC], so "applied through FUN_00448be0" is [INF].

```
MAT+0xb0 = (SkinAmbiant.rgb, 1)        material ambient  -> multiplies __AmbLight_Ambient_0
MAT+0xc0 = (SkinDiffuse.rgb, 1)        material diffuse  -> multiplies __DirLight_Diffuse_0
MAT+0xd0 = (SkinSpecular.rgb, 1)       material specular -> multiplies __DirLight_Specular_0
SPECPARM.x = SpecCoeff * 0.5           (double 0.5 @0xf49748)
SPECPARM.y = SpecPow
SPECPARM.z = 0.1                       (float @0xede03c; RIMCoeff is NOT used here)
COEFFS.x   = DiffWrap
COEFFS.z   = ScatteringCoeff           (COEFFS.y and SPECPARM.w untouched)
```

Single-value callbacks:
- They have no readable caller; they are reached by E8 calls from the encrypted section [ENC]. Each one tests for
  TD_Skin2 or TD_Skinface.
- **FUN_00cbe1a0(float *v)**: `COEFFS.y = *v` [CODE]. This is **freckle** [INF: COEFFS.y is the freckle-mask weight
  in both shaders, and freckle is the only remaining `.avt` skin float].
- **FUN_00cbe220(float *v)**: `SPECPARM.w = *v` [CODE]. This is **age** [INF: SPECPARM.w is the wrinkle-normal weight
  in TD_Skinface].

Unused or irrelevant paths:
- **FUN_00cb6cb0**: the same block applied to **clothes-slot models** (avatar+0x1188 + i·0x1c0, 11 slots), with
  SPECPARM.xyz = raw (SpecCoeff, SpecPow, RIM) and COEFFS.xy = (DiffWrap, Scattering). No clothes bank contains a
  TD_Skin2 or TD_Skinface material (byte search of all 677 avatar banks on the Mac) [DATA], so this path never
  applies.
- FUN_00cab140 (GUID d5013ed0db79baba, COEFF1/COEFF2/SPECPARM/BASECLR): no `.sob` family has that GUID, so it is dead
  for this data [DATA].

### 1.4 `.avt` fields (corrected) [CODE FUN_00cbd030 field-name strings + store offsets; DATA keys]

| file key | name | AvatarConfiguration offset | type |
|---|---|---|---|
| cd26 | BodyID | +0x00 | u16 |
| 01f9 | SkinID | +0x02 | u16 |
| 6cb0 | SkinAmbiant_R/G/B | +0x10 | f32×3 |
| 1a6c | SkinDiffuse_R/G/B | +0x20 | f32×3 |
| e173 | SkinSpecular_R/G/B | +0x30 | f32×3 |
| cbfd | SpecCoeff, SpecPow, RIMCoeff | +0x40, +0x44, +0x48 | f32 |
| e9ce | DiffWrap, ScatteringCoeff | +0x50, +0x54 | f32 |
| 82b9 | EyeID | +0x58 | u16 |
| c881 | EyebrowsID | +0x5a | u16 |
| c643 | EyebrowsColor_R/G/B(/A) | **+0x5c..+0x5f, u8** (the file holds floats 0..1) | |
| d448 | ClotheIDArray / ColorIDArray | +0x60 / +0x76 (u16×11), +0x8c / +0x97 (u8×11) | |
| 9d93 | FaceParamArray | +0xa2, 49 × u8 | |
| 8d6d | age | +0xd4 | f32 |
| 3f89 | freckle | +0xd8 | f32 |
| 5182 | ClotheSetType | +0xdc | u8 |

The keys are bound to names by read order, which is confirmed by the values (cbfd = (0.3, 3, 0.7), and so on).

Intro values [DATA, `py/avt.py`; age/freckle as float]:

Only the values that differ from age 0 and freckle 0 are listed:

| avt (PNJ) | ScatteringCoeff | age | freckle |
|---|---|---|---|
| AVT_W_Random03 (Dance_05_WOMAN_01) | 0.4 | 0 | **0.48** |
| AVT_W_Random07 (Dance_Transat_02_WOMAN) | 0.85 | 0 | **0.36** |
| AVT_Misc_W01 (Dance_Walk_03_WOMAN) | 0.88 | 0 | **0.28** |
| AVT_Misc_M02 (Dance_Sit_MAN) | 1.0 | **0.5** | **0.52** |
| **AVT_M_Random03 (DJ, Dance_Walk_01_MAN)** | 0.9 | 0 | **1.0** |
| AVT_Misc_M01 (Dance_Dj_MAN) | 1.0 | **0.637** | 0 |
| STO_Stuart (Dance_04_MAN_03) | 1.0 | **0.4** | 0 |
| AVT_M_Random01 (Dance_04_MAN_04) | 1.0 | 0.025 | 0 |
| AVT_M_Random02 (Dance_Walk_03_MAN) | 0.86 | 0.037 | 0 |

ScatteringCoeff of the others:

| ScatteringCoeff | avt |
|---|---|
| 0.85 | W_Random01, 05, 09; STO_Tess; SPT_Todd; AVT_Male |
| 0.512 | W_Random02 |
| 1.0 | W_Random04, 06; AVT_Female, Female_Afrik, Female_Asia; Misc_W03; M_Random04; Male_Afrik; Male_Asia |
| 0.6 | W_Random08; STO_Miami |
| 0.7 | Misc_W02 |
| 0.8 | Misc_W04; SPT_Jude |

Other differences:
- RIMCoeff is 0.3 on W_Random09, SPT_Todd, SPT_Jude, STO_Stuart and Misc_M02.
- SpecPow is 4 on Male_Asia.
- SkinAmbiant is (0.95, 1, 1) on Misc_W03.

All SkinAmbiant, SkinDiffuse and SkinSpecular values are (1, 1, 1) except AVT_Misc_W03. DiffWrap = 0.6 everywhere.
SpecCoeff = 0.3 everywhere, so in-game SPECPARM.x = **0.15**, not the 2DM's 0.3, and SPECPARM.z = **0.1**, not 0.7.

### 1.5 TD_Avatar eyebrows [CODE FUN_00cb1830]

This applies to a TD_Avatar material (GUID d5013ed0db79dadb) whose hashcode equals `EYEBROW` (id64 written at
cb82d1: 0x42455945 / 0x574f52):

- render states: MAT+0x24 |= 2, MAT+0x44 = MAT+0x45 = 2, MAT+0x46 = 1;
- `UVOFF` = 4 floats taken from an atlas entry, at entry+0x10..+0x1c. This is the EyebrowsID atlas; the table is
  filled by [ENC] code [INF];
- `COLOR` = (EyebrowsColor u8 R, G, B, A) × (1/255) (float 0.0039215 @0xf0c538; the scaling call 0x429870 is [ENC]).

Use the `.avt` c643 floats directly as COLOR. The UVOFF atlas stays [UNK]: keep the 2DM UVOFF.

### 1.6 Other runtime skin hooks

- **FUN_00cbe3d0** [CODE]:
  - It applies to TD_Clothe2 materials with S_USERTYPE2 set and S_USERTYPE3 clear (MAT+0x331 / +0x332) whose slot-2
    texture hash (MAT+0x148) is `"SKIN"`.
  - It replaces the hash and pointer at MAT+0x168 / +0x170 with a runtime texture, sets MAT+0x337 = 1 and calls
    FUN_008bb9f0.
  - No avatar bank contains the string "SKIN" outside common_bodies (Mac byte search) [DATA], so this is **dead for
    the shipped data**.
- **SkinID** [CODE partial]:
  - FUN_00cb6f80 looks SkinID up in a runtime list (container 0x11ba1b0, 0x20-byte entries, filled by [ENC] code).
  - FUN_00cacf90 then stores entry.u16@+4 at avatar+0x2692 and an entry pointer at +0x2610/+0x2614, before
    FUN_00448b80 [ENC].
  - The random-avatar generator FUN_00cb6685 clamps SkinID to the list size.
  - Only one `_C` albedo per sex exists in common_bodies, so SkinID cannot select a diffuse texture [DATA]. Its
    effect is [UNK]. The `.avt` colour fields of §1.4 carry the per-avatar skin values.
- **AVT_F_0x%p / AVT_B_0x%p** (FUN_00cb0d00) [CODE]:
  - These are two per-avatar layers with their own camera and depth buffer: FOV 0.6 (?), aspect 1.9, near 0.1,
    far 10 (constants 0xef2754, 0xf4b0dc, 0xede03c, 0xedd270).
  - The clear colour is 0xff7f7f7f, and the background is (0.484, 0.492, 0.520).
  - This is an offscreen view or portrait of the avatar [INF]. It is **not** a skin compositor: no compositing shader
    exists.
  - **avatar_spec §6.3 is wrong** on this point.

### 1.7 Texture addressing of the 2DM slots: the face artefacts [DATA + INF; fixed in OpenTDU2]

2DM texture slot (0x20 bytes at MAT+0x100+0x20·i): +0x00 slot type, +0x08 texture hash, **+0x18 address U,
+0x19 address V** (D3DTADDRESS numbering: 1 wrap, 2 mirror, 3 clamp), +0x1a 1. Values 1, 2 and 3 all occur
(sector-5-0-6-7 + avatar banks: (1,1) 95, (2,2) 80, (1,2) 26, (2,1) 18, (3,3) 12, (1,3) 8, (3,2) 3, (3,1) 1);
the ocean runtime material writes 1 for a world-tiled map and 3 for a projected one [CODE FUN_008be1e0,
ocean_fix_spec] [INF: D3D numbering]. Before this fix OpenTDU2 sampled every 2DM texture with linear **wrap**.

Avatar slots [DATA]: TD_Skinface / TD_Skin2 SKIN, NORMAL, LUT (COLOR_*), G, N2, MASK1/2 = (2,2) mirror, DETAIL
(NOISE, tiled by UVSCALE) = (1,1); TD_Avatar EYEBROW atlas / lashes = (2,2); hair layers A–D / Key = (2,1)
(strands tiled along v); `implant_front_n` = (1,2), `implant_casque` = (1,1).

1. **Grey / pale flat patch on one side of the face (Tess, the male candidates) = LUT addressing.**
   The skin shaders index the scattering LUT with `u = wrap·s`, `wrap = (N·L + DiffWrap)/(1 + DiffWrap) + rim`
   (§1.2), which is **negative on the side turned away from the sun**. With wrap addressing a slightly negative u
   reads the bright right end of the 256-texel LUT row: a flat, light, pinkish-grey region whose border (u = 0)
   follows the normal-map noise (hard, jagged edge). The game's mirror addressing (slot 3 = (2,2)) reads the dark
   start of the row. The region does not depend on FaceFX, shadows (checked with `-noshadows`), the facial bones
   (checked with the 42 facial bones forced to bind) nor the second OCC_HEAD primitive (checked by not drawing it:
   the patch is in the first one).
2. **"Hair on the forehead" of the male candidate 1 (AVT_Male_Intro, hair M_HR_R_Groom) = `implant_front_n`.**
   That TD_Hair shell (57 vertices, 3DD material 7) covers the forehead hairline with v ∈ [0.13, 1.30] on
   `hair_implant_front_nuque.2db` (256², DXT5, alpha = hairline). Its V is mirror: v > 1 reads the transparent
   bottom rows again. With wrap, v > 1 jumped to the opaque top of the texture: a black band with the widow's peak
   shape just above the eyebrows. Hiding the implant materials removes the band; hiding EYEBROW does not; forcing the
   facial bones to bind does not (so not FaceFX, not a bone) [EXP]. Offline renders: `implant_front_bind.png` (wrap),
   `implant_front_bind_mirrorV.png` (mirror) in `/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad/face2/`.

OpenTDU2: `MaterialTextureSlot::AddressU/V` (2dm.cpp), sampler heap `SH_LinearUV + 3u + v` (wrap / mirror /
clamp per axis, `RenderDevice::getSampler(filter, u, v)`), set per slot in `EdenRenderer::setupMaterial`
(renderer-wide; slots with 0 keep wrap; explicit overrides — shadows, ocean, post-fx, TD_backwash — are unchanged).

---------------------------------------------------------------------------------------------------
## 2. Body-zone hiding

### 2.1 Zone table [CODE FUN_00cada30 @cae0a7 / FUN_00cad7c0]

The avatar holds the zone table at +0x24c0 + 0x10·z: `{u64 id64(zone material name); u8 visible @+8}`.

The order is: BASE, BOTTOM_FEET, BOTTOM_ANKLES, BOTTOM_CALVES, BOTTOM_KNEES, BOTTOM_THIGHS, BOTTOM_PELVIS,
TOP_LOWER_ABDOMEN, TOP_ABDOMEN, TOP_LOWER_BACK, TOP_BACK, TOP_UPPER_BACK_CENTER, TOP_UPPER_BACK_SIDES,
TOP_CLEAVAGE_LARGE, TOP_CLEAVAGE, TOP_CLEAVAGE_SIDES, TOP_UPPER_CHEST, TOP_ARMS_SHOULDERS, TOP_ARMS, TOP_ARMS_FOREARMS,
TOP_ARMS_HANDS (= `kZoneMaterials` in eden_avatar.cpp).

Man versus woman meshes [DATA]:
- man_body has **19** zone materials; there is no TOP_LOWER_BACK or TOP_BACK, because the man's back is inside
  TOP_UPPER_BACK_*.
- woman_body has all 21.
- 3DD material order (man): BASE, ANKLES, CALVES, FEET, KNEES, PELVIS, THIGHS, ABDOMEN, ARMS, FOREARMS, HANDS,
  SHOULDERS, CLEAVAGE_LARGE, CLEAVAGE_SIDES, LOWER_ABDOMEN, CLEAVAGE, UPPER_BACK_CENTER, UPPER_CHEST,
  UPPER_BACK_SIDES.
- On the man, TOP_UPPER_CHEST is the small inverted-Y patch under the collar, TOP_CLEAVAGE is the whole pectoral
  plate and TOP_CLEAVAGE_LARGE is the upper belly (`man_body_zones_front.png`).

### 2.2 Garment flag record [CODE FUN_00cb6860]

When a DBClothe row is turned into a garment descriptor, the 21 DBClothe bytes are packed **in zone order** at
descriptor +0x38..+0x4c.

Runtime DBClothe row offsets, from the field deserializer at 0xdff500:

| zone | DBClothe field | row offset |
|---|---|---|
| BASE | `_base` | 0x04 |
| FEET | `_feet` | 0x28 |
| ANKLES | `_ankles` | 0x01 |
| CALVES | `_calves` | 0x10 |
| KNEES | `_knees` | 0x6e |
| THIGHS | `_thighs` | 0x9c |
| PELVIS | `_pelvis` | 0x80 |
| LOWER_ABDOMEN | `_lower_abdomen` | 0x6f |
| ABDOMEN | `_abdomen` | 0x00 |
| LOWER_BACK | `_lower_back` | 0x70 |
| BACK | `_back` | 0x03 |
| UPPER_BACK_CENTER | `_upper_back_center` | 0x9d |
| UPPER_BACK_SIDES | `_upper_back_sides` | 0x9e |
| CLEAVAGE_LARGE | `_cleavage_large` | 0x12 |
| CLEAVAGE | `_cleavage` | 0x11 |
| CLEAVAGE_SIDES | `_cleavage_sides` | 0x13 |
| UPPER_CHEST | `_upper_chest` | 0x9f |
| SHOULDERS | `_shoulders` | 0x8c |
| ARMS | `_arms` | 0x02 |
| FOREARMS | `_forearms` | 0x48 |
| HANDS | `_hands` | 0x58 |

The same function sets descriptor +0x0c = slot = FUN_00cb2f30(`_parttype` − 1).

### 2.3 Slots [CODE]

parttype → slot (jump table 0xcb2f78):

| parttype | item | slot |
|---|---|---|
| 1 | bottom | 2 |
| 2 | glasses | 5 |
| 3 | hair | 0 |
| 4 | gloves | 3 |
| 5 | helmet | 6 |
| 6 | shoes | 4 |
| 7 | top | 1 |
| 8 | dress | 7 |
| 9 | beard | 8 |
| 10 | accessories | 9 |
| 11 | belt | 10 |

The DBPNJ → config slot mapping in FUN_00cb5bd0 gives the same order.

Slot objects:
- They sit at avatar+0x1180 + 0x1c0·slot (vtable 0xef3738).
- vfunc +0x0c (FUN_00cb1af0) returns the garment descriptor: slot+0x4c, else +0x48, else +0x04 (pending, loading or
  current).

### 2.4 Rule [CODE FUN_00cac1b0, FUN_00cab7e0, FUN_00cabad0]

```
FUN_00cab7e0: visible[z] = 1 for all 21 zones
for slot in (1 top, 2 bottom, 7 dress, 4 shoes, 3 gloves):          // only these five
    d = slot.vfunc0c();  if (d) for z: if (visible[z]) visible[z] = d[0x38+z]
for z: FUN_00448a90(body = avatar+0x88, &zone[z])                    // [ENC] applies to the body material id64
```

- FUN_00cabfa0 is a second entry point that copies a full 21-byte array (used by an override path).
- FUN_00cac1b0 is re-run on clothes changes (callers at cab66a, cab6bb, caeca4).
- Hair, glasses, helmet, beard, accessories and belt never hide skin.
- The head model is never touched.

### 2.5 Comparison with eden_avatar.cpp

The current rule takes all 10 DBPNJ slots, skips files containing "NO_", and hides z if any garment has 0. The
semantics are the same (an AND over the garments), but the slot set is wrong. The DBClothe items it mishandles
[DATA, db_avatar.json]:
- M_NO_HAIR: `_feet` = 0, skipped only by the "NO_" hack;
- W_PN_H_Cap_JANE (helmet): `_calves` = 0;
- W_SW_H_Hat_02 (helmet): `_base` = 0;
- M_PN_A_Wounds_Ryan_Tyler and M_PN_A_Adam (accessories): cleavage_large, cleavage and upper_chest = 0. These would
  punch chest holes.

None of them is worn in the intro, so the intro result is identical.

**Exact fix**: only apply flags from DBClothe rows whose `_parttype` ∈ {7, 1, 8, 6, 4}, which are the DBPNJ fields
`_clothe_top`, `_clothe_bottom`, `_clothe_dress`, `_clothe_shoes` and `_clothe_gloves`, and drop the "NO_" test from
the zone loop. Every NO_ row in those slots is all 1. Keep the "NO_" test only for deciding whether to load a mesh.

### 2.6 The DJ

Config [DATA]:
- Entity `DJ` → DBPNJ `STO_Party_DJ` (hash 555) → `AVT_M_Random03.avt` (BodyID 0, man occ; freckle 1.0).
- top M_PN_T_Shirt_SoundOperator (hash 340, colour 0)
- bottom M_PN_B_Pants_SoundOperator (341)
- shoes M_PN_S_Shoes_SoundOperator (342)
- acc M_PN_A_HP_SoundOperator_01 (339)
- hair M_HR_R_HAIRLESS_02 (284, colour 2)
- beard M_HR_M_BEARD_1DAY (255)
- dress, glasses, gloves, helmet: NO_ items

Zone flags (0 = hide):

| item | base | feet | ank | calv | knee | thigh | pelv | lowAbd | abd | lowBk | back | ubC | ubS | clvL | clv | clvS | uChest | shoul | arms | fore | hands |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Shirt_SoundOperator | 1 | 1 | 1 | 1 | 1 | 1 | 1 | **0** | **0** | 1 | 1 | **0** | 1 | **0** | **0** | **0** | 1 | 1 | 1 | 1 | 1 |
| Pants_SoundOperator | **0** | 1 | **0** | **0** | **0** | **0** | **0** | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| Shoes_SoundOperator | 1 | **0** | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |

- Hidden, under both the game rule and the OpenTDU2 rule: BASE, FEET, ANKLES, CALVES, KNEES, THIGHS, PELVIS,
  LOWER_ABDOMEN, ABDOMEN, UPPER_BACK_CENTER, CLEAVAGE_LARGE, CLEAVAGE, CLEAVAGE_SIDES.
- Drawn: UPPER_BACK_SIDES, UPPER_CHEST (the collar patch), SHOULDERS, ARMS, FOREARMS, HANDS.
- The shirt is a **sleeveless** top: arms and shoulders stay visible, which is correct.

Why the chest is open in OpenTDU2:
1. The shirt mesh is closed over the chest, and its winding agrees with its normals on all 716 triangles. The body
   agrees as well. CPU skinning of `fi_dancedjm_npc` at t = 4.42 keeps the shirt outside the body
   [DATA, `meshrender.py`].
2. The shirt 2DM states are `01 01 00 03 00 00 00 02 02 01 06`, so **cull = 3**. Body zones and most clothes use 2.
3. eden_renderer.cpp (lines 622–627 and 745–748) maps 2 → VK_CULL_MODE_FRONT_BIT and 3 → VK_CULL_MODE_BACK_BIT,
   with the CW front face and a flipped viewport. Since body and shirt are wound the same way, **3 culls exactly the
   faces that 2 keeps**, so the shirt's outside is culled.
4. Looking through the culled front, you see the shirt's inner back (dark), the hidden CLEAVAGE / ABDOMEN zones,
   which show nothing, and then the sky through the neck or armhole. That is the "hole".

Experiments [EXP], scratch copy of eden_avatar.cpp, the original build untouched:

| experiment | result |
|---|---|
| zones not hidden | the body chest fills the hole; the shirt is only visible at its silhouette, i.e. its outer faces are missing |
| clothes not culled | shirt complete, speaker logo visible |
| cull 3 → none for all avatar materials | shirt complete, no other change (`dj_cull3_none.png`) |

Eden cull state 3 = **two-sided** [DATA + INF]. Every 3 in the data is on geometry that must be seen from both sides,
or where culling does not matter:

| where | materials with cull 3 |
|---|---|
| world | `Fu_Crane_B_dblS` ("dblS" = double-sided), `HT_an_awning`, `balcons`, `SF_Trash_Pal` |
| cars | glass materials (`Glass`, `Glass_SK`, `Glass_Opaque`, `Glass_LGT`) |
| flash UI | every flash material (td_flash) |
| avatars | eyelashes (`*_LASHES`), glasses / swimsuit `UNCHANGEABLE` parts, `W_PN_T_SWIMSUIT`, the sleeveless DJ shirt and its pants, all hair-palette `_mat` entries, `LOW` bodies |
| runtime materials set to 3 in readable code | the fullscreen passes LightnessCopy (922487), HL_Stencil (92252d), Gradient (96f4ef) |

The D3D render-state translation itself was not found in readable code [ENC].

---------------------------------------------------------------------------------------------------
## 3. Implementation plan for OpenTDU2

### 3.1 Cull state 3 = two-sided (renderer; this fixes the DJ hole)

In `EdenRenderer::getPipeline` (eden_renderer.cpp):

```cpp
uint8_t cullMode = bNoCull ? 1 : material.CullMode;
if ( cullMode == 3 ) cullMode = 1;                 // Eden 3 = two-sided (data: *_dblS, lashes, glass, DJ shirt)
if ( bMirrored && cullMode == 2 ) cullMode = 4;    // 4 = internal "cull the other face"
...
rasterizer.cullMode = cullMode == 2 ? FRONT_BIT : cullMode == 4 ? BACK_BIT : NONE;   (keep -edenswapcull)
```

The TD_ShadowBox special case (line 446) currently swaps 2↔3. Rewrite it to produce the internal "other face" value
(4) instead of 3, so ShadowBox keeps its current behaviour.

Shadow casters copy CullMode (line 1263); they need no change.

### 3.2 Zone rule (eden_avatar.cpp)

```cpp
static const char *kZoneSlots[] = { "top", "bottom", "dress", "shoes", "gloves" };   // slots 1,2,7,4,3
for ( const char *pSlot : kZoneSlots ) {
    XmbValue c = DBClothe row of DBPNJ["_clothe_" + slot];
    if ( !c.isValid() ) continue;
    for ( z ) if ( c[kZoneFlags[z]].asInt(1) == 0 ) zoneVisible[z] = false;   // no "NO_" test here
}
```

Keep the existing loading loop over all 10 slots. The "NO_" test there only decides whether to load a mesh. Hiding
by setting `material.pProgram = nullptr` on the body material whose `Hashcode == GetIdentifier64bit(full zone name)`
stays as it is: it skips both the colour and the Z passes.

### 3.3 Skin constants (eden_avatar.cpp, after loading body and head)

Parse the `.avt` values. Generalise `FindAvtValue` to return the raw big-endian value bytes:

| key | value |
|---|---|
| 6cb0 | amb f32×3 |
| 1a6c | dif f32×3 |
| e173 | spe f32×3 |
| cbfd | SpecCoeff, SpecPow, RIM |
| e9ce | DiffWrap, Scattering |
| 8d6d | age f32 |
| 3f89 | freckle f32 |
| c643 | eyebrow colour f32×4 |

Defaults when a key is missing (FUN_00cbd680): colours 1, age 0, freckle 0, Spec/Wrap taken from the 2DM.

Then:

```cpp
for ( part in {body, head} ) for ( EdenMaterial &m : part->getMaterials() ) {
    if ( m.ShaderSetGUID != 0xd5013ed0dbcdcdcdull && m.ShaderSetGUID != 0xd5013ed0dbcddbcaull ) continue;
    m.Ambient  = Vector4f( amb, 1 );  m.Diffuse = Vector4f( dif, 1 );  m.Specular = Vector4f( spe, 1 );
    m.Parameters[id64("SPECPARM")] = Vector4f( 0.5f * specCoeff, specPow, 0.1f, age );     // FUN_00cbe2a0 + cbe220
    Vector4f &c = m.Parameters[id64("COEFFS")]; c.x = diffWrap; c.y = freckle; c.z = scattering;  // cbe2a0 + cbe1a0
}
```

- The 2DM textures stay bound as they are (SKIN, NORMAL, NOISE, COLOR_*, *_G, N2, B1/B2). No render target or
  compositing pass is needed.
- OpenTDU2 already multiplies `__AmbLight_Ambient_0` / `__DirLight_Diffuse_0` / `__DirLight_Specular_0` by
  material.Ambient / Diffuse / Specular, which reproduces MAT+0xb0/+0xc0/+0xd0.
- COEFF1 and COEFF2 (FaceFX) stay at the 2DM values (0, 0, 0, 1).

Visible result:
- freckles on 6 intro PNJ, including the DJ (freckle 1.0: full DCLCLR in the G.b speckles);
- wrinkles on AVT_Misc_M01 (0.637), AVT_Misc_M02 (0.5) and STO_Stuart (0.4);
- per-avatar LUT rows (ScatteringCoeff 0.4–1.0);
- weaker specular and rim on all avatars (0.15 / 0.1 instead of 0.3 / 0.7).

### 3.4 Eyebrows (implemented)

For the TD_Avatar material with `Hashcode == id64("EYEBROW")`, `COLOR = EyebrowsColor` (`EdenAvatar::applySkin`).
FUN_00cbd030 stores R, G, B (+0x5c..+0x5e; +0x5f is not written there) — a u8 file value as is, any other type
through 0x5bbd30 [ENC]; the `.avt` files hold f32 (AVT_Male: (0, 0, 0, 1) = black). OpenTDU2: ×255 rounded [INF],
then ×0.0039215 (FUN_00cb1830). TD_Avatar ps 4–7 (S_USERTYPE1, the EYEBROW permutation) output alpha = tex.r: the
atlas `eyebrows_male/female.2db` is an opaque DXT1 whose red channel is the brow mask (2 × 4 cells of 256 × 64).
The 2DM UVOFF (0, 0) is kept (cell 0) until the EyebrowsID atlas table ([ENC]) is known.

### 3.5 Corrections to fold into avatar_spec.md

- §6.3: no runtime skin composite. AVT_F / AVT_B are camera layers. Replace it with §1 of this file.
- §3.2:
  - EyebrowsColor is u8×4 at +0x5c.
  - ColorIDArray sits at +0x8c / +0x97.
  - The key → name mapping is confirmed by value.
- §3.3 "Draw a body zone only if every worn garment has 1": restrict it to the top, bottom, dress, shoes and gloves
  slots (FUN_00cac1b0).

### 3.6 Still unknown

- The [ENC] callers that pass age and freckle; the mapping rests on shader semantics.
- What SkinID does: the 0x11ba1b0 list and FUN_00448b80.
- The EyebrowsID / EyeID UV atlas.
- How FUN_00448a90 hides a zone (material flag or draw skip); both look the same.
- What AVT_F / AVT_B are used for.
