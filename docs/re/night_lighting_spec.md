# Night lighting of the world: street light maps, night windows, lamps — spec

Tags: **[CODE]** readable x86 (>= 0x6c1000) · **[DATA]** game files · **[SHADER]** .sob bytecode ·
**[INF]** inferred (reason given) · **[ENC]** behind SecuROM, not read.

Related: `neon_spec.md` (shop signs, `building_neon_strength`), `spot_exterior_spec.md` / `pool_interior_spec.md`
§5.2 (NIGHTLMAP day / night lightmaps of the spot exteriors and interiors), `weather_spec.md` (time of day).

## 1. Night weather values [DATA]

`game_weathers.xmb` (same value in every intempery of a key): `outside_street_lightmap` 0 by day, **1.5** at night;
`building_nightwindow_frequency` 0 / **1.0**; `building_neon_strength` 0 / 1.0; `spot_exterior_lightmap_day_night`
(1, 0) / (0, 1); `building_shop_front_back_lighting` (1.2, 0.5) (key "00"); `outside_lightbeam_intensity`,
`impostor_ambient` (other consumers, §5).

## 2. Frame user vectors (Var_Frame_VS c0..c7) [CODE + SHADER]

`FUN_00d769e0` (per frame scene setup from the weather, 0xd76ce6..0xd76d70):
```
UserVector1 (DAT_00ffb5f0) = ( 1.0 (DAT_00edf668), outside_street_lightmap, 1.0, building_nightwindow_frequency )
UserVector2 (DAT_00ffb600) = ( building_shop_front_back_lighting.x, .y, 0, 0 )
```
(init FUN_00d79140..0xd79222: x = w = 1, y = z = 0). The mapping DAT_00ffb5e0 + 0x10·k = UserVector k follows from
the shaders below (c1.y = street light map, c1.w = night windows) [SHADER]; no other readable writer of 0x00ffb5f0.
FUN_009875e0 also reads `min(UV1.x + UV1.y, 1)` (= 1) into the colour (+0x80..+0x8c) of the world light-model list
(+0x28b8) [CODE].

Consumers (scan of every VS that declares Var_Frame_VS, `scratchpad/rn/uv.py`):

| library | use |
|---|---|
| TD_HeightMap (sector / area terrain) | VS `oT1.xy = (c1.x, 2·c1.y)`; PS: `m = tex(g_Shadow_Snight_Grass_Sampler)` (sector `-s` map), `m.xy *= oT1.xy`; sun light `·m.x`, then **`out += m.y · albedo · (0.972, 0.839, 0.651)`** = the baked street-lamp light (G channel) × 2 × outside_street_lightmap |
| TD_Ground, TD_Road (sector ground objects, roads) | VS `oT2.zw = c1.xy` (same pair to the PS) |
| TD_Building (92 VS), TD_HardwareInstance (2) | VS `oD0.x = (c1.w >= 0.5) ? 1 : 0` → PS: night window term = `tex(s6, window uv) · lum(base) · c19.x · oD0.x` (fake interiors, slot 6) |
| TD_TallGrass | c1.xy only |

UserVector2 has no consumer in these libraries (VS scan) [unknown].

### 2.1 Which props get the street light map, and from which texture [CODE + SHADER + DATA]

TD_Ground (GUID b0f1234b0f123473) adds the lamp light of the **sector `-s` map G channel** to every object drawn with it:
vs `oT2.xy = (world.xz - sectrOff.xz) · (1/768)` (c64 / c66 = `__World` rows, c68 = material parameter `sectrOff`),
`oT2.zw = c1.xy`; ps_1 `S = tex(s2, t2.xy)` (slot 2 = SHADOW), `light = N·L lerp(c10, c11)`, out = `albedo · light +
albedo · (2 · S.g · c1.y) · (0.972, 0.839, 0.651)` (the `dp2add r0.w, r1.y, v1.w, 0` = 2 · S.g · outside_street_lightmap;
d3d9dis.py prints opcode 90 as "dsx") [SHADER TD_Ground vs_0 / ps_0 / ps_1]. So at night (c1.y = 1.5) a TD_Ground prop
is lit up to `3 · albedo` where its SHADOW texture has G = 1. The SHADOW slot is a **runtime slot**: in the library
materials its texture hash is 0 and the material handler 0x00d712c0 marks it as runtime (`slot 2 = ~0` when empty,
counted in S_NBTEXTURES) [CODE]. Who fills it:

- **Sector objects** (`sector-…-o`): `FUN_00982af0` (end of the sector setup FUN_00986760) walks the sector materials
  (+0x1a7c): TD_Road (c8ad93c95455291b) every SHADOW slot, TD_Ground and TD_Ground_HM slot 2 (+0x150, whatever its type:
  DETAIL1 in `5067tunnel_mesh` of sector-5-0-6-7-o, SHADOW elsewhere in the materials checked [DATA]) = the sector `-s`
  texture (sector resource +0x674), addressing (+0x158 / +0x159) = 3 / 3 (CLAMP); for TD_Ground / TD_Ground_HM
  `sectrOff = (sector x, 0, sector z, 0)` (sector +0x30 / +0x34 = the sector origin minus the origin shift
  DAT_01367a10, FUN_00986760) [CODE]. TD_Road samples the `-s` map with its own TEXCOORD1 (no sectrOff) [SHADER].
- **Library instances of the `.pmi` lists** (shared commonworld `library.2dm` materials, `sectrOff = (0, 0, 0, 768)`
  in the 2DM [DATA]): FUN_00982af0 also writes three override records `{ +0 TD_Ground guid, +8 3, +0x24 texture = the
  -s map, +0x2c / +0x2d addressing 2 / 2 }` at sector +0x11680 / +0x18dc0 / +0x20500 = **list + 0x7670** of the lists
  +0xa010 (kind 1, **O.pmi**), +0x11750 (kind 2, **S.pmi**) and +0x18e90 (FUN_00988730) (same record as the helper
  FUN_00987e80) [CODE]. The list kinds (+8) are set at 0x985168..0x9851cd: +0xa010 = 1, +0x11750 = 2 [CODE], +0x28d0 = ebx (0 = V, the
  register value is not traced [INF]). FUN_00987ec0 (called by the list loader FUN_00988d10, 0x988a63 / 0x98992b) gives every model instance with a
  TD_Ground material `+0xb8 = list + 0x7670`, flags `|= 0x10000`, and its instance parameter `sectrOff = (sector x
  (+0x30), 0, sector z (+0x34), 768.0 (DAT_00f4aa88))` [CODE]; FUN_00988070 moves sectrOff with the origin shift
  [CODE]. The V list record (+0x28d0 + 0x7670) is not written by readable code, but no V.pmi model uses TD_Ground
  (Ibiza sectors 5-0-5..7-6/7, 5-1-5..7-0, 3-3-3..5-5..7: V 0, O 51..1113, S 0..131 instances with a TD_Ground material, OTDU_GROUND_LOG)
  [DATA]. How the [ENC] submit (0x4dfa00 / 0x4dfa80) applies the record to the runtime slot is not readable: taken as
  "SHADOW of the TD_Ground materials = record texture, addressing MIRROR (D3DTADDRESS 2)" [INF].
- The library TD_Ground materials (commonworld library.2dm, 22): **V_Rocks, V_Rocks_Lod, V_RocksG, V_RocksG_Lod,
  R_Rocks, R_Rocks_Lod, R_Rocks_Falaise (x2)** (the rocks), **SF_Sets_Concret** (planter / kerb concrete),
  **Set_Grass**, **V_Ground (x2)**, **FU_Swim_Stone**, **SF_TLigh_Plan**, **SF_WLine / SF_WMark** (road marks),
  **SF_gliss / _Sk / _Low, SF_Gliso / _Sk / _Low** (guard rails, S.pmi) [DATA].
- The `-s` map is sector local: the `.lmp` lamp records (sector local x / z, §4) fall exactly on the G spots of the
  `-s` map with u = (x - sector x) / 768, v = (z - sector z) / 768 (sector 5-0-6-7: 118 lamps; there the records start
  at the offset of the u32 at file +0x2c = 0x2d8, the grid at the u32 at +0x28 = 0x30) [DATA].

Night lighting per prop family (what the game feeds, readable part):

| props | shader | night term |
|---|---|---|
| terrain (sector / area heightmap) | TD_HeightMap | `-s` G × 2 × outside_street_lightmap (own grid UV) |
| roads | TD_Road | `-s` map via TEXCOORD1, c1.xy |
| sector ground objects, pavements | TD_Ground (sector material) | `-s` G at the object's place (sectrOff = sector origin) |
| rocks, concrete sets, grass sets, road marks, guard rails (O / S `.pmi`) | TD_Ground (library) | `-s` G of **their sector** at their place (list override + instance sectrOff) |
| buildings (TD_Building), hardware instances | TD_Building / TD_HardwareInstance | night windows (c1.w), no street light map |
| vegetation (V `.pmi`), other families | TD_Vegetation… | no UserVector1 term |

## 3. Spot exterior lamps and sprites [CODE]

`FUN_00981b60` (spot exterior setup) → `FUN_00981870` collects, besides the NIGHTLMAP TD_Building materials, the
sprite materials (GUID fcdaa96bdeadbeef) that have **OcclusionDayNight** (0x97909ca3a58487f1; at most 127, list
+0x3ec), and `FUN_00980d30` the scene nodes drawn with **TD_Lamp** (c913741cbe17490d; at most 64, list +0x1e4,
node flags |= 0x30). Every frame `FUN_00980e50(x, y)` with (x, y) = `spot_exterior_lightmap_day_night`:
```
NIGHTLMAP (TD_Building U6)  = (x, y, 0, 0)
OcclusionDayNight.z (+8)    = 1.0 (DAT_00f44c70) - x            (sprites)
node colour .w (+0x8c)      = y,  rgb kept                       (TD_Lamp nodes)
```
TD_Lamp [SHADER] (vs_0): `colour = __Instance_Color.rgb · __Material_Emissive · __Instance_Color.w + c64`
(ambient), PS `oC0 = colour [· tex]`: the halo / bulb emissive is switched by y (0 by day, 1 at night).

## 4. Street lamps (streetlamps.xml, .lmp) [CODE + DATA]

- `islands/<island>/fx/streetlamps.xml` (`%sIslands/%s/FX/streetlamps.xml`, parser **FUN_00aef060**, called from
  the island object setup FUN_00aefe60): `<StreeLamps><Lamp name type color near far open_angle falloff_angle/>`;
  table of `max(type)+1` entries of 0x14 bytes at object +0x198 (count byte +0x19c), indexed by `type`:
  `{ RGBA bytes of "#rrggbbaa" (FUN_00aee100), near, far, f(open_angle · π/180), f(falloff_angle · π/180) }`
  (f = CRT thunk 0x6c8d32 [ENC], cos [INF: spot light cone]). Ibiza: 8 types (SF_LighA … SF_LighJ), colour
  f8d6a6 with intensity 0x32..0x57, near / far 6.5 … 25 / 8.5 … 35 m, open 25 … 32°, falloff 40 … 55°.
- Sector banks: `Sector-x-y-sx-sy.lmp` (bank type 4/49) "LAMPDATA": header 0x20 (version 1, size), u32 0x30 =
  offset of a 6 x 6 grid of 128 m cells (u32 `{u16 first, u8 count, 0xde}`), u32 0xc0 = offset of the lamp records
  (8 bytes: u16 x, u16 ?, u16 z, u8 **type** (0..7 = streetlamps.xml), u8 0xad); x / z in 768/65536 m of the sector
  (record 0 of sector-3-3-4-6: (224.4, 122.6) in cell 1) [DATA; the u16 at +2 (height?) not resolved]. 154 lamps in
  sector 3-3-4-6.
- No reader of the +0x198 table and no loader of the .lmp type was found in readable code [ENC / unknown]. The
  world itself is not lit by these lights: the street lamp light on the ground is baked (`-s` map G, §2). From the
  data (spot cones with near / far) they are dynamic lights for the moving objects (cars, avatars:
  `car_spotlight_interior_exterior_influence`, `avatar_spotlight_interior_exterior_influence`) [INF] —
  not implemented (no car in the World Viewer).

## 5. Other night values [CODE, partial]

- `outside_lightbeam_intensity` (0xe93d5131 / f3d8cbf1): read in FUN_009871c0 (sector .pmi cell draw) and multiplied
  by DAT_01367c0c (no readable writer), passed to the [ENC] instance submit functions (0x4dfa00 / 0x4dfa80): the
  TD_LightBeam light cones of the .pmi instances [INF]. Not implemented.
- `impostor_ambient` (0x919d8ea8 / a69d999c): FUN_00d769e0 → renderer +0x45c0 (TD_Impostor) and FUN_00981600.
  Not part of this work.

## 6. OpenTDU2 implementation

- `EdenSceneView::setupScene`: UserVectors[1] / [2] from the drawn weather (§2) — all scene views (World Viewer,
  FREERIDE, cinematics). With the FREERIDE weather "14" the values stay (1, 0, 1, 0): no change by day.
- `EdenSceneView::registerLightmapMaterials( model, Exterior )` also registers the OcclusionDayNight sprites
  (`ExteriorSprite`) and the TD_Lamp materials (`ExteriorLamp`) of the exterior models; `updateLightmapMaterials`
  writes `.z = 1 - x` and the TD_Lamp `__Instance_Color = (1, 1, 1, y)` (material stage constant: OpenTDU2 has one
  material per exterior model, the node colour rgb is taken as 1 [INF]).
- §2.1 (white rocks at night): the library TD_Ground materials of the O / S `.pmi` instances had no texture in their
  runtime SHADOW slot, so the renderer bound its default **white** texture: G = 1 → `+ 3 · albedo · (0.97, 0.84, 0.65)`
  at night (rocks, planter rims = SF_Sets_Concret), and R = 1 (no baked sun shadow) by day. `-noprojector`, `-carlights
  0`, `-nonightlmap` did not change it (not the car lights nor NIGHTLMAP). Now (`EdenWorld::loadSector`, `drawInstances`):
  per sector `GroundInstanceParams[list]` = `sectrOff (x, 0, z, 768)` for V / O / S and, for O / S, an
  `EdenDrawParams::TextureOverride` { TD_Ground, SHADOW, the sector `-s` map, linear mirror } applied by
  `EdenRenderer::recordDraw`; only for the instances whose library node (LOD 0..2) has a TD_Ground material.
  The sector objects get `sectrOff` = sector origin and CLAMP on the `-s` slot (TD_Ground / TD_Ground_HM slot 2,
  TD_Road SHADOW slots): before, with the 2DM `sectrOff` 0, the `-s` lookup of the TD_Ground sector objects was half a
  sector off on the odd sector rows (lamp pools in the wrong place, e.g. the spawn EPH_L3DI pavement unlit).
  `OTDU_GROUND_LOG=1` logs the TD_Ground instances per list.
- Already present before this work: NIGHTLMAP of spot exteriors / interiors, neon set + blink (`neon_spec.md`).
- Not done: §4 dynamic street lamp lights, §5 light beams, the car headlights.

Screenshots (World Viewer, Ibiza town 21962, 11, 23675, yaw -169, `-noexteriors`): before
`scratchpad/rainnight/base_night22.png` (22:00, no street light map, no windows), after `night22.png`.
Spot exterior ENC_PRIV (club, `-campos 19878,148,18002 -camyaw 131 -campitch -10 -sectorradius 0`): `encpriv_night.png`
(23:00, night lightmap, neons, lamps) / `encpriv_day.png` (14:00); 1 TD_Lamp material registered (`ENC_PRIV_PUB_01`).

Screenshots §2.1 (`scratchpad/nightprops/`, FREE_DRIVE 22:00 `-carpos 36336.1,5.38,5739.31 -carheading -74.8
-chaseorbit 120`, EPH_L3DI, sector 5-0-6-7): `before_spawn.png` (white rocks and planter rims), `after_spawn.png`
(rocks / rims at the night ambient; the pavement under the car is lit: the car (sector local 624, 363) stands 4 m
from the `.lmp` lamp (623.4, 359.3, type 2) whose pool the `-s` map now gives at the right place), `cmp_spawn_night.png`; day check
`after_spawnpos_day.png` (14:00); town `after_town22.png` / `after_town22_noext.png` (World Viewer 21962, 11, 23675,
yaw -169: windows, neons, lamp pools unchanged); tunnel `after2_tun180.png` (`-carpos 35973.8,6.0,6102.1 -carheading 7
-chaseorbit 180`).
