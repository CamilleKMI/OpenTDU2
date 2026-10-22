# Press start screen: 3D scene (reverse engineering notes)

All the 3D rendering of the title screen goes through the original shader
libraries (`Euro/Shaders/*.sob`) translated to SPIR-V (DXVK DXSO), with the
constants bound by name (CTAB).

## Material preparation (FUN_00d71900)
Each material GUID (MAT. +0x300) has a handler in the table at `0x00f8fa10`
(`{u64 GUID; handler(MAT *, Key *)}`) which rewrites the permutation
switches, sometimes the GUID itself (e.g. `1b38348331d3942a` -> TD_CarPaint_NM,
`b39c35015789e65c` / `69f07f72719bf862` -> TD_CarMat_NM,
`37078865c0ca336e` -> TD_Vegetation) and the render states, and returns the
render bucket. Implemented in `source/render/eden/eden_material_handlers.cpp`
(the handlers in the encrypted code range are left out). Most handlers clear
`S_FOG`: the fog comes from the haze post pass.

Render states (MAT. +0x30): DepthTest, DepthWrite (1 on, 2 off), ?, Cull,
?, AlphaTestDisable, ?, AlphaRef, SrcBlend, DstBlend, BlendOp (6 = no
blending), ZFunc (D3DCMPFUNC, TD_ShadowBox: GREATEREQUAL).

## Cinematic car
`.sce` car object (type 6, `CIN_CAR_01`) -> `entities.xmb` `Entity__CinCar`
(`id_car` 431 for M_IBIZA_01) -> DB `DBCarPhysicsData` (`One_77`,
`_default_rims`) -> `DBRims` (`Aston/One_77_F_01`, 20"). The car rests on the
road surface (the cinematic gives it 0.57 m above; in game the physics drops
it). Rim meshes are normalized (radius 0.212 m): the radius comes from the
database diameter when D_RIM_xx carries no scale.

Car shaders: `EnvCoeff` = (car_envmap_factor [x car_envmap_factor_static_cubemap
for the static cube], specular, fresnel coefficient, fresnel power). The
dynamic environment cube is rendered from the car, faces for the z mirrored
directions (the car shaders sample it with (x, y, -z)).

Contact shadows: TD_ShadowBox volumes (D_SBOX + one box per wheel), back faces
drawn with GREATEREQUAL, multiplied (ZERO, SRCCOLOR), reading the screen
view position buffer.

## Screen buffer gViewPos / gTexSamplerShadowRealTime
"FullscreenDepthTarget" (A8R8G8B8): R sun visibility (min of the shadow
cascades), G motion, B/A linear view depth (value = 2^(E-15) (1 + (B&7)/8 +
A/2040), E = B >> 3). Eden view space is right handed (z = -depth).

## Sky, clouds, sun
td_dome scattering constants: FUN_00931140 / FUN_0092fb20 (also used by the
clouds, the heightmap and the haze). The dome shader rotates the sun
direction (angle 2 pi frac(0.5 - 0.0858 (1 + dir.y)) - pi) before the
scattering. Clouds: clouds.bnk (CLOUD_x nodes), TD_Clouds / TD_CloudMesh.
Sun: halo/core with TD_SunHalo at camera - 39000 sundirection
(FUN_00972550), lens flares with TD_Flare (fx/sun.bnk, sunflare_* weather
parameters); the visibility (TD_SunVisible occlusion in the original) is
read back from the depth around the sun.

## Post processing
See `postfx_spec.md`: haze (edrender_pfx_haze ps_3, blend ONE/SRCALPHA) and
photoshop (downsample, Gaussian sigma 1, 256x256 LUT, apply). The sky keeps
the dome's own z prepass depth (~1): the haze leaves it alone.
