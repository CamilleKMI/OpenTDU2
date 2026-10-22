# Avatar lighting (weather avatar_* parameters, interior spot lights, camera light, CARINTEG/ATTEFACT)

Reverse-engineering spec for OpenTDU2. Target: avatars (TD_Skin2 / TD_Skinface / TD_Clothe2 / TD_Hair / TD_Eye /
TD_Avatar / TD_Accessories2) are too dark in the party house during the intro cinematics (Tess in C_FIESTA_INTRO
shot 2, C_TESS).

Tags:
- **[CODE]** readable x86 of TestDrive2.exe (VA >= 0x6c1000).
- **[DATA]** game files.
- **[SHADER]** `.sob` bytecode (`/tmp/claude-0/sob/all/<family>/*.dxso`, disassembled with `py/d3d9dis.py`, CTAB read
  with `py/ctab.py`).
- **[INF]** inference; the reason is given.
- **[ENC]** the path goes into the SecuROM range; it was not read.
- **[UNK]** unknown.

Calling conventions [CODE, from usage]:
- `FUN_0041d8a0(lo, hi)` [ENC callee] is stdcall (it pops its 8 bytes). EAX = weather context. It returns the
  registry entry; the current value is at `+0x18`.
- `FUN_00459b50(ecx = &a, edx = &b, [&out, t])` [ENC] is cdecl. Its use in the rain path (b87d55) shows it is
  `lerp(a, b, t) = a + (b - a)·t` [INF, strong].
- `FUN_0040dae0(esi = &vec4)` [ENC] normalizes the vector in place and returns ESI in EAX [INF: every call site passes
  a direction with w = 0 and reads back the same buffer].
- `0x6c8d32` and `0x6c8d3e` sit inside the encrypted block just above 0x6c1000; their bytes are garbage [ENC].
  - 0x6c8d32 is called on `angle·0.5`, so it is cos [INF].
  - 0x6c8d3e is called on a squared length, so it is sqrt [INF].

---------------------------------------------------------------------------------------------------
## 0. Summary

1. **Storage** [CODE]. Every `avatar_*` value is a weather-registry entry. The registry is built by FUN_00af30d0,
   vector B at 0xf84dd8, push FUN_00af2730.
   - Each entry is 0x38 bytes: `+0 u64 id` (GetIdentifier64bit of the name), `+8 vec4 default`, `+0x18 vec4
     current`, `+0x28 type` (1/2/3 = float1/2/3), `+0x30 name`.
   - Consumers look the entry up by id with FUN_0041d8a0. Nothing caches it in a struct.
   - The per-avatar results are stored in the character object, the avatar instance light objects, and the
     material symbols CARINTEG / ATTEFACT (§2).
2. **The pipeline per frame**:

   ```
   FUN_00a4da90  (gather: interior/exterior Entity__Light spots + sun, shadow influence, camera light)
     -> char+0x178 light block, char+0xe0 shadow, inst spot light
   FUN_00b89060 -> FUN_00b87aa0 (lum clamp, factors, in-car multiplier)
     -> inst ambient light (+0x2a00) and dir light (+0x2a50)
   FUN_00cacc00 -> FUN_00caf850 (per material)
     -> CARINTEG = (h0, h1, shadow, 1), ATTEFACT = (1-a, 1-d, 1-s, envmap)
   ```

3. **avatar_sun_ambient / avatar_sun_diffuse / avatar_sun_specular have no readable consumer.**
   - Their ids (`98a8948e 95a0f8e4`, `a8a5988a 98a0e9e9`, `a8a29791 a7f2e5e2`) appear nowhere in .text or in the
     data sections apart from the registration.
   - Either they are read from encrypted code or they are unused [ENC/UNK].
   - The readable avatar light uses the **generic `sun_ambient` / `sun_diffuse` / `sun_specular` / `sundirection`**.
4. **The likely reasons OpenTDU2 is too dark** [CODE + SHADER + DATA]:
   - **(a) CARINTEG/ATTEFACT.** OpenTDU2 leaves the 2DM values in place: CARINTEG = (0.35, 0.5, 1), ATTEFACT =
     (0, 0, 0) on body, clothes and hair.
     - That gives a per-vertex light scale `k = (y_obj − 0.35)/0.15`, which is unclamped (negative below 0.35 m, ≫1
       above).
     - The game overwrites these symbols on every avatar material: (0, 0, shadow, 1) and (1, 1, 1, env) out of a car,
       so k ≡ 1.
   - **(b) Real-time shadow.** OpenTDU2 applies the full shadow (CARINTEG.z = 1). The game uses
     `avatar_shadow_influence × (1 − best_spot_weight)`, which is 0.4 or less with the `Avatar` weather.
     - In the house the sun shadow map covers everything (roof), so OpenTDU2 avatars keep only their ambient light.
   - **(c) Interior spot light gathering** (`avatar_spotlight_interior_exterior_influence` = (0.5, 0) with `Avatar`).
     It changes the light colours and direction.
   - **(d) Camera light** (`avatar_cameralight_factor` 0.25). It feeds only the `__SpotLight_*` of TD_Avatar,
     TD_Accessories2 and TD_Pedestrian.
5. **Weather in the party-house cinematics** [DATA]:
   - The base preset is `weat:Avatar`, set by the AVATAR_CHOICE flow; select_character_spec §2.4.
   - The C_FIESTA_INTRO_01..06 `.cin` files carry only `wmod:Avatar_Avatar`, which changes nothing but `sundirection`.
   - The C_TESS cin has no weather message.
   - The game mode is AVATAR_CHOICE (0x20), which is in the "interior" list, so gathering is on.

---------------------------------------------------------------------------------------------------
## 1. Registry entries [CODE FUN_00af30d0; ids computed with py/edenid.py; defaults read from the registration code]

| name | reg. code | id (hi lo) | type | default | readable consumers |
|---|---|---|---|---|---|
| avatar_cameralight_factor | af65fb | d9fce1c3 ece5f71c | f1 | 0 | a4da90 (gate), a4ca50 (colour scale), 9b4c10, a171f0 |
| avatar_cameralight_desynchro | af6647 | d801f1c7 39373f0d | f1 | 0 | a4ca50, 9b4c10, a171f0 (< 0.5 → camera axis, ≥ 0.5 → weather direction) |
| avatar_cameralight_direction | af6693 | cdedf0cb 3834401e | f3 | (0, −1, 0) | a4ca50, 9b4c10, a171f0 |
| avatar_spotlight_interior_exterior_influence | af66eb | 948e7671 dacaedd6 | f2 | (0, 0) | a4da90 → a4beb0 |
| avatar_car_envmap_factor | af673b | d6fbfcd2 dae607d2 | f1 | 0.7 | b87aa0 (→ char+0x19c → ATTEFACT.w), cac7e0 (helmet) |
| avatar_sun_ambiant_factor | af678f | e3fcd7cf dbfff836 | f1 | 1 | b87aa0, b92d20 |
| avatar_sun_diffuse_factor | af67e3 | f7f9dbcb deffe93b | f1 | 1 | b87aa0, b92d20 |
| avatar_sun_specular_factor | af6837 | fce5d8d7 06f23731 | f1 | 1 | b87aa0, b92d20 |
| avatar_car_night_reflection_factor | af688b | 3f2d1f1c 47339b79 | f1 | 0 | b87aa0 |
| avatar_lum_min | af68d7 | 4c5fa08a a1a0a396 | f1 | 0 | b87aa0, b92d20 |
| avatar_lum_max | af6923 | 4c5faa82 a1a0a396 | f1 | 1 | b87aa0, b92d20 |
| avatar_shadow_influence | af6977 | a1edf4e6 e8dae3cf | f1 | 1 | a4da90 (2×), b87aa0 |
| avatar_sun_ambient | af69cb | 98a8948e 95a0f8e4 | f3 | (0.1, 0.1, 0.1) | **none readable** [ENC/UNK] |
| avatar_sun_diffuse | af6a23 | a8a5988a 98a0e9e9 | f3 | (0.8, 0.8, 0.8) | **none readable** [ENC/UNK] |
| avatar_sun_specular | af6a7b | a8a29791 a7f2e5e2 | f3 | (1, 1, 1) | **none readable** [ENC/UNK] |
| avatar_rain_specular_coef | af6ad3 | daf3e5f3 05d0eb1d | f1 | 1 | b87aa0 (in-car path, via FUN_0041d8f0) |
| car_spotlight_interior_exterior_influence | af5b20 | 89979594 8e7671c9 | f2 | – | a4da90 tail (cars, not avatars) |

Weather context passed in EAX to FUN_0041d8a0 [CODE]:
- Most parameters: `[0x117a3a8] + [0x117a320]·0x44` (current view/weather slot).
- `sundirection`: fixed slot `0x117a364`.
- Both are the active preset in practice [INF].

Values [DATA, `py/weather.py islands/ibiza/fx/cinematic_weathers.xmb`, full dump `/tmp/claude-0/al/cw.txt`]:

| preset | sio (int, ext) | cam factor / desync / dir | lum min/max | amb/diff/spec factor | shadow infl | env / night | sun_ambient | sun_diffuse | sun_specular |
|---|---|---|---|---|---|---|---|---|---|
| **Avatar** | **(0.5, 0)** | 0.25 / 0 / (0, −1, 0) | 0 / 1 | 1 / 1 / 0 | **0.4** | 1 / 0 | (0.502, 0.580, 0.769) | (2.055, 1.522, 0.957) | (1.914, 1.443, 1.082) |
| Intro | (0, 0) | 0.2 / 0 / (0, −1, 0) | 0 / 1 | 1 / 1 / 1 | 0.4 | 1 / 0 | (0.314, 0.392, 0.549) | (2.431, 1.820, 1.114) | (1.914, 1.443, 1.082) |
| Meeting_Tess | (0, 0) | 0.2 / 0 | 0 / 1 | 1 / 1 / 1 | 0.5 | 1 / 0 | (0.502, 0.580, 0.769) | (2.055, 1.522, 0.957) | (1.082, 0.863, 0.769) |
| Home, Cin_Gen | (0, 0) | 0.2 / 0 | 0 / **0.8** | 0 / 0.5 / 0.5 | 0.5 | 1 / 0 | … | … | … |

---------------------------------------------------------------------------------------------------
## 2. Readable consumers

### 2.1 Driver FUN_00a4da90(ctx) [CODE]

FUN_00a4da90 is called from about 23 game-mode update functions (9a4916 … de1450). `ctx` is the mode's light
container. Its lights are at `ctx+0xbc0 + i·0xa0`, with the count at `ctx+0xbbc` (at most 64).

It reads, in this order:
- `cf = avatar_cameralight_factor`;
- `sio = avatar_spotlight_interior_exterior_influence.xy`;
- `sunDir = normalize(sundirection)`;
- `sunA`, `sunD`, `sunS` = `sun_ambient`, `sun_diffuse`, `sun_specular`.

**Mode gate**:
- mode = `[[0xfd50fc]+8]`; names come from the table at 0xf7eed8.
- If the mode is **not** in {SHOP 3, CARSHOWCASE 6, GARAGE 8, LIVING 9, GOODIES 0xa, GAMBLING_HALL 0xb, DRESSING 0xc,
  ESTATES 0x11, CLUB 0x12, LOBBY 0x13, DRIVING_SCHOOL 0x16, HAIRDRESSER 0x19, PHOTOGRAPH 0x1a, CASINO 0x1d,
  STICKERS 0x1e, CINE 0x1f, AVATAR_CHOICE 0x20, SURGERY 0x21, AIRPORT 0x22, FIRST_WM 0x23}, then `sio = (0, 0)`.

**Loop 1** runs over the avatar list `DAT_011bb894[i]` (16-byte entries, count `DAT_011bb898`). Each `char` has an
instance `inst = char+0x1d4`.

```
P = FUN_00b87100(char)              (a skeleton point [ENC]) transformed by char+0x10 (world matrix) [INF],
                                    minus the world origin (0x1367a10, or 0x1178130 if [0x117810c]) when char+0xc == 0
if (sio.x > 0 || sio.y > 0 || cf > 0):
    w = FUN_00a4beb0(ctx, &P, &dir, &amb, &diff, &spec, &sio)       // §2.2
    char+0x178 -> { +0x00 dir, +0x10 amb, +0x20 diff, +0x30 spec }
    char+0xe0  = avatar_shadow_influence * (1 - w) + w * 0.0        // double 0.0 @0xf49750
    if (inst+0x29f8 spot light): FUN_00a4ca50(char, amb, diff, spec)  // camera light §2.4
    FUN_00448f50(inst, {9, 1})                                      [ENC]
else:
    char+0xe0  = avatar_shadow_influence
    char+0x178 -> { sunDir, sunA, sunD, sunS }
    spot light (inst+0x29f8) colours +0x10/+0x20/+0x30 = 0          (camera light off)
    FUN_00448f50(inst, {9, 0})                                      [ENC]
```

**Loop 2** (a4e1be) runs over `[0xfd9290]+0x214[n]` (count `+0x208`; objects whose vfunc +0x68 is true and whose
`[obj+0x178]+0x1b0/+0x1b1` are set; pedestrians/NPCs [INF]).
- It does the same gathering and camera light, with the light object at `[obj+0x178]+0x50`.
- It writes **no** shadow value.
- These objects are finished by FUN_00b92d20 (§2.7).

**Tail** (a4e5ad): the same gathering for cars at 0x102d880 with `car_spotlight_interior_exterior_influence`. Not
needed for avatars.

### 2.2 Gathering FUN_00a4beb0(ctx, P, outDir, outAmb, outDiff, outSpec, sio) → returns w [CODE]

```
best = FLT_MIN (0x00800000); bestIdx = -1; qBest = 1
accDir = accA = accD = accS = 0
for each light E (i < ctx+0xbbc):
    d    = P - E.pos                                   (E+0x50; from light to avatar)
    s    = clamp(|d|^2 - E[0x84]^2, 1.1920929e-7, 100000)      E+0x84 never written by readable code [UNK, assume 0]
    infl = E.isInterior (E+0x90) ? sio.x : sio.y
    if infl > 0 and coneTest(E, d, s):                 FUN_00a4ba40
        len = sqrt(s)                                  0x6c8d3e [ENC, INF sqrt]
        a   = atten(E, len, d) * infl                  FUN_00a4bd70
        if a > 0:
            q = s*s
            accDir += normalize(d) * a / q
            accA += E.amb (E+0x10) * a;  accD += E.diff (E+0x20) * a;  accS += E.spec (E+0x30) * a
            if a > best: best = a; qBest = q; bestIdx = i
if bestIdx != -1:
    t = 1 - (best - 1.1920929e-7)                      (doubles @0xf4ace0, 0xf44c70)
    accDir += sunDir * t / qBest
    accA += sunA * t;  accD += sunD * t;  accS += sunS * t
    outAmb = accA; outDiff = accD; outSpec = accS
else:
    accDir = sunDir; outAmb = sunA; outDiff = sunD; outSpec = sunS
outDir = normalize(accDir)
return best        (≈ 0 when no light contributes)
```

Supporting functions:
- `coneTest` (a4ba40): `c = cos(E[0x88]·0.5)`; true iff `dot(E.dir, d)^2 ≥ c²·s` **and** `dot(E.dir, d) > 0`.
  E+0x88 is the *outer* full angle.
- `atten` (a4bd70):
  `saturate(E[0x70] − E[0x74]·len) · saturate(E[0x7c]·dot(E.dir, normalize(d)) + E[0x78])`.

Light-entry precompute FUN_00da07c0 [CODE]:
- `cO = cos(E[0x88]/2)` and `cI = cos(E[0x8c]/2)`.
- `E[0x7c] = (cI − cO > 1.19e-7) ? 1/(cI − cO) : 8388608`.
- `E[0x78] = −cO·E[0x7c]`.
- So the cone term is `saturate((dot − cos outer)/(cos inner − cos outer))`.
- `E[0x70]` and `E[0x74]` are set by FUN_004bf410(E, cutOff, dropOff) [ENC]. [INF] they are a linear fall-off,
  `saturate((cut − len)/(cut − drop))`, i.e. E70 = cut/(cut − drop) and E74 = 1/(cut − drop).

### 2.3 Light list: Entity__Light → ctx (FUN_00deef20 → adder FUN_00a4c7a0) [CODE]

Registration FUN_00deef20 (Entity__Light; once, flag +0x14c):
- A jump table on `mode − 3` picks the ctx.
- SHOP, GARAGE, LIVING, GOODIES, GAMBLING_HALL, ESTATES, CLUB, DRIVING_SCHOOL, HAIRDRESSER, PHOTOGRAPH, CASINO,
  STICKERS, **AVATAR_CHOICE**, SURGERY, AIRPORT, FIRST_WM → `(modeobj−0x90)->vfunc+0x64()`.
- DRESSING → `mode+0x17070−0x90`.
- CINE → FUN_009b42b0.
- Other modes register no lights.

Entity__Light fields. The loader FUN_00def230 reads them by name; the strings are at 0xf10cb4… [CODE]:
`+0xe0 Rot, +0xf0 Pos, +0x100 Ambient, +0x110 Diffuse, +0x120 Specular, +0x130 Intensity, +0x134 DistanceDropOff,
+0x138 DistanceCutOff, +0x13c AngularDropOff, +0x140 AngularCutOff, +0x144 isInterior` (= value ≠ 0, default 1).

> Correction to pool_interior_spec §5.3: +0x13c is **AngularDropOff** and +0x140 is **AngularCutOff**. This follows
> the string order at def319/def331.

Entry `E` written by a4c7a0 (`E = ctx+0xbc0 + n·0xa0`; skipped when n == 64):

| E offset | value |
|---|---|
| +0x10 | Ambient × Intensity |
| +0x20 | Diffuse × Intensity |
| +0x30 | Specular × Intensity |
| +0x50 | entity +0x40 (matrix translation = orient_pos_scale.pos) − world origin |
| +0x60 | normalize(−entity +0x20) = −Y axis of orient_pos_scale [INF: for RT_Int_78 (rot (0, 0, −0.174, 0.985)) this gives (−0.34, −0.94, 0), a ceiling spot pointing down] |
| +0x88 | 2 × AngularCutOff (outer full angle) |
| +0x8c | 2 × AngularDropOff (inner full angle) |
| +0x70..+0x7c | FUN_004bf410(E, DistanceCutOff, DistanceDropOff) [ENC], then da07c0 |
| +0x90 | isInterior |

House data [DATA, `/tmp/claude-0/tg/ni_ent.txt`]:
- There are 55 Entity__Light: 23 with isInterior = 1 (RT_Int_*) and 32 with 0 (RT_Ext_*).
- They are at y ≈ 36.17. Drop-off 10, cut-off 20, angles 0.314 (drop, inner) / 0.873 (cut, outer).
- RT_Int: Ambient (0.388, 0.380, 0.380), Diffuse (0.757, 0.678, 0.553), Specular (0.878, 0.835, 0.784), Intensity
  0.45–0.65.

### 2.4 Camera light FUN_00a4ca50(eax = char, edi = spot light, &amb, &diff, &spec) [CODE]

```
anchor = char.pos (char+0x40..0x48) + (0, 1, 0)   (− world origin when char+0xc == 0)
if avatar_cameralight_desynchro < 0.5:
    dir = camera axis from FUN_0043dae0([0xfd51c0]+0xa0) [ENC]
else:
    dir = normalize(avatar_cameralight_direction)   (world space; no camera transform applied [INF])
L+0x10 = amb  * cf;  L+0x20 = diff * cf;  L+0x30 = spec * cf      (cf = avatar_cameralight_factor)
L+0x50 = anchor - 10000 * dir                                      (double 10000 @0xf49bd8)
L+0x60 = normalize(dir)
L+0x88 = L+0x8c = 2.0; FUN_00da07c0; FUN_004bf410(L, 40.0 [0xf90bc8, variable], 20.0) [ENC]
```

- The camera light uses the **gathered** avatar colours (§2.2), scaled by cf, so it is a fill light that follows the
  local lighting.
- It is attached as the instance **spot light**. Instance light set (FUN_00cac920 / FUN_00cab910):
  - `inst+0x29e8 → inst+0x2a00` ambient light (default 0xf86870);
  - `inst+0x29f0 → inst+0x2a50` directional light (default 0xf868c0);
  - `inst+0x29f8 → inst+0x2ab0` spot light (default 0xf86920).
- These map to `__AmbLight_*_0`, `__DirLight_*_0` and `__SpotLight_*_0`. The binder is [ENC]; the mapping is [INF]
  from the three types and from which shaders read spot constants.
- Direction convention [SHADER + INF]. The TD_Avatar PS lights with `max(N · normalize(P_obj − __SpotLight_Pos_0), 0)`.
  - With the light at `anchor − 1e4·dir`, the surfaces facing the camera are lit only if `dir` is the camera's
    **backward** axis.
  - So the readable code is consistent with "SpotPos = anchor + 1e4·cameraForward". Use that in OpenTDU2.
- Variants FUN_009b4c10 (called after a4da90 in FUN_009b54c0) and FUN_00a171f0 (FUN_00a19a30) do the same, but:
  - the colours are `sun_* × cf`;
  - the anchor is FUN_00b87100(char).
  - Their mode is [UNK].

### 2.5 Final avatar light FUN_00b87aa0(char, inst+0x2a50, inst+0x2a00), called from FUN_00b89060 every frame [CODE]

**Mode gate**:
- `useSun = mode ∉ list2`, where list2 = list1 minus LOBBY 0x13 and PHOTOGRAPH 0x1a.
- ESTATES: useSun iff `vfunc+0x64()->+0x829 == 0`.
- CINE: useSun iff the bytes +0x829 and +0x82a of `modeobj−0x90+0x1adc90` are both 0 [INF: they are
  "interior cinematic" flags].
- AVATAR_CHOICE → `useSun = false`.

```
if useSun:
    char+0xe0 = avatar_shadow_influence
    if char in car (char+0x1b8 → car+0x79c0, char+0x230 ∈ {2000, 3000}):
        dir, amb, diff, spec = car cockpit light (car+0x5d0, 0x540, 0x5a0, 0x5b0)
        if vfunc+0x2c() == 1 and wet > 1e-5: spec = lerp(spec, spec * avatar_rain_specular_coef, wet)
    else:
        dir = sundirection (FUN_004224f0 [ENC]); amb, diff, spec = sun_ambient, sun_diffuse, sun_specular
else:
    dir, amb, diff, spec = char+0x178 block (§2.1)

v  = clamp(1.0, avatar_lum_min, avatar_lum_max)        (the 1.0 is a constant: fldz; fadd 1.0 @0xf4aee0)
m  = *(float*)char+0x198                               (light-factor preset, §2.6)
AmbOut  = lerp(amb,  amb  * v, avatar_sun_ambiant_factor)  * m  = amb  * (1 + (v-1)*fa) * m
DiffOut = lerp(diff, diff * v, avatar_sun_diffuse_factor)  * m
SpecOut = lerp(spec, spec * v, avatar_sun_specular_factor) * m
inst+0x2a00 (ambient light) +0x10 = AmbOut
inst+0x2a50 (dir light)     +0x20 = DiffOut, +0x30 = SpecOut, +0x50 = normalize(dir)
char+0x19c = avatar_car_envmap_factor * (1 + (v - 1) * avatar_car_night_reflection_factor);  char+0x1a0 = 1
```

With `Avatar` (lum 0/1) v = 1, so the factors do nothing. With Home and Cin_Gen (lum_max 0.8):
- Amb × 1 (fa = 0);
- Diff and Spec × 0.9.

### 2.6 Material symbols FUN_00b89060 → FUN_00cacc00 → FUN_00caf850 [CODE]

```
p = *(float[6]*)char+0x198:   p0 = m (light multiplier, §2.5), p1,p2 = height range, p3,p4,p5 = attenuation
cacc00(inst, p1, p2, 1-p3, 1-p4, 1-p5, char+0x19c (env), char+0xe0 (shadow), wet)
   → caf850 on inst+0x1180 + i*0x1c0 (11 garment slots), inst+0x88, inst+0xd8
   (or caf850 on inst+0x1d0 alone when char+0x1e8 == 2 && inst+0x1170 <= 0)
then FUN_00cac7e0: helmet material vec (inst+0x2b58) .x = car_fresnel_coeff_power_boost_specular.x * avatar_car_envmap_factor
```

FUN_00caf850 handles each material of the mesh by family GUID (`d5013ed0` high dword):

| family | GUID low | write |
|---|---|---|
| TD_Eye | db79bbbb | CARINTEG = (p1, p2, shadow, 1); ATTEFACT = (1−p3, 1−p4, 1−p5, env) |
| TD_Skin2 | dbcdcdcd | same |
| TD_Skinface | dbcddbca | same |
| TD_Clothe2 | db79aeae | same |
| TD_Visor | cacadaca | same |
| TD_Accessories2 | dba9daca | same |
| TD_Hair | dbdedede | same |
| TD_Belt | 413ab646 426c5358 | same |
| TD_Pedestrian | db79baed | symbol SHADOWATTENUATION (a38aab90 998f8df5) = (shadow, shadow, shadow, 1) |
| TD_Clothe2Bike | db79caca | FUN_004483f0(CARINTEG, 1) and FUN_004483f0(ATTEFACT, 1) [ENC] |
| **TD_Avatar** | db79dadb | **not written**: keeps the 2DM CARINTEG/ATTEFACT |

The ids are `CARINTEG = 4745544e 49524143` and `ATTEFACT = 54434146 45545441`.

Presets for `char+0x198` [CODE + DATA]:
- They come from `fx.ini [AVATARLIGHTFACTOR]`, parsed by the fx.ini table at d6e50b–d6e551. The handlers d65490,
  d65420 and d653b0 copy 6 floats.
- Selection is done by b8b7d0 and b90c50; the ctor b8d3f6 sets OUTCAR.

| key | storage | p0..p5 | used when |
|---|---|---|---|
| AVATAROUTCARLIGHTFACTOR | 0xfa91a8 | **1.0 0 0 0 0 0** | default; not in a car; car class 0x41c in 0x12..0x13 or 0x16 |
| AVATARCONVERTIBLELIGHTFACTOR | 0xfa9178 | 0.8 0.1125 0.40 1.0 0.75 0.75 | car +0x8c == 2 or 4 |
| AVATARINCARLIGHTFACTOR | 0xfa9190 | 0.5 0.115 0.45 0.6 0.95 1.0 | other cars |

So for a standing avatar: **CARINTEG = (0, 0, shadow, 1), ATTEFACT = (1, 1, 1, env)**.

Reset FUN_00cacfe0 (from FUN_00cab3f0, for every instance of an avatar set) writes CARINTEG = (0, 0, 0, 1) and
ATTEFACT = (1, 1, 1, 0). So an avatar never updated has **no** real-time shadow and k = 1.

The 2DM defaults that OpenTDU2 currently uses [DATA, `py/mat2dm.py`]:

| material | CARINTEG | ATTEFACT |
|---|---|---|
| body zones (TD_Skin2) | (0.35, 0.5, 1) | (0, 0, 0) |
| M_PN_T_Shirt (TD_Clothe2) | (0.35, 0.5, 1) | (0.4, 0.9, 1.0) |
| hair (TD_Hair) | (0.25, 0.5, 1, 10) | (0, 0, 0) |
| ASIA_LASHES / EYES_S (TD_Avatar) | (0.35, 0.5, 0) | (0, 0, 0) |

### 2.7 Pedestrian/NPC finish FUN_00b92d20(ecx = obj) [CODE]

FUN_00b92d20 applies the same `v` clamp and factors to the `obj+0x178` block. It writes:
- `+0x70` amb;
- `+0xd0` diff;
- `+0xe0` spec;
- `+0x100` normalize(dir).

There is no `m` multiplier. **The original has a bug: diffuse is `lerp(amb_orig, diff·v, fd)`** (ECX = block+0x10
for the diffuse call). Reproduce it only for that object class.

---------------------------------------------------------------------------------------------------
## 3. Shader consumption [SHADER]

Common to all skinned avatar VS (c-register order differs per family; take it from the CTAB):

```
h = (y_obj - carInteg.x) / (carInteg.y - carInteg.x)            (not clamped; y of the skinned / rigid object position)
k = atteFact.xyz + h * (1 - atteFact.xyz)                       (k.x ambient, k.y diffuse, k.z specular)
shadow s = lerp(1, shadowRT.x, carInteg.z)                      (Skin2/Skinface PS: v2.w; Clothe2 oD4.x; Hair oD5.w; Eye oD1.z)
```

With the game values (carInteg.xy = (0, 0), atteFact = 1), `h = y·rcp(0)` is ±inf and `k = 1 + inf·0`.
- D3D9 shader arithmetic gives 0·inf = 0, so k = 1. DXVK has `d3d9.floatEmulation` for exactly this case [INF].
- In SPIR-V the same input gives NaN. **OpenTDU2 must write carInteg.xy = (0, 1)**, which gives k ≡ 1 exactly.

Per family:

| family | VS lights | PS lights | formula |
|---|---|---|---|
| TD_Skin2 / TD_Skinface | c64 `__DirLight_Dir_0`, c65 CamPos, c66 `carinteg`, c67 `atteFact` | c9 `__AmbLight_Ambient_0`, c10 `__DirLight_Diffuse_0`, c11 `__DirLight_Specular_0` | `L = lerp(T(k.x·c9)·lut, T(k.y·c10)·lut, wrap·s)`; spec × T(k.z·c11) (skin_zones_spec §1.2) |
| TD_Clothe2 | c64 CamPos, c65 Dir, c66 Amb, c67 Diff, c68 Spec, c69 specParm, c70 carInteg, c71 atteFact (lighting is done in the VS) | baseClr | `oD1 = lerp(Amb·k.x, Diff·k.y, max(N·−L, 0))`, spec·k.z, shadow carInteg.z |
| TD_Hair | c64 Dir, c65 CamPos, c66 atteFact, c67 carInteg (float4) | c9..c11 Amb/Diff/Spec | k via oD5.xyz / oD6.x, shadow oD5.w |
| TD_Eye | c64 Dir, c65 Amb, c66 Diff, c67 Spec, c68 carInteg, c69 atteFact | – | as Clothe2 |
| **TD_Avatar** (lashes, eyebrows, teeth, tongue, EYES_S; rigid, no skin) | c64 `__SpotLight_Pos_0`, c65 Dir, c66 CamPos, c67..c69 Amb/Diff/Spec, c70 carInteg.xy, c71 atteFact, c72 uvoff | c9 `__SpotLight_Ambient_0`, c10 `__SpotLight_Diffuse_0`, c11 `__SpotLight_Specular_0`, c12 specparm, c13 color | `col = tex·color·(lerp(Amb·k.x, Diff·k.y, NL_sun·shadow) + lerp(SpotAmb, SpotDiff, max(N·nrm(P−SpotPos), 0))) + sunSpec·k.z·shadow·spec + spotSpec` |
| TD_Accessories2 | like TD_Avatar (SpotPos, atteFact float4) | `__SpotLight_Diffuse_0`, `__SpotLight_Specular_0` | – |
| TD_Pedestrian | Dir, SpotPos, Amb, Diff, SpotAmb, SpotDiff | baseClr, shadowAttenuation | – |

Notes on TD_Avatar:
- Its h uses **head-bone-local** y. With the 2DM (0.35, 0.5, 0) / (0, 0, 0), its sun term is k = (y_local − 0.35)/0.15,
  which is usually negative, and the spot (camera) term carries most of the light. This is the original's behaviour
  as well, since the game never writes TD_Avatar [CODE + SHADER].
- Only TD_Avatar, TD_Accessories2 and TD_Pedestrian read `__SpotLight_*`. Skin, clothes, hair and eyes do not see the
  camera light.

---------------------------------------------------------------------------------------------------
## 4. Expected values for the party house (`Avatar` + `Avatar_Avatar`, AVATAR_CHOICE, OUTCAR)

For an avatar with no interior spot in range (w ≈ 0):

| value | result |
|---|---|
| amb | sun_ambient (0.502, 0.580, 0.769) |
| diff | sun_diffuse (2.055, 1.522, 0.957) |
| spec | sun_specular (1.914, 1.443, 1.082) |
| dir | sundirection (modifier) |
| CARINTEG | (0, 0→1, **0.4**, 1) |
| ATTEFACT | (1, 1, 1, 1) |
| Spot (camera) light | 0.25 × (amb, diff, spec) |

Under one RT_Int spot at full weight (a = 0.5·att·cone, max 0.5):

| value | result |
|---|---|
| amb | 0.5·(0.388, 0.380, 0.380)·I + 0.5·sun_ambient |
| diff | 0.5·(0.757, 0.678, 0.553)·I + 0.5·sun_diffuse |
| shadow | 0.4·0.5 = 0.2 |
| dir | blended toward the spot's down direction |

---------------------------------------------------------------------------------------------------
## 5. Implementation plan (OpenTDU2; sources untouched by this spec)

1. **Data**
   - `WeatherPreset` accessors with the registry defaults of §1, for example:
     - `getFloat("avatar_shadow_influence", 1.0f)`;
     - `getVector4("avatar_spotlight_interior_exterior_influence", {0, 0, 0, 0})`;
     - `getFloat("avatar_cameralight_factor", 0)`;
     - `getFloat("avatar_cameralight_desynchro", 0)`;
     - `getVector3("avatar_cameralight_direction", {0, −1, 0})`;
     - `avatar_lum_min` 0 and `avatar_lum_max` 1;
     - `avatar_sun_ambiant_factor`, `_diffuse_factor` and `_specular_factor`, each defaulting to 1;
     - `avatar_car_envmap_factor` 0.7 and `avatar_car_night_reflection_factor` 0.
   - Use the active preset after `findModified`.
   - Parse `fx.ini [AVATARLIGHTFACTOR]` (three 6-float keys). Use AVATAROUTCARLIGHTFACTOR for cinematic avatars.
   - Parse `ArrayOf_Entity__Light` of the house entity export, next to Entity__Actor in IntroCinematic. Store:
     - pos (orient_pos_scale.pos, in the same space as the avatar positions);
     - dir = −Y axis of orient_pos_scale.rot;
     - Ambient, Diffuse and Specular × Intensity;
     - DistanceDropOff / DistanceCutOff;
     - inner = AngularDropOff, outer = AngularCutOff;
     - isInterior.

     Precompute `cosOuter`, `cosInner`, `coneScale = 1/(cosI − cosO)` (8388608 if ≤ 1.19e-7), `coneBias = −cosO·coneScale`
     and `distA = cut/(cut − drop)`, `distB = 1/(cut − drop)` [INF for the dist pair]. Keep at most 64.
2. **Per avatar per frame** (CPU, `EdenAvatarLighting`), following §2.1–§2.5 literally:
   - P = a chest or neck bone world position [INF; b87100 is ENC]. A fallback is root + (0, 1, 0).
   - Gate by mode: the intro/C_TESS run in AVATAR_CHOICE → gathering on; `useSun = false`.
   - `w = gather(...)`; `shadow = avatar_shadow_influence·(1 − w)`.
   - Apply the lum clamp and factors with `m = 1`.
   - Camera light colours = gathered × cf.
   - `SpotPos = anchor + 1e4·cameraForward`, with anchor = root + (0, 1, 0) [INF, §2.4]. When desynchro ≥ 0.5, use
     `anchor − 1e4·normalize(avatar_cameralight_direction)`.
   - `envOut = env·(1 + (v − 1)·night)`.
3. **Draw parameters**: add `const EdenAvatarLighting *pAvatarLighting` to `EdenDrawParams`. Set it in
   `EdenAvatar::draw` / `EdenModel::drawSkinned` for all parts, including the rigid TD_Avatar items. The rigid items
   currently go through `renderer.draw` without params, so pass the params there too.
4. **Constant writer** (`EdenRenderer::fillConstants`, before the generic sun branch) when `pParams && pParams->pAvatarLighting`:
   - `__AmbLight_Ambient_0` = AmbOut × material.Ambient; `__DirLight_Diffuse_0` = DiffOut × material.Diffuse;
     `__DirLight_Specular_0` = SpecOut × material.Specular. Keep the existing material-colour convention.
   - `__DirLight_Dir_0` = invWorld·normalize(DirOut), as a vector.
   - `__SpotLight_Ambient_0` / `_Diffuse_0` / `_Specular_0` = camera colours. Material multiply is [UNK]; use × material
     colours for consistency. `__SpotLight_Pos_0` = invWorld·SpotPos (a point).
   - For materials whose `ShaderSetGUID` is in the §2.6 table except TD_Avatar, compare
     `ToUpper(StripSuffix(name))` (this covers `carinteg`, `carInteg` and `atteFact`):
     - `CARINTEG` → (0, **1**, shadow, 1). Use (p1, p2) for in-car presets when p1 ≠ p2.
     - `ATTEFACT` → (1 − p3, 1 − p4, 1 − p5, envOut).
     - TD_Pedestrian `SHADOWATTENUATION` → (shadow, shadow, shadow, 1).
   - TD_Avatar keeps the 2DM CARINTEG/ATTEFACT, as the original does.
5. **Check**:
   - Render C_FIESTA_INTRO shot 2 and C_TESS with a `-logavatarlight` dump of w, the light count, amb/diff/spec, dir
     and shadow per avatar.
   - Expect shadow ≤ 0.4 and k = 1 everywhere.
   - First try fix 4.(CARINTEG/ATTEFACT + shadow) alone. It is the cheapest and the most likely to give most of the
     brightness back.

---------------------------------------------------------------------------------------------------
## 6. What remains [ENC]/[UNK]

- Consumers of `avatar_sun_ambient`, `avatar_sun_diffuse` and `avatar_sun_specular`: none readable [ENC/UNK].
- FUN_0041d8a0 (lookup), FUN_00459b50 (lerp), FUN_0040dae0 (normalize), FUN_004bf410 (distance coefficients
  E+0x70/0x74), FUN_00448f50 (light enable), FUN_0043dae0 (camera axis), FUN_00b87100 internals (bone point), and
  0x6c8d32/0x6c8d3e (cos/sqrt) [ENC]. The semantics given above are [INF].
- The binder from the instance light objects (+0x2a00/+0x2a50/+0x2ab0) to `__AmbLight/__DirLight/__SpotLight_*_0`
  constants, and whether material colours multiply the spot colours [ENC/INF].
- E+0x84 (subtracted from |d|²) is never written by readable code [UNK, assume 0].
- Which list the cinematic PNJ belong to: avatar list 0x11bb894 (b89060 path) or the 0xfd9290 list (b92d20) [UNK]. It
  changes only the diffuse bug and the `m` multiplier.
- The game modes of FUN_009b54c0 and FUN_00a19a30 (camera light = sun × cf variant) [UNK].

## 7. Artefacts

- Decompilations: `/tmp/claude-0/al/avl1.c`, `avl2.c` and `avl3.c`.
- Listings: `/tmp/claude-0/al/a4da90.s`, `a4beb0.s`, `b87aa0.s`, `b92d20.s` and `9b4c10.s`.
- Weather dump: `/tmp/claude-0/al/cw.txt`.
- Private Ghidra copy: `/tmp/claude-0/al/gh`, script `/tmp/claude-0/al/dec.sh`.
- Helper scripts: `/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad/{ids,reg,regdef,findid,findptr,rd,who}.py`.
