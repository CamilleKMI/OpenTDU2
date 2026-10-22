# TDU2 car paint selection / application spec (focus: press_start CIN_CAR_01, One-77)

Tags: [CODE] readable x86, [DATA] files, [SHADER] D3D9 bytecode, [INF] inferred, [ENC] SecuROM-encrypted (not analysed).
Decompiled code: /tmp/claude-0/paint_dec.c (main), /tmp/claude-0/paint_dec2.c (getters, misc).
Scratch (extracted banks, dumper): /tmp/claude-0/paint/ (py/dump2dm.py = full .2dm material/parameter dumper,
o77_dump.txt = one_77.2dm, colors_dump.txt = colors.2dm).

## 0. Result (TL;DR)

For CIN_CAR_01 (id_car 431 = 0x1af, paint_idx 0) the game selects DBCarsAndColors row #348, whose
`_color_id_1`, `_color_id_2` and `_callipers` are all `""`. The material lookups for those empty names fail, so
**no paint parameters are overridden: the One-77 is rendered with the parameters stored in one_77.2dm
material `Paint` (TD_CarPaint2), unchanged** [CODE+DATA]. The entity's `paint_type` is never read by the CinCar
spawner [CODE]. The silver-blue look comes from the TD_CarPaint2 ramp shading
(dark RampClr2 -> light RampClr1 + RampFlake flakes + env reflection), not from a different color. Rendering it
with a flat RampClr1 (or RampClr2) color is what looks wrong (section 6).

## 1. Database

### DBCarsAndColors (1405 rows) [DATA db.txt]
Fields: `_callipers` (str), `_car` (u64 = id_car), `_color_id_1` (str), `_color_id_2` (str), `_color_name` (str),
`_interior_1.._interior_15` (u64, DBInterior refs), `_price`.
In-memory record (stride 0x108, fields in alphabetical order) [CODE d8e6a0/d8fba0 + INF]:

| off | field | used by |
|---|---|---|
| +0x00 | _callipers (string object, 0x1c) | FUN_00c5bb40 (brake caliper material) |
| +0x20 | _car (u64) | |
| +0x28 | _color_id_1 | c5ff20(car, id1, mode 0) = body paint |
| +0x44 | _color_id_2 | c5ff20(car, id2, mode 10) = second color (bicolour) |
| +0x60 | _color_name | returned by d8e6a0 (UI name) |
| +0x80.. | _interior_N (u64 each) | FUN_00d8b9a0 / d8fba0 (index = interior_idx) |

Rows for one car: `FUN_00d8b940(car)` = count (multimap DAT_01024c74, (end-begin)/0x108), `FUN_00d8c610(car)`
with EDI = index returns `base + index*0x108` [CODE; the multimap itself is ENC (0x41b640/0x41b750)].
paint_idx = index into the rows of that car, in table order [INF: multimap insertion order = table order].

Rows with `_car = 0x1af` (One-77): exactly one, row 348:
```
_callipers = ""   _color_id_1 = ""   _color_id_2 = ""   _color_name = "--"
_interior_1 = 0x4ad, _interior_2.._15 = 0   _price = 0
```
Other cars use real ids, e.g. car 0x113 (Alfa_Brera): N_01 "Nero", R_04, N_02, GM_F04, GM_03, GM_04, JM_07, BM_03,
BM_07, RM_07..., `_color_id_2 = "##"`, `_callipers = "Cal_G_R"`.

### DBCarPhysicsData row 65 [DATA]
`_id_car = 431`, `_car_file_name = "One_77"`, `_isvalidcarpaintdeluxe = 1` (paint-shop eligibility only).
No color field. No other table holds car colors (DBMaterial = house materials).

## 2. Resources

- vehicules/high/one_77.bnk: one_77.2dm (group 9) is the only material file; **there is no one_77_color.2dm**
  (also not on the full install) [DATA].
- vehicules/colors.bnk -> colors.2dm (group 14) materials: C_B_01, TR_01..TR_11 (interior trims, `TR_%02d`),
  METAL, CARBON, GLOSSY, MATT (paint types), all TD_CarPaint/TD_CarPaint2 [DATA]. No `N_01`-style ids.
- Colour ids like "N_01"/"GM_F04" are not present as plain identifiers in any bank of the install
  (grep of Euro/Bnk, database, root files) [DATA]; the car loader (FUN_00c50300 region, c5093e) registers an
  optional second 2dm of the car bank (lookup FUN_00d8aea0(bank, 0x15, 2)) into the material-library list
  DAT_01179390 (FUN_00c4cc30) [CODE]. Where present this would be the per-car colour library; one_77.bnk and
  brera_italia_independent.bnk have none [DATA].
- Lookup of a colour material: `FUN_00c4c0f0(hash64)` walks DAT_01179390..DAT_01179394 (registered 2dm
  libraries: colors.2dm via GServiceCarColors "%sVehicules\colors.bnk", + car colour libs) and returns MAT+0x10
  [CODE]. Hash = GetIdentifier64bit (FUN_008bb130 -> ENC); hash("") = 0 -> no material.

## 3. Code path

### 3.1 CinCar entity [CODE]
Field reader (vtable 0xf0a300 slot 0, FUN_00e2a4f0): +0xe0 id_car, +0xe4 rim_indice, +0xe8 paint_type
(default 2), +0xec paint_idx, +0xf0 interior_idx.
Spawner FUN_00dc2cd0 (paint_dec.c):
```
car = FUN_00c3dc20(id_car,...)                 // load vehicle
disp = car+0x79c0
disp+0x252c = 1; disp+0x2530 = 0 /*paint type forced 0*/; disp+0x2534 = paint_idx; disp+0x2538 = interior_idx
if FUN_00d8bb90(id_car):
    FUN_00d8e6a0(ECX=id_car, EAX=paint_idx, disp)     // DB colour
    if !isTraffic: FUN_00c59fc0(disp, rim_indice);
                   rec = d8c610(id_car); intr = d8b9a0(rec, interior_idx); c53380(disp, intr)
```
`paint_type` (+0xe8) is not read [CODE].

### 3.2 FUN_00d8e6a0 (apply DB colour) [CODE]
```
n = d8b940(car); if n == 0 return
if idx < 0 || idx >= n: idx = 0
rec = d8c610(car)[idx]
c5ff20(disp, rec._color_id_1, 0)    // body
c5ff20(disp, rec._color_id_2, 10)   // 2nd colour
c5bb40(disp, rec._callipers)        // caliper colour -> STIR*_F/_R materials (+0xb0/+0xc0 colours)
return rec._color_name
```

### 3.3 FUN_00c5ff20(disp, name, mode) (copy colour material into car) [CODE]
```
sprintf(nameN, "%s_N", name)
mat  = c4c0f0(hash(name));  if !mat return 0        // <- One-77: name "" -> not found -> nothing changes
matN = c4c0f0(hash(nameN)); if !matN matN = mat
mode 1/2/3/9: copy material onto interior slot arrays (+0x258c/+0x25cc/+0x260c/+0x254c)
mode 0/10   : for (mat, matN): read Diffuse/Ambient/Specular (+0xb0/+0xc0/+0xd0, *255 -> bytes), MatParam,
              SpecParm, SpecBlinn, FlakeABNS, RampPow, RampClr1, RampClr2, RampClr3, RampFlake,
              BlinnClrA/B and write them into the car paint state (FUN_0045a*, ENC) which later feeds the
              setters below.
```
Paint-parameter getters (return pointer to the float4 in MAT., by shader GUID) [CODE]:

| getter | param | shader |
|---|---|---|
| e26c00 | RAMPCLR1 | CP, CP2, CarPaint_NM |
| e26b40 | RAMPCLR2 | CP, CP2, CarPaint_NM |
| e26ae0 | RAMPCLR3 | CP2 only |
| e26a20 | RAMPFLAKE | CP2 only |
| e26cc0 | RAMPPOW | CP (if MAT+0x30), CP2 |
| e26a80 | LAYRCLR1 | all but 0x20/0xbc6c7363/0x31d66466 |
| e26d70 | FLAKEABNS | CP2 |
| e26dd0 | SPECBLINN | CP2 |
| e269c0 / e26960 | BLINNCLRA / BLINNCLRB | CP2 |
| e26e30 | SPECPARM | CP, CarPaint_NM ... |
| e26ef0 | MATPARAM | |
Setters (bytes/255 -> float4): c56b90 (RampPow.xy, RampClr1, RampClr2), c56e10 (RampClr1/2), c56d70
(RampClr3), c56dc0 (RampFlake), c56cd0 (BlinnClrA/B), c56890 (LayrClr1), c56a00 (FlakeABNS), c56940 (MatParam),
c56a40 (SpecParm/SpecBlinn); they are only called from ENC code.

### 3.4 Paint types (player cars / paint shop) [CODE FUN_00c62490, FUN_00c62620, FUN_009bb770]
`type 0` = DB colour (3.2). Otherwise the base material comes from colors.2dm:
1/5 CARBON, 2/6 MATT, 3/7 METAL, else (4/8) GLOSSY, applied with mode 0 and 10, then the custom RGB colours
(params 7..9) via FUN_008c8d20. Not used by the cinematic car.

## 4. One-77 paint materials (one_77.2dm) [DATA]

`Paint` (MAT @0x70a0, GUID 183157b81c6c5377 = TD_CarPaint2, spec `010200010000010000000100010100010000...`;
textures NORMAL=..DAMA(ge normal), CUBEMAP, DIRT_SCRA(tch), FLAKE, USER_TEX(stickers), IMPACT_M(ask)).
`Paint_LR` (TD_CarPaint, low-res: RampPow 1, RampClr1 (0.372549,0.537255,0.623529), RampClr2
(0.219608,0.258824,0.376471), MatParam (1,1,1,5), SpecParm (10,1,0,0), LayrClr1 1, GlossFct 1).
`Paint_01` (1b38348331d3942a gas cap: RampPow 5, RampClr1 0.227451 grey, RampClr2 (0.168627,0.196078,0.223529)).
There is no `Paint_SK`/`PAINT2`/`P_*` material in this car.

## 5. FINAL parameter values for CIN_CAR_01 `Paint` (paint_idx 0) = one_77.2dm defaults [DATA+CODE]

| name | float4 | /255 |
|---|---|---|
| DIRTCOLOR0..3 (flags 1) | (0, 0, 0, 0) | |
| RampPow | (10, 10, 1, 0) | |
| RampClr1 | (0.568627, 0.717647, 0.843137, 1) | (145,183,215) |
| RampClr2 | (0.203922, 0.301961, 0.435294, 1) | (52,77,111) |
| RampClr3 | (0.811765, 0.811765, 0.811765, 1) | (207,207,207) |
| RampFlake | (0.407843, 0.709804, 0.776471, 1) | (104,181,198) |
| BlinnClrA | (1, 1, 1, 0) | |
| BlinnClrB | (1, 1, 1, 0) | |
| LayrClr1 | (1, 1, 1, 1) | |
| SpecBlinn | (0.02, 0.05, 1, 1) | |
| MatParam | (1, 1, 0.2, 5) | |
| FlakeABNS | (0, 0.9, 1, 15) | |
| DmgMask | (0, 0, 0, 0) | |
| EnvCoeff | file (0,0,0,0) - **overwritten at runtime**, see below | |

Runtime overrides independent of the paint choice [CODE]:
- EnvCoeff: FUN_00c5a580 caches a pointer to EnvCoeff of the car's TD_CarPaint2 materials (car+0x780c,
  +0x7824); FUN_00c5a2e0 writes every frame `EnvCoeff = (X, F.w, F.x, F.y)` with
  F = weather `car_fresnel_coeff_power_boost_specular` (hash 0x482f9775ac92775e) and X = weather float
  (lookup hash 0xa5ac99e5fbfcd2c9 = `car_rim_envmap_factor`; the x87 trace suggests X = factor * (1 - car[+0x774c]))
  [CODE, exact X formula INF]. Cinematic weathers Cin_Gen etc.: F = (1,1,1,1), factors 1.0 -> EnvCoeff ~ (1,1,1,1)
  for a clean car [DATA+INF].
- RampPow.z: FUN_00d58980 sets RAMPPOW.z (TD_CarPaint2) / SPECPARM.z (TD_CarPaint) to a float argument when
  MAT+0x313 != 0 (Paint: spec byte 3 = 1) and a cubemap texture matches (env-map system) [CODE, value not traced].

## 6. How TD_CarPaint2 uses them [SHADER TD_CarPaint2_ps_47, full-feature permutation]
Constant registers: c9-12 World, c13 Ambient, c14 DirLight diffuse, c15 DirLight specular, c16 AlphaRef,
c17 RampPow, c18 RampClr1, c19 RampClr2, c20 RampClr3, c21 BlinnClrA, c22 BlinnClrB, c23 SpecBlinn,
c24 MatParam, c25 EnvCoeff, c26 LayrClr1, c27 RampFlake, c28 FlakeABNS; s1 detail, s2 reflection cube,
s3 dirt/scratch, s4 micro flakes, s5 stickers, s15 shadow.
```
flake  = 2*tex(s4, triplanar(pos * FlakeABNS.w))-1          (tangent space)
Nf     = FlakeABNS.x*flake + FlakeABNS.z*(0,0,1)
H      = normalize(normalize(v2) + normalize(v1))
base   = lerp(RampClr2, RampClr1, pow(max(dot(H,Nf),0), RampPow.x))
base  += pow(max(dot(H, FlakeABNS.y*(flake+(0,0,1))),0), RampPow.y) * RampFlake
paint  = lerp(base, detail*LayrClr1, detail.a*LayrClr1.w); stickers lerp
dirt   : scratch mask s3.y blends toward RampClr3 (primer); reflection cube * EnvCoeff.x * MatParam.x * fresnel
lighting: ambient/diffuse/shadow, Blinn specular with BlinnClrA/B, SpecBlinn
```
With RampPow.x = 10 the light RampClr1 only appears in a lobe around H (highlight band); the body
is mostly RampClr2 (steel blue 52,77,111), lifted by the cyan flakes (RampFlake) and the env reflection
(EnvCoeff.x ~ 1 at runtime, file value 0). OpenTDU2's generic path takes RampClr1 as a flat material color
(source/render/model.cpp:78); any path that renders with flat RampClr1/RampClr2, or with the file EnvCoeff of 0
(no reflection), will look like plain blue rather than the in-game silver-blue metallic [INF].
