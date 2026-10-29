# Weather, time of day, sky, moon and clouds (GSWeather)

Sources: TestDrive2.exe readable code (VA >= 0x6c1000, Ghidra + `text.asm`), the island fx files
(`Euro/Bnk/islands/<island>/fx/`), `fx/weatherfx.bnk`, the shader libraries `td_dome.sob`, `td_moon.sob`,
`TD_Clouds.sob`, `TD_CloudMesh.sob`. Tags: [CODE] read in the code, [DATA] read in the files, [INF] inferred,
[ENC] in the SecuROM encrypted part (not read). Implementation: `source/render/eden/eden_weather.{h,cpp}`
(`WeatherLibrary`, `WeatherSystem`), `eden_scene_view.cpp` (clouds), `eden_sky.cpp` (moon),
`source/game/worldviewer/gm_world_viewer.cpp` (panel). Decompiles: scratchpad `gsweather.c` (0xaf0000-0xafe600),
`dome.c` (0x92e000-0x934200), `clock.c` (0xc1ed00-0xc1fb00).

## 1. Files of `islands/<island>/fx/` [DATA][CODE]

Loaded by the GSWeather init FUN_00afd4d0 (paths `%sIslands\%s\FX\...`), in this order, into the weather lists of
the service (singleton 0x0117a028):

| file | list (count, array) | content (Ibiza) | use |
|---|---|---|---|
| game_weathers.xmb | +0x4c0, +0x34 (6 intemperies) | 24 time keys "00" "01.30" "02.30" "03.30" "04" "04.20" "04.45" "05.15" "05.30" "05.45" "05.46" "06" "08" "12" "14" "18" "19.30" "20.15" "21" "21.30" "21.35" "22" "23.00" "23.20", each 1 global block (16 params) + 6 intemperies (152 params each) | the time of day weather (§2) |
| specific_weathers.xmb | +0x4c4, +0xfc | SURGERY, INTRO, DRESSING, MAP, SHOP, GARAGE, CLUB (1 intempery) | per game mode fixed weathers |
| cinematic_weathers.xmb | +0x4c8, +0x124 | 48 presets (Cin_Gen, Intro, Avatar, Garage...) | `weat:` cinematic messages |
| bank_weathers.xmb | +0x4cc, +0x1ec | ISTICK_B (Casino: ICASSLO1, ISHOP_CA, ICASHAL with 6 intemperies) | per bank (interior) weathers |
| modifiers.xmb, cinematic_modifiers.xmb, specific_modifiers.xmb, bank_modifiers.xmb | FUN_00afbcf0 | 356 / 30.. / 40 modifiers: `GAME_MODE_SHOP` (144), `GAME_MODE_LIVING` (144), `GAME_MODE_DRESSING`, `GAME_MODE_GARAGE`, `GAME_MODE_SCHOOL`, `GAME_MODE_GOODIES`, `Old_Weather`, `Photo_01`, `Film_01`, `Clouds`... | §4 |
| fx.ini | FUN_00d6b5b0 parser | GLOW-INGAME, PAUSE, GRASS (NBGRASSBITMAPS 9, NBBUSHBITMAPS 4), AVATARLIGHTFACTOR, MAPLODSELECTION, SLIDESLOD | post fx / grass / avatar (not weather) |
| grasstypes.xml | | GrassType Type/Name, Grass and Bush bitmaps, sizes, blend modes | tall grass (not implemented here) |
| streetlamps.xml | | 8 lamp types SF_LighA..J: RGBA colour (alpha = intensity), near/far attenuation (m), spot open/falloff angles | night lighting (other engineer) |
| clouds.bnk | FUN_00c34490 | clouds.3dd/.3dg/.2dm, CLOUDS and CLOUDSB textures | §6 |
| tallgrass.bnk, vfx_env.bnk | | grass textures, island ambient effects | not weather |

Xmb layout (all weather files): `WEATHER_EXPORT { EXPORT_WEATHER weather[] }`, `EXPORT_WEATHER { String name;
Int32 hour, minutes, numberOfIntemperies; EXPORT_INTEMPERY intemperys[] }`, `EXPORT_INTEMPERY { EXPORT_PARAM
weather_params[] }`, `EXPORT_PARAM { String name; Object param }` (float1..float4, STRING_PARAM).

Loader FUN_00afc4d0 [CODE]: one weather instance (0x94 bytes) per EXPORT_WEATHER; name (unknown_weather_name if
missing), time (hour, minutes) clamped to [0, 24) / [0, 60), seconds 0 (+0x38/+0x3c/+0x40). **File intempery 0 =
the global block** (looked up in the global registry 0x00f84dc8, list +0x74) and **file intemperies 1..n = the
weather types** (registry 0x00f84dd8, per intempery list +0x88 + k*0x44, +0x30). game_weathers is loaded with 6
intemperies (FUN_00afd4d0 passes 6), the others with `count - 1`. A parameter not in the registry is ignored;
a registered parameter missing from the file keeps its registry default (FUN_00af2ba0 builds every list from the
registry). Strings are copied with a 16 character limit.

### 1.1 Parameter registry FUN_00af30d0 [CODE]
Entries of 0x38 bytes: `+0 u64 id` (the 8-byte additive fold of the upper cased name, FUN_00af0340 =
`GetIdentifier64bit`), `+8 vec4 value A`, `+0x18 vec4 value B (current)`, `+0x28 components` (0 = string, 1..4 =
float1..4), `+0x2c interpolation mode`, `+0x30 name`. Global registry (11 entries): sundirection (default
(0,-1,0)), tunnel_light_direction/diffuse/specular/ambient, tunnel_light_envmap_attenuation,
tunnel_car_envmap_factor/headlights_intensity/caustics_intensity/poslight_intensity/headlight_flare_intensity.
Intempery registry (~150): sun_*, sunflare_*, fog_*, Skydome_*, clouds_*, ocean_*, rain_*, thunder_*, wind_*,
car_*, avatar_*, photoshop_*, building_*, spot_*... (dump: scratchpad `registry.txt`, partial emulation of the
function). **Every parameter has mode 0 except `photoshop_crappymode_enable` (mode 10)** (the only immediate
`mov [esi+0x2c],0xa`, at 0xaf4c85).

### 1.2 Weather types (intemperies) [CODE][DATA]
Names (table 0x00f7e08c): **0 Sunny, 1 Cloudy, 2 Rainny, 3 Before_rain, 4 After_rain, 5 Tempest**, then
"Max_intempery". In game_weathers.xmb (14:00): Sunny = CLOUD_B with AlphaBlend (0, 0, 0.4, 0) (no cloud),
haze 0.7; Cloudy = CLOUD_B (0.7, 0.6, 0.7, 0), haze 0.5, more wind; Rainny = CLOUD_R, rain_alpha 120, grey
sun/fog; Before_rain = CLOUD_R, no rain; After_rain = CLOUD_B, sunny light; Tempest = CLOUD_R, rain, thunder_off_on
1. Other exe strings of the forecast UI: weather_sunshine / weather_cloudy / weather_rainy / weather_storm /
weather_random, d_/n_ cloudy/rainy/stormy icons.

## 2. Time of day evaluation [CODE]

Times are (h, m, s) floats; seconds = h * 3600 + m * 60 + s (0x00f49b30 = 3600, 0x00f49b28 = 60, 0x00f49b38 =
86400).

1. **Key choice FUN_00af7d40**: best = 86400 (0x00f15338); for every key i (file order):
   d = now - t_i; if d == 0 the key is taken at once; else m = min(d, d + 86400) (= d), and if 0 <= m <= best:
   A = i, best = m. B = A + 1, or the first key after the last one.
2. **Blend factor FUN_00af7aa0(a, b, now)**: A, B, C in seconds; if (int)b.h - (int)a.h < 0 then B += 86400 and,
   if (int)now.h < (int)a.h, C += 86400. If A != B: t = (C - A) / (B - A), returned when 0 <= t <= 1 (0x00edf668);
   every other case returns 0 (xmm0 cleared at 0xaf7ac1).
3. **Parameter blend FUN_00af1ac0(dst, A, B, t)**, per entry, `switch(components - 1 + mode)`:
   case 0/1: float1/float2 lerp; case 2/3: float3/float4 through FUN_0049bc80 / FUN_0049bbe0 [ENC] (the readable
   twins FUN_00af0f20 / FUN_00af0a60 do a plain component lerp: taken as such [INF]); case 10..12 (mode 10):
   lerp by floor(t + 0.5) (`floor` import 0xe414bc); default (strings, mode 10 float4): B if t > 0.5 (0x00edd4a0)
   else A. The global list is always blended; the intempery list of the destination's index (`*dst`) is blended
   from A's intempery `*A` and B's `*B` when the three indices are valid. FUN_00af0530 then copies B -> A (commit).
4. **Weather type blend FUN_00af8f60** ("Bilinear Generated Weather" / "Linear Generated Weather", FUN_00afa770):
   the forecast state is two (intempery, h, m, s) records, source +0xb30 and destination +0xb20. Both keys A and B
   are evaluated for the source intempery into the scratch weather +0x38c, then for the destination intempery into
   +0x420, with the same key factor t; the final weather = FUN_00af1ac0(out, +0x38c, +0x420, t2) with
   t2 = FUN_00af7aa0(source time, destination time, now), stored in GSWeather +0x28 (0x0117a050).
5. **Forecast slices** FUN_00af88d0 / FUN_00c1f1b0: offline (flag 0x0124aaed) source = slice (hour / 2) of the
   12-entry table 0x00fd5280, time = 2 * slice h, 0 m, 0 s; destination = the same call with +2 h (ECX = 2).
   FUN_00af82d0 clears the table: **offline the weather is always Sunny** (intempery 0). Online (UDP service,
   "[WEATHER] Weather forecast out of bound : day/slice", FUN_00c1f970) the table comes from the server: 12
   slices of a day of `+0x84 * 100` centiseconds (`+0x84` defaults to 9000 s, FUN_00c1f390), per island zone.
6. **Clock**: current time GSWeather +0x4e4/+0x4e8/+0x4ec (FUN_00c1f110: the service time 0x0124aae0, island 1
   -6 h (0x00f49da4), island 2 +6 h (0x00edf660)). FUN_00af8ce0(dt): time += +0x4f4 * dt, wrapped by
   FUN_00af7520 (fmod 86400). +0x4f4 = **9.6** (0x00f4ae88, FUN_00afb1e0) or 480 (0x00f4ae84, game mode 0x14):
   24 h of game time per 2.5 h. +0x4f0 = game seconds elapsed in the frame (FUN_00afa350 end, 0xafa761), copied
   to 0x0117a518 for the clouds.
7. **sun / moon**: `sundirection` is a weather parameter (global block), lerped like the others: the sun has no
   astronomical model. At night the keys keep y = -0.3 and sweep x/z back: the night "sun" is the moonlight
   direction.

The time keys are also blended by the GAME_MODE modifier of the current game mode (FUN_00af8570:
`GAME_MODE_%s` with the mode name table 0x00f7eed8, applied with FUN_00af84d0 to both keys) and by the
cinematic / user modifier name at +0x611.

### 2.1 OpenTDU2 [CODE -> source]
`WeatherSystem` (eden_weather.cpp) implements 1-6 with the same formulas (lerp `t * B + (1 - t) * A` and the
blend factor in extended precision then rounded to float, like the x87 code; float3/float4 [INF] above), `tick()` advances the clock by `speed * dt` (World Viewer default 0 = frozen, "game x9.6"
button), `getCurrent()` is the evaluated weather, `getForecastBlend()` = +0x28, the source / destination cloud
names feed the cloud slots (§6). The forecast table is replaced by the panel choice of the source and next slice
intemperies (the offline game would always give Sunny). GAME_MODE modifiers: FREERIDE has none in the data, so
the World Viewer applies none.

## 3. Sky dome, haze, fog, lights
Unchanged (eden_sky.cpp, eden_postfx.cpp, docs/re/postfx_spec.md §5, title_scene.md): scattering constants from
`Skydome_*` (FUN_00931140 / FUN_0092fb20), haze pass from `Skydome_haze` and `fog_*`, sun light from `sun_*`.
All of them now follow the evaluated weather every frame.

Stars [CODE, not implemented]: MatDome (FUN_00c38340) is created with the texture "STARS" (fx/weatherfx.bnk
stars.2db) and a 30x30 per-face cube sphere (FUN_00c37e20, vertices normalised with FUN_00403620 /
FUN_00448620 [ENC]); FUN_00c32c00 fills the star attribute stream (uv + brightness) with FUN_00403620 /
FUN_0045f9a0 [ENC]. Constants (FUN_0092ed80): `StarsFade` = (Skydome_starsFadeStart,
1 / (Skydome_starsFadeEnd - Skydome_starsFadeStart), 0, 0) (dome material, id fold of "STARSFADE"),
`StarsIntensity` = Skydome_starsPercent. The star vertex data cannot be rebuilt without the encrypted callees:
the dome stays on its star-less permutation [ENC].

## 4. Modifiers [CODE][DATA]
`EXPORT_MODIFIER { name; type (1, 2); weather_name; intempery_index (-1 = global block, 0.. = intempery);
param[] }`. FUN_00af04a0 writes a modifier value over both A and B of the matching entry (override). In
modifiers.xmb the GAME_MODE_SHOP / LIVING modifiers exist for every key x intempery (24 x 6). The World Viewer and
FREERIDE use none (no GAME_MODE_FREERIDE entry).

## 5. Moon [CODE][SHADER]
- Material MatMoon (FUN_00c384f0): shader set 0x2428b45eec949166 (td_moon), texture "MOON" (weatherfx.bnk
  moon.2db), states +0x30 1, +0x31 2, +0x38 5 / +0x39 6 (SRCALPHA / INVSRCALPHA), +0x20 bucket 0x14; geometry 4
  vertices (-1,-1,0) (-1,1,0) (1,1,0) (1,-1,0) with uv (0,0) (0,1) (1,1) (1,0), indices 0 1 3 2.
- Constants (FUN_0092ed80, read at +0x18 of the weather entries): **PosMoon** = (-sundirection * 100000
  (0x00f4ac08), Skydome_moonPercent); **SizeMoon** = (Skydome_moonSize * 10 (0x00f49820), moon object +0x1400,
  +0x1404, +0x1408 [not traced]).
- td_moon vs: camera facing quad of half size SizeMoon.x at PosMoon (basis from CamPos - PosMoon), uv.x mirrored,
  depth just in front of the far plane; ps: colour = MOON.rgb + (0.392, 0.412, 0.510), alpha = MOON.a * PosMoon.w.
- Data: moonPercent 1 from 23:20 to 03:30, 0.75 at 04:00, 0 from 04:20 to 23:00; moonSize 1000 at night, 250 by day.

## 6. Clouds [CODE][DATA][SHADER]

### 6.1 clouds.bnk (Ibiza)
clouds.3dd hierarchy (node names are name folds; `?` = name not recovered):

```
SCENE01 / CLOUDS
  ?(59435f74b49e9886) / ?(5989be74b49e9886) / ?YCLO_B_  geometry, material M_Clouds_Mesh_C (TD_CloudMesh, CLOUDS)
  CLOUD_R_0 (21277, 0, 16339) / CLOUD_R_0_N (empty), CLOUD_R_0_F / CR0_F  material Clouds_near (TD_Clouds)
  ?(a78ca096b4cef7df) / ? / ?YCLO_R_  geometry, M_Clouds_Mesh_C (TD_CloudMesh)
  CLOUD_B_0 / CLOUD_B_0_N, CLOUD_B_0_F / CB0_F  Clouds_near + Clouds_nearB (TD_Clouds, CLOUDS / CLOUDSB)
  CLOUD_A_0 / CLOUD_A_0_N, CLOUD_A_0_F / CA0_F  Clouds_nearB + Clouds_near
WORLD
```

### 6.2 Cloud sets and slots
- FUN_00932390 / FUN_00932450: a cloud set named by `clouds_cloud3dname` ("CLOUD_B") is the objects
  "<name>_0" .. "_3" (sprintf "_%d"), each found by FUN_00931da0 with its "_N" (near) and "_F" (far) children
  (up to 16 sub-nodes each, positions saved). Every node of the set gets blend states SRCALPHA / INVSRCALPHA
  (+0x48 5, +0x49 6) and bucket 0x14 (0x1a for the far layer of the non cyclone sets).
- Up to 5 cloud sets (0x13e0 bytes each, 0x011c95d8); the dome object holds 4 slots +0x1460 .. +0x146c:
  0 = current, 1 = previous (FUN_00932530 swaps them when clouds_cloud3dname changes), 2 / 3 = CLOUD_CYCLO and
  CLOUD_CYCLO_RAINY (not in the island data).
- Per frame FUN_00930e70: f = clamp(GSWeather +0x28, 0, 1); slot fades (FUN_0092dfa0) = f, 1 - f;
  FUN_009306a0 -> FUN_009301c0 (rotation), FUN_009306f0 -> FUN_00930460 (colours and alpha), node +0x70 = 20000.
- **Rotation** FUN_009301c0: angle = clouds_cloud3dSpeed.x (slots 0/1, .y for slots 2/3) / 1000 (0x00f49a38) *
  game time of the frame (0x0117a518); FUN_0043a940(angle) [ENC] rotates each object, its translation is then
  restored. OpenTDU2: rotation about the vertical axis through the object origin [INF].
- **Colours / alpha** FUN_00930460: node +0x80 = clouds_cloud3dcolor (instance colour), clouds_cloud3dcolor2
  (FUN_009306f0 second vector) -> ColorDown [INF: register binding not traced]; every material of the set gets
  AlphaBlend = clouds_cloud3dAlphaBlend with y *= (1 - slot fade) for slots 0 / 1 (slot 2: x = y, slot 3: x = w).
  FUN_00930d70: GROUNDCOLOR (0.7, 0.6, 0.5).
- Shaders: **TD_CloudMesh** (dome cloud meshes): position y *= AlphaBlend.z, alpha = tex.a * AlphaBlend.x, sky
  scattering in-scatter (same constants as the dome) blended by Haze / 2, depth at the far plane. **TD_Clouds**
  (far layer billboards built in the VS around the camera direction): alpha = tex.a * AlphaBlend.y.
- Data: Sunny AlphaBlend (0, 0, 0.4, 0) at every key = **no cloud** in Sunny; Cloudy (0.3 .. 0.8, 0.25 .. 0.7,
  0.7, 0); Rainny (0.15 .. 0.2, 0.2 .. 0.4, 2, 0.4 .. 0.6), CLOUD_R. Speed (0.6, 0.2) Sunny/Cloudy 14:00, (1.6,
  0.6) Rainny.

### 6.3 OpenTDU2
`EdenSceneView::setCloudLayers(source, destination, f)` (from the WeatherSystem) draws the source set with
AlphaBlend.y * (1 - f) and the destination set with AlphaBlend.y * f (one draw when both names are equal [INF]);
`setGameTimeDelta()` drives the rotation (default dt * 9.6). The TD_CloudMesh meshes `?YCLO_<letter>_` are not
reached by the CLOUD_X_n lookups; they are drawn with the set of the same letter [INF].

## 7. Open points
- [ENC] FUN_0041d8a0 (parameter lookup), FUN_0049bc80 / FUN_0049bbe0 (float3/float4 blend), FUN_0043a940 (cloud
  rotation matrix), the star vertex generator callees, the online forecast server table.
- [INF] float3/float4 blend = linear; rotation axis Y; dome cloud mesh selection by letter; ColorDown = color2.

## 8. Verification (World Viewer, Ibiza, EAS_A spot area)
Screenshots `scratchpad/weather/` (960x540, lavapipe): a_0800_cloudy, a_1930_sunny, a_2130_rainny, a_0000_sunny,
a_1400_beforerain, a_1400_tempest, b_0000_moon (moon at -sundirection), b_0230_cloudyA (CLOUD_A billboards
only), b_1230_cloudy_to_rain (forecast f = 0.25, CLOUD_B + CLOUD_R), b_2015_cloudy, b_1400_cloudy_high (cloud
dome over the island), b_1800_afterrain_panel (panel), c_timeflow (05:00 + x3000 for 60 frames = 05:49:10,
keys 05.46 -> 06 t 0.226, forecast f 0.910). FREERIDE (-skipcine) unchanged (weather "14").
