# Car glass / reflections: environment cube of the car (garage CUBECAR)

Tags: [CODE] readable exe (VA >= 0x6c1000), [DATA] game files, [SHADER] .sob disassembly,
[INF] inference, [ENC] SecuROM range (not read).

User report: the headlight / window glass of the California looks slightly blue in OpenTDU2.

## 1. California glass materials [DATA]

`vehicules/high/california.bnk` california.2dm (TD_CarPaint family 183157b81c6c7357, shader version 9):

| material | states (+0x30) | blend | slots | params |
|---|---|---|---|---|
| Glass, Glass_SK (windows) | `01 01 00 03 00 01 00 00 05 06 01` | SRCALPHA / INVSRCALPHA | DETAIL California_Glass, DETAIL2 G_I, REFLECTION **CUBEMAP**, DIRTCOLOR DIRT_SCRATCH | MATPARAM (0.8, 1, 0.3, 5), SPECPARM (1000, 1, **0**, 0), ENVCOEFF 0 (runtime) |
| Glass_Opaque | `... 02 01 06` (opaque) | - | as Glass | as Glass |
| Glass_LGT / Glass_SK_LGT (light covers) | `01 01 00 02 00 01 00 00 05 06 01` | SRCALPHA / INVSRCALPHA | DETAIL California_Glass, REFLECTION CUBEMAP, DIRTCOLOR | diffuse 0.1529 / 0.1137, MATPARAM (0.3, 1, 0.5 / 0.3, 5), SPECPARM (1000, 1, 0, 0) |

* Spec byte 3 (S_USEREFLECTION) = 1 for all of them.
* `G_I` = common_car.bnk `g_i.2db` (32x32 DXT1, all blocks zero: opaque black) -> the glass base colour is
  black, the look is the reflection + specular.
* `CUBEMAP` = common_car.bnk `cubemap.2db`: 256^2 DXT1 cube, 7 mips, an outdoor island shot (sky, palms);
  mean colour (111, 125, 132): blue-grey.
* Interior bank: Glass_I (dddddddd1c6c7357 = TD_CarGlassDirt, inside view), 7 slots use `CUBEINT`
  (common_car cubeint.2db, 128^2 grey) and 2 use `CUBEMAP`.

## 2. TD_CarPaint glass pixel shader (ps_109: Glass / Glass_Opaque / Glass_SK) [SHADER]

```
R      = reflect(-V, N) (world), sampled as texCUBE(s3, (R.c9, R.c10, -R.c11))
refl   = texCUBE.rgb * (1 - texCUBE.a * SpecParm.z) * v5          (v5: VS fresnel * EnvCoeff * MatParam)
base   = lerp(lerp(v0, detail * c17, detail.a * c17.w), G_I.rgb, G_I.a)
spec   = pow(max(R.L, 1e-4) * shadow, SpecParm.x) * c15 * SpecParm.y
out.a  = detail.a
```

## 3. How the original binds the cube [CODE]

* `FUN_00d58a90(envobj, cubeName, mirrorFlag)`: for the materials of the car whose family is one of
  183157b81c6c7357 (TD_CarPaint), ca00000000000010, 183157b81c6c5377 (TD_CarPaint2), dddddddd1c6c7357
  (Glass_I), b39c35015789e65c, ca00000000000020, 6638348331d66466, 1b38348331cacaca, and TD_Mirror only
  when the flag (car env +0x2ab6) is set:
  * `FUN_00d56df0(mat, DAT_011f8e88, cube)`: every texture slot whose **texture hashcode** (+0x108) is
    DAT_011f8e88 gets the new texture (+0x110, sampler states 3 / 2); returns true if it changed.
    DAT_011f8e88 is the default car cube name: `FUN_00c4b0e0(DAT_011f8e88)` (restore path of
    FUN_00d58d20) finds that texture in the global texture table. It is written in encrypted code
    [ENC]; it is "CUBEMAP", the REFLECTION texture of every car material [INF]. So CUBEINT slots are
    never replaced.
  * `FUN_00d58980(value)`: if spec[3] (S_USEREFLECTION) and the slot changed: SPECPARM.z
    (183157b81c6c7357, ca..10, 1b38348331cacaca) or RAMPPOW.z (183157b81c6c5377, 183157b81c6ccaca) =
    value (component z: the parameter lookup FUN_0041f8c0 [ENC] returns the value, see the DMGMASK copy
    at c483f6), then FUN_008a8950 (program reselection). value = 1.0 (0x00edf668), or 0 when the cube is
    one of the dynamic env targets `DAT_011c9f00 + 0x48` / `DAT_011c9f04 + 0x48`.
* `FUN_00d58df0(car env)`: dynamic cube (DAT_011c9f04 for the player car in one configuration, else
  DAT_011c9f00) -> FUN_00d58a90.
* `FUN_00d58d20(car env, tex)`: static cube `tex` (or the default CUBEMAP when 0) -> FUN_00d58a90.
* `FUN_00c62440(bool dynamic, flag)` (esi car env, edi static cube): dynamic -> FUN_00d58df0 once
  (+0x2ab5 = 1), else FUN_00d58d20(edi) (+0x2ab5 = 0).
* Car slot manager (scene + 0x33e0, entries 0x90 bytes): FUN_00a50d20 / FUN_00a50fc0 call
  FUN_00c62440(entry +0x6c, 1) with **edi = manager +0x140 = scene +0x3520** (the scene static cube).
* **FIRST_WM** load `FUN_009aa2c0` (the mode that plays `C_LIFE_SOUND`, garage.bnk
  `garage_int_M_LIFE_SOUND_Totem_Garage.cin`): `scene(this + 0x1b4d20) + 0x3520 = FUN_00a4baf0(scene)`,
  then FUN_00de15f0 (cars of the scene). `FUN_00a4baf0` walks the scene bank texture list (+0x750) and
  returns the texture named **"CUBECAR"** (hash 0x0052414345425543), else 0.
  The CINE mode has the same lookup (FUN_009b51a0 -> FUN_00b3e5f0: every car env object
  -> FUN_00d58d20(CUBECAR)).
* Car env factors (FUN_00c4df00): `env +0x6a0 = car_envmap_factor`, `+0x6a8 =
  car_envmap_factor_static_cubemap` (default 0.7, 0x00eddb08); FIRST_WM (0x23) is in the interior mode
  list (3, 0x1d, 0x1e, 8, 0x16, 0x23) whose car light comes from the env object.

## 4. Garage data [DATA]

`scenarics/cinematics/intro/garage.bnk` holds `cubecar.2db`: a 512^2 DXT5 cube, 8 mips, shot inside the
garage (white walls, ceiling lamps, grey floor; mean (143, 133, 120), warm), with a meaningful alpha
(the SPECPARM.z attenuation). Only the garage material metal_Strip_02 references it besides the car
binding. new_intro.bnk (AVATAR_CHOICE) and press_start.bnk have no CUBECAR (cubecarb / cubemap only).

## 5. Root cause in OpenTDU2

* IntroCinematic rendered the dynamic car cube around the car, but its Environment pass draws no set
  (drawSets returns at once) and the garage has no world: the cube held only the sky dome and clouds ->
  blue sky reflected on the glass, light covers, mirrors and chrome inside a closed garage. Before the
  cube was ready, the common_car CUBEMAP (outdoor, blue-grey) was used.
* `CinematicCar::assignEnvironment` replaced the REFLECTION slot of every material (also the CUBEINT
  ones of the interior) and never set SPECPARM.z / RAMPPOW.z.

## 6. Fix (OpenTDU2)

* `CinematicCar::assignEnvironment(renderer, cube, bDynamic)` = FUN_00d58a90: same family list, only
  slots whose texture hashcode is CUBEMAP, SPECPARM.z / RAMPPOW.z = 0 (dynamic) / 1 (static).
* `IntroCinematic::loadCar`: CUBECAR of the scene bank found -> static cube bound, the dynamic cube is
  neither rendered nor assigned, EnvCoeff.x keeps the static factor (car_envmap_factor_static_cubemap).
  `-nocubecar` restores the dynamic cube (comparisons).
* FREERIDE / TitleScene / AVATAR_CHOICE: dynamic cube as before, now with z = 0 written as the original
  does. The California Paint has RAMPPOW (4, 15, **1**, 0) in the file (authored for the default static
  CUBEMAP): before the fix the dynamic cube kept z = 1 (paint reflection scaled by the render target
  alpha); the glass files already have SPECPARM.z = 0.
* Checked with `-edendumpconstants` [runtime]: TD_CarPaint2 ps87 (Paint) c16 = RAMPPOW (4, 15, 1, 0) and
  TD_CarPaint ps109 (Glass_Opaque) c16 = SPECPARM (1000, 1, 1, 0) in FIRST_WM: c16.z is the cube alpha
  attenuation of both shaders.

## 7. Remaining

* DAT_011f8e88 = "CUBEMAP" [INF] (writer [ENC]).
* The slot flag +0x6c of the FIRST_WM California (static vs dynamic) is not traced; the static cube is
  [INF] from FUN_009aa2c0 loading CUBECAR into the slot manager and from the bank content.
* EnvCoeff.x formula of FUN_00c5a2e0 (x87, Ghidra output unusable) [INF], see paint_spec 5.
* The TD_Mirror condition (+0x2ab6) is left out [INF].
