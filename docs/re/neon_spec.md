# World neons: registration and blinking (AnimValue) — spec

Tags: **[CODE]** readable x86 (>= 0x6c1000) · **[DATA]** game files · **[SHADER]** .sob bytecode ·
**[INF]** inferred (reason given) · **[ENC]** behind SecuROM, not read.

Follows `spot_exterior_spec.md` §3 (neon set, WeatherEmissive from `building_neon_strength`). This note covers the
world (sector, area, .pmi instance, actor) neons and the blink animation.

## 1. Registration FUN_00985e40(mgr, model instance) [CODE]

Loop over the materials of the model instance (`[instance+0xc]` material list, entry `+0x18 + 0x10 i`, material
data at `+0x10`). `U4` = byte `MAT. +0x333` = spec[0x23].

| condition (shader GUID at +0x300) | neon set (mgr+0x1bc4, FUN_0041e1e0) | blink entry (mgr+0x1bb0) |
|---|---|---|
| TD_NEONDECAL `63078165ca37416d`, U4 == 0 | yes | yes |
| TD_NEONDECAL, U4 != 0 | yes | no |
| TD_pubneon `18315cacac6cff57`, U4 == 0 | yes | yes |
| TD_pubneon, U4 != 0 | yes | no |
| sprite `fcdaa96bdeadbeef`, U4 != 0 | yes | yes |
| sprite, U4 == 0 | no | no |

(`spot_exterior_spec.md` called `18315cacac6cff57` "NEONDECAL 2": it is the ShaderSet `TD_pubneon`, td_pubneon.sob
[DATA].)

Blink entry (0x20 bytes, 0x985ef6..0x986098):

| offset | value |
|---|---|
| +0x00 | parameter block: `alloc(8, 0x10)` then FUN_008ad580(block, shader GUID, name list 0xf2c7c8 = { `AnimValue` 0x554c41564d494e86 }, 1, 0) |
| +0x04 | speed = (r1 · 0.7 + 0.3) · 3.0 (doubles 0xf49eb8, 0xf4a178, 0xf49a50) → 0.9 … 3.0 |
| +0x08 | phase = r2 · 4.0 (double 0xf49a68) → 0 … 4 |
| +0x0c | pointer to the block's AnimValue (FUN_0041f710 / FUN_0041f830) |
| +0x10 | model instance |
| +0x18 | material hashcode |

- seed = block pointer · 0x19660d + 0x3c6ef35f; r1, r2 = FUN_005abe00(&seed) twice. FUN_005abe00 is **[ENC]**. The
  readable code uses the same LCG inline (0xc8f8af, 0xca0426, 0xdf54a9…): seed = seed · 0x19660d + 0x3c6ef35f,
  value = float(seed & 0x7fffff | 0x3f800000) − 1 ∈ [0, 1). OpenTDU2 takes FUN_005abe00 as that generator **[INF]**.
  The seed comes from a heap pointer: the original phases differ from run to run, OpenTDU2 seeds from a stable key.
- Initial AnimValue = one-hot vector: component floor(phase) (CRT `floor` + `_ftol` FUN_006c8f00) = 1.0
  (0xedf668), others 0.
- FUN_00448310(material hashcode, block, model instance) **[ENC]** attaches the block: from the arguments, an
  override of the material's AnimValue for that model instance **[INF]**.

## 2. Callers (which model instances) [CODE]

| caller | model instance |
|---|---|
| FUN_00960c30 (world object with flag 8 (`+0x88`), after FUN_00960c70 appended it to the active list) | `+0xf4`, else each element of the list `+0xf8` (FUN_009865c0) |
| FUN_00986e90 (streamed world objects of mgr+0x205d0, once loaded) | same `+0xf4` / `+0xf8` |
| FUN_00988d10 (static .pmi lists V/O/S), list kind 1 (O) only (call 0x98948b), per record | `[record object]`: a model instance taken per record from a pool (FUN_008b9100 → FUN_00da06e0 / FUN_00da0440), only when FUN_00982260 = "has a neon set material" |
| FUN_00988730 (`SPECIFIC` blocks, 0xf2c7b0) | same per object |
| FUN_00981b60 → spot exteriors | see spot_exterior_spec |

So every .pmi instance gets its **own** blink state per blinking material (random speed and phase), even though the
library materials are shared.

## 3. Per-frame update FUN_009834d0(mgr, dt) [CODE]

Called at the end of FUN_009875e0(mgr, dt, …); the caller FUN_00991570 adds the same `dt` to a running clock
(`+0x19c`), so it is the frame time.

1. x = weather value `0x9ba1f0e6e2dce8f3` (`building_neon_strength`) `+0x18`; FUN_009829d0(x) for each material of
   the neon set: `WeatherEmissive` = (x, x, x, 0), param `0x97909ca3a58487f1`.xy = (x, 0) (only if present).
2. FUN_009821a0(dt) for each blink entry (independent of the weather):
   `phase = (float) fmod((float)(speed · dt + phase), 4.0)` (`_CIfmod`), stored at +8;
   AnimValue = one-hot(floor(phase)).

The state changes 0.9 … 3 times per second and loops through the 4 components.

## 4. Shaders [SHADER]

- TD_NEONDECAL ps 3 / 4 / 10 / 11 (U4 = 0 permutations): `r = dp4(tex(gGlowTexSampler), AnimValue)`,
  `oC0.rgb = r · rgbExposure_EmissiveChannel · oD0 [· base colour]`; oD0 = WeatherEmissive · __Material_Emissive
  (vs). Other permutations use the glow `.x` only (no AnimValue).
- TD_pubneon ps 4/5, 12/13, 20/21… (U4 = 0): glow (s2, 2nd UV set) `dp4` AnimValue · rgbExposure_EmissiveChannel ·
  colour · (WeatherEmissive · __Material_Emissive) added to the lit colour.
- So the 4 channels of the NEONGLOW texture are 4 states of the sign. `advertising_a_neon.2db` (512², DXT5,
  commonworld) [DATA]: the channels are nearly identical; some glyphs differ (e.g. `CAFE` / `C` / `A` / `F`, the
  pharmacy cross only in one channel, letters missing in `IBIZA BAR`, `SNACK`, `Mikool`): letter chasers and
  flickering on the shop signs.
- The TD_pubneon materials have **no AnimValue** in their 2DM [DATA library.2dm `SF_Sign_Neon`]: without the
  registration the glow term is 0.

## 5. Data (Ibiza) [DATA]

Byte search of the shader GUIDs in `islands/ibiza/level`: 71 banks hold neon materials (commonworld + 70 sectors).
Ibiza town (area 3-3, sectors 4-5 … 6-7):

| material | bank | shader | U4 | set | blink |
|---|---|---|---|---|---|
| SF_Sign_Neon, SF_Sign_Cine_Ne | library.2dm | TD_pubneon | 0 | yes | **yes** |
| SF_AdPnl_Neon(_A/_P), SF_AdPnl_Pacha_, SF_SpecT_Neon | library / sector -o | TD_pubneon | 1 | yes | no |
| SF_Fount_water_, SB_ChOld_*, FU_VBoat_*, FX_SHAFT_04, 3367_Wall_Neon | library / sector -o | TD_NEONDECAL | 1 | yes | no |
| FX_SHAFT, FX_LIGHT, SB_chrch_Bell | library.2dm | sprite | 0 | no | no |

The shop signs are .pmi O instances (library nodes `SIG_A…`, `SIG_B…`, `SIG_C…`, `CINE_…`): 469 blinking
instances in the sectors loaded around (21900, 23560) (3-3-4-5 … 3-3-6-6). `building_neon_strength` is 1.0 at night (00 … 05.46), 0.0 from 06 (game_weathers,
intemperies 1 … 6; intempery 0 has no value).

## 6. OpenTDU2 implementation

- `render/eden/eden_neons.*`: the material tests, FUN_009829d0 (`ApplyStrength`), the blink state
  (`Blink::initialize` = FUN_00985e40 random part, `Blink::update` = FUN_009821a0).
- `EdenWorld`: sector and area -o scenes register at load (one model instance each [INF]: the world objects behind
  FUN_00986e90 were not mapped to the bank nodes); the library and actor materials form a shared neon set;
  `loadInstances` creates one blink state per instance and blinking material of the instanced library node (or actor
  model) — the materials drawn by the node [INF]. `updateNeons(weather, dt)` (called by `EdenSceneView::updateNeons`
  every frame) writes WeatherEmissive and advances every blink state.
- Per instance AnimValue: `EdenDrawParams::MaterialVectors` (material, parameter, value), used by `fillConstants` in
  place of the material parameter for that material only (the FUN_00448310 override).
- `EdenSceneView::registerNeonMaterials` (spot exteriors, FirstOutro, intro, mission props) also creates the blink
  states of its models.
- `-noworldneons`: no world registration (comparisons). `OTDU_NEON_LOG=1`: logs every blinking instance.

Not done / open:
- The exact random values (heap pointer seed, FUN_005abe00 [ENC]).
- S.pmi (guard rails, skinned, see pmi_spec.md 1.5) are drawn and get the same per instance blink registration as the library models (their TD_Ground materials do not blink); F.pmi is empty.
