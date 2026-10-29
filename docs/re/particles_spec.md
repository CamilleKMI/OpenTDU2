# Eden particle systems (.prt / .bfx): ECAS fountains — spec

Tags: **[CODE]** readable x86 (>= 0x6c1000) · **[DATA]** game files · **[SHADER]** .sob bytecode ·
**[INF]** inferred (reason given) · **[ENC]** behind SecuROM, not read.

Scope: the water jets of the Casino (ECAS) garden, and every particle effect placed the same way (Entity__Fx of a
bank). Files compared [DATA]: 2 ecas.bnk fountains, 2 casino vfx_env.bnk, 4 ibiza vfx_env.bnk, 62 ibiza
worldactors.bnk (vfx_cars), 4 + 2 icashal.bnk (vfx_casino, vfx_nightclub), 13 e_hotela.bnk (vfx_hostel), 2
eph_l3a.bnk (butterflies / flies): 91 .prt.

## 1. Code: the simulation is encrypted [CODE -> ENC]

- RTTI `.?AVedFxParticles@@` (type descriptor 0x00f93368) -> vtable **0x00f48ff4**: every method is in
  0x005e4000..0x005ef000 (0x5e6e30, 0x5e7270, 0x5e7ac0, 0x5e7340 ...), i.e. below 0x6c1000 [ENC]. Same for
  edFxComposite (vtable 0x00f4911c), edFx, edFxFx, edFx_S.
- The edFx class descriptor (0x01459bb8..) is filled by FUN_00e36db0 [CODE] with callbacks 0x5e7300, 0x5e9030,
  0x5e9040, 0x4aab50, 0x5e6f90, 0x5e6e40 [ENC], object size 0x2e0, name "edFxParticles".
- edFxParticles animatable parameters (strings at 0x00f48e04..): "Gen. Size", "Gen. Speed", "Gen. Emis Per Sec",
  "Alpha", "Life Time", "Gen. Part Per Emis", "Weight", "Life Time Random", "Speed Random", "Size Random",
  "Weight Random" [DATA, exe strings; no readable xref].
- FUN_00b17540 [CODE]: links the bank's .prt files to the fx: for every Fx__Particles of the fx source, the class
  list DAT_01459bac (`+0x2c` class id, `+0x10` callback) is called with tag `0x2e505254` (".PRT"), and with
  `0x2e534e44` (".SND") for the sounds; the callbacks are [ENC].
- The module identifiers of the .prt (below) appear nowhere in the readable code or data of the exe [DATA].
- Rendering: render layers "PARTICLES", "PARTICLES_BLIT", "PARTICLES_COCKPIT" (layer table built at 0x00d74e11) and
  "FxUpscale" / TD_ParticlesBlit (FUN_00a9c610) [CODE]; the vertex building is [ENC].

So everything below about **behaviour** is [INF] from the data; the layout and values are [DATA]; the vertex
format and the quad construction are [SHADER].

## 2. Placement chain [DATA]

entities.xmb (ecas.bnk):

| object | ECAS content |
|---|---|
| `Entity__Fx` { orient_pos_scale, Fx, AutoStart } | 6 entities `V_Fx_Fountain_Big` (27.42, 1.64, 128.69), `_Big_01` (-26.37, 1.64, 128.69), `_Middle` (37.95, 1.41, 132.39), `_Middle_01` (16.25, ...), `_Middle_02` (-15.20, ...), `_Middle_03` (-36.90, ...), identity rotation, AutoStart 1 |
| `Fx__Root` { SourceData } | VFX_BigFountain(_01), VFX_MidFountain(_01.._03) |
| `Fx__SourceData` { Fx[] (.bfx 0/31), ParticlesList[] { ParticlesNode }, SoundList[] { SoundNode } } | VFX_BigFountain, VFX_MidFountain |
| `Fx__Particles` { Prt[] (.prt 0/18), PrtMat[] (.2DM 0/2, texture 0/3) } | vfx_casino_47BA2640E16DA49D (big), vfx_casino_523056C91843F2A9 (mid); texture `t_waterdrop_01_da` |
| `Fx__Sound` | s_fountain_loop_01 / _02 |

.bfx (vfx_casino_F1E3.. / 8C2B..) [DATA]: node count 6, root identity matrix, root id = the Fx__SourceData `__Hc`,
name "VFX_BigFountain"; children (duration 0x7f7fffff = FLT_MAX, 2 children at +0xb0 / +0x1b0): a " DNS" (sound)
node (S_fountain_loop_01, volume 0.708, cone 180 / 180, distances 0.25 / 10 / 20 / 40) and a particle node
"Flow_01" with an **identity transform** (+0x1b0) and the .prt system id at +0x240 ("CTRP" end tag). Every
particle node of the 17 .bfx compared has an identity transform.

World transform of an emitter: spot frame * T(entity pos) R(entity rot) * (.prt emitter matrix). The .prt emitter
matrix carries the position in the e_hotela files (entities at (0, -0.26, 0) / (-56.80, 0.54, 0), emitters at
(28.36, 0.76, 28.85) ...) and is the identity in ecas [DATA].

## 3. .prt layout [DATA]

RenderFile header (v0x1b, magic ".PRT"), one ".PRT" section, then a "MAT." section { u64 material hashcode } =
the PrtMat .2DM material (`a0947f857c908755` "No Texture_1783" for both fountains). File offsets below.

| offset | content |
|---|---|
| +0x24 f32 | 0.02 in all 91 files (simulation step [INF]) |
| +0x28 / +0x2c | name table offset / count. Entries { u64 id, "XXXXXXXX" }: [0] the system id (unique), then object type identifiers shared by all files: `849b99a086e9ece6` generator, `534552939196998e` ("....RES") sprite renderer, `49529a8a8ea690a1` ("......RI") curve set, `529a8a8a94979a8a` (".......R") curve owners, `GRAVITY` |
| +0x30 | force block offset (0x100) |
| +0x44 | max particles (300 for the fountains, 1 for the hotel water sheets, 0x8a for smoke) |
| force +0x00 | acceleration vector: **(0, -15, 0) mid, (0, -35, 0) big**; non-zero **iff** a GRAVITY name exists (76 / 76 files); -9.81 in one file (vfx_cars_ADFC...) -> m/s^2 [DATA + INF] |
| force +0x10 / +0x20 | vector range (smoke (0, 0.25, 0) .. (0, 0.5, 0), rain (0, -0.5, 0) .. (0, -1, 0), river steam (-0.55, 0, -0.35) .. (-0.45, 0, -0.25)); 0 for the fountains. Used as a per particle constant velocity [INF] |

Generator block, `g` = offset of the generator type id (0x2b8 in every file):

| offset | fountain mid / big | others (examples) | meaning |
|---|---|---|---|
| g-0xf8 | identity (+ 1e-8 noise) | smoke scale (0.1, 0.3, 0.1) + translation | emitter matrix (D3D rows) |
| g-0x68 | (0, 1, 0) | (0, 0, -1) | emission direction |
| g-0x58 | (1, 1, 1) | (0.1, 0.1, 0.3), (10, 10, 10) | emitter volume |
| g-0x28 | 0 | 0.5 | unknown |
| g-0x24 | **0.75 / 1.5** | 9 (smoke), 0.3 ... | "Gen. Size" [INF] |
| g-0x20 | 0.7 | 0.7, 0.9, 1 | "Weight" [INF] (not used, see 5) |
| g-0x14 | 0.05 | 3, 0.4 ... | "Size Random" [INF] |
| g-0x10 | 0 | 0.3, 0.1 | "Weight Random" [INF] |
| g+0x08 | **10 / 25** | 0 .. 19 | "Gen. Speed" (m/s) [INF] |
| g+0x0c | 0 | 0.1 .. 9 | "Speed Random" [INF] |
| g+0x10 | **0.0873 / 0.0349** (5 / 2 deg) | 0 | cone angle (rad) [INF] |
| g+0x14 | 0 | 0.0873, 1.22, 1.57 | cone angle, other mode [INF] |
| g+0x18 / g+0x1c | **110 / (1/110)** | 45, 18, 1 ... | "Gen. Emis Per Sec" and its period |
| g+0x20 / g+0x24 | **1.2 / 0** | 1.1 / 0.2, 50 / 0 ... | "Life Time" (s), "Life Time Random" |
| g+0x2c | 1 | 1 | "Gen. Part Per Emis" [INF] |
| g+0x40 / g+0x44 | 7 / 3 | 1, 2, 6, 7 / 0x3, 0x10003, 0x20002 | emitter shape / flags (7 = point [INF]) |
| g+0x48..0x54 | 1, 1.0, 1.0, 110 | rate x 1..12 | unknown |

Consistency check of the fountain values [INF]: 110 particles/s x 1.2 s = 132 alive < 300 max; mid jet apex
10^2 / (2 x 15) = 3.3 m, big 25^2 / 70 = 8.9 m.

Sprite renderer block, `r` = offset of its type id - 8:

| offset | fountain | smoke (4EF27963) | meaning [INF] |
|---|---|---|---|
| r+0x64 / 0x68 / 0x6c | 0 / 0 / 0 | 3.142 / 0.785 / 0.785 | rotation random, spin min / max (rad, rad/s) |
| r+0x80 / 0x84 | 0.25 / 0.25 | 0.125 / 0.125 | atlas cell size (uv) |
| r+0x90 / 0x94 | 4 / 4 | 8 / 8 | atlas columns / rows |
| r+0xa4 / 0xa8 | 16 / 16 | 64 / 64 | random start frames / frames |
| r+0xc0 | 0.0 | 20.0 | frame rate (frames / s) |

`t_waterdrop_01_da` (256^2 DXT5) is a 4 x 4 atlas of 16 water drop shapes (white RGB, shaped alpha) [DATA]: each
fountain particle shows one random cell, not animated (frame rate 0).

Curve set (c = offset of its type id): c+0x34 count, c+0x38 first curve; curves of 0x80 bytes
{ u32 flags (0x510000; 0x910000 for id 13), f32 1, u32 `0xeffe00<id>`, u32, f32 value[28] } [DATA].

| id | values | meaning [INF] |
|---|---|---|
| 1 / 2 / 3 | 0..255 | red / green / blue (absent = white) |
| 9 / 10 | 0..255, always identical pairs (10 has field +0xc 0x00cf0718) | size x / y scale (smoke: 0 -> 255 growing; butterflies 198 -> 255) |
| 12 | 0..255 | alpha (envelopes 0 -> 255 -> 0 for smoke, rain, steam, butterflies) |
| 13 | flag curve, -1..1 (butterflies oscillate) | unknown, not used |

Fountains: 9 / 10 = 0 -> 255 over the first 6 samples, 255 until sample 10, then linearly to 0 at sample 27;
12 = 186.4 constant (73 % opacity) [DATA].

## 4. Rendering [SHADER]

Material (PrtMat 2DM) [DATA]: ShaderSet TD_Particle `c4c4c0dedec0c4c4`, spec 0:1 1:1 10:1 12:1 32:1 33:1, COLOR
`t_waterdrop_01_da`, states `03 01 00 03 00 01 00 02 05 06 01` (SRCALPHA / INVSRCALPHA, no depth write in the
pipeline), PTHALP (1, 0, 0, 0), POSCOEF (0, 1, 0, 0). The shader system selects **TD_Particle vs10 / ps0** (exact
item `db57ff210aa4aee5`).

Vertex inputs (all TD_Particle vs): v0 POSITION (center, `__World` space), v1 NORMAL (x, y half size, z depth
offset), v2 TEXCOORD1 (corner 0/1: offset = v1.xy * (1 - 2 v2.xy)), v3 TEXCOORD2.x (rotation: angle -v3.x),
v4 COLOR, v5 TEXCOORD0 (uv); vs 8..11 also v6 BINORMAL.
- vs 0..7: camera facing quad, corner offset rotated by `InvView` (c29..c31).
- vs 8..11 (the fountains): quad around the axis v6 (object space): with `__CamPos_ObjSpace` (c64): side = axis x
  to-camera, up = axis tilted toward the camera by **a quarter** of the view elevation angle (acos polynomial, then
  sincos of -angle / 4), depth offset v1.z along the remaining normal; colour = v4.rgb * lerp(`__AmbLight_Ambient_0`,
  `__DirLight_Diffuse_0`, 0.75), alpha v4.a.
- ps0: `oC0 = tex(s0, v5) * colour`. ps 2 / 3 are the soft particle variants (gZpass depth in s1).

## 5. OpenTDU2 implementation (`render/eden/eden_particles.*`)

- `EdenParticleSystemDesc::parse`: the layout of 3 (objects found by their type ids, not by fixed offsets).
- `EdenParticleEmitter` [INF where not a read value]: fixed steps of +0x24 (0.02 s); emission Rate/s x PerEmission;
  spawn at the emitter point (shape 7) or in the unit volume of the emitter matrix (other shapes); direction
  uniformly in the cone (g+0x10, else g+0x14) around the emitter direction; speed / life / size = value +
  random(-1..1) x random field; per particle constant velocity in [force+0x10, force+0x20]; v += acceleration dt,
  p += (v + drift) dt; atlas start cell random in [0, start frames), + age x frame rate; angle random x rotation
  random, + spin dt. Random numbers: the game LCG (seed * 0x7ff8a3ed + 0x2aa01d31, FUN_0095b3e0 [CODE]).
  Weight / Weight Random are read but not applied (the gravity acceleration is used as is: -9.81 in one file
  suggests a plain acceleration).
- Draw: one draw per emitter (blended, deferred and sorted by the renderer at the emitter origin), particles
  sorted back to front, vertices in the frame's transient buffer (once per frame, reused by the cube / main
  passes), static quad index buffer of max particles; half size = size x curve(9 / 10) / 255 / 2 (the size is
  taken as the full width [INF]); colour = curves 1 / 2 / 3 / 12 (255 when absent); v6 = the particle motion
  direction (vs 8..11 axis [INF]).
- `EdenParticleSet::load(bank, hashcodes, entities, frames)`: the chain of section 2 (AutoStart entities), one
  emitter per spot frame. Used by `SpotExterior` (World Viewer): ecas 6 emitters, e_hotela 20, eph_l3a 3.

Open / not done:
- The real simulation (edFxParticles) is [ENC]: drag / weight, shape types other than 7, the emitter flags, the
  meaning of g-0x28 and g+0x48..0x54, curve 13 and the renderer fields r+0x00..0x60 are unknown.
- AlignedAxisMat (vs 2 / 3 / 6 / 7) is not provided (no bank seen using it).
- The low resolution particle target (PartLowResRT + FxUpscale blit, section 6.3) is not used: particles are drawn
  at full resolution into the scene.

## 6. Soft particles: the depth texture (hotel fountain sprites) [SHADER + CODE + INF]

### 6.1 Selection

The e_hotela fountain effects (`Fountains_12..24` .2DM, 13 materials, 20 emitters) have spec 0:1 1:1 10:1 12:1 **35:1**
(S_USERTYPE4) and one texture slot (COLOR) [DATA]; the shader system selects **TD_Particle vs1 / ps2** (ps3 is the
alpha tested variant). The casino `vfx_env` systems (4EF27963 / B5671C52, the FxVFX_Splash_Hydravion splash) have the same switch; the Ibiza `vfx_env`
materials (33 / 34) and the ECAS fountains do not [DATA].

### 6.2 ps 2 (exact transcription) [SHADER]

CTAB: `Var_PS { DepthAlpha }` (c9), `Var_Scene_PS { FogColor, FogConfig, Shadow_*, RenderTargetSize (c6),
StereoUvMadd (c7) }`, samplers `gBaseTexSampler` (s0), **`gZpass` (s1)**. vs1 outputs `o3 = ViewProj * p` (clip
position) and `o4 = View * p` (view position; Eden views are right handed: z < 0 in front).

```
uv    = ( (0.5 x + 0.5 w, -0.5 y + 0.5 w) + 0.5 RenderTargetSize.xy ) / w        (x, y, w: clip position)
uv    = uv * StereoUvMadd.xy + StereoUvMadd.zw
(B,A) = tex(gZpass, uv).zw
E     = floor(B * 31.875);  scene = 2^(E - 15) * (1 + (B*255 - 8E) * 0.125 + A * 0.125)
fade  = (scene + viewPos.z) * DepthAlpha                                         (= scene depth - particle depth)
oC0   = (tex(s0, uv0).rgb * colour.rgb, fade * tex.a * colour.a)
```

The decode constants (255, 31.875, 0.125, -15) are exactly the **FullscreenDepthTarget B / A encoding** of the
Z pre-pass (`postfx_spec.md` 6.2): gZpass is the screen view depth buffer. `DepthAlpha` is a material parameter
(fold `0x504c41485450868c`, shown "PTHALP"): **1.0** in every water sheet .2DM, i.e. a 1 m fade [DATA]. The alpha is not
saturated in the shader; the 8 bit target clamps it to [0, 1] before blending (D3D9 and Vulkan alike).

### 6.3 Who binds it [CODE + ENC]

- No .2DM slot: the texture is set at draw time. The edFxParticles render (vertex building, material use) is in the
  encrypted part [ENC]; no readable function writes a texture into slot 1 of a TD_Particle material.
- `FullscreenDepthTarget` is renderer+0x16588 (FUN_00d75d00, created with "MainTarget", "MainDepthBuffer",
  "HeadLightsRT"; the ZPREPASS layers render into the RT set +0x1653c that holds it) [CODE]. The readable code
  hands its texture (+0x48) to the haze and the motion blur materials (FUN_00d7a0e0: `+0x174d0 +0x448`,
  `+0x174f0 +0xcc`) [CODE].
- FxService (FUN_00a9c980): builds the `FxUpscale` material (TD_ParticlesBlit, FUN_00a9c610) and, in low
  resolution mode (+0x1194), renders the PARTICLES layer into `PartLowResRT` (FUN_00c32980: screen size >> shift)
  then blits it (FUN_00a9b910 writes the target into FxUpscale slot 0) [CODE]. The ZPREPASS DOWN target (512 x 128,
  Gaussian blurred) cannot be the gZpass source: a blurred B / A code does not decode [INF].
- So gZpass = FullscreenDepthTarget [INF, from the exact decode match].

### 6.4 What the hotel effects are [DATA]

The 13 e_hotela .prt are **single sprites**: max particles 1, Gen. Size 0.5 / 0.6 / 1.0, speed 0, rate 1/s, life
35 / 50 s, emitter matrices at the basin surface (y -0.05 / 0.75 in the bank frame) and on the bowls: small
splash / foam sprites where the jets land. The large translucent white rings and curtains around the hotel
fountains are **not particles**: they are the TD_Building material `WaterF` of the exterior scenery (UV animated
COLOR / DETAIL, `spot_exterior_spec.md` 2). The soft fade therefore only changes the contact pixels of the
sprites with the bowls and the water (before / after below).

### 6.5 OpenTDU2

- `EdenMaterial::ScreenDepthSlot`: set by `EdenParticleLibrary::getSystem` when the TD_Particle program samples s1
  and the .2DM has no slot 1; `EdenRenderer` binds the frame screen texture there (`gViewPos` of
  `EdenSceneView`, the B / A depth of the Z pre-pass, set for the blended pass where the particles are flushed),
  point clamp sampler (the code must not be filtered, as for the haze). Other passes (environment cube, ocean
  reflection) have no screen texture: the white default decodes as a very far depth (no fade).
- `RenderTargetSize` = (1 / W, 1 / H, W, H), `StereoUvMadd` = (1, 1, 0, 0), `DepthAlpha` from the .2DM.
- Half pixel: the shader adds the D3D9 half texel (0.5 / W before the divide); Vulkan pixel centres are already at
  +0.5: the lookup lands on a texel edge, the point sampler takes one of the two neighbours (at most one pixel of
  shift) [INF].

Before / after (World Viewer, E_HOTELA, `-campos 11057.2,6.5,17227.6 -camyaw 0 -campitch -45`):
`scratchpad/fx3/hotel3_before.png` / `hotel3_after.png`, zoom + diff `hotel3_zoom.png` (170 pixels change: the
sprite edges against the bowl stem fade out instead of being cut).

## 7. Island ambient effects (vfx_env.bnk) [CODE + DATA]

### 7.1 Data

`islands/<island>/fx/vfx_env.bnk` entities.xmb [DATA]:

| island | content |
|---|---|
| ibiza | 4 `Fx__Root__World { __Hc, __Name, SourceData, TriggerDistance }`: FxVFX_EnvFarmSmoke (1000), FxVFX_EnvFlamingo (1000), FxVFX_EnvFactorySmoke (1000), FxVFX_Env_RiverSteam (900); 4 Fx__SourceData (one .bfx with an identity particle node + one Fx__Particles each), no sound |
| casino | no Fx__Root__World: one Fx__Root `FxVFX_Splash_Hydravion` (2 systems), not placed by the world (seaplane splash [INF]) |
| hawai | not read in this pass (its sectors place 3 sources, below) |

Placement: **the sector `Sector-x-y-sx-sy-F.pmi`** (bank group 5 / sub 24). Same .PMI format as the other lists
(`pmi_spec.md` 1): one `MOB.` per placed source, **MOB hashcode = the Fx__SourceData __Hc**, type byte 5,
extraSize 0, the usual 32 byte records (position, scale, rotation, cell) [DATA]. Example sector-3-3-4-5: MOB
`1696c0402dbfb0bc` (VFX_EnvFarmSmoke), 1 record at local (468.5, 35.4, 20.45), yaw 4.86 rad.

Scan of every sector bank of the install (`-F.pmi` whose MOBs are all type 5) [DATA]: Ibiza FarmSmoke 21,
FactorySmoke 2, RiverSteam 8 (sector 3-2-7-0), Flamingo 0; Hawai `4a6d767aab83e9f1` 358, `ea0e3d8588c9f051` 111,
`f9a644e93df6d723` 22; Casino none. At most 27 records in one sector.

### 7.2 Code

| function | role |
|---|---|
| FUN_00c390b0 | "Particles" service ctor (vtable 0x00ef6fd0, tag 'part'): 16 sector slots of 0xd40 bytes at +0x2b0, bank object +0xd6c0 (FUN_00b35240: export types Entity, Scenery, FxWorld, FxSourceData, FxParticles, FxSound...; +0x13c load state = -1) |
| FUN_00c39050 | loads the island bank "vfx_env" of folder "FX" into +0xd6c0 (FUN_00b9f1d0) |
| FUN_00b17930 | Fx__Root__World loader: base FUN_00b17820 (SourceData -> +0x10), `TriggerDistance` -> **+0x20, default 1000.0** (DAT_00eddb38) |
| FUN_00c38f40 | sector loaded (+0x2a4 enabled): finds the sector slot (sector = DAT_01024b88 + 0x23d0 + i * 0x269c0, i < 16) and reads its group 5 / sub 24 resource (FUN_00d8aea0) |
| FUN_00c38d40 | F.pmi loader: version 2000..2100, `.PMI`; for every MOB record while count < **0x1e (30)**: entry (0x70 bytes at slot+0x10 + n*0x70) +0x00 = MOB hashcode, +0x10 matrix = FUN_00988250 (the static instance decoding: scale, rot X / Z / Y, position) then **+= (sector origin x, 0, sector origin z)** (ints sector+0x194 / +0x198), +0x50 bbox, **+0x60 = TriggerDistance^2** of FUN_00b34a40(hash) (0 when not found or when the bank state +0x13c is -1 or 7), +0x64 cell (w9 & 0x1ff), +0x68 scale x |
| FUN_00b34a40 | bank object list (+0x514, count +0x504): the root whose +0x10 object has +0x8 / +0xc = the hash (the SourceData) |
| FUN_00c38fb0 | per frame: FUN_00c38b10 for every used sector slot |
| FUN_00c38b10 | sector state +0x1a0 == 1: for every entry, d^2 = |entry translation - DAT_011958f0..f8|^2 (the camera position [INF: the view struct 0x11958e0 holds -forward at +0 and the position at +0x10, also read by FUN_00c392d0]); **TriggerDistance^2 < d^2: stop and release** the fx (vfunc +0x1c, FUN_005e64d0 [ENC]); else **start it if not running** (FUN_00a9bb30(hash) -> FUN_0048e2f0 [ENC] creation, FUN_00a9ba20, FUN_00b17ac0) and **set its matrix** (vfunc +0x40 with a copy of entry+0x10) |
| FUN_00c389d0 | sector unloaded: every running fx of the slot is stopped |

### 7.3 OpenTDU2 (`world/eden_world.*`, `render/eden/eden_particles.*`)

- `EdenWorld::loadEffectsBank` (island load): vfx_env.bnk, Fx__Root__World SourceData -> TriggerDistance.
- `EdenWorld::loadEffects` (sector load): `-F.pmi` parsed with `ParsePmi` (same decoding and sector origin as
  FUN_00c38d40), at most 30 entries, TriggerDistance^2 (0 without a root: never started).
- `EdenWorld::updateEffects(camera, dt)` (called by `EdenSceneView::tick` with the view camera): the FUN_00c38b10
  rule; a started effect gets one `EdenParticleInstance` per Fx__Particles of its source (`EdenParticleLibrary::
  getSourceSystems`), world = entry matrix * .prt emitter matrix (the .bfx particle nodes are identities [DATA]),
  empty at start [INF: the edFx start is encrypted]; stopped = destroyed. Unloaded sectors drop theirs.
- `EdenWorld::drawEffects`: blended draws in the main, environment cube and ocean reflection passes.
- Enabled by the World Viewer and FREERIDE (`setEffectsEnabled`, `-noworldfx` turns it off); the Particles
  service enable flag (+0x2a4) per game mode is not decoded [INF].

Test (World Viewer, ibiza, `-campos 22305,48,22255 -camyaw 53 -campitch -8`, 700 frames): sectors 3-3-3-5,
3-3-4-4, 3-3-4-5 place 5 effects (FactorySmoke (21364.3, 138.3, 22845.7) 1112 m away: not started; FarmSmoke
(22356.5, 35.4, 22292.5) at 75 m and 3 FarmSmoke at ~565 m: started). Before / after:
`scratchpad/fx3/wfx_before_long.png` / `wfx_after_long.png`, crop + diff `wfx_compare.png` (the farm chimney
plume). FREERIDE: wired the same way (`EdenSceneView::tick` with the chase camera); no sector with a `-F.pmi`
effect lies within the streamed radius of the story start house (36382, 5730), so nothing starts there.

Open: the Hawai sources (birds, fountain, waterfall splash) are handled by the same code but were not run (no
Hawai sector staged); a restarted effect starts empty (the game may pre-warm it: [ENC]).

