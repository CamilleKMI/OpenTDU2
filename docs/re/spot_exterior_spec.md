# Spot exteriors (houses, dealers, shops, hotels, casino): per-bank resources — spec

Tags: **[CODE]** readable x86 (>= 0x6c1000) · **[DATA]** game files · **[SHADER]** .sob bytecode ·
**[INF]** inferred (reason given) · **[ENC]** behind SecuROM, not read.

Scope: `islands/<island>/exterior/<spot bank>.bnk` drawn by the World Viewer (`SpotExterior`) and by FREERIDE
(`FirstOutro`, the house of C_FIRST_OUTRO). User report: Casino (ECAS) pool drawn white, facade neons static.

## 1. Original spot exterior setup — FUN_00981b60 [CODE]

Called once the exterior bank of a spot is loaded (`param_1 + 0xc` = the bank's level object):

| step | function | effect |
|---|---|---|
| 1 | FUN_00b9a950, FUN_00b9f160 | builds the bank scene, links its nodes to the spot |
| 2 | FUN_00980f30 / FUN_00980d30 | per-spot node setup; halos (GUID c913741cbe17490d) node flags (pool_interior_spec 5.2) |
| 3 | FUN_00981870 | collects the NIGHTLMAP materials: TD_Building (`0x183157b81c6cff57`) with spec[0x25] (U6) == 1, at most 63, plus GUID `fcdaa96bdeadbeef` sprites (param `0xa58487f1_97909ca3`), at most 127 |
| 4 | FUN_00980e50(weather value) | writes `spot_exterior_lightmap_day_night` (table key `0x3e4b4320_354b3b3b`) |
| 5 | **FUN_00d7e950(&GSReflection 0x011f8ca0, bank)** | **always** called, for every spot exterior (no bank name test here) |

FUN_00d7e950 (pool_interior_spec 2.4): FUN_00d7df50 registers every TD_Water (U4 -> slot 4) and TD_Furnitures (U7 ->
slot 6) material of the bank, clearing U4 / slot 4 for TD_Water. Then **only when GSReflection is enabled for the game
mode** (`mgr+0x26`, table 0x00f817e0) it builds the projectors of the `Entity__Actor__Mesh` (class 'MESH') scene
D_REFLECT and re-enables the slot (FUN_00d7de80(1)).

Consequence per mode [CODE table 0x00f817e0]:
- FREERIDE (1, the free roaming mode): disabled -> TD_Water uses its U4 = 0 permutations (ps 12/13, no s4
  refraction). The pool is still textured (nuancier COLOR + caustic normals + reflection), not white.
- SPOT_VIEWER, LIVING, CASINO, SHOP, ... (enabled): the D_REFLECT `REFRACT` node becomes a refraction projector and
  the water samples the REFLECT render target in s4 (same as the intro pool).

`ecas` [DATA]: D_REFLECT = `ecas_59E1C536C6C6FAD9` (materials `Pool`, `Spots_01`), nodes `D_REFLECT` (no geometry ->
a mirror projector) and `REFRACT` (refraction projector, plane y = 1.73 in the spot frame). The water is the `caustic`
material (TD_Water, spec U4 = 1) of the main scenery `ecas_sc_DB42CB20E8099A8F`.

## 2. Texture coordinate animations (.UVA) — the binding is per texture slot [DATA + SHADER]

The previous OpenTDU2 rule (`<material>[_<SlotName>]_<k>.UVA` -> `__UVA_k`, k from the file name) is wrong: the
number at the end of the file name is not the slot.

2DM layout (offsets are file offsets, relocated through `REAL`):
- MAT. texture slot (0x20 bytes at MAT. payload +0x100 + i * 0x20): `+0x1c u32` = offset of an `MTPA/MTP.` payload,
  0 = no animation. The REAL list of `ecas_sc_DB42…2DM` contains 0x3bc = `Neons_Curve` slot 0 +0x1c.
- MTP. payload (0x20): `u32 1`, `u32[4] 0`, `+0x14 u32` = offset of a `UVAA/UVA.` payload, `u32[2] 0`.
- UVA. payload: `u64 id`, `u64 0` (runtime pointer). The id equals the ANM! header id (+0x10) of one `.UVA` file of
  the bank.

| bank | material (shader) | slot i (type) | .UVA file | spec[8] UV matrix mask |
|---|---|---|---|---|
| ecas | Neons_Curve (TD_NEONDECAL) | 0 NEONGLOW | `Neons_Curve_4` | 1 |
| ecas | Neons_Curve_01 (TD_NEONDECAL) | 0 NEONGLOW | `Neons_Curve_01_3` | 1 |
| ecas | Neons_Normal (TD_NEONDECAL) | 0 NEONGLOW | `Neons_Normal_2` | 1 |
| ecas | caustic (TD_Water) | 0 NORMAL0 / 1 NORMAL1 | `caustic_0` / `caustic_Normal1_1` | 3 |
| eph_l3di | caustic (TD_Water) | 0 / 1 | `caustic_0` / `caustic_Normal1_1` | 3 |
| e_hotela | WaterF (TD_Building) | 0 COLOR / 1 DETAIL | `WaterF_2` / `WaterF_Detail_3` | 3 |
| e_hotela | caustic_03 (TD_Water) | 0 / 1 | `caustic_03_0` / `caustic_03_Normal1_1` | 3 |

Every UVAA id of these banks is referenced by exactly one slot (none unreferenced) [DATA, ecd_ferr / eas_a have none].

Slot i drives `__UVA_i` [DATA + SHADER]:
- The material spec[8] (UV matrix mask) has bit i set exactly for the animated slots i (1 for the neons, 3 for
  caustic / WaterF). The shader permutations select the `__UVA_0` / `__UVA_1` dyn constants (sob dyn 0x0e / 0x0f)
  from that mask (TD_NEONDECAL attrib 3: `dyn[19 0e]`; TD_Water vs 46: `dyn[0e 0f]`).
- TD_NEONDECAL vs 3: `oT1.xy = (u, v, 1) . c65.xyw / c66.xyw` = `__UVA_0`.
- So with the file-name rule the neons got `__UVA_4` (dropped), `__UVA_3`, `__UVA_2` (not read by the shader) and the
  hotel waterfall `__UVA_2` / `__UVA_3`: all static.
- The routine that copies the slot animation into `__UVA_i` was not located in readable code; the mapping above is
  [DATA + SHADER], consistent on all 7 cases.

Animations [DATA]: Neons_Curve / Neons_Curve_01: U offset 0 -> 0.227 -> 0 over 1.6 s (15 keys); Neons_Normal: U 0 -> 1
over 4 s; caustic: U 0 -> 1 over 10 s.

## 3. Neons: WeatherEmissive from the weather [CODE]

The ECAS facade neons (TD_NEONDECAL `Neons_Curve`, `Neons_Curve_01`, `Neons_Normal`, `neon`, `SPOT_halo`...) were not
only static: they were **invisible**. TD_NEONDECAL vs: `oD1.xyz = c69 (WeatherEmissive) * c64 (__Material_Emissive)
[* vertex colour]`, ps 5: `oC0 = tex(s0, uvAnim).x * c9 * oD1`, blend SRCALPHA / ONE [SHADER]. The 2DM default of
`WeatherEmissive` (param 0x45979b91a7948ea4 = fold of the name) is (0, 0, 0, 0) [DATA], so nothing is added until the
game writes it:

| function | role |
|---|---|
| FUN_00985e40(mgr, bank/scene) | for every material: TD_NEONDECAL `63078165ca37416d` or `18315cacac6cff57` (any switches), or `fcdaa96bdeadbeef` with spec[0x23] != 0 -> inserted in the neon set mgr+0x1bc4 (FUN_0041e1e0). The same GUIDs with spec[0x23] == 0 (sprite: != 0) also get an `AnimValue` (0x554c41564d494e86) animation entry in mgr+0x1bb0 (blinking; random start [INF]) — none of the ECAS neons (spec[0x23] = 1) |
| callers | FUN_00960c30 (scene node method, nodes with flag 8 = geometry), FUN_009865c0, FUN_00986e90 (streamed world objects), FUN_00988d10 (instance banks, after FUN_00982260 = "bank has a neon material") |
| FUN_009875e0 | per-frame world update, ends with FUN_009834d0 |
| FUN_009834d0 | x = weather value `0x9ba1f0e6e2dce8f3` (= fold of **`building_neon_strength`**, game_weathers.xmb) +0x18 (.x); FUN_009829d0(x) for every material of the set, then the AnimValue updates (FUN_009821a0) |
| FUN_009829d0 | param `0x97909ca3a58487f1` (name not found) .xy = (x, 0); `WeatherEmissive` = (x, x, x, 0) |

Spot exterior nodes are world objects of the same manager (FUN_00981b60 appends them to mgr+0x21b74, which
FUN_009875e0 updates) [CODE]; that they reach FUN_00960c30 is [INF] (vtable call path not followed).

## 4. OpenTDU2 implementation

- `Render2DM` resolves slot +0x1c -> MTP. -> UVA. id (`MaterialTextureSlot::UVAnimationId`, magic checks on both
  sections); `EdenRenderer::setupMaterial` copies slots 0..3 to `EdenMaterial::UVAnimationIds`.
- `EdenModel::bindUVAnimations(bank)`: slot k with an id -> the bank .UVA of that id -> `__UVA_k`. Generic for every
  caller (sectors, areas, intro, FirstOutro, SpotExterior, mission props). A UVAA id that no slot of the 2DM
  references falls back to the file name rule (logged; not seen in the data).
- `SpotExterior` (World Viewer): every model goes through `registerProjectiveMaterials` (FUN_00d7df50); the
  Entity__Actor__Mesh scenes register their projectors once per spot frame (`registerReflections`, after the frames
  are known); unloading a streamed exterior removes its materials / projectors (`EdenSceneView::
  removePlanarReflectionModel`).
- World Viewer: GSReflection off like FREERIDE (mode 1); `-gsreflection` turns it on (value of the SPOT_VIEWER,
  LIVING, CASINO... modes) for comparisons.
- FREERIDE `FirstOutro`: `registerProjectiveMaterials` for every house model (GSReflection off in mode 1).
- Neon set: `EdenSceneView::registerNeonMaterials` (same list as the NIGHTLMAP materials, cleared together),
  written every frame in `updateLightmapMaterials` from `building_neon_strength`. Registered for SpotExterior,
  FirstOutro, IntroCinematic and CinematicProps models, with the AnimValue blink states of their blinking materials.
  World sector / area / .pmi instance neons and the blink: see `neon_spec.md` (`18315cacac6cff57` is the ShaderSet
  TD_pubneon).

## 5. ECAS tower panels white by day: an unresolved REFLECTION cube, not `Ranking_screen_`

Identification [OpenTDU2 test]: hiding materials by name, the white tower faces are the TD_Building material
**`windows_05`** of `ecas_sc_DB42CB20E8099A8F` (one primitive, 96 indices, bank-frame bounds x -25.5..26.6,
y 16.4..35.9, z 82.3..97.9 = the glass of both towers). `Ranking_screen_` is a different primitive: 108 indices,
bounds x -1.0..2.2, y 0.6..1.1, z -23.4..-22.9 (a 3 m x 0.5 m strip at the entrance) and was never white.

`windows_05` [DATA]: COLOR `fake_c` (64^2 DXT1, pure white), DETAIL `glass_ecas_d`, GLOSS `glass_ecas_g`, NORMAL
`glass_ecas_n`, slot 8 `glass_ecas_night`, slot 3 REFLECTION hash `0050414d45425543` = **"CUBEMAP"**. ecas.bnk has
no texture of that name (only `cubemap_ecas`); the only one on the island is **elandca.bnk entry 41,
`spots\casino\ecas\maps\cubemap.2db`** (256^2 DXT1 cube, 6 faces): an ECAS map packed in the ELANDCA bank, the
exterior of the neighbouring spot (ELANDCA, 178 m away) [DATA].

Game side:
- Texture references of a material slot are resolved by hashcode (2DM slot +0x08) into the runtime pointer of the
  slot (+0x10; MAT. payload +0x110 + i * 0x20, written by FUN_00b45560 / FUN_00c63c10 / FUN_00d56df0) [CODE]; the
  resolution of the 2DM references at load time is [ENC] (not found in readable code).
- The runtime render target named "CUBEMAP" (FUN_00c32b30: `FUN_008b73b0(size, size, ..., "CUBEMAP")`, the EnvMap
  service cube) is not bound by name: the car code replaces the slots whose texture hash is the default car cube
  explicitly (FUN_00d58a90 / FUN_00d56df0, car_glass_spec.md) [CODE]. So a building slot keeps the static
  `cubemap.2db` [INF from the explicit car replacement].
- OpenTDU2 resolved the slot when ECAS was set up, before ELANDCA was loaded: `nullptr` -> white cube fallback ->
  the TD_Building reflection term (MATPARAM.x 0.5) over a white COLOR = white glass.

`Ranking_screen_` (for the record) [DATA + CODE]: material fold `0x8fa69c8e90a084d6` = GetIdentifier64bit
("Ranking_screen_01") (the 2DM name field keeps 15 characters); COLOR `dark_grey`, DETAIL `occlu_white`, REFLECTION
`mtx_icas_hal`, DETAIL2 `nuancier_roulette`; the same material is in `icashal.bnk` (`flashscreen_ranking_*.2DM`),
where `Entity__Actor__CasinoFlashScreen__CasinoFlashScreenRanking` actors (Movie "Cas_Ranking", Material "",
BitmapId 1, RenderTargetId 1) render the flash movie `frontend/*/cas_ranking.bnk` into a render target
("SCFlashScreen_%d", FUN_00de6000: width x 512, + depth "SCFlashScreenDB") and FUN_00b45560 writes it into texture
slot `BitmapId` of the material named `Material` of **the actor's own model** (first material when the name is
empty; MAT +0x38 = 5 (SrcBlend SRCALPHA) and +0x49 = 2 when the actor field +0x220, read from the entity by
FUN_00b45cd0 through an [ENC] reader, is > 0) [CODE]. ecas.bnk has no such actor: its `Ranking_screen_`
keeps its 2DM textures (a dark grey strip); no flash RTT is involved on the exterior.

## 6. Particle effects (fountains)

`Entity__Fx` entries of the bank (ECAS: 6 fountains `V_Fx_Fountain_*`, AutoStart 1): see `particles_spec.md`.
Drawn by `EdenParticleSet` for every spot exterior (World Viewer). Banks with Entity__Fx [DATA]: ecas (casino),
e_hotela (Ibiza hotel, 20 emitters: single splash sprites of the fountains, soft particles; the white water rings
are the TD_Building `WaterF` mesh, not particles), eph_l3a (Hawaii house: butterflies, flies).
FREERIDE's house (eph_l3di, FirstOutro) has none.

## 7. OpenTDU2 implementation (sections 5, 6)

- `EdenRenderer::resolveMissingTextures` / `SpotExterior::resolveForeignTextures`: after every exterior load or
  unload, the World Viewer looks up again (all loaded banks, by hashcode) the slots whose texture was missing when
  the material was set up; the slots resolved this way are cleared before any exterior is unloaded (the owner may
  go) and resolved again. ECAS: 1 slot (windows_05 REFLECTION <- elandca CUBEMAP). The load-order independence is
  [INF] (the game's resolver is [ENC]).
- `SpotExterior::startEffects` (after the frames are set) / `update` / `draw`: `EdenParticleSet`; the sound loops
  of the same Entity__Fx (`startSounds`, 3D voices at the entity positions, `sound3d_spec.md` 4.6).

Before / after (World Viewer, casino, ECAS, weather 14): `scratchpad/casino2/base0b.png`, `base3.png` (before:
white tower glass, no fountains) -> `after_t0.png` (glass), `after_f1b.png`, `after_f3b.png` (glass + fountains).

Not done / open:
- The interior flash screens (CasinoFlashScreenRanking, `Cas_Ranking` movie RTT) are not implemented (no interior
  is drawn by the World Viewer; the exterior has none).
- Fountain sound loops: done, `sound3d_spec.md` 4.6 (`SpotExterior::startSounds`).
- Hotel water sheets (soft particles, gZpass): done, `particles_spec.md` 6.
