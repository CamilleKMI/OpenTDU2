# Falling rain: rain blocks, drops, splashes, rain sound — spec

Tags: **[CODE]** readable x86 (>= 0x6c1000) · **[DATA]** game files · **[SHADER]** .sob bytecode ·
**[INF]** inferred (reason given) · **[ENC]** behind SecuROM, not read.

Complements `rain_shadow_spec.md` (wet road / puddles / ripples, already implemented in `EdenWorld::updateWetGround`).
Sources: `TestDrive2.exe` (decompiled with a private Ghidra copy; raw asm where the decompiler loses stack values),
`fx/weatherfx.bnk`, `TD_Rain.sob`, `TD_Gouraud.sob`, `islands/ibiza/fx/game_weathers.xmb`,
`islands/ibiza/sound/ambience/ambiences.xmb`.

## 1. Weather values [DATA]

`game_weathers.xmb`: 24 time keys, each with a global block and 6 intemperies (file blocks 1..6 = Sunny, Cloudy,
Rainny, Before_rain, After_rain, Tempest, exe table 0x00f7e08c). The rain values do not depend on the hour
(same at 14 and 22, except `rain_ground_wet` / `rain_ground_global_intensity` of After_rain):

| parameter | Sunny | Cloudy | Rainny | Before_rain | After_rain | Tempest |
|---|---|---|---|---|---|---|
| rain_alpha | 0 | 0 | 120 | 0 | 0 | 120 |
| rain_tiling | 0 | 0 | 2.0 | 0 | 1.2 | 2.0 |
| rain_scroll_speed | 0 | 0 | 3.0 | 3.0 | — | 3.0 |
| rain_splash_max / alpha | 0 / 0 | 0 / 0 | 200 / 200 | 0 / 0 | 132.61 / 0 | 200 / 200 |
| rain_droplets_max / life / freq | 0 | 0 | 10 / 0.8 / 0.15 | 0 / 0.8 / 0.15 | 0 | 10 / 0.8 / 0.15 |
| rain_ground_global_intensity | 0 | 0 | 1.0 | 0 | 0.75 (14) / 1.0 (22) | 1.0 |
| rain_ground_wet | 0 | 0 | 0.6 | 0 | 0.5 / 0.7 | 0.6 |
| rain_ground_puddle_height | 0 | 0 | 0.62174 | 0 | 0.62174 | 0.62174 |
| rain_ground_ripple_intensity / scale / scroll | 0 / 0.1 / 0 | idem | 0.3478 / 0.8 / 0.4196 | 0.3478 / 0.8 / 0.4196 | 0 / 0.1 / 0.9043 | 0.3478 / 0.8 / 0.4196 |
| thunder_off_on | 0 | 0 | 0 | 0 | 0 | 1 |
| sun_shadow_opacity | 1 | 1 | 0.15 | 1 | 1 | 0.15 |

(dump `scratchpad/rn/gw.txt`; the sky / fog / light darkening of Rainny and Tempest belongs to the weather presets,
see `weather_spec.md`.)

## 2. Weather FX root and setters [CODE]

`FUN_00aa7200` (per frame) → `FUN_00aa6ce0` copies the weather into the WFX objects (root +0xdb0 rain, +0xdb4 splash,
+0xdb8 drop, +0xdbc droplets, +0xdc0 lightning, +0xdc8 WetGrd, `rain_shadow_spec.md` A.1). `FUN_00aa61a0`:

```
rain (+0xdb0):  +0x201 = rain_alpha > 0.001 (DAT_00ecb0c4);  +0x59a8 = rain_scroll_speed;
                +0x59b8 = (int)rain_alpha;  +0x59b4 = rain_tiling
drop (+0xdb8):  +0x7b6c = (int)rain_alpha;  vfunc+0x18(enable = rain_alpha > 1e-4 (double 0xf49b18))
```
Splash (+0xdb4): +0x4d4c = (int)rain_splash_max, +0x4d54 = (int)rain_splash_alpha, +0x201 = max > 0
(`rain_shadow_spec.md` A.1.1). The textures are looked up by name in the weatherfx bank object `DAT_0117ac70`
(FUN_00aa6560).

## 3. WFX_Rain — rain blocks (ctor FUN_00de2a70, vtable 0x00f1afe0) [CODE]

vtable: [1] FUN_00de2be0 init, [2] FUN_00de24f0 update, [3] FUN_00de2b60 submit, [4] FUN_00de1970 release,
[7] FUN_00de19c0 world origin shift. Ctor: +0x59a8 = 2.0, +0x59ac = 2.0, +0x59b0 = 200.0, +0x59b4 = 3.0
(DAT_00edf3b8), +0x59b8 = 0xff, +0x59bc = 0; 9 blocks of 0x9b0 bytes at +0x270 (pointers +0x24c..+0x26c).

**Init FUN_00de2be0**: material "RainGrp", GUID **TD_Rain e1b3c51172e345ff**, specs [0] = 2, [1] = 2, [0x21] = 1,
2 slots: 0 = `RAIN_FAR` (slot bytes +0x16..+0x19 = 2, 1, 1, 1: address wrap), 1 = `RAIN_ALP` (2, 2, 2, 2). States:
+0x30 = 1, +0x31 = 2 (no Z write), +0x32 = 0, +0x33 = 3 (two sided), +0x35 = 0 (alpha test), +0x37 alpha ref,
+0x38 = 5, +0x39 = 6, +0x3a = 1 (SRCALPHA / INVSRCALPHA / ADD), +0x20 (u16) = 0x2d. Each block: FUN_00dbed00(size
75.0 = DAT_00f4a39c): +0x894 = 75, +0x898 = f(2·75²)·0.5 (f = CRT thunk 0x6c8d3e [ENC], sqrt [INF] → 53.03 m),
+0x8c0 = 4 quads, dynamic buffer FUN_008bd2a0(1, 16 vertices, 24 indices). Block ctor FUN_00dbe810: +0x990 seed
0x3039, +0x99c = 10.0, +0x9a0 = 5.0, +0x9a4 = 0xff, +0x998 = 0, +0x89c = 0, +0x8b0..+0x8bc = 0.

**Shader TD_Rain** (1 VS, 1 PS) [SHADER]: `oPos = WVP·pos`, colour and uv passed; PS `oC0 = tex(s0, uv) · colour`.
RAIN_FAR (512², DXT5) [DATA]: white streaks, alpha ≤ 63/255 (mean 0.9/255). The second slot RAIN_ALP is not sampled.

**Grid FUN_00de1f40** (first frame, or camera outside every block):
```
p   = camera (0x01195b80) + k · (-m.z_row.x, -m.z_row.z)      k = +0x898 (53 m), m = camera matrix (+0x50 rows,
                                                              z row +0x70; -z = forward [INF, ocean_spec 4.3])
g   = corner of the camera sector (FUN_00da6d40 = EdenWorld::getSectorOrigin, world +0x269b0/+0x269b8)
x0  = g.x + (int)((p.x - g.x)/75)·75 ,  z0 = g.z + (int)((p.z - g.z)/75)·75      (_ftol truncation)
blocks: 0 (x0,z0), 1 (x0-75,z0-75), 2 (x0,z0-75), 3 (x0+75,z0-75), 4 (x0-75,z0), 5 (x0+75,z0),
        6 (x0-75,z0+75), 7 (x0,z0+75), 8 (x0+75,z0+75)       FUN_00dbe5d0(block, centre)
quads (block +0x8d0, 4 × 0x30 = {A, B, (u, v)}), the same for every block:
   A (-37.5, 75, 37.5)  B (0, 0, -37.5)     A (0, 75, 37.5)    B (37.5, 0, -37.5)
   A (-37.5, 75, -37.5) B (37.5, 0, 0)      A (-37.5, 75, 0)   B (37.5, 0, 37.5)
```
FUN_00dbe5d0: centre (+0x8a0), then 8 values `seed = seed·0x7ff8a3ed + 0x2aa01d31; r = (uint)seed·2^-32` → the
(u, v) offsets of the 4 quads.

**Recentring FUN_00de23c0 / FUN_00de19f0**: if the camera is not inside block 0 (FUN_00dbe3a0: `c ± 37.5` on x and
z), the first block 1..8 that contains it becomes the centre: the blocks still on the new grid are kept (with their
offsets), the others are re-placed with FUN_00dbe5d0 (switch tables in FUN_00de19f0); none → FUN_00de1f40.

**Per frame FUN_00de24f0**: dt = DAT_00fdb8d0. Slant: `w = v_cam (camera +0x360, if v·forward > 0 else 0) ·
(+0x59b0 · DAT_0117ac84) / 30`; per block `t = clamp(|centre - camera| / 100, 0, 1)` and the top offset
(+0x8b0..+0x8bc) = FUN_0045b810(0, w, t) [ENC: lerp INF]. Scroll speed of every block
`(+0x59ac · DAT_0117ac84 + 1) · +0x59a8`; +0x99c = tiling, +0x9a4 = alpha, material +0x37 = +0x59bc.
DAT_0117ac84 (also used by the drops) has no writer in readable code [ENC] (car speed factor [INF]).

**Block update FUN_00dbe930(dt)**:
```
base (+0x8a4) = ground height at the centre (FUN_00bd6c30; DAT_011956c4 when DAT_0117ac89)
if (37.5 < camera.y - 0) base = camera.y - 37.5                    (+0x89c = 0)
world matrix = translation(centre.x, base, centre.z) - world origin
s += speed·dt;  if (s > T) s -= f(s)                               (f = CRT thunk 0x7c7f66, floor [INF])
colour = (200, 200, 200, alpha)   (D3DCOLOR bytes c8 c8 c8 alpha)
per quad (A, B, u, v):  (A + top offset,  u,     s + v)        (A.x, B.y, A.z,  u,     s - T + v)
                        (B.xz + offset at A.y, u + T, s + v)   (B,              u + T, s - T + v)
```
So the texture scrolls down at `rain_scroll_speed` uv/s, tiled T times over 84 m x 75 m. Rainny: alpha 120, T 2,
speed 3.

## 4. WFX_Drop — drops (ctor FUN_00aa5940, vtable 0x00f1af94) [CODE]

Material "RainDrop" **TD_Gouraud 73078865ca37336d**, specs [0] = 1, [1] = 1, [0x0a] = 1 (S_NBPRELIGHT), texture
`DROP` (16 x 64, slot bytes 2, 2, 2, 2), states as the rain but +0x35 = 1 (no alpha test). Buffer 1200 vertices /
1800 indices (300 quads, FUN_00aa5aa0). Ctor: seed +0x7b40 = 0x3039, period +0x7b48 = 0.05, lead +0x7b4c = 12,
fall +0x7b50 = 3.5, +0x7b54 = 1.0, length +0x7b58 = 1.5, width +0x7b5c = 0.06, boost +0x7b60 = 3.0, spawn
+0x7b68 = 50, alpha +0x7b6c = 200, max +0x7b70 = 200, box +0x7b74 = 40.

Update FUN_00aa4eb0 (car = DAT_00fd51c0: rows +0xf0 right, +0x110 z, +0x120 position, +0x400 velocity):
```
C     = car.pos - car.z_row · 12 · 1.0           (box centre, 12 m ahead [INF: -z forward])
s     = normalize(car.velocity) · 1.5 · DAT_0117ac84      (streak slant)
kill  = min(car.pos.y - DAT_011956c4, 20)
d     = 3.5 · 9.81 · dt · (3.0 · DAT_0117ac84 + 1)
timer -= dt; if (timer <= 0 && alive <= 200) { n = min(50, 200 - alive); timer = 0.05 }
per slot (300, 0x60 bytes):
  alive: FUN_00aa4c90(d, 40) with C:
     y -= d
     m.x = ((int)((C.x + x)·100) % 4000)/100 ;  m.z likewise
     cell = ((int)((C.x+x)/40), (int)((y+C.y)/40), (int)((C.z+z)/40))
     rel  = (20 - m.x, y - d, 20 - m.z)
     if cell == previous cell: top = C + previous rel, bottom = C + rel   (world, - world origin)
     previous rel = rel, previous cell = cell
     if y < -kill: dead
  dead and n > 0: x = rand·40 - 20, y = rand·20, z = rand·40 - 20, the rest cleared, alive
vertices (alive drops, strip with degenerate joints): top + s (0, 0); top - (0, 1.5, 0) (1, 0);
   top + s + 0.06·right (0, 1); top + 0.06·right - (0, 1.5, 0) (1, 1); colour (200, 200, 200, alpha)
```
The drops stay fixed in the world modulo a 40 m box centred on C (1/100 m precision) and fall at 34.3 m/s.

## 5. WFX_Splash — ground splashes (ctor FUN_00aa4930, vtable 0x00f1b03c) [CODE]

Material "RainSplash" TD_Gouraud (same specs / states as the drops, +0x20 = 0x1e), texture `SPLASH` (256², 3 x 3
atlas), buffer 2000 / 3000 (500 quads). Ctor: +0xad8 = 25 (width), +0x4d3c = 9 frames, +0x4d44 = 1/3 (cell),
+0x4d4c = 250 (max), +0x4d50 = 20 (spawn), +0x4d54 = 0xff (alpha), +0x4d58 = 0.01 (period), +0x4d5c = 0.35 (size),
+0x4d60 = 0.35 (life), seed +0x4960 = 0x3039.

Zones FUN_00aa3960: 15 zones, `x = (col + 0.5)·8.333 - 25·0.5`, `z = (row + 0.5)·(-8.333)` (col 0..2, row 0..4),
weight 8.333. FUN_00aa3b10 (every frame, one zone, round robin +0x4d30): zone position = car matrix · local, height
from FUN_00bd6c30 (kept if > -99999.99).

Update FUN_00aa3d80:
```
timer -= dt; if (timer <= 0 && alive <= max) { n = 20; timer = 0.01 }
alive slot: life -= dt (dead below 0); frame = (int)((0.35 - life) / (0.35/9))
dead slot and n > 0: zone = FUN_0095b3e0 (LCG, 0..14); offset = (rand·8.333 - 4.1667, 0, rand·8.333 - 4.1667);
   life = rand·(0.35 - 0.28) + 0.28; size = rand·(0.35 - 0.175) + 0.175;
   pos = FUN_00aa3a80(zone, offset) (zone position + offset on the zone plane); pos.y += size
vertices: h = size/2, R = car right row, U = car up row, cell (frame % 3, frame / 3)·1/3:
   c - hR + hU (u0, v0), c - hR - hU (u0, v1), c + hR + hU (u1, v0), c + hR - hU (u1, v1); colour (200,200,200,alpha)
```

## 6. Not decoded / not implemented

- **WFX_Droplets** (+0xdbc, screen droplets, TD_Droplets_Oncam, `DROPLETS_NEXT_IT` / `DROPLETS_KEPT_TEX` render
  targets; `rain_shadow_spec.md` A.6): not implemented (in-car / chase camera effect).
- **WFX_Lightning** (+0xdc0, `thunder_*`, `lightning.3dd` of weatherfx.bnk, TD_Lightning; thunder sounds
  `eclair_*.wav`, ambiences.xmb Lightning[]): not implemented (Tempest only).
- **DAT_0117ac84** (car speed factor: rain slant, scroll boost, drop boost) [ENC writer]; OpenTDU2 uses 0.
- FUN_0045b810 (slant lerp), FUN_005abe00-like CRT thunks (sqrt, floor) [ENC], taken as lerp / sqrt / floor [INF].
- Zone choice FUN_0095b3e0 (the scaling of the LCG value to 0..14 is hidden in `_ftol` arguments) [INF uniform].

## 7. Rain sound [DATA + CODE + INF]

`ambiences.xmb` (`%sIslands/%s/Sound/Ambience/Ambiences.xmb`, loaded by the ambience manager FUN_00cc13c0, client
names "AmbienceRain" = bfx client 13): `ASTExport { Environment[]; ReverbPreset[]; LightningExport[] Lightning;
RainResource[] Rain }`, `RainResource { Curve { PointF[] Points; Color; Boolean Visible }; Color CurveColor;
Int32 CurveColorARGB; String Name; String Path; Single Volume; Single Pitch }` (packed). Ibiza: Name `Rain_Mid`,
Volume 1.0, Pitch 1.0, curve (0, 0) → (1, 1) [DATA, raw bytes]. The sound file is
`islands/<island>/sound/ambience/rain_mid.wav` (FUN_00cbf3e0 builds `.../Ambience/%s.wav` from the name). The
runtime law (which weather value drives the curve) was not found in readable code; OpenTDU2 plays the loop on
client 13 with `Volume · Curve(rain_alpha / 255)` [INF] while the rain is on.

## 8. OpenTDU2 implementation

- `render/eden/eden_rain.{h,cpp}` (`EdenRain`), owned by `EdenSceneView`: setters (§2), rain blocks (§3), drops
  (§4), splashes (§5), sound (§7). Draws are deferred blended draws of the main view only (not in the environment
  cube / ocean reflection). The World Viewer has no car: the **camera frame** stands in for the player car frame
  (drop box centre, splash zones, billboard axes) [INF]; ground heights from `EdenWorld::getHeight` /
  `getSurfaceHeight`; DAT_0117ac84 = 0.
- World Viewer checkbox "Rain" (default off, `-rain` sets it): **off** = no falling rain / sound (the wet ground keeps
  the weather values); **on** = rain with the weather values when the weather rains (`rain_alpha` > 0: Rainny,
  Tempest), otherwise the `rain_*` / `thunder_*` values of the Rainny intempery at the same time of day
  (`EdenRain::resolveWeather`, own `WeatherSystem` on game_weathers.xmb). The resolved values also drive the wet
  ground (`EdenWorld::updateWetGround`). The checkbox does not change the weather type (sky, fog, light): use the
  weather combo / `-intempery Rainny` for the full rainy weather.
- FREERIDE / cinematics: the flag stays off (no change).

Screenshots (World Viewer, Ibiza town 21962, 11, 23675, yaw -169): `scratchpad/rainnight/rain_day14.png` (Sunny
14:00 + Rain), `rain_rainny14.png` (Rainny 14:00 + Rain), `rain_night22_panel.png` (22:00 + Rain, panel with the
checkbox).
