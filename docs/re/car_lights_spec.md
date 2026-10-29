# Car lights (brake, reverse, blinkers, position lights, headlights) + California rear

Tags: **[CODE]** readable x86 (VA >= 0x6c1000) · **[DATA]** game files · **[SHADER]** .sob bytecode ·
**[INF]** inferred (reason given) · **[ENC]** SecuROM range (VA < 0x6c1000), not read.

Related: `california_spec.md` (node slots, LOD), `car_glass_spec.md`, `weather_spec.md` (weather values),
`accessories_spec.md` (blinker keys / menu, other engineer).

## 1. Why the California rear was wrong

The rear of the convertible is the skinned trunk HANIM_S (`california_spec.md`): its round tail lights are the
materials **LGT_RL_SK / LGT_RR_SK**, the horse badge Chrome_Logo_SK, the "Ferrari" script Chrome_SK, the plate
Ws_Plate_SK, the lamp covers Glass_SK_LGT [DATA, california.3dd / .2dm]. The light materials (TD_CarPaint_NM
vs6-7/ps96, front LGT_FL / LGT_FR ps24) carry 4 extra maps: DETAIL `california_lgt_of` (lamp off), NORMAL
`california_lgt_ts`, GLOSSLIGHT `california_lgt_g` (64x64 DXT5 masks) and TEXLIGHT `california_lgt_on` (lamp
on) [DATA].

ps96 [SHADER]:
```
m      = tex(GlossLight, uv) * (1,1,1,-1) + (0,0,0,1)        // (r, g, b, 1 - a)
emis   = tex(TexLight, uv) * dot(m * c17, 1)                // c17 = GlossFct (Var_PS c9..c17)
diffuse_term *= prod_i(1 - c17.i * m.i)                      // lit lamp: no diffuse / specular
```
The material data has **GLOSSFCT = (1, 1, 1, 1)** [DATA]: drawn as loaded, every lamp function is on (round
lamps glowing orange / yellow, lower triangles lit red / white). In the game GLOSSFCT is a per frame light state
(§2): by day with no pedal it is (0, 0, 0, 0) and the lamps show the dark red `lgt_of` texture — the reference
picture. That was the rear difference; the other rear parts (plate TDU2 Ws_Plate_SK, diffuser Metal_Noir /
Av_Grille, exhausts EXHAUST Chrome / Black, horse Chrome_Logo_SK) were already drawn from the data. The blue
tint of the horse in a 14:00 shot is the chrome reflecting the blue sky cube (the reference is a hazy sunset).

## 2. Light state -> materials

### 2.1 Light material groups [CODE + DATA]
`FUN_00c5ee20` (car setup, not kind 2): for every material of the car whose name hashcode (8-byte additive fold)
equals one of the 9 names of the table **0x00f7e700** — `LGT_FL, LGT_RL, LGT_RL_SK, LGT_FR, LGT_RR, LGT_RR_SK,
LGT_R_S, LGT_L, LGT_R` — it pushes (material, pointer to its GLOSSFCT) into the vector of group k at car
+0x783c + k * 0xc. California: LGT_FL, LGT_FR, LGT_RL, LGT_RR, LGT_RL_SK, LGT_RR_SK.

### 2.2 Per frame logic FUN_00c588d0 (called from the car update FUN_00c62a70) [CODE]
`ctrl` = car +0x280 (driver / vehicle state). `table` = 0x01230500 + kind * 200 (kind = vfunc +0x2c), a
table of member function pointers filled by FUN_00e33810 with **encrypted** functions (0x452c00, 0x452f00,
0x4548e0, ... [ENC]); they write the GLOSSFCT channels / node flags.

| call | argument |
|---|---|
| table+0x00 | 1 if `ctrl+0x1d0` (brake) and `ctrl->owner+0x79c8 != 0`, else 0 |
| table+0x38 | 1 if `ctrl+0x1d1` (reverse) else 0 |
| table+0x60 | headlights: mode 2 → flare + 1, mode 1 → flare, mode 0 / pop-up moving → 0 |
| table+0x68 | position lights: poslight (0 if < 0.05; 0 in mode 0); mode 2: the raw flare once the timer +0x1f24 > 0.2 s (0x00f49b58) |
| table+0x70 | 1 if `ctrl+0x1d9` (and mode != 0) — function not identified |
| table+0x28 / +0x30 | left / right blinker value (FUN_00c57ef0, §2.3) |

* `flare` = car_headlight_flare_intensity (DAT_00fd5234), 0 when < 0.05 (0x00f49838) or `ctrl+0x1d8`.
  `poslight` = car_poslight_intensity (DAT_00fd5244). Both from the weather (FUN_00c4e710 reads
  car_headlights / car_headlight_flare / car_caustics / car_poslight _intensity and their `tunnel_` variants every
  frame); the tunnel values are used when car +0x2acc >= 5.0 (0x00edf65c; the car is in a tunnel).
* `mode` = `ctrl+0x1d4`: 0 everything off (blinker value 0 too), 1 normal (weather driven), 2 headlights forced.
  Mode 2 with car_headlights_intensity > 0.1 starts the pop-up headlight animation (+0x1f1c / +0x1f1d, state
  +0x2524 = 3, FUN_00c67280: players +0xacf0 / +0xad60 sampled at +0x1f20, 0.2 s; the California has none).
* `ctrl+0x1dd` → car +0x178: police light bar (FUN_00c5a9a0: 4 lamps, phase timers +0x17c..+0x188 modulo
  +0x19c, writes GLOSSFCT of the bar material) — not a California part.
* `car+0x2adc < 1.0` hides every light node (flag 4) — fade / damage, not used.

Weather [DATA game_weathers.xmb, Ibiza, keys]: car_headlight_flare_intensity 1.0 from 21:00 to 05:45;
car_poslight_intensity 1.0 from 21:35 to 05:45; both 0.0 from 05:46 to 20:15 (keys 05.46 ... 21.30);
car_caustics_intensity 0.1 (21:35), 0.5 (22:00, 23:00), 1.0 (23:20 ... 04:00), 0.5 / 0.25 at dawn, 0 by day;
car_headlights_intensity 1.0 ... 2.0 at night, 0 by day; tunnel_car_* (global block): flare 1.0, poslight 1.0,
caustics 0.65, headlights 2.2.

### 2.3 Blinkers FUN_00c57ef0(dt, value) [CODE]
value = 1 (0 in mode 0). Timer car +0x1f10: -1 (0x00ede404) while neither `ctrl+0x1db` (left) nor `+0x1dc`
(right); set to 0 when one starts (lamp lit at once); then `t = fmod(t + dt, 1.0)` (0xe2dc12, QWORD 0x00f44c70);
lit while t < 0.6 (0x00ef2754): **period 1.0 s, 0.6 s on / 0.4 s off**. Left output ×0 unless +0x1db, right ×0
unless +0x1dc, then table+0x28(left), table+0x30(right). Left / right are exclusive in the player input (no
hazard in readable code, `accessories_spec.md`).

### 2.4 GLOSSFCT channels [DATA + INF]
The writers are [ENC]. Channel meaning from the masks of `california_lgt_g` and the TEXLIGHT colour under them
(`lgt_on`, mean RGB): **r** (421 px) red (185, 19, 18): lower red triangle + bottom strip → **brake**;
**g** (140 px) white (207, 196, 196): lower white triangle → **reverse**; **b** (499 px) headlamp reflector + rear
ring (191, 134, 133) → **position lights / headlights**; **1 - a** (80 px) amber (232, 154, 32): round lamp centres
→ **blinkers**. Left / right from the group names (LGT_xL left, LGT_xR right: +X is the car's left) [INF].

### 2.5 Lamp glow meshes and projected light boxes
* Nodes `?A_<f>_<FL|FR|RL|RR>_..` (first character folded) with the additive Caustics materials (TD_CarPaint
  ps3: colour x gloss map, no intensity constant [SHADER]): f = **L** lights (Caustics_02 white, front),
  **S** stop (Caustics red, rear), **R** reverse (Caustics_02 white, rear), **W** warning (Caustics_04 orange,
  front) [DATA]. Shown with their function [INF: their visibility is set by the [ENC] functions].
* Car slots 13 BREAKING, 14 REVERSING, 15 HEADLIGHT (name table 0x00edef08) = TD_ShadowBox ps1 boxes (base
  texture breakcaustics / reversecaustics / headlightcaustics of common_car.bnk projected on the scene depth,
  additive, × `Intensity`) placed by the data (BREAKING behind the bumper, HEADLIGHT 6 m ahead, 9.6 x 11.9 m)
  [DATA]. FUN_00c63a30 / FUN_00c638d0 [CODE]: v = car_caustics_intensity (tunnel_), 0 in mode 0, +1.0 in mode 2;
  if v > 0.05 and +0x2524 == 0: BREAKING INTENSITY = v when `ctrl+0x1d0`, REVERSING when `ctrl+0x1d1`
  (FUN_00c5a4f0 writes INTENSITY of the slot materials, then the box is submitted); HEADLIGHT INTENSITY = v x
  (kind 1: 0.3, else 1).
* Flare sprites, projected headlight, light beams and tunnel detection: sections 6 to 9 (second car lights pass).

## 3. Input → state (OpenTDU2) [INF]
The writers of `ctrl+0x1d0 / +0x1d1` are not identified in readable code: brake = brake pedal > 0, reverse =
gear -1 (VehicleDynamics). Blinkers from the accessories (car_accessories.cpp, `ctrl+0x1db / +0x1dc`). Light mode
default 1 (ENC: the player value is not readable). Tunnel: road network tunnel sections (section 6).

## 4. OpenTDU2
* `CinematicCar` (source/game/cinematic/cinematic_car.*): `setBraking`, `setReverse`, `setIndicator(Off / Left /
  Right / Hazard)`, `setHeadlights(0 / 1 (-1) / 2)`, `setInTunnel`, `updateLights(dt)` (blink timer, high beam
  timer), `isIndicatorLampOn()`. `collectLights()` (load) finds the 9 light material names, the lamp glow nodes and
  the 3 light boxes; `applyLights(weather)` (draw) writes GLOSSFCT = (brake, reverse, position [front: max with
  headlights], blinker of the side) (front: brake / reverse 0), the glow node visibility and the box INTENSITY.
* FREERIDE (gm_freeride.cpp) feeds brake / reverse every frame and runs updateLights after the accessories.
* Test switches: `-indicator left|right|hazard`, `-carlights 0|1|2`, `-nolightboxes`, `-weather <key>` (e.g. 23.20).
* API note for the accessories engineer: /tmp/claude-0/car_lights_api_note.md.
* Flares: `collectLights()` also finds the `Fa_%c_FL/FR/RL/RR[%d]` sprite nodes (section 7, LightNode.bFlare /
  bFront); `applyLights` shows each one with the value of its function letter (`-noflares` hides them).
* Tunnel: `RoadNetwork` (source/world/road_network.*) loads ROAD_DETAIL, `getCarTunnelDistance(car, camera)` =
  FUN_00c41d20 behind the 100 m camera test; `CinematicCar::setTunnelDistance(d, dt)` keeps +0x2acc / +0x2ad0 /
  +0x2ad4 and switches the tunnel_ values at d >= 5 (`isInTunnel`, `getTunnelBlend`; `-tunnel 0|1` forces it,
  `-logtunnel` logs it). FREERIDE and FREE_DRIVE call it each frame after `updateLights`.
* Projected headlight: `CinematicCar::getHeadlightSpot(world, camPos, HeadlightSpot&)` (section 8.2, variant 0
  constants) -> `EdenSceneView::setHeadlightSpots()`; the view renders one additive spot pass per spot into
  `HeadLightsRT` (R16F, headlight.frag = ps_1), then `EdenShadows::resolve(..., pHeadLights)` adds the ps_0 blit
  (headlightblit.frag) into G/B of the shadow buffer. Switches `-noprojector`, `-showheadlights`, `-hldebug 1..5`,
  `-loghl`.
* Beams: `CinematicCar::loadBeams()` (fx/weatherfx.bnk, `lightbeam_rt`) and `drawBeams()` at the front `Fa_L_F?`
  nodes with LightBeamFadeParams (section 9). Switches `-nobeams`, `-beamintensity x`.
* Chase camera (chase_camera.cpp): the eye is no longer clamped above the island heightfield when the car is
  more than 2 m below it (tunnels) [INF].

## 5. Results (FREERIDE chase view, `-skipcine -fixeddt 50`)
Screenshots in the session scratchpad `carlights/`: `before_rear.png` (GLOSSFCT = data (1,1,1,1): every lamp lit),
`after_rear.png` (14:00, no pedal: dark red round lamps, clear lower lamps, as the reference picture),
`compare_ref_before_after.png` (reference / before / after), `after_brake.png` (brake: lower red lamps + the
third brake light strip of the r mask), `night_ind.png` (`-weather 23.20 -indicator left`: rear position rings,
left blinker lit, HEADLIGHT box light pool ahead), `night_brake_ind.png` (+ brake: BREAKING box red pools behind
the car), `night_brake_indoff.png` (blink dark phase). Remaining rear difference: the horse badge and the chrome
take the colour of the reflected sky (blue at 14:00, the reference is a hazy sunset).

Second pass (scratchpad `carlights2/`, Ibiza, `-weather 23.20` unless noted):
* `base_front.png`: front view at night before this pass. `r9.png`: `r7_off` (`-noprojector`) / `r9_on`: the
  projected headlight pool ahead of the car. `r8.png`: the same before the `_sat` modifiers (overexposed, fixed).
  `r3_hl.png`, `r4*` / `r5*`: `-showheadlights` / `-hldebug` views of HeadLightsRT.
* `t12.png`: `t1_beam` (`-beamintensity 0.4`, side view: no beam visible [INF], see section 9) and
  `t2_flare_front` (charger_srt8_police: front flare sprites + police bar). `t2_flare_rear.png`: left blinker flare.
* `t3b4b.png`: reverse (hold brake until gear R, then reverse: reverse lamps lit and the REVERSE box).
* `t6.png`: `t6a` in the road 792 tunnel (pose `-carpos 35973.8,6.0,6102.1 -carheading 7`): automatic detection,
  distance 182 m, tunnel values, projected headlight on the walls; `t6b` same pose with `-tunnel 0`; `t6c` the
  tunnel seen from the front (`-chaseorbit`).
* `t4c1c.png`: the dark marks on the California front are also present with `-nobeams` (not from the beams).
* `fr_night.png`: FREERIDE (`-skipcine -weather 23.20`): position lamps and projected headlight in the village.
* Follow-up: the white band of `t4c_tunnel_front` (camera above the hill, before the chase camera change) was not a
  light: it was `5067Shadow_tunn` (sector-5-0-6-7-o, TD_ShadowCaster GUID ac78df32bc4589df), the shadow caster hull
  over the tunnel, drawn in the main view. Its handler (0x00d70f30) returns bucket 0x38 = SHADOWCASTERONLY (bucket
  name table 0x00f7f604, entry index = bucket + 1) [CODE + DATA]; EdenRenderer::draw now draws bucket 0x38 in the
  shadow caster passes only (`y1.png`: same view, the road under it). The bright rocks / planter edges of `t1c_nobeam`
  are unchanged with `-noprojector`, `-carlights 0` and `-nonightlmap` (`y2345.png`: on / -noprojector /
  -carlights 0 / HeadLightsRT, `y6.png`): not from the car lights; the projected headlight only covers the cone
  ahead of the car (HeadLightsRT panel).

## 6. Tunnel detection (car +0x2acc) [CODE + DATA]
### 6.1 Rule (FUN_00c62a70, end of the car update)
* `+0x2acc = 0`, `+0x2ad4 = 0`; then, if the driver (`car+0x280`) is a player kind (`ctrl+0x1c & 3 == 1`) and the
  camera (0x01195ba0, 4 floats) is closer than 100 m to the driver position (`ctrl+0x80`, squared distance <
  10000.0, 0x00edda84):
  * no route helper (`ctrl+0x79d4 == 0`): `+0x2acc = FUN_00c41d20()`, `+0x2ad4 = clamp((d − 10) / 10, 0, 1)` (QWORD
    0x00f49820 = 10.0);
  * with a route helper whose current road section (helper +0x1b4 road, +0x1ba section) is of type 6: `+0x2acc =
    5.0` (0x00edf65c), `+0x2ad4 = 1.0`.
* `+0x2ad0 = k · +0x2ad0 + (1 − k) · +0x2ad4`, `k = exp(log(0.3) · dt / 0.0333)` (0x00f4a178 = 0.3, 0x00f4a270 =
  1/30; 0xe2dc0c = log [INF: CRT import beside exp 0xe2db8a and fmod 0xe2dc12], 0xe2db8a = exp).
* Readers: `+0x2acc >= 5.0` selects the `tunnel_car_*` weather values (FUN_00c573c0 poslight 0xfd5248/44,
  FUN_00c573f0 caustics 0xfd5240/3c, FUN_00c57420 flare 0xfd5238/34, FUN_00c57450 headlights 0xfd5230/2c) and
  FUN_00c638d0 / FUN_00c63a30 (light boxes); `+0x2ad0 > 0` (0x00ecd6ec) makes FUN_00c4df00 blend the car lighting
  (light_ambient / diffuse / specular) toward `tunnel_light_*` (FUN_00459b50 lerp by +0x2ad0).

### 6.2 FUN_00c41d20: distance to the nearest tunnel end
The driver road position: `ctrl+0x20` = 1-based handle into the road-position table (DAT_0119b438 count,
DAT_0119b43c entries of 0x30 bytes, entry +4 = handle), `ctrl+0x24` = parameter on the road (FUN_00cf4540: ×
road length +0x18 walked through the road's section list). FUN_00cf9f40 returns the road object (road +0x28 =
its ROAD_DETAIL record). If the section at the position has **type 6** (section +0x11), the position is walked
segment by segment (one cubic Bezier segment = 3 control points, `(numPoints + 2) / 3 − 1` per section,
FUN_00cfcbd0 / FUN_00cfcb60 test the road ends) forward and backward while the section type stays 6; FUN_00cfde30
measures the road distance from the car to both ends; the smaller one is returned (0 outside tunnels).

### 6.3 Road network data [DATA] (`<island>.bnk` group 12)
* sub 0 `Ibiza_ROAD_MAP.IA` ("RI41DAEH", "EDON" node records), sub 1 `Ibiza_ROAD_DETAIL.IA`: "DI41DAEH", u32
  number of roads (1392), then per road `"ATED", u32 id, u32 numSections, u32 offset[numSections]` (from the
  record) and the sections: `u16 numPoints, u16, u32, u32, u32, u8, u8 type, u16 length (m)`, then the points
  (`f32 x, y, z`, plus one f32 after every knot: small values, bank angle [INF]). Section length = Bezier arc length
  ± 2 m. The loaded copy is used in place by the code (record +8 count, +0xc + 4i section, section +0 numPoints,
  +0x11 type).
* Ibiza section types: 0 (216), 1 (497), 2 (3022), 3 (1816), 4 (29), **6 (30)**. Tunnel runs (road id: length,
  sectors): 473: 130 m (3-3-3-4), **792: 521 m (5-0-6-7 / 5-1-6-0, from (35951, 4, 5921) to (35989, 9.6, 6436))**,
  825: 526 m (3-1-5-0), 826: 533 m (3-0-3-7), 827: 915 m (3-0-2-7 / 3-1-2-0), 864: 197 m (3-4-0-2), 867: 105 m
  (3-3-4-5), 868: 165 m (3-3-3-5), 869: 453 m (3-3-5-4 / 3-3-5-5), 907: 99 m (3-4-1-0 / 3-4-2-0), 948: 425 m
  (4-2-6-7), 1006: 237 m (3-4-0-1). The road 792 tunnel is the `5067tunnel_mesh` / `5160tunnel_mesh` geometry
  (material TUNNEL_C, RoadTunnelMater) of sectors 5-0-6-7 and 5-1-6-0.
* Not used here: `IsTunnel` (0x00ef27e0) is a property of another reflected class (FUN_00cbea40 ctor), not read by
  the car code.

### 6.4 OpenTDU2
`RoadNetwork` (source/world/road_network.*): loads ROAD_DETAIL, `project()` = nearest road centre line within 10 m
horizontally / 5 m vertically [INF: stands in for the game's road position of the driver, kept by its traffic / AI
code], `getTunnelDistance()` = FUN_00c41d20, `getCarTunnelDistance()` = the 100 m camera rule.
`CinematicCar::setTunnelDistance(d, dt)` sets +0x2acc, the tunnel flag (d >= 5), +0x2ad4 / +0x2ad0. FREE_DRIVE
and FREERIDE call it every frame. `-tunnel 0|1` forces the flag, `-logtunnel` (FREE_DRIVE) logs it. The +0x2ad0
blend of the car lighting toward `tunnel_light_*` is exposed (`getTunnelBlend`) but not applied to the car
shading.

## 7. Headlight flares (sprites) [CODE + DATA]
* FUN_00c5f060 (once, DAT_00fd5210): for each letter of **"LBSWR"** (0x00ef5fb8) and n = 0..5, the hashcodes of
  `Fa_%c_FL`, `Fa_%c_FR`, `Fa_%c_RL`, `Fa_%c_RR` (n = 0) or `Fa_%c_FL%d`... (n = 1..5), each also with `_C`
  appended (0x00ef5fcc); per car the nodes of those names (car +0x710 node list): flare nodes RL / RR / FL / FR at
  car +0x1f84 / +0x1ffc / +0x2074 / +0x20ec (+ type·0x18 + n·4), their objects at +0x2164.., the `_C` glow mesh
  nodes at +0x232c / +0x23a4 / +0x241c / +0x2494; FUN_00c5efe0 flags the TD_Building (`183157b81c6cff57`)
  materials of the `_C` objects (+0x346 = 1, FUN_008a8950). FUN_00c582e0 keeps those objects out of the normal
  car draw list; FUN_00c588d0 sets / clears node flag 4 (hidden) on all of them while `car+0x2adc < 1`. The
  per-function visibility is written by the encrypted table functions [ENC].
* Data: **the California has only the `_C` glow meshes** (Fa_L_FL_C, Fa_L_FR_C, Fa_R_RL_C, Fa_R_RR_C, Fa_S_RL_C,
  Fa_S_RR_C, Fa_W_FL_C, Fa_W_FR_C, Caustics materials): it has no flare sprite in the game. Flare sprite nodes
  (7 / 8 character names, found in the bank strings) exist in 6 of the 133 car banks: ac_427, charger_srt8_police,
  desmosedici, diavel, fatboy, viper_srt10_police. Example charger_srt8_police: FA_L_FL1..3 / FA_L_FR1..3
  (FRL_L, scale 0.47..0.66, rotated 180° about Y: their local Z faces backward), FA_B_RL / RR (FRL_F), FA_R_RL /
  RR (FRL_L), FA_S_RL / RR (FRL_S), FA_W_RL / RR (FRL_W); FA_G_* are the police light bar (FRL_G, FUN_00c596f0,
  `"?CALEDIS"` z = 0).
* Sprite: 4 vertex quad, position 0, normal = corner (±1, ±1) [DATA]; materials FRL_x = TD_Sprite
  (`fcdaa96bdeadbeef`, the common_mesh.2dm FRL_F / L / S / W and the car's own FRL_x), blend SRCALPHA / ONE,
  texture LIGHT_FX (white star) or LIGHT_FXRED (FRL_L of common_mesh), vertex colour FA_F / FA_S (255, 62, 0),
  FA_W (255, 167, 0), FA_L white. TD_Sprite vs (e.g. vs_128) [SHADER]: `ScaleDist` = (2, 25, 2, 0.004):
  size × (1 + clamp(dist − 2, 0, 25) / 25 · 2), quad facing the camera (c29..c31) scaled by the node scale (c61.y)
  and by `k = 0.5 − dot(toCamera, localZ)`, alpha × k² (view angle fade), pushed toward the camera by
  `k · dist · 0.004 · scaleZ` (depth bias); colour = vertex colour × `__Instance_Color` (rgb × w); the soft
  particle permutations test the scene depth (FullscreenDepthTarget). Occlusion = the depth test / soft depth (no
  readback query in readable code).
* Letters [INF: from the node positions and colours]: L front lights (headlight function), B back = rear position
  lights, S stop, W warning (blinker of the node side), R reverse. Intensity: the encrypted functions presumably
  write the node colour (`__Instance_Color`); OpenTDU2 keeps (1, 1, 1, 1) and shows / hides the node with its
  function value (car_headlight_flare_intensity for L etc.) [INF].
* OpenTDU2: `CinematicCar::collectLights` builds the same name hashes (flareNodes, causticNodes); `applyLights`
  shows each node when its function value > 0. `-noflares` hides the sprites.

## 8. Projected headlight (HEADLIGHTS pass) [CODE + SHADER]
### 8.1 Manager (DAT_00fd964c, ctor FUN_00921d90, init FUN_00922d60)
26 slots of 0x9a0 bytes at +0x30 (count +0xfa8c). Constants (ctor, 4 variants: 0 player car (slot +0x2d8 set:
FUN_0042b1d0 / FUN_00c3f6d0 of the driver), 1 other cars, 2 / 3 the same for vehicle kind 1 (bikes)):

| field | values (variant 0..3) | use |
|---|---|---|
| +0xfaa8 | 62, 17, 118, 62 | beam near (FUN_00922d60 uses [1]) |
| +0xfab8 | 102, 22, 107, 127 | far (slot +0x360) |
| +0xfac8 | 1.4835, 1.1345, 1.1868, 0.7854 | slot +0x364 (near of the attenuation) and +0x36c (inner cone angle) |
| +0xfad8 | 1.6057, 1.4486, 1.3614, 1.1519 rad | slot +0x368 (fov = outer cone angle) |
| +0xfae8 | 0.4712, 0.7854, 0.6283, 0.5411 rad | slot +0x20 tilt (rotation about X, down) |
| +0xfaf8 | 3.1, 2.3, 1.2125, 1.5125 | slot +0x18 height offset |
| +0xfb08 | −1.6, −0.6, −0.3, −0.8 | slot +0x1c (= −value) back offset |
| +0xfaa0 / +0xfaa4 | 400 / 650 | camera distance fade (and beam fade × 0.18) |

### 8.2 Per car (FUN_00921260, called at 0x00c3eb28 / 0x00c3eca4 for each driver)
Slot +0x10 = car_headlights_intensity (tunnel_, FUN_00c57450); +0x1d0.. / +0x50.. / +0x110.. = lamp matrices from
the encrypted member functions of the tables 0x012307b8 / 0x012307f0 [ENC]; +0x2d0 / +0x2d1 = brake / reverse;
+0x24 = high beam (player, light mode 2). The spot frame: `M = RotX(+0x20) · T(0, faf8 + 0.5·v.y, fb08 + 0.5·v.z)
· carMatrix` (row vectors; `v` = the vector returned by the driver vfunc +0x18 [INF: the car size]); position
+0x330 = M.T, direction +0x340 = normalize(M.row2). Not submitted when `ctrl+0x1d8`, beyond 650 m from the camera,
or out of the view frustum (sphere at car + forward · far / 2).
FUN_009208f0: +0x360 = far, +0x364 = +0xfac8, +0x350 = far / (far − near), +0x354 = 1 / (far − near), +0x368 =
+0xfad8, +0x36c = +0xfac8; FUN_00da07c0: a = f(+0x368 / 2), b = f(+0x36c / 2), +0x358 = −a / (b − a), +0x35c = 1 /
(b − a) (f = 0x6c8d32 [ENC; INF: cos]).

### 8.3 Shader constants (FUN_009227c0) and pass (FUN_00923230)
Screen quad `HL_ScreenQuad#n` with an `edrender_pfx_lightness` material (FUN_00922030), into **HeadLightsRT**
(FUN_00d75d00, screen size) in the pass `HEADLIGHTS`, then `HEADLIGHTS BLIT` [CODE]:
* `Spot_PositionDropOffNear` = (view-space +0x330, w); w = m = max(sqrt(S18² + S1c²), g(π/2 − tilt − fov/2) · S18)
  (g = 0x40d470 [ENC; INF: tan]), replaced by 0.5 + f / 1.01 when f = 1.01·far·(1/far) > m − 0.5 (never for the
  constants: w = 3.49 m for the player car).
* `Spot_Direction` = view-space +0x340; `Spot_Attenuation` = (+0x350, +0x354, +0x358, +0x35c); `Spot_Color` =
  (+0x300, +0x304, +0x308, 1) (not written by readable code [ENC; INF: 1]); `ProjMatrix` = FUN_00e27470 (spot
  look-at + perspective of fov +0x368); `vars` = (1 / RT width, 1 / RT height, intensity, ·).
* intensity = I' · clamp(I, 0, 1)² · fade, I = slot +0x10, I' = I (1 if high beam and I < 1), fade = 1 below 400 m,
  linear to 0 at 650 m (FUN_00923230).
* `edrender_pfx_lightness` ps_1 [SHADER, result modifiers read from the tokens]: view position from Zpass, `L = P −
  pos`, `c = sat(dot(L/|L|, dir))`, value = `(sat(c·att.w + att.z) · sat(att.x − |L|·att.y))² · Spot_Color.x ·
  texldp(gProjTexture, ProjMatrix·P).y · vars.z · drop`, drop = 0 below w − 0.5, 2(|L| − (w − 0.5)) up to w, 1
  beyond; `min(value, 32)`; ps_2 = the same + sFrameBuffer (accumulation of the slots).
* Blit = ps_0: `t = sat((1 − 2^(−0.28853902·L)) · 0.15625)`, written depth-style (`y = (E·8 + floor(8f)) / 255,
  z = frac(8f)`, `t·32768 + 0.01 = 2^E (1 + f)`) into G / B of the s15 buffer; the scene shaders (TD_Road ps_0...)
  decode G / B and add `× 32` to the lighting before the albedo (`(A·lit + 32·t) · albedo`).
* Texture: `projectivespotlighttexture` (fx/weatherfx.bnk, 512², the headlight pattern): it is the texture of the
  material "No Texture" of `weatherfx/headlight/headlight.3dd` (node CUBE_01) [DATA], the model the manager loads
  by bank ids (+0xfb18 / +0xfb1c, FUN_00920660 / FUN_00920890 copy its material +0xc + 0x10 into the slot)
  [CODE; INF: ids not resolved, the only such model]; the name string is also stored with the HL_ materials
  (0x00f2fe7c). `weatherfx/headlight/caustics.3dd` holds BreakCaustics / reversecaustics / headlightcausti boxes.
* OpenTDU2: `CinematicCar::getHeadlightSpot` (variant 0), `EdenSceneView::setHeadlightSpots`, the GLSL passes
  `resources/shaders/headlight.frag` (ps_1 math, spot frame) and `headlightblit.frag` (ps_0, added to G / B of the
  shadow buffer after the shadow combine). The HL cone stencil is not needed (the saturated terms are 0 outside).
  Needs the real time shadow buffer (not drawn with `-noshadows`). `-noprojector` disables it; `-showheadlights`
  shows HeadLightsRT; `-hldebug 1..5` debug outputs.

## 9. Light beams [CODE + DATA + SHADER]
* FUN_00922d60 creates per slot 6 beam objects (FUN_00964a30: model of the bank ids +0xfb20 / +0xfb24, material
  cloned with `_WithZ`, the LightBeam_RT target added as a sampler, `LightBeamFadeParams` pointer kept).
  FUN_00923230: when the manager flag +0x11 (1) and slot +0x2d8 (the player car) are set, for each lamp k of the
  slot (front +0x50 + k·0x40 when +5+k, the others +0x110 + k·0x40 when +8+k) FUN_009211e0 orients the beam
  (RotY of the global 0x0145a2a0, mirrored for the other side, π when not high beam) and FUN_00964b90 draws it:
  `LightBeamFadeParams = (400 · 0.18, 1 / (250 · 0.18), I'' , car_lightbeam_intensity)` with I'' = clamp(I, 0, 1)²
  · fade (0x00f4b098 = 0.18), blend SRCALPHA / ONE.
* Data: fx/weatherfx.bnk `lightbeam_rt.3dd` (cone mesh, node at z 12.3, scale 2) with `LightBeam_RT_01`
  (TD_LightBeam `693b66820e317b71`, RT permutation vs_3 / ps_3: analytic cone integration against gZpass, colour
  = gPertubationMap × emissive, `RTLightBeamParams` (12, 0.2, 9, 4)); `lightbeam_std` is the street lamp beam.
  car_lightbeam_intensity [DATA game_weathers.xmb]: absent in the Sunny intempery, 0.2..0.5 in the others at night.
* OpenTDU2 [INF: lamp matrices encrypted]: one beam per front side at the `Fa_L_F?` node (flare or _C) position,
  car orientation, the model's own node transform. `-nobeams`, `-beamintensity x` force.
