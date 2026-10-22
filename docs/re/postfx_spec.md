# TDU2 post-processing ("PFX") pipeline — reverse-engineered specification

Source: `TestDrive2.exe` (readable .text 0x6c1000–0xe41000 only), shader libraries in
`/tmp/claude-0/sob/all/edrender_pfx_*/` (the per-permutation `.dxso` files are there, not in
`/tmp/claude-0/pfx/<lib>/`, which is empty). Full PFX shader disassembly + CTABs: `/tmp/claude-0/pfx_shaders_dis.txt`.
Decompiled sources: `/tmp/claude-0/pfx_dec.c … pfx_dec14.c` (see §9 for an index).

Legend: **[CODE]** = read directly from readable x86; **[SHADER]** = read from bytecode only;
**[INF]** = inference (reason given); **[ENC]** = the relevant code is in the encrypted range (< 0x6c1000) → unknown.

PE note: file offset ≠ VA−0x400000 for every section. `.text` VA 0x401000 → file 0x1000 (VA−0x400000),
`.rdata` VA 0xe41000 → file 0xa40800 (VA−0x400800), `.data` VA 0xf61000 → file 0xb60600 (VA−0x400A00).
Use `/tmp/claude-0/py/pe.py` (`PE().off(va)`).

---------------------------------------------------------------------------------------------------

## 0. Summary of the important findings

* There is **no separate glow/bloom pass in TDU2**. `fx.ini [GLOW-INGAME]` (`FXB_LUMTHRESHOLD`, `FXB_BLURNBPASS`,
  `FXB_BLURSIZE`, `FXB_MODCOEFF0/1`) and `[PAUSE] FXP_*` are parsed by `FUN_00d6b5b0`, but **every handler is an
  empty stub `ret 4`** (0xd65310, 0xd65300, 0xd652f0, 0xd652e0, 0xd652d0, 0xd652c0/b0/a0). [CODE] They have no effect.
* "Bloom" is part of the **photoshop LUT**: the final pass is
  `out = LUT( channel = scene.c , lum = luma(lerp(scene, blur(scene/4), BlurredLumFactor)) )`, and the LUT contains
  softlight + color balance + overlay + a bloom ramp + gamma.
* The final post chain on the full-screen target is: **(motion blur, disabled by default) → photoshop (LUT create + apply)**,
  executed after the render layer `WHEELS RADIAL BLUR`, before `Before2D`/`After2D` (HUD).
* The screen buffer read by scene shaders as `gViewPos` / `gTexSamplerShadowRealTime` is the RT
  **"FullscreenDepthTarget"** (W×H, A8R8G8B8): RG = screen motion vector and BA = encoded linear depth written by the scene
  shaders' ZPREPASS permutations, then **R is overwritten by the full-screen sun-shadow pass (edrender_pfx_shadow,
  blend MIN)**. See §6.
* Haze (`edrender_pfx_haze`) is drawn by the sky/dome object as full-screen triangles, constants copied from the dome's
  scattering constants + weather `Skydome_haze`, plus `fog_*`. See §5.

---------------------------------------------------------------------------------------------------

## 1. Infrastructure (needed to read the rest)

### 1.1 Render-target formats (engine enum → D3DFMT) [CODE]
Table at `.rdata 0xf73218` (pairs {engine id, D3DFMT}):

| eng | D3DFMT | eng | D3DFMT | eng | D3DFMT |
|---|---|---|---|---|---|
| 0 | R8G8B8 (20) | 8 | A2R10G10B10 (35) | 0x10 | Q16W16V16U16 (110) |
| 1 | A8R8G8B8 (21) | 9 | G16R16 (34) | 0x11 | R16F (111) |
| 2 | X8R8G8B8 (22) | 0xa | A16B16G16R16 (36) | 0x12 | G16R16F (112) |
| **3** | **A8R8G8B8 (21)** | 0xb | L8 (50) | 0x13 | A16B16G16R16F (113) |
| 4 | R5G6B5 | 0xc | L16 (81) | 0x14 | R32F |
| 5 | A1R5G5B5 | 0xd | V8U8 (60) | 0x15 | G32R32F |
| 6 | A8 | 0xe | Q8W8V8U8 (63) | 0x16 | A32B32G32R32F |
| 7 | A8R3G3B2 (29) | 0xf | V16U16 (64) | 0x17.. | DXT1..5 |

### 1.2 Main render targets (`FUN_00d75d00`, `FUN_00d75b20`) [CODE]
Created with `FUN_008b6f70(w, h, engFmt, msaa=[renderer+0x17498], flags=2, name)`; W,H = `DAT_01367cf8/cfc` (screen).

| renderer field | name | size | format |
|---|---|---|---|
| +0x1657c | `MainTarget` | W×H | eng 3 = A8R8G8B8 (MSAA = option) |
| +0x16580 | `MainDepthBuffer` | W×H | depth eng 0x40 (`FUN_008b73b0`) |
| +0x16588 | `FullscreenDepthTarget` | W×H | eng 3 = **A8R8G8B8** |
| +0x1658c | `HeadLightsRT` | W×H | `FUN_009206d0`: R16F if supported, else G16R16F, else A16B16G16R16F |
| +0x16564 | `ZPREPASS DOWN` | **512×128** | format of MainTarget |
| +0x7a4 / +0x7bc | `NOISE_*` | 32×32 / 256×256 | — |

Render-target *sets* (5 dwords: color0..3, depth) inside the renderer:

| set | color0 | depth |
|---|---|---|
| +0x16500 | MainTarget | MainDepthBuffer |
| +0x16528 | MainTarget | MainDepthBuffer |
| +0x1653c | **FullscreenDepthTarget** | MainDepthBuffer |
| +0x16550 | ZPREPASS DOWN (512×128) | none |
| +0x16568 | `[DAT_01459dc8+0x18]` (full-screen non-MSAA target / back-buffer side) | `[DAT_01459dc8+0x28]` |
| +0x163f4 | HeadLightsRT | MainDepthBuffer |
| +0x163e0 | **not written by readable code** (shared by `RT SHADOW 2D` and `HEADLIGHTS BLIT`) [INF: = FullscreenDepthTarget, see §6] |

RT objects carry their texture at `rt+0x48`; switching away from a render target resolves it into that texture
(`FUN_008a6780` → vfunc +0x20, flag 0x40000). So a pass may sample `rt+0x48` (the last resolved copy) while rendering
into the same `rt`. For Vulkan: when a pass reads the color target it writes, copy the target first.

### 1.3 Material / constant binding conventions [CODE]
* `FUN_008b82d0(matSet, "PFX_xxx", "edrender_pfx_lib", spec[48], texDesc[0x200], 0)` builds a material from a library
  item. `spec[0]` is computed = number of non-null texture slots in `texDesc` (slots at +0x18, stride 0x20).
  `FUN_008b8450` is the same with the library given by its 64-bit GUID.
* Shader constants are fetched by 64-bit *symbol ids* (the 8-char packed/hashed names shown in the library dumps, e.g.
  `0x53524156` = "VARS", `0x534d41524150` = "PARAMS"); `FUN_008b7d90` = PS constant, `FUN_008b7d30` = VS constant.
  The returned pointer is a persistent float block the code writes into.
* Material sampler slots (byte offsets in the material): slot n at `+0x110+0x20*n` = texture (`rt+0x48`),
  `+0x116+0x20n`, `+0x117+0x20n` (addressing, raw value 1 or 2), `+0x118/+0x119+0x20n` = 2 (min/mag filter;
  2 = D3DTEXF_LINEAR [INF]). Use linear filtering + clamp-to-edge in Vulkan.
* Material render-state bytes (raw; decoding by consistency of shaders/semantics, **[INF]**):
  `+0x38` = SrcBlend, `+0x39` = DestBlend (D3DBLEND numbering: 1 ZERO, 2 ONE, 5 SRCALPHA, 6 INVSRCALPHA),
  `+0x3a` = BlendOp (1 ADD, 4 MIN; **6 = "no blending"** sentinel), `+0x3b` = color-write *disable* mask
  (bit0 R, bit1 G, bit2 B, bit3 A). Values seen: photoshop materials 0x38/39/3a = 2/1/6 (opaque);
  haze-full 2/5/1 (ONE, SRCALPHA, ADD); FSShadowMat 2/2/4 (ONE, ONE, **MIN**), mask 0xe (→ R only);
  motion blur 5/6 (alpha blend).
* Draw = full-screen quad mesh `DAT_0149acac+8` via `FUN_008a4aa0`; render target via `DAT_0149acb0+8` +
  `FUN_008a6780`. Draws are **recorded into a command buffer** (`ctx+8`, opcode 0x12 = draw), executed later.
* All PFX VS take a `vars` vector: `vars.xy = (1/W, 1/H)` of the *render target* (or source, see each pass) and do
  `pos.x -= vars.x; pos.y += vars.y` = the D3D9 half-pixel correction. **In Vulkan drop this offset** and use
  `uv = pos*(0.5,-0.5)+0.5` evaluated at pixel centers – that reproduces D3D9 exactly.

### 1.4 PFX effect objects and the post graph [CODE]
Created in the scene renderer setup `FUN_00d7a0e0`:

| renderer field | ctor | class / shaders | enabled (`+8`) | attached after (dependency) |
|---|---|---|---|---|
| +0x174d4 | `FUN_008b2a60` → `FUN_008b2380` | generic Blur effect (`edrender_pfx_blur` via PFX_Blur), params: Method=1, sigma=2.5 (`0xede4cc`), passes=1 | 1 | render layer `ZPREPASS DOWNSAMPLE` (+0x2560) |
| +0x174f0 | `FUN_008b2980` → `FUN_008b15e0` | Motion blur `PFX_MotionVector` / `edrender_pfx_motionvector`, "Effect strength" @+0xec; tex `+0xcc` = FullscreenDepthTarget | **0** | render layer `WHEELS RADIAL BLUR` (+0x6bd0) |
| +0x174d0 | `FUN_008b2910` → `FUN_008afd70` | **Photoshop** (`PFX_LutCreate` + `PFX_ApplyLut`); `+0x448` = FullscreenDepthTarget tex | 1 | effect +0x174f0 |

Nodes are registered with `FUN_00421680(&DAT_013685b0, node, dependency)` **[ENC]** (graph walk itself is encrypted);
execution order is inferred from the dependencies [INF]. Each effect's render method receives `(ctx, targetRT)` where
targetRT is the color target of the layer it hangs on (for photoshop: set +0x16568 color = `[DAT_01459dc8+0x18]`).

### 1.5 Frame order (render-layer table) [CODE]
83-entry layer table built in `FUN_00d73170` (copied to renderer+0x1663c, 0x2c bytes each: view, ?, ?, RT set,
flags, name …). Names in order (RT set in brackets):

```
NOISE_COMP, NOISE_ASS, OCEAN_NMAP, DROPLETS_NEXT_IT, DROPLETS_KEPT_TEX, RT HUD MAP,
RT SHADOW NEAR CLR/NEAR, MEDIUM CLR/MEDIUM, FAR CLR/FAR, PLAYER CAR CLR/PLAYER CAR   (shadow maps)
ENVMAP_* (6 faces x {plain, NO_CARS, WITH_CARS}), SKYENVMAP, CARPAINT*, UV BORDER,
CLEAR_ZSTENCIL [+0x1653c], ZPREPASS_CAR_OPPOSANT [+0x1653c], ZPREPASS_CAR [+0x1653c],
ZPREPASS [+0x1653c = FullscreenDepthTarget+MainDepth],
ZPREPASS DOWNSAMPLE [+0x16550 = 512x128]   <- Blur effect node (+0x174d4) runs after this
ZPREPASS SKY [+0x1653c], HEADLIGHTS [+0x163f4 = HeadLightsRT],
RT SHADOW 2D [+0x163e0]      <- full-screen sun shadow (edrender_pfx_shadow), §6
HEADLIGHTS BLIT [+0x163e0],
REFLECT 2D, REFLECT FOR ACTORS 1ST/2ND LAYER,
NEAR_OPAQUE [+0x16500 MainTarget], NEAR ALPHATEST, CARPLAYER, CAROPPOSANT, CARPAINT 3D PREVIEW,
NEAR_OPAQ_CA, NEAR_CA, NEAR_COMPO, NEAR_AFTER_CA, PARTICLES_COCKPIT(_BLIT), CARPLAYER ALPHA,
CAROPPOSANT ALPHA, CARPLAYER_CA, CARCOMPO, PARTICLES(_BLIT), SUNMARKER, HELMET, HELMET CA,
HELMET COMPO, SUN_HALO, WHEEL_MASK,
NO_MSAA_SWITCH [+0x16568]    (MainTarget -> non-MSAA full-screen target)
WHEELS RADIAL BLUR [+0x16568] <- motion blur node (disabled) -> PHOTOSHOP node
Before2D [+0x16568], After2D [+0x16568]  (HUD/2D)
```

Where haze is drawn inside this list is **not determined** (the dome records haze draws into its own display lists;
§5). Everything in this table is CPU-side ordering; which layers are active per frame is decided at runtime
(partly [ENC]).

---------------------------------------------------------------------------------------------------

## 2. Photoshop effect (LUT creation + apply) — `FUN_008afd70` (ctor), `FUN_008b1220` (init), `FUN_008b0aa0` (render)

### 2.1 Tweakable parameters (object fields) and defaults [CODE]
Each tweak object is 0x30 bytes; value at `obj+0x1c`. Default = `clamp(default, min, max)`; getter by index =
vtable[5] `FUN_008afb00`.

| idx | name (debug) | value field | default | range | written from |
|---|---|---|---|---|---|
| 3 | "Opacity" (softlight) | +0xb8 | **0.55** (`.data 0xf90738`) | — | weather `photoshop_softlight_opacity` |
| 4 | "Brightness" | +0xe8 | **1.25** | — | `photoshop_softlight_brightness` |
| 5 | "Overlay Opacity" | +0x118 | **0.30** | — | `photoshop_overlay_opacity` |
| 6–8 | "Shadows R/G/B" | +0x148/+0x178/+0x1a8 | 0 | — | `photoshop_color_balance_shadows` (xyz) |
| 9–11 | "Midtones R/G/B" | +0x1d8/+0x208/+0x238 | 0 | — | `photoshop_color_balance_midtones` |
| 12–14 | "Hightones R/G/B" | +0x268/+0x298/+0x2c8 | 0 | — | `photoshop_color_balance_hightones` |
| 1 | "Lut Downscale" (int) | +0x2f8 | **0** (set to 2 then re-clamped to 0) | [0,7] | — → LUT = 256>>0 = **256×256** |
| 0 | "Blurred Lumincance Factor" | +0x3d8 | **0.6** | [0, 4] | not a weather param (re-set to 0.6 at scene setup) |
| 2 | "Gamma" | +0x408 | **0.8** | [0.4, 1.6] | brightness option, §2.6 |
| 18 | "OoF Blurred Lum" | +0x3a8 | 2.0 | [0, 6] | `photoshop_crappymode_depth_oofdepth_blend.z` |
| 16 | "Focus Depth" | +0x348 | 50 | — | `…depth_oofdepth_blend.x` |
| 17 | "Out of Focus Depth" | +0x378 | 100 | — | `…depth_oofdepth_blend.y` |
| 15 | "Fake Crappy Blur" (bool) | +0x328 | false | — | `photoshop_crappymode_enable` (`== 1.0f`) |
| 19 | "Bloom black level" | +0x468 | 0.5 | — | `photoshop_bloom_black_level` |
| 20 | "Bloom white level" | +0x498 | 1.0 | — | `photoshop_bloom_white_level` |
| 21 | "Bloom intensity" | +0x4c8 | 0.25 | — | `photoshop_bloom_intensity` |

Weather → photoshop (`FUN_00d769e0`, per weather update; values fetched by the 8-byte additive name hash of
`FUN_00af0340`: `h[i%8] += toupper(c)`) [CODE]. The setters are encrypted functions **[ENC]**:
`0x422410(softlight_opacity)`, `0x4222e0(softlight_brightness)`, `0x422260(overlay_opacity)`,
`0x422110(&shadows)`, `0x421fc0(&midtones)`, `0x421e70(&hightones)`, `0x421d20(&crappy_vec3)`,
`0x421c20(black, white)`, `0x421ba0(intensity)`; crappymode_enable is written directly
(`[ps+0x328] = (v == 1.0f)`). The 1:1 mapping above is inferred from call order + argument types + the fact that the
render code reads exactly those fields [INF, high confidence]; possible clamping inside the setters is unknown.

SS_Death_Valley values: softlight_opacity 0.25, softlight_brightness 1.5, overlay_opacity 0.6,
CB shadows (0,0,0), midtones (0.19128533, −0.24740888, 0.01698844), hightones (0,0,0),
bloom black 0.5, white 1.0, intensity 0.15, crappymode (50,100,2) enable 0.

### 2.2 Render targets of the effect (render `FUN_008b0aa0(ctx, src)`) [CODE]
`src` = color target of the hosting layer (W×H, A8R8G8B8).

| RT | size | format |
|---|---|---|
| tmp1 ("PfxTmp") | (W>>2)×(H>>2) | same as src |
| tmp2 ("PfxTmp") | (W>>2)×(H>>2) | same as src |
| LUT ("PfxTmp") | 256×256 (`0x100 >> LutDownscale`) | eng 3 = A8R8G8B8 (8-bit!) |
| blur intermediate (inside blur) | size of tmp1 | format of tmp2 |

### 2.3 Pass sequence [CODE]
1. **Downsample** `src → tmp1` (`FUN_0089eb30` → `FUN_0089ea70` → `FUN_0089e730`): RT smaller than src and not
   exactly 1× or 2× → shader pass `PFX_Sampling` (`edrender_pfx_sampling`, single item 6422fb175d04f76b, vs_0/ps_0),
   VS `vars = (1/rw, 1/rh, 0.25/rw, 0.25/rh)` (rw,rh = tmp1 size; derived as `(sw/rw)*0.25/sw`).
   Taps (uv = dst pixel center): `(u−vars.z, v), (u+vars.z, v), (u, v−vars.w), (u, v+vars.w)`, bilinear, average ×0.25.
   (For an exact ×2 or ×1 ratio a StretchRect copy with linear filter is used instead.)
2. **Gaussian blur** `tmp1 → tmp2` (`FUN_0089eb70`, method 1, sigma = 1.0) — see §3. Result: 5-tap separable
   Gaussian, normalized weights (w0, w1, w2) = (0.402620, 0.244201, 0.054489).
3. **LUT creation**: RT = LUT, material `PFX_LutCreate` (`edrender_pfx_lut_creation`, item 6422fb175d04f76b,
   vs_0/ps_0), opaque, full-screen quad. Constants: §2.4.
4. Constant update for the apply pass and the LUT creation PS constants (PARAMS, BALANCE, bloom_params) are written
   **after** step 3 was recorded but before step 5. Whether step 3 uses this frame's or last frame's values depends on
   whether the encrypted material-apply (`FUN_00522e20`) snapshots constants at record time **[ENC]**.
   For a reimplementation: use the current frame's values (difference is ≤1 frame).
5. **Apply**: RT = `src` (sampling its resolved copy), material `PFX_ApplyLut` (`edrender_pfx_apply_lut`),
   samplers: s0 `sA` = src, s1 `sB` = tmp2 (blurred quarter-res), s2 `sLut` = LUT, s3 `sD` = FullscreenDepthTarget.
   Item: `f4a7cf260ad9d328` (spec[0]=4, ps_0) normally; `25d6c5d0fd095995` (spec[0]=4, spec[32]=1, ps_1 =
   "crappy" depth-of-field variant) when Fake Crappy Blur is on (material rebuilt with `FUN_008bb9f0`, flag at
   material+0x330). vs_0 in both.

### 2.4 Constants [CODE]
LUT creation PS (`Var_PS`, c9..c14):

| CTAB name | reg | value |
|---|---|---|
| `params` (PARAMS) | c9.xyz | (Opacity=softlight_opacity, Brightness=softlight_brightness, Overlay Opacity) |
| `balance__array3[0]` | c10 | (Shadows R,G,B, 0) |
| `balance__array3[1]` | c11 | (Midtones R,G,B, 0) |
| `balance__array3[2]` | c12 | (Hightones R,G,B, 0) |
| `bloom_params` | c13.xyz | (black, 1/(white−black) [computed in double], intensity) |
| `Gamma` | c14.x | Gamma (§2.6) |

Apply VS `vars` (c60): `(1/W, 1/H, BlurredLumFactor)` with W,H = src size; vs passes `vars.z` to the PS in `v0.z`.
Apply PS_1 `depthParams` (c9): `(FocusDepth, 1/(OoFDepth − FocusDepth), OoFBlurredLum)`; before that,
`OoFDepth = max(OoFDepth, FocusDepth + 1e-5)` via the tweak setter `FUN_004223b0` (encrypted setter, args visible).

### 2.5 Shader math [SHADER, exact transcription]

**LUT texel** (LUT size N=256). VS: `u = 0.5*x+0.5`, `v = 0.5−0.5*y`, no half-pixel offset → in D3D9 the texel
(i, j) is evaluated at `u = i/N, v = j/N` (pixel centers at integer coordinates). In Vulkan compute with
`u = (gl_FragCoord.x − 0.5)/N`, `v = (gl_FragCoord.y − 0.5)/N` to be bit-faithful. `u` = input channel value,
`v` = luminance value.

```
P = params, S = balance[0], M = balance[1], H = balance[2], B = bloom_params, G = Gamma
b = P.y * v                                   // scalar
// --- color balance (GIMP-style transfer, 16 = 255/15.9375) ---
fS_neg = 1.075 - 1/(1 + 15.9375*(1-b));  fS_pos = 1.075 - 1/(1 + 15.9375*b)
C = b + S * ( S <= 0 ? fS_neg : fS_pos )                       // per channel (S.rgb), C is RGB now
C = C + M * 0.667 * (1 - ((C*255 - 127) * 0.007874015718698502)^2)
fH_neg = 1.075 - 1/(1 + 15.9375*C);      fH_pos = 1.075 - 1/(1 + 15.9375*(1-C))
C = C + H * ( H <= 0 ? fH_neg : fH_pos )
// --- soft light of the channel value u with blend layer C ---
hi = sqrt(u)*(2C-1) + 2u(1-C)
lo = 2u*C + u*u*(1-2C)
SL = (C.g - 0.5 >= 0) ? hi : lo      // NOTE: selector is C.g for all three channels (as compiled)
X  = lerp(u, SL, P.x)                 // lrp: P.x*SL + (1-P.x)*u
// --- overlay, base X, blend v (per channel selector) ---
O  = (X - 0.5 >= 0) ? 1 - 2(1-X)(1-v) : 2*v*X
Y  = lerp(X, O, P.z)
// --- bloom ramp ---
Y  = Y + B.z * (v - B.x) * B.y
// --- gamma ---
out.rgb = exp2(G * log2(Y))          // D3D9 log = log2(|x|): negative Y -> |Y|^G ; Y=0 -> 0
out.a   = 1
```
Shader immediates: c0 (2,1,−1,0), c1 (15.9375, 1, 1.075, 0.007874015718698502), c2 (255, −127, 0.667, −0.5).
Output is stored into A8R8G8B8 → saturate + 8-bit quantize (UNORM).
**Watch the abs()**: there is no clamp before `log`, and D3D9 `log` is `log2(|x|)` (DXVK emulates the same). With
SS_Death_Valley values the LUT corner (u=0, v=0) is `Y = 0.15*(0−0.5)*2 = −0.15` → stored `0.15^0.8 = 0.219`, i.e.
deep blacks are lifted. To be faithful, implement `pow(abs(Y), G)` (and `0` for `Y == 0`), not `pow(max(Y,0), G)`.

**Apply (ps_0)**:
```
A = tex(sA, uv); Bq = tex(sB, uv)                // sB = blurred quarter-res, bilinear
L = dot(lerp(A.rgb, Bq.rgb, vars.z), (0.299, 0.587, 0.114))   // lrp, vars.z = BlurredLumFactor (may be >1)
out.r = tex(sLut, (A.r, L)).r ; out.g = tex(sLut, (A.g, L)).g ; out.b = tex(sLut, (A.b, L)).b ; out.a = 1
```
(LUT coordinates are used raw, linear filtering; no half-texel adjustment.)

**Apply (ps_1, crappy DoF)**: as ps_0 but the lerp factor is
`t = BLF + ((depth − Focus) * depthParams.y) * (OoFBlurredLum − BLF)` (unclamped `lrp`), depth decoded from `sD.BA`
(§6.2).

### 2.6 Gamma source (`FUN_00d72f00`, brightness option) [CODE]
```
b = clamp(option_brightness, 0, 1); renderer[+0x185e8] = b
DAT_01367ce8->vfunc4( b*0.7999999523162842 + 0.6 )        // device-level gamma/brightness [ENC target]
gamma = (DAT_01367d1c & 1) ? (b*0.7999999523162842 + 0.6) * 0.8 : 0.8
photoshop->SetGamma(gamma)        // FUN_004214f0 [ENC]; inferred to write the Gamma tweak (+0x408)
```
With b = 0.5, gamma = 0.8 either way. Default option value / flag 0x1367d1c origin unknown [ENC].

---------------------------------------------------------------------------------------------------

## 3. Generic blur (`edrender_pfx_blur`, PFX_Blur) — `FUN_0089f470`, `FUN_0089eb70`

Materials: 12 items = spec[32] ∈ {0..5} × spec[33] ∈ {0,1}; same shader code for spec[33]=0/1 (spec[33]=1 is selected
when the *source* format is eng 0x12 = G16R16F; difference = material/sampler state only) [CODE+SHADER].

| spec[32] | ps/vs | kernel [SHADER] |
|---|---|---|
| 0 | ps_0/vs_0 | 4 taps `(u±vars.z, v±vars.w)` corners, average |
| 1 | ps_1/vs_1 | 9 taps 3×3 tent (weights 1,2,1 / 2,4,2 /1,2,1, ÷16) |
| 2 | ps_2/vs_2 | 5-tap 1-D Gaussian: offsets 0,±1,±2 × step; weights coeff_0123.xyz |
| 3 | ps_3/vs_3 | 7-tap: 0,±1,±2,±3; coeff_0123.xyzw |
| 4 | ps_4/vs_4 | 9-tap: 0,±1..±4; coeff_0123 + coeff_4 |
| 5 | ps_5/vs_5 | 12-tap Poisson disc, radius vars.z, average (÷12) |

Method (effect "Method" int, `FUN_0089f470`): 1/2/3 → separable Gaussian (`FUN_0089eb70`) using material index
method+1 (=2/3/4), (+6 if src format = 0x12); 0/4 → `FUN_0089f040` and 5 → `FUN_0089f220` (not used by anything
found here).

Gaussian (`FUN_0089eb70(ctx, dst, src, sigma)`, EAX = method) [CODE]:
* weights `w(x) = exp( x² * 1.0 / (−2.0 * sigma²) )` for x = 0,1,2,3,4 (`FUN_0089e0a0`, constants 1.0 @0xf44c70,
  −2.0 @0xf49c70; the import is assumed to be `exp`).
* normalization: method 1: `w0 + 2(w1+w2)` (only w0..w2 normalized); method 2: `w0+2(w1+w2+w3)`;
  method 3: `w0+2(w1+w2+w3+w4)`.
* pass H: RT = PfxTmp (size of src, format of dst), `vars = (1/src.w, 1/src.h, 1/src.w, 0)`.
* pass V: RT = dst, sampling PfxTmp, `vars = (1/src.w, 1/src.h, 0, 1/dst.h)`.
* `coeff_0123 = (w0,w1,w2,w3)`, `coeff_4 = w4` (VS constants, passed through as interpolants).

Instances:
* Photoshop: method 1, sigma 1.0 → (0.402620, 0.244201, 0.054489).
* Blur effect +0x174d4 on `ZPREPASS DOWN` (512×128, in place through PfxTmp): method 1, sigma 2.5, 1 pass →
  (0.232638, 0.214752, 0.168930). This 512×128 buffer is later read by `FSZRestore` (motion-vector restore,
  §6.4) [CODE]; its exact content (copy of FullscreenDepthTarget RG) is [INF].

---------------------------------------------------------------------------------------------------

## 4. Motion blur (`edrender_pfx_motionvector`, PFX_MotionVector) — `FUN_008b1c20` init, `FUN_008b1710` render

Disabled by default (`+8 = 0`), enable condition unknown [ENC]. Three materials (+0xa4, +0xb4, +0xc4).
Render: half-res RT (W/2×H/2; format of src with X8→A8 promotion 2→3, 0x1e→0x1f, 0x45→0x46);
pass 1 writes it from src + `sDepthMotion` = FullscreenDepthTarget (RG); optional pass with `[DAT_01459dc8+0x28]`
if large enough; final composite into src with VS `vars = (1/W, 1/H, strength/f(), strength)` and alpha blending
(0x38/0x39 = 5/6). Shader details in the dump (ps_0: up to 5 iterations along the motion vector; ps_1: weight =
|mv|·45.25; ps_3: motion-vector restore). Not needed unless motion blur is implemented.

---------------------------------------------------------------------------------------------------

## 5. Haze (`edrender_pfx_haze`) — dome object, `FUN_00932c50` init, `FUN_009309a0` constants, `FUN_0092f1d0` draws

### 5.1 Materials [CODE]
| object field | name | spec | item | shaders | role [SHADER] |
|---|---|---|---|---|---|
| (+0x64/+0x68) | `PFX_Haze_Precalc` | S_FOG=1, spec[32]=2 | e18cdb334589bf6c | vs_3 / ps_2 | renders the 2-D haze LUT (`DAT_011c95dc`): x = distance, y = cos(view,sun) |
| +0x11c4 | `PFX_Haze` #0 | S_FOG=1 | 439e1bd4aa48a957 | vs_0 / ps_0 | screen pass using the LUT (`sLut`) |
| +0x11d0 | `PFX_Haze` #1 | S_FOG=1, spec[32]=3 | 30fdd1c5b25935d1 | vs_0 / ps_3 | screen pass computing scattering per pixel |
| +0x11dc | `PFX_Haze` #2 | S_FOG=1, spec[32]=1 | 92ef11225d9823ea | vs_1 / ps_1 | writes 0 (mask/clear) |
| +0x11e8 | `PFX_Haze` #3 | S_FOG=1, spec[32]=1, spec[33]=1 | af59f1a6ff5332ae | vs_2 / ps_1 | writes 0 |

Samplers: `sZ` = `[DAT_00ffb5d8]+0x48` (screen depth buffer, decoded BA, §6.2), `sLut` = `[DAT_011c95dc]+0x48`.
Draw geometry: each material has a recorded full-screen triangle (−1,−1), (−1,3), (3,−1), z = 0; #2 uses a
projection-dependent triangle; re-recorded when the camera projection changes (+0x1190/+0x1194).
Haze-full material state: SrcBlend ONE, DestBlend SRCALPHA (bytes 2/5) → `dst = src.rgb + dst.rgb*src.a` [INF from
bytes + shader output form]. Which of #0/#1 is drawn per frame and in which layer: **not determined** (selection is
in the dome per-frame code; precalc LUT size not found).

### 5.2 Constants (`FUN_009309a0`) [CODE]
PS `Var_PS` c9..c20 copied verbatim from the dome's scattering block (dome+0x1300..0x136c, the same values the sky
shader uses):

| CTAB | reg | source |
|---|---|---|
| BetaRPlusBetaM | c9 | dome+0x1300 |
| HGg_Saturation | c10 | dome+0x1310 |
| BetaDashR | c11 | dome+0x1320 |
| BetaDashM | c12 | dome+0x1330 |
| OneOverBetaRPlusBetaM | c13 | dome+0x1340 |
| Multipliers | c14 | dome+0x1350 |
| SunColorAndIntensity | c15 | dome+0x1360 |
| SpectrumToRGB (4×4) | c16–c19 | set by `FUN_0041d9e0` (matrix) |
| Haze | c20.x | **weather `Skydome_haze`** (raw value; 0.4 in SS_Death_Valley) |
| scene `FogColor`, `FogConfig` | c0, c1 | renderer+0x174b0 = fog_color; +0x174c0 = (fog_start, fog_end, fog_max) |
| VS `vars` | c61 | (1/LUT.w, 1/LUT.h, 1, 1) for the precalc; screen (1/W, 1/H, 1, 1) for the screen materials |

### 5.3 Shader math (ps_3, full version) [SHADER]
```
p = view-space position reconstructed from sZ depth (see §6.2) and the VS ray (v0/v1)
dist = |p| ; c = max(dot(normalize(p), normalize(v2 /*sun dir, view space, from VS*/)), 0)
phaseR = c*c*0.5 + 2.0                                         // as compiled (Rayleigh-like factor)
phaseM = HGg.x * (HGg.y - HGg.z*min(c, 0.98))^-1.5            // HG: c10.x/y/z
d   = max(dist - 500, 0) * 15
ext = exp2(-d * BetaRPlusBetaM * 1.4426950216) = exp(-d*BetaR+M)   // 4 spectral channels
L   = (BetaDashR*phaseR + BetaDashM*phaseM) * OneOverBetaRPlusBetaM * (1 - ext) * Multipliers.x * SunColorAndIntensity
rgb = SpectrumToRGB * L ;  rgb = ln(max(rgb,0) + 1)
lum = dot(rgb, (0.299,0.587,0.114)) ; rgb = rgb + HGg_Saturation.w*(rgb - lum)
rgb *= Haze
f   = min(FogConfig.z, (dist - FogConfig.x)/(FogConfig.y - FogConfig.x))      // NOT clamped below 0
out.rgb = FogColor*f + rgb
out.a   = (1 - (1-ext.w)*Haze) - f
```
ps_2 (precalc, LUT texel (u,v)) is identical with `dist = u*3072`, `c = v` (no max(·,0);
`phaseR = 0.5v²+2`, HG uses `min(v,0.98)`), `d = max(u*3072 − 500, 0)*15`, fog `f = min(FogConfig.z,
(u*3072 − FogConfig.x)/(FogConfig.y − FogConfig.x))`, same `out.rgb`/`out.a` formulas.
ps_0 (LUT version) outputs `tex(sLut, (dist * 0.00032552084 /*=1/3072*/, dot(normalize(p), normalize(v2))))`
directly (rgb and alpha), i.e. it is the tabulated ps_3.

---------------------------------------------------------------------------------------------------

## 6. Screen depth / shadow buffer ("FullscreenDepthTarget" = `gViewPos` / `gTexSamplerShadowRealTime`)

### 6.1 Production
1. `CLEAR_ZSTENCIL`, `ZPREPASS_CAR_OPPOSANT`, `ZPREPASS_CAR`, `ZPREPASS`, `ZPREPASS SKY` render into RT set +0x1653c
   = {FullscreenDepthTarget, MainDepthBuffer} [CODE]. The geometry is drawn with the *scene* materials' z-prepass
   permutations (not a PFX). Example `TD_Building_ps_544`, `TD_Road_ps_100` [SHADER]:
   * `R,G = clamp(motionNDC * 32 (or 64), −1, 1) * 0.5 + 0.5` — screen-space motion vector (current − previous
     projected position).
   * `B,A` = depth encoding (below).
2. `ZPREPASS DOWNSAMPLE` → 512×128 `ZPREPASS DOWN`, then Gaussian blur (method 1, σ=2.5) [CODE]; content [INF].
3. `RT SHADOW 2D` (RT set +0x163e0) draws the full-screen sun-shadow materials of the shadow manager
   (`FUN_00977e80`, library GUID e674a83899eaeb37 = `edrender_pfx_shadow`):
   * `FSShadowMat[i]`, i = 0..3 (one per cascade slice NEAR, MEDIUM, FAR, PLAYER CAR): spec[36+i] = 1
     (USERTYPE5..8 = slice select), spec[40] = 3 at creation (USERTYPE9: values 1..3 exist = filter quality),
     spec[46] ∈ {0,1,2} (USERTYPE15, set elsewhere [ENC/unknown]), spec[47] = `DAT_011c9eb9` (USERTYPE16, adds
     `SliceUvOffset`). Blend: ONE, ONE, **MIN**, write-disable mask 0xe → **writes R only** [INF from bytes].
     PS (e.g. ps_0) [SHADER]: reconstruct view position from `sZ` BA, `ViewToLight[4]`, `UVMatrix[8]` → shadow-map
     uv for the slice, 2×2 hardware-free PCF on `sShadow3` (depth compare `ref*-0.5+0.5`-style, see dump),
     slice fade by `SlicePhaseScale`, output `oC0 = 1 + FadeAlphaFactor * (shadow − 1)` broadcast to all channels.
     `FadeAlphaFactor` (`"......H"`, 4 bytes) = `max(0, shadowmgr[+0x1e4])` per frame (`FUN_00978790`) –
     presumably `sun_shadow_opacity` [INF].
   * `FSShadowMatStencil`: spec[34] = 1 → item 20505a9c818abd29 → vs_4 / **ps_42 (`oC0 = 1.0`)**, opaque
     (2/1/6), write-disable mask 0xf (no color written → stencil/marking pass) [CODE+INF].
   * Two more materials (+0xd34, +0xd40) built by `FUN_00977b50` with different stencil settings.
4. `HEADLIGHTS` renders light volumes (`edrender_pfx_lightness`, 26 materials, `FUN_00922030`) into HeadLightsRT
   (R16F…); `HEADLIGHTS BLIT` blits it into RT set +0x163e0 (blend/channel unknown).
5. `FSZRestore` (`edrender_pfx_motionvector` ps_3, material renderer+0x33e4, write-disable 0xc → writes R,G) samples
   `ZPREPASS DOWN` and rewrites RG = `clamp((tex.xy*2−1)*2.5, −1, 1)*0.5+0.5` (restores motion vectors for the motion
   blur after the shadow pass overwrote R) [CODE+SHADER]. Where it is scheduled: unknown.

Inference [INF]: RT set +0x163e0's color target is FullscreenDepthTarget (not written by readable code, but:
FSShadowMat reads `sZ` B/A and min-blends only R; FSZRestore restores RG; scene shaders read R = shadow, BA = depth
from the same texture). The R channel therefore holds `min(1, shadow_slices…)`; G = motion-vector Y (or headlight
term, unknown); B/A = depth.

### 6.2 Depth encoding (B,A) [SHADER, exact]
Encode (zprepass PS, `w` = view depth from VS):
```
d' = w*32768 + 0.01
E  = floor(log2(d'))
f  = (32768*w) * 2^-E - 1          // mantissa fraction in [0,1)
B  = (E*8 + floor(8f)) / 255        // = (E<<3 | floor(8f)) as a byte (stored *0.003921568859)
A  = frac(8f)
```
Decode (all PFX shaders): `E = floor(B*31.875)`, `lo3 = B*255 − 8E`,
`depth = 2^(E−15) * (1 + lo3*0.125 + A*0.125)` (A in [0,1], i.e. byteA/2040).

### 6.3 Real-time shadows OFF
Not resolvable in readable code: the per-frame shadow manager (`FUN_00978790`) enables/disables the cascade layers
via `DAT_00ffb68c[...]+8` and slice count `[+0x460]`; with 0 slices no `FSShadowMat` slice pass is drawn and the R
channel keeps what the ZPREPASS wrote (motion X) unless another pass writes it [ENC/unknown]. The only permutation
that writes a constant "unshadowed" value is item 20505a9c818abd29 (ps_42, `1.0`) — used as the stencil material;
ps_43 (`1.0` + depth 0) exists but is not referenced by any item. **Recommendation for OpenTDU2**: when real-time
shadows are off, write R = 1.0 (ps_42 semantics) over the FullscreenDepthTarget with a R-only mask, keep BA.

---------------------------------------------------------------------------------------------------

## 7. Other PFX libraries
* `edrender_pfx_lightness`: headlight/light-volume shaders (26 materials `FUN_00922030`) and `PFX_Zpass`
  (`FUN_0089f630`/`FUN_0089fd40`, VS `vars=(1/w,1/h)`, optional texture with `vars=(-1/m[0], -1/m[5])` from a
  projection matrix). Item specs are empty in the dump → permutation mapping unknown.
* `edrender_pfx_anaglyph` (PFX_Anaglyph): stereo 3D output only.
* `edrender_pfx_sampling`: downsample (§2.3).

---------------------------------------------------------------------------------------------------

## 8. Known unknowns (encrypted or unresolved)
1. Bodies of all photoshop setters (0x421ba0 … 0x422410, 0x4214f0, 0x4223b0) [ENC] → possible clamping.
2. Graph registration/execution (`FUN_00421680`) [ENC] → exact execution order is inferred from dependencies.
3. Whether LUT-creation constants are snapshotted at record time (1-frame lag) [ENC: `FUN_00522e20`].
4. What fills RT set +0x163e0 and the exact scheduling of `RT SHADOW 2D`, `HEADLIGHTS BLIT`, `FSZRestore`.
5. Haze: which of the two screen materials is drawn and in which layer; precalc LUT size/format.
6. Enable condition of the motion blur; FSShadow spec[46]/spec[40] runtime values; default brightness option.
7. Material state byte semantics in §1.3 are inferred (consistent across all materials, not proven).

---------------------------------------------------------------------------------------------------

## 9. Index of decompiled sources
| file | content |
|---|---|
| pfx_dec.c | lib list `FUN_008ae1a0/008ae6e0`, photoshop init `FUN_008b1220`, ctor `FUN_008afd70`, haze init `FUN_00932c50`, PFX_Blur init `FUN_0089f7e0`, PFX_Zpass `FUN_0089f630`, fx.ini loader `FUN_00d6f530`, weather registration `FUN_00af30d0` |
| pfx_dec2.c | photoshop vtable: render `FUN_008b0aa0`, param getter `FUN_008afb00` |
| pfx_dec3.c | RT pool `FUN_0089e360`, Gaussian `FUN_0089eb70`, set-RT `FUN_008a6780`, draw `FUN_008a4aa0`, constant lookup |
| pfx_dec4.c | downsample dispatcher `FUN_0089ea70`, motion blur render `FUN_008b1710`, blur render `FUN_008b2591`, renderer setup `FUN_00d7a0e0`, frame begin/end `FUN_00df36e0/00df3810` |
| pfx_dec5.c | effect factories, blur param setters, `FUN_0089f470`, `FUN_0089e4a0/e730/e990/ea00`, `FUN_0089e0a0` |
| pfx_dec6.c | main RT creation `FUN_00d75d00`, render-layer table `FUN_00d73170` |
| pfx_dec7.c | motion blur init `FUN_008b1c20` |
| pfx_dec8.c | lightness materials `FUN_00922030`, `FUN_0089fd40`, fx.ini parser `FUN_00d6b5b0` |
| pfx_dec9.c / pfx_dec10.c | full-screen shadow materials `FUN_00977b50/00977e80`, shadow manager per-frame `FUN_00978790` |
| pfx_dec11.c / pfx_dec12.c | haze init / haze display lists `FUN_0092f1d0` |
| pfx_dec13.c | RT create `FUN_008b6f70`, HeadLightsRT format `FUN_009206d0` |
| pfx_dec14.c | weather → photoshop/fog `FUN_00d769e0`, brightness/gamma `FUN_00d72f00` |
| pfx_shaders_dis.txt | CTAB + disassembly of every PFX permutation (note d3d9dis: `dsx` with 4 operands = dp2add) |

Reference LUT generator (Python, same math as §2.5):
```python
import numpy as np
def tdu2_lut(P, S, M, H, black, white, inten, gamma, N=256):
    i = np.arange(N) / N                      # D3D9 pixel-center convention
    u, v = np.meshgrid(i, i)                  # u: x (channel), v: y (lum)
    u = u[..., None]; v = v[..., None]
    S, M, H = map(np.asarray, (S, M, H))
    b = P[1] * v
    C = b + S * np.where(S <= 0, 1.075 - 1/(1 + 15.9375*(1-b)), 1.075 - 1/(1 + 15.9375*b))
    C = C + M * 0.667 * (1 - ((C*255 - 127) * 0.007874015718698502)**2)
    C = C + H * np.where(H <= 0, 1.075 - 1/(1 + 15.9375*C), 1.075 - 1/(1 + 15.9375*(1-C)))
    hi = np.sqrt(u)*(2*C-1) + 2*u*(1-C); lo = 2*u*C + u*u*(1-2*C)
    SL = np.where(C[..., 1:2] - 0.5 >= 0, hi, lo)
    X = P[0]*SL + (1-P[0])*u
    O = np.where(X - 0.5 >= 0, 1 - 2*(1-X)*(1-v), 2*v*X)
    Y = P[2]*O + (1-P[2])*X
    Y = Y + inten * (v - black) * (1.0/(white - black))
    return np.clip(np.abs(Y) ** gamma, 0, 1)  # then quantize to 8 bit
```
