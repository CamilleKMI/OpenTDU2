# TDU2 wet road / puddles / rain + real-time sun shadows — reverse-engineered specification

Sources: `TestDrive2.exe` (readable .text only), shader libraries `Euro/Shaders/*.sob`
(split permutations in `/tmp/claude-0/sob/all/<lib>/`), `islands/ibiza/fx/cinematic_weathers.xmb`,
`fx/weatherfx.bnk`, `Shadows.cpr` and `Quality_Settings.cpr` (copied from the user's install, decrypted with
`otdu2_bnk_tool decrypt`, plain text in `/tmp/claude-0/rs/cpr/`).
Decompiled code: `/tmp/claude-0/rain_shadow_dec1.c … rain_shadow_dec6.c`, raw asm extracts (the decompiler loses
most stack values in the big shadow functions) in `/tmp/claude-0/rain_shadow_dec7_asm.txt`.

Legend: **[CODE]** read from readable x86, **[DATA]** read from files, **[SHADER]** read from bytecode,
**[INF]** inference (reason given), **[ENC]** behaviour hidden in the SecuROM range (< 0x6c1000).

---------------------------------------------------------------------------------------------------

## 0. Summary

* The wet road is **only** `TD_Road` (and `TD_Tunnel`, same handler/GUID family). The rain permutation is
  **spec[40] = S_USERTYPE9 = 1**, which adds 3 texture slots: s6 ripples (`ripple_NN`), s7 **cube** reflection
  map, s8 `PUDDLES`. No other scene library (TD_Ground, TD_Building, TD_HeightMap…) has rain constants. [SHADER+CODE]
* The puddle "mirror" is **not a planar reflection**: s7 is a `samplerCUBE` and the object `WFX_WetGrd` binds
  `[0x011c9f00]+0x48`, the texture of the player-car dynamic environment cube (the RT of the `ENVMAP_*` layers,
  the same cube the car paint uses). It is sampled with `texldl` lod 0 along the reflected view vector,
  direction `(R.x, R.y, -R.z)`. `REFLECT 2D` (RT `REFLECTIONMAP2D`, W×H A8R8G8B8 + D24S8) is not used by TD_Road. [CODE+SHADER]
* Per-frame constants: `RainSpecs = (rain_ground_global_intensity, rain_ground_wet, 1.0, rain_ground_puddle_height)`,
  `RainRippleSpecs = (scroll, scroll, rain_ground_ripple_intensity, rain_ground_ripple_scale)` with
  `scroll += dt * rain_ground_ripple_scroll_speed * 0.15`. Ripple animation: 16 textures `RIPPLE_01..16`, one step
  every **0.025 s** (cycle 0.4 s). [CODE]
* Press-start preset `SS_Death_Valley`: GI 1.0, wet 0.6, puddle height 0.62174, ripple intensity **0** (still
  mirror puddles, no ripple bumps), ripple scale 0.1, scroll speed 0.90435; rain/droplets/splash invisible
  (`rain_alpha` 0, `rain_droplets_max` 0, `rain_splash_alpha` 0). [DATA]
* Sun shadows: 3 PSSM cascades + 1 player-car map. Split distances
  `d_i = λ·n·(f/n)^((i+1)/N) + (1-λ)·(n + (f-n)(i+1)/N)` (N = 3) from the active shadow config
  (`SET_SHADOW_CONFIG_SLICES name near far λ`). Shadow maps are R32F (engine fmt 0x14) colour + D24S8,
  side = `SET_SHADOWS_RESOLUTION` (code default 1024; Shadows.cpr 2048 2048 2048 1024). [CODE+DATA]
* Casters are drawn with `TD_ShadowCaster`: colour = `(clip.z*2-1, 0, 0, 1)` in R32F. [SHADER]
* The full-screen pass `RT SHADOW 2D` draws `FSShadowMat[i]` (edrender_pfx_shadow): reconstructs the view
  position from the encoded depth (FullscreenDepthTarget B/A), projects with `ViewToLight[i]` and
  `UVMatrix[2i..2i+1]`, 2×2 PCF with bilinear weights, `out = 1 + Shadow_SlicesDist.w·(lit-1)` (car slice also
  × `FadeAlphaFactor`), blended **MIN into R** (ONE/ONE/MIN, write mask R only). `Shadow_SlicesDist.w` =
  weather `sun_shadow_opacity` (manager flag +0x431, default on). [CODE+SHADER]
* Correction to postfx_spec §6.1 [SHADER, see §B.7]: the scene shaders' `gTexSamplerShadowRealTime` (s15) is
  read as R = sun shadow, **G/B = a log-encoded light term (×32, headlights [INF])**, **A = multiplier of all
  lighting**. That is not the layout of FullscreenDepthTarget (B/A = depth), so the RT of set +0x163e0 that
  receives the shadow (and HEADLIGHTS BLIT) is most likely a separate W×H buffer, not FullscreenDepthTarget.

---------------------------------------------------------------------------------------------------

# Part A — wet road, puddles, rain

## A.1 Weather parameters

### A.1.1 Registration and consumers [CODE]
Parameters are registered in `FUN_00af30d0` (entries 0x38 bytes: {u64 id, default, current, …, name}); every
consumer fetches them with `FUN_0041d8a0(lo, hi)` → entry, value at `+0x18`. The id is
GetIdentifier64bit(name) (uppercase, char i added into byte i&7):

| parameter | id (hi lo) | consumer |
|---|---|---|
| rain_ground_global_intensity | dfe6e7f4 4f40243b | FUN_00aa6ce0 → WetGrd +0x264; also FUN_008c6880 (tyre marks) |
| rain_ground_wet | 4fa68cb6 ad8d8fa7 | FUN_00aa6ce0 → WetGrd +0x274; FUN_00c559f0 (car wheel FX, threshold 0.33) |
| rain_ground_puddle_height | dbdde5f4 f5ecd447 | WetGrd +0x270 |
| rain_ground_ripple_intensity | ede7e4ff 4f401d46 | WetGrd +0x268 |
| rain_ground_ripple_scale | e4eed1f4 00ecd4f3 | WetGrd +0x26c |
| rain_ground_ripple_scroll_speed | eb352739 503f333f | WetGrd +0x278 |
| rain_tiling | 4c49545f 4e908f9b | FUN_00aa61a0 → rain object [+0xdb0]+0x59b4 |
| rain_scroll_speed | 9788a3b2 ad958de5 | rain object +0x59a8 |
| rain_alpha | 504c415f 4e49829a | rain object +0x59b8 = int(alpha), enabled if alpha > 0.001; [+0xdb8]+0x7b6c = int(alpha), enabled if alpha > 1e-4; droplet alpha (FUN_00aa2100) |
| rain_droplets_max / _life_time / _spawn_freq | 909fa3b2 a28e8dfa / 98e3f0fb f6edd2e8 / f0eaf5f8 01dce4e3 | droplets [+0xdbc]: +0x3890 = int(max), +0x3888 = life, +0x3894 = freq, enabled if max > 0 |
| rain_droplets_refraction_oncam | e2f33947 281d2e47 | FUN_00aa2100: `refract.z` of TD_Droplets_Oncam |
| rain_splash_max / rain_splash_alpha | 4ca894ac ad919493 / 94a09fa0 ad9194d4 | splash [+0xdb4]: +0x4d4c = int(max), +0x4d54 = int(alpha), +0x201 = (max > 0) |
| thunder_off_on | 5f529393 ad9b8ea3 | lightning [+0xdc0] enabled if > 0.5 |
| sun_shadow_opacity | 8d8489a3 aead05f6 | shadow manager FUN_00978790 → Shadow_SlicesDist.w (§B.6) |

`FUN_00aa6ce0` (called from FUN_00aa7200) copies the current weather values into the WFX objects every frame
(weather FX root `param_1`: +0xdb0 rain, +0xdb4 splash, +0xdb8 rain 2, +0xdbc droplets, +0xdc0 thunder,
+0xdc8 **WFX_WetGrd**). For WetGrd it ends with `vfunc+0x18(enable = GI > 0.0)`.

### A.1.2 Values [DATA] (`python3 -I py/weather.py cinematic_weathers.xmb`, full dump `/tmp/claude-0/cw.txt`)

| preset | GI | wet | puddle_h | ripple_int | ripple_scale | ripple_scroll | rain_alpha | tiling | scroll | splash max/alpha | droplets max/life/freq | refr_oncam | sun_shadow_opacity |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **SS_Death_Valley** | 1.0 | 0.6 | 0.62174 | 0.0 | 0.1 | 0.90435 | 0 | 1.2 | 1.4 | 132.61 / 0 | 0 / 0 / 0 | 15 | 1.0 |
| SS_Old_Town | 0 | 1.0 | 0.5 | 1.0 | 0.6 | 0.05 | 0 | 0 | 0 | 0 / 0 | 0 | 15 | 1.0 |
| SS_Arche | 0 | 0 | 0 | 0 | 0.1 | 0 | 0 | 0 | 0 | 0 / 0 | 0 | 15 | 1.0 |
| SS_Chapelle | 0.99883 | 0.60047 | 0.62160 | 0.00117 | 0.10059 | 0.90335 | 80 | 12.75 | 1.39836 | 132.45 / 0 | 1 / 2 / 0.08152 | 30 | 0.99883 |
| Cin_Gen | 0 | 1.0 | 0.5 | 1.0 | 0.6 | 0.05 | 0 | 0 | 0 | 0 / 0 | 0 | 15 | 0.0 |

So with SS_Death_Valley the road is wet with puddles and mirror reflections; there is no falling rain, no ripple
bumps, and the droplets and splashes do not show.

## A.2 WFX_WetGrd object (vtable 0x00f1b174, ctor FUN_00aa06e0, type 6) [CODE]

Fields: +0x200 init done, +0x201 enabled, +0x208 current ripple texture (0 = unchanged this frame),
+0x20c PUDDLES texture, **+0x210/+0x214 ripple scroll (u,v)**, +0x220[16] visible sectors, +0x260 count,
**+0x264 GI, +0x268 ripple_intensity, +0x26c ripple_scale, +0x270 puddle_height, +0x274 wet,
+0x278 ripple_scroll_speed**, +0x27c ripple timer, +0x280 ripple frame index. Ctor defaults: +0x264 = 1.0,
+0x26c = 0.6.

* init (vtbl[1] FUN_00aa0660): `+0x20c = texture "PUDDLES"` (id 0x0053454c44445550) from the weatherfx bank
  object `DAT_0117ac70` (FUN_00aa6560 = lookup by id).
* update (vtbl[2] FUN_00aa0b60), `dt = DAT_00fdb8d0` (frame time in s [INF: used as a timer decrement elsewhere]):
  ```
  s = dt * ripple_scroll_speed * 0.15            // 0.15 = double @0xf49d68
  scroll_u += s;  scroll_v += s                  // the same value on both axes
  FUN_00aa0440(dt)                                // ripple animation
  FUN_00aa0a30(enabled = GI > 0.0)
  ```
* ripple animation FUN_00aa0440:
  ```
  if (timer < 0) { tex = lookup("RIPPLE_%02d" , frame+1); frame = (frame+1) & 15; timer = 0.025 (@0xf49ba0);
                   +0x208 = tex }
  else           { +0x208 = 0; timer -= dt }
  ```
  The ids are hashed in place: `RIPPLE_01` = 0x305f454c50504983 … `RIPPLE_16`.
* FUN_00aa0a30(enabled): `w = GI*wet`; `detailAniso = enabled && w > 0.6 ? (w > 0.9 ? 1 : 2) : 4`;
  `rippleAniso = enabled && GI*ripple_intensity > 0.05 ? 2 : 1`; gathers the visible sectors (FUN_00aa05a0:
  16 slots of 0x269c0 at `DAT_01024b88+0x23d0`, state 1 and visible), and for each sector's `NETWORK` model
  (hash 0x004b524f5754454e) applies FUN_00aa0890 to every material. It does the same 16 times on an object of
  type 'apor' (0x726f7061).
* FUN_00aa0890(material) (only when MAT GUID = c8ad93c95455291b = TD_Road):
  * FUN_00aa0780(enable): if `MAT.spec[40] != enable` and spec[38] (`MAT+0x336`, UT7) == 0, then
    slot 6 (MAT+0x1c0, ripples): addressing +0x1d6/7 = 2, filters +0x1d8/9/a = 1;
    slot 7 (MAT+0x1e0, reflection): **texture +0x1f0 = `[0x011c9f00]+0x48`**, +0x1f6 = 2, +0x1f7 = 0,
    +0x1f8/9/a = 2; slot 8 (MAT+0x200, puddles): texture +0x210 = PUDDLES, +0x216/7 = 2, +0x218/9/a = 1;
    the runtime-texture ids of slots 6,7,8 (+0x1c8, +0x1e8, +0x208) = enable; MAT+0xfc = 0x10;
    **`MAT+0x338 = spec[40] = S_USERTYPE9 = enable`**; then FUN_008bb9f0 rebuilds the material.
  * FUN_00d72bc0 on slot 3 (MAT+0x160, detail map) with detailAniso and on slot 6 with rippleAniso: sets the
    slot anisotropy `round(v*[0xf7f9c4]=1.0)` clamped to [1, max aniso] (+0x14) and the filter (+0x16 = 4/5 when
    ≥ 2). [CODE; meaning of the filter values INF]
  * constants (looked up by symbol id in the material):
    ```
    RainSpecs       (PS, 434550534e4941a5) = ( GI, wet, 1.0, puddle_height )
    RainRippleSpecs (VS, 50a38c979e9c869e) = ( scroll_u, scroll_v, ripple_intensity, ripple_scale )
    ```
  * if `+0x208 != 0`: slot 6 texture `MAT+0x1d0 = +0x208` (the current ripple frame).
* disable (vtbl[6] FUN_00aa0be0(false)) runs FUN_00aa0a30(0), which removes UT9 again.

The permutation hash (re-solved with the GF(2) hash, `/tmp/claude-0/rs/road_items.txt`): every TD_Road PS
that has RainSpecs is an item with **S_USERTYPE9 = 1** and 3 more textures, e.g. the base material
`COORDTEX=3 GLOSS NORMAL TANGENT UT1 UT2 UT3` → dry item `d52f25b147a47cd3` (vs12/ps16, NBTEX 5) →
rain item `80c56b296b1c5877` (vs14/ps18, NBTEX 8). UT7 never coexists with UT9 (matches the spec[38] test above).

## A.3 Textures (fx/weatherfx.bnk) [DATA]
`ripple_01..16.2db` (11008 bytes each, entries 8–23), `puddles.2db` (174848), `cuberain.2db` (262208, a cube,
**not referenced by the WetGrd code** — the slot gets the dynamic env cube; whether cuberain is used elsewhere
is unknown), `flaque.2db`, `drop.2db`, `droplets.2db`, `droplets_02_nmap.2db`, `rain_alp/rain_far/splash.2db`.

## A.4 TD_Road rain shader (e.g. vs_2/ps_2 = shadow-receiving, vs_3/ps_3 = without s15 and without cube) [SHADER]

### VS (Var_VS c60: WVP c60–63, InvWorldView c64–67, CamPos_ObjSpace c68, DirLight_Dir_0 c69, RainRippleSpecs c70)
Inputs v0 pos, v1 normal (w = handedness), v2 tangent, v3 uv0, v4 uv1. B = normalize(cross(N,T)·N.w).
```
oPos   = WVP·pos
oT0.xy = uv0                         ; colour map
oT2.xy = uv1 ; oT2.zw = N.xz / N.y   ; detail+gloss uv ; slope normal
oT4    = -(L·T, L·B, L·N)            ; L = DirLight_Dir_0
oT5    = (cam-pos)·(T, B, N)         ; view vector, tangent space
oT6    = -(C·T, C·B, C·N) , C = (InvWorldView rows)·(0.43849536, -0.63550049, 0.63550049)   ; "counter" light
oT7.xy = pos.xz * RRS.w - RRS.xy     ; ripple uv (scrolling)
oT7.zw = pos.xz * 0.0703125          ; puddle uv (1/14.22 m)
oD0.xyz = T*0.5+0.5 ;  oD0.w = (25 - |cam-pos|) * (1/15) * RRS.z     ; ripple fade, NOT clamped
oD1.xyz = B*0.5+0.5 ;  oD1.w = (N.y >= 0.2) ? 1 : 0                  ; wet mask for flat surfaces
```
### PS (Var_PS c9 Amb, c10 Diffuse, c11 Specular, c12 RainSpecs, c13 SpecularParams, c14 SpecularParams1;
Var_Scene_PS c6 RenderTargetSize; samplers s0 colour, s3 detail, s4 gloss, s6 ripples, s7 CUBE, s8 puddles,
s15 gTexSamplerShadowRealTime). RS = RainSpecs.
```
T = 2*oD0.xyz-1 ; B = 2*oD1.xyz-1 ; Ns = normalize(oT2.z, 1, oT2.w)
rip   = (tex(s6, oT7.xy).xy - 0.5) * 2 * oD0.w
p     = 0.5*(tex(s8, 1.7213541*oT7.xy).x + tex(s8, oT7.zw).x)      ; first tap scrolls with the ripples
puddle = (p - Ns.y + RS.w) >= 0
g     = tex(s4, oT2.xy) ; bump = 2*(g.wy - 0.5) ; specMask = g.z
n_ts  = normalize( (puddle ? rip : bump), 1 )
wet   = (puddle ? 1 : RS.y) * oD1.w
V     = normalize(oT5)
fres  = (1 - dot(V, n_ts)) * RS.x
Rw    = reflect-ish: r = V - 2·dot(V,n_ts)·n_ts ; Rw = r.x*T + r.y*B + r.z*Ns
refl  = texCUBElod(s7, float4(-Rw.x, -Rw.y, Rw.z, 0)) * fres * wet        ; = lookup along (R.x,R.y,-R.z)
dark  = 1 - 0.5*wet
S     = tex(s15, vPos.xy * c6.xy)          ; R shadow, G/B log-encoded term, A multiplier
spec  = c11*specMask * ( c13.y*pow(saturate?(dot(V, reflect(-L,n))*S.r), c13.x)
                       + c14.y*pow(dot(V, reflect(-Cdir,n))*S.r, c14.x) )   ; no saturate in the code
ndl   = max(dot(bumpN, normalize(oT4)), 0) * S.r          ; bumpN = (bump, sqrt(1-|bump|²)) — gloss bump, not rip
lit   = lerp(c9, c10, ndl)
H     = 32 * 2^(E-15) * (1 + lo3/8 + S.b/8),  E = floor(S.g*31.875), lo3 = S.g*255 - 8E
albedo = tex(s0,oT0) * tex(s3,oT2.xy)
out.rgb = (S.a*lit + H) * albedo.rgb * dark + S.a*(refl + spec)
out.a   = albedo.a
```
Notes: the reflection and the puddle use the *slope* normal Ns (from the vertex normal) as the tangent-space Z;
in a puddle the bump is replaced by the ripple normal (zero when ripple_intensity = 0 → perfect mirror of the
cube); `pow` operands are not clamped (negative bases give NaN on D3D9 → treat as 0). `vPos·RenderTargetSize`
has no +0.5 (D3D9 vPos is the pixel corner); in Vulkan use `(gl_FragCoord.xy-0.5)*c6.xy` to sample the same point.

### Values for SS_Death_Valley
`RainSpecs = (1.0, 0.6, 1.0, 0.62174)`, `RainRippleSpecs = (s(t), s(t), 0.0, 0.1)` with
`s(t) = 0.15·0.90435·t = 0.13565·t`. Puddle where `p ≥ N.y - 0.62174` (flat road: p ≥ 0.378); outside the
puddles the colour is darkened by 0.7 (1 - 0.5·0.6) and reflects 0.6·(1-N·V) of the env cube; inside by 0.5 with
the full (1-N·V) cube reflection.

## A.5 Reflection map [CODE]
* `DAT_011c9f00` (struct at 0x011c9ef0: +0x10 cube 1, +0x14 cube 2 = `DAT_011c9f04`, +0x18 depth): RT set
  `DAT_00ffb958` colour0 = `[0x011c9f00]`, `DAT_00ffb984` = {0x011c9f04, depth 0x011c9f08} (FUN_00d57060);
  FUN_00d58df0 binds `[0x011c9f00]+0x48` to the actors' reflection slot (player car), `[0x011c9f04]+0x48` for the
  other case; FUN_00cbdf90 does the same for the generic reflection slot. So the road's cube is the **dynamic
  environment cube of the player car (ENVMAP_* layers)**. Its size comes from `SETENVMAPRESOLUTIONS`
  (Quality_Settings: 256/128, 512/256, 1024/512, 1024/1024 for levels 0–3). Who creates the RTs: not found in
  readable code [ENC].
* `REFLECTIONMAP2D` (FUN_00c32a00): colour `FUN_008b6f70(W, H, eng 3 = A8R8G8B8)` with viewport (0,0,W,H) and
  depth `FUN_008b73b0(W,H,0x40 = D24S8)`; used by the `REFLECT 2D` layer (flags 0x40800000). TD_Road has no 2D
  reflection sampler, so it does not matter for the road. The mirror camera, clip plane and object list of
  REFLECT 2D were not traced (`MatDomeReflect`/`renderListDomeReflect` suggest the sky dome drawn mirrored for
  water) [unknown].

## A.6 Other rain FX (inactive in SS_Death_Valley)
Falling rain (WFX_Rain blocks, WFX_Drop, WFX_Splash) and the rain sound: see `rain_spec.md`.
* Droplets on camera (FUN_00aa2100, TD_Droplets_Oncam): `half_pixel = (0.5/W, 0.5/H)`,
  `refract = (k²/W, k²/H, rain_droplets_refraction_oncam, -)` with k = `+0x38ac`; per droplet the alpha byte is
  `round(life/lifeTime · clamp(2·rain_alpha/255, 0, 1) · 255)`. [CODE]
* `td_wetground.sob` (TD_Wetground: VS `scroll`, gCubeMapTexSampler, gMaskTexSampler) is in the library list
  (FUN_00d71e60), but no readable code was found that uses it [unknown].

---------------------------------------------------------------------------------------------------

# Part B — real-time sun shadows

## B.1 Configuration [CODE+DATA]
`fx.ini`-style commands (handlers registered in FUN_00d6b5b0), records found by name hash (FUN_004eeef0 **[ENC]**),
fields relative to the config record (`FUN_009739d0` returns rec+0x18 for the current config name
`[DAT_00fd50fc+0x28]`, fallback id 0x18d497dfa3b6edda = "DefaultShadowConfig" [INF]):

| command | stores |
|---|---|
| SET_SHADOW_CONFIG_SLICES name near far λ | +0 near, +4 far, +8 λ, **+0x10 = 3 slices** |
| SET_SHADOW_CONFIG_MAX_CAST_DIST name d | +0xc |
| SET_SHADOW_CONFIG_BIAS name b0 b1 b2 b3 | +0x18..+0x24 (per slice) |
| SET_SHADOW_CONFIG_STEREO_CONV name c | +0x30 |
| SET_SHADOWS_RESOLUTION r0 r1 r2 r3 | `DAT_00f7e208..214` (ints, default 1024 each) and words `[DAT_010017f0+0x2d6..]` |

Data: `Shadows.cpr`: `SET_SHADOWS_RESOLUTION 2048 2048 2048 1024`. `Quality_Settings.cpr` per level:
256³+256, 1024⁴, 1536³+1024, 2048³+1536. The `LOGIN` config (same in all levels):
`SLICES 0.8 136.0 0.5`, `BIAS 2.5 2.5 2.5 1.0`, `MAX_CAST_DIST 196`, `STEREO_CONV 10`. Other configs (level 3):
FREERIDE 4.2/275/0.9, CINE_VERY_FAR 0.8/500/0.5, PG 1.6/90/0.5, GARAGE_FIRST_PERSON 0.8/48/0.5 …
(full text in `/tmp/claude-0/rs/cpr/qs.txt`). **Which config is active on the press-start screen is decided by
`[DAT_00fd50fc+0x28]` [ENC]**; `LOGIN` is the likely one [INF: name].

Shadow manager (instance `DAT_00fd95b0`, ctor tail at 0x977850) defaults: +0x434 = 10.0, +0x438 = 500.0
(provisional light near/far), +0x464 = 900.0 (light distance), +0x468 = 1.0 (ortho scale), +0x46c = 10.0,
+0x470 = 1.0 (SlicesDist.w when the weather is not used), +0x1e4 = 1.0 (FadeAlphaFactor),
+0x430 = 1, **+0x431 = 1 (use weather sun_shadow_opacity)**, +0x49f = 0 (fixed centre off),
+0x474 = 1 (enabled), +0x49c = `(DAT_00fe4c7b & 4) == 4`, bounds +0xa0.. = ±FLT_MAX. [CODE]

## B.2 Shadow map render targets [CODE]
`FUN_00c32380` (colour) / `FUN_00c32880` (depth), shadow RT struct at 0x011c9ea8:

| slice | name | colour RT | depth RT |
|---|---|---|---|
| 0 | SHADOW_NEAR | size0², eng 0x14 = **R32F** (or eng 0x49 = FOURCC `NULL` when `[0x011c9eb8]`) | size0², eng 0x40 = D24S8, flags 4 when `[0x011c9eb9]` |
| 1 | SHADOW_MID | size1², R32F | D24S8 |
| 2 | SHADOW_FAR | size2², R32F | D24S8 |
| 3 | SHADOW_PLAYER_CAR | size3², R32F | D24S8 |

`DAT_011c9eb9` = hardware depth-shadow mode (spec[47] of FSShadowMat, INTZ/RAWZ depth textures, eng
0x4b/0x4a) — the normal path is R32F. Layers (index into the 0x2c-byte layer table, `DAT_00ffb68c[idx*0xb]`):
`DAT_00edd930 = {8, 10, 12, 14}` = RT SHADOW NEAR/MEDIUM/FAR/PLAYER CAR (light cameras and casters),
`DAT_00edd940 = {7, 9, 11, 13}` = the matching `… CLR` layers (the HAZE layer, index 3, was missing from the
list in postfx_spec §1.5). A layer is enabled (`+8 = 1`) when `i == 3 || i < slices`. [CODE]

## B.3 Casters [SHADER+CODE]
* FUN_00978530 sends per slice the actor lists (`mgr+0x120+0xc·i`, cars through FUN_00978410) and the
  mesh/instance lists (`+0x150…`, `+0x180…`, culled by FUN_00976300) to the slice layer (FUN_0041fb80 [ENC]).
  The switch to the caster permutation (S_PASS_SHADOWCAST, spec[17]) or the GUID `TD_ShadowCaster`
  (ac78df32bc4589ff) happens in the encrypted scene handler [ENC].
* `TD_ShadowCaster` (128 items, specs not stored): VS `oPos = WVP·pos`, `oT1.x = oPos.z*2 - 1`
  (vs_4; skinned variants with `__Skin_Matrix[96]`); PS ps_1 `oC0 = (oT1.x, 0, 0, 1)`, alpha-tested ps_2/3/4
  (`texkill(tex.a - __Alpha_Ref)`, s0 base, s2 alt, min of two maps) write `oT1.x` too. The shadow map holds
  `2·z_ortho - 1`; empty texels are 1.0 (the pass treats ≥ 0.998 as "no caster", B.5) [INF clear value].

## B.4 Per-frame computation — FUN_00978790 / FUN_00976300 [CODE, asm in rain_shadow_dec7_asm.txt]
1. `slices = cfg+0x10` (3), `maxCast = cfg+0xc` → `mgr+0x45c`. Enable layers (B.2).
2. Splits (FUN_00973890, i = 0..slices-1): `mgr+0x43c+4i = λ·n·pow(f/n,(i+1)/N) + (1-λ)·(n+(f-n)(i+1)/N)`,
   n = cfg+0, f = cfg+4, λ = cfg+8, N = slices. LOGIN (n 0.8, f 136, λ 0.5): d = (25.149, 57.742, 136.0)
   (= 0.5·0.8·170^(k/3) + 0.5·(0.8 + 135.2·k/3), k = 1..3).
3. For each enabled slice i (FUN_00976300(mgr, i), sun direction `dir` in ecx):
   * slice frustum = copy of the main camera with near = camera near (i = 0, 3) or `d_{i-1}` (i = 1, 2), far =
     `d_i` (i < 3) or `d_{N-1}` (car slice); its 8 corners → `mgr+0x20..+0x90`.
   * centre `c` = mean of the 8 corners (×0.125) — or `mgr+0x10` when +0x49f is set.
   * eye = `c - dir·mgr[+0x464]` (900 m); view = LookAt(eye → c) with up (0,1,0), or (1,0,0) when
     `|dir·up|` makes them parallel (test with 1e-4) [CODE; argument order of the helper INF].
   * corners → light view; AABB (x, y, z); intersected with the caster bounds of the slice
     (`mgr+0xa0+0x10i` min / `+0xe0+0x10i` max) and the caster lists (actors' and meshes' bounding volumes).
   * ortho: `width = scale·(xmax-xmin)`, `height = scale·(ymax-ymin)` (scale = +0x468 = 1.0), `P00 = 2/width`,
     `P11 = 2/height` (written into the layer camera +0x110/+0x124, extents +0x164/+0x168); near/far =
     `-zmax`, `-zmin` (also kept in `mgr+0x410+4i` / `+0x420+4i`). No texel snapping found.
   * texture matrix (stack 0xf0): `m00 = Bv/width`, `m11 = -Cv/height` (Bv, Cv = `[[layer+0x30]+8]`,
     `[+0xc]`, the RT viewport scale [INF: = 1]), `m22 = 1`, `m30 = m31 = 0.5 + 0.5/size_i` (D3D9 half texel);
     multiplied (FUN_00ddb740 / FUN_00dd9d20) with the light view/projection → world-to-shadow matrices stored
     at `mgr+0x210+0x40i` (UV) and `mgr+0x310+0x40i` (depth row at +0x330).
4. Scene constants (every scene object of `DAT_013685d0`, the first must be `DAT_00ffb920`):

| scene field | shader constant | value |
|---|---|---|
| +0x140+0x10i | Shadow_WorldToLight[i] | row `mgr+0x330+0x40i` with `w += bias_i` [INF: stack offsets] (0 when the slice is off) |
| +0x180+0x40i | Shadow_WorldToUVMatrix[2i, 2i+1] | rows of `mgr+0x210+0x40i` (shader reads 2 rows per slice: c43/44 slice 0 … c49/50 slice 3) |
| +0x280 | Shadow_ExtraParams [INF: name by elimination] | `(-1/P00, -1/P11, 0, 0)` of the main camera (P at camera+0x90) |
| +0x290 | **Shadow_SlicesDist** | `(d0, d1, d2, w)`, `w = sun_shadow_opacity` if +0x431 else `mgr+0x470` |
| +0x2a0 | Shadow_RTSizes01 | `(1/s0, 1/s0, 1/s1, 1/s1)` |
| +0x2b0 | Shadow_RTSizes23 | `(1/s2, 1/s2, 1/s3, 1/s3)` |

   Only TD_Hair, TD_TallGrass and TD_STICKERMAP read WorldToLight/WorldToUVMatrix themselves (forward
   lookups); every other scene shader uses the screen buffer s15.R (scan: `/tmp/claude-0/rs/shadow_usage.txt`).
5. FS-pass constants (for every FSShadowMat j and slice i, loop at 0x979c00): `UVMatrix[2i..2i+1]` (8 floats)
   and `ViewToLight[i]` (4 floats) = the world→shadow rows × InvView (view → shadow) [CODE: products via
   FUN_00ddb740]; `*FadeAlphaFactor = max(0, mgr+0x1e4)`; with the hardware mode also `SliceUvOffset`.

## B.5 Full-screen pass `RT SHADOW 2D` (edrender_pfx_shadow, GUID e674a83899eaeb37) [CODE+SHADER]
Materials (FUN_00977e80): `FSShadowMat[i]` at `mgr+0xd54+0xc·i` with spec[36+i] = 1 (slice), spec[40] = 3,
spec[47] = `DAT_011c9eb9`, spec[46] = 0 at creation (1/2 = depth from INTZ/RAWZ with `InvProj`); states
0x30 = 3, 0x31 = 2 (no Z write), 0x33 = 3, **0x38 = 2, 0x39 = 2, 0x3a = 4 (ONE, ONE, MIN), 0x3b = 0xe (R only)**,
0x35 = 1, 0x41 = 1, stencil bytes 0x50..0x53 = 0,0,0,2, 0x54 = 1, 0x58 = 0x5c = 0xff. Constant pointers:
+0xd90 VARS, +0xdb0 ViewToLight, +0xdc0 UVMatrix (4 parts of 0x20), +0xe10 SlicePhaseScale, +0xe20 SliceUvOffset,
+0xe00 InvProj, +0xda0 id 86b1989f85c5eff8 (unresolved name). `FSShadowMatStencil` (+0xd34 / +0xd40, two
copies with stencil 0x51..0x53 = 2,1,7 and 3,0,7, colour mask 0xf, ps_42 `oC0 = 1`) restrict the slices with
the stencil; the geometry they draw (`FSShadowQuad`?) and the stencil semantics are not resolved [unknown].

Item map (PS attrib, spec46 = 0, spec47 = 0): slice 0 → ps_36 (spec40 2,3) / ps_30 (spec40 1);
slice 1 → ps_24 (spec40 2) / ps_18 (spec40 1,3); slice 2 → ps_6 (spec40 1,2) / ps_12 (spec40 3);
car → ps_0 (all spec40). spec47 = 1 (hardware PCF, 4 taps at ±SliceUvOffset averaged ×0.25): ps_37/31, 25/19,
7/13, 1. spec46 = 1/2 (depth from sZ.r through InvProj): ps_38–41, 32–35, 26–29, 20–23, 8–11, 14–17, 2–5.
The 4 slice shaders only differ in the registers: ViewToLight[i], UVMatrix[2i..2i+1], SlicePhaseScale
component i, RTSizes01.xy / .zw / RTSizes23.xy / .zw, sampler s(1+i).

VS (vs_0): `vars = c60`; oT0.xy = screen uv, oT0.zw = NDC xy, oT5 = (-1/Proj.x, -1/Proj.y, -Proj.z, Proj.w)
terms, oT1.xy = `uv·vars.zw·0.5` (pixel/2).

PS slice i (ps_36 for i = 0; Var_PS c9 ViewToLight[4], c13 UVMatrix[8], c21 SlicePhaseScale, c22 SliceUvOffset,
c23 FadeAlphaFactor; Var_Scene_PS c2 SlicesDist, c4/c5 RTSizes; c7 StereoUvMadd; s0 sZ, s1..s4 sShadow0..3):
```
uvz   = oT0.xy * c7.xy + c7.zw
d     = decode(sZ.b, sZ.a)                       ; postfx_spec §6.2
Pv    = ( (d*(oT5.z - oT0.z) + oT5.w)*oT5.x , -d*oT5.y*oT0.w , -d , 1 )   ; view position
z     = dot(Pv, ViewToLight[i])
uv    = (dot(Pv, UVMatrix[2i]), dot(Pv, UVMatrix[2i+1])) + (frac(oT1.xy) - 0.5) * SlicePhaseScale[i]
t00 = sShadow_i(uv), t10 = (uv + (r,0)), t01 = (uv + (0,r')), t11 = (uv + (r,r'))     ; r = RTSizes 1/size
occ_k = (t_k*0.5 + 0.5 < z) && (z < 0.999 || t_k < 0.998)
lit_k = 1 - occ_k
f   = frac(uv / (r, r'))
lit = lerp( lerp(lit00, lit10, f.x), lerp(lit01, lit11, f.x), f.y )
out = 1 + SlicesDist.w * (lit - 1)                    ; car slice: 1 + SlicesDist.w*FadeAlphaFactor*(lit-1)
```
`SlicePhaseScale` is set from `mgr+0xe10` (value written elsewhere; not decoded [unknown]). The result is MIN-blended
into R of the RT set +0x163e0, initialised to 1 where nothing is drawn [INF].

## B.6 sun_shadow_opacity [CODE]
FUN_00978790 at 0x9797b5: `SlicesDist.w = (mgr+0x431) ? weather(sun_shadow_opacity) : mgr+0x470`; it scales
every slice pass (B.5), so opacity 0 (Cin_Gen) turns shadows off and 1.0 (SS_Death_Valley) gives full shadows.
Other readers of the id: 0xc51f14, 0xdbbf17, 0xdbbf31, 0xdbcc48 (avatar / other renderers, not analysed).

## B.7 The s15 buffer read by scene shaders [SHADER]
TD_Road ps_0/ps_2 (and the other shadow-receiving scene PS) sample `gTexSamplerShadowRealTime` at
`vPos·RenderTargetSize`: R = sun visibility (multiplies N·L and the specular dots), (G,B) decoded with the
depth-style log format and ×32 added to the lighting (headlight term [INF]), A multiplies the whole lit colour
and the reflection/specular. FullscreenDepthTarget holds RG = motion and BA = depth (TD_Road ps_100 writes
oC0.xy motion, .z/.w depth), so s15 cannot be that target; the RT of set +0x163e0 (RT SHADOW 2D +
HEADLIGHTS BLIT) is a separate W×H buffer [INF]. For OpenTDU2: R = min over the shadow passes (1 if
shadows are off), G = B = 0 (no headlights), A = 1 unless the contact-shadow boxes write it [INF].

---------------------------------------------------------------------------------------------------

## Unknowns
1. Active shadow config name on the press-start screen ([DAT_00fd50fc+0x28], ENC); LOGIN assumed.
2. Exact element layout of the world→shadow matrices (FUN_0041d9e0 copy is encrypted, matrix helpers
   partially traced); bias placement in WorldToLight.w (stack offsets).
3. SlicePhaseScale values; stencil geometry/semantics of FSShadowMatStencil; where RT set +0x163e0 is created.
4. REFLECT 2D mirror camera / clip plane / object list (not needed for TD_Road).
5. Creation of the env cube RTs 0x011c9f00/04 (ENC); cuberain.2db use.
6. Meaning of the slot filter bytes set by FUN_00d72bc0 (aniso) and of the "apor" object loop in FUN_00aa0a30.

## File index
| file | content |
|---|---|
| rain_shadow_dec1.c | weather registration FUN_00af30d0, renderer setup FUN_00d7a0e0, FSShadowMatStencil FUN_00977b50, FSShadowMat FUN_00977e80, layer table FUN_00d73170, fx.ini parser FUN_00d6b5b0, shadow RTs FUN_00c32380/FUN_00c32880, lib list FUN_00d71e60, droplets FUN_00aa2c00 |
| rain_shadow_dec2.c | FUN_008c6880 (tyre marks/GI), FUN_00c559f0 (wheel FX/wet), FUN_00aa6ce0 (weather → WFX), shadow frame FUN_00978790, droplets on camera FUN_00aa2100, REFLECTIONMAP2D FUN_00c32a00, dome reflect FUN_00c38340 |
| rain_shadow_dec3.c | WetGrd: FUN_00aa0780, FUN_00aa0890, FUN_00aa0990, FUN_00aa0a30, vtable methods, ctor FUN_00aa06e0, rain setter FUN_00aa61a0 |
| rain_shadow_dec4.c | ripple frame FUN_00aa0440, sector gather FUN_00aa05a0, slot aniso FUN_00d72bc0, bank lookup FUN_00aa6560 |
| rain_shadow_dec5.c | shadow config handlers, FUN_009739d0, split FUN_00973890, light camera FUN_00976300, caster submit FUN_00978530, env cube binders FUN_00d57060/FUN_00d58df0/FUN_00cbdf90 |
| rain_shadow_dec6.c | config setters FUN_00974630/9745b0/9744d0/974540 |
| rain_shadow_dec7_asm.txt | raw asm of FUN_00976300, FUN_00978790, FUN_00aa6ce0, FUN_00aa0440, FUN_00aa0660, FUN_00aa0890, FUN_00aa0b60 |
| rs/road_items.txt | TD_Road items resolved to spec switches (hash solver) |
| rs/d_pfx_shadow.txt | edrender_pfx_shadow items/specs |
| rs/shadow_usage.txt | which shaders really read Shadow_* constants |
| rs/cpr/Shadows.dec, rs/cpr/qs.txt | decrypted Shadows.cpr / Quality_Settings.cpr |
| cw.txt | dump of cinematic_weathers.xmb |
