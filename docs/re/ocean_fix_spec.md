# Ocean fixes: stair-step bands over the bay + static shore foam

Tags: [CODE] readable exe, [DATA] game files, [SHADER] .sob bytecode, [INF] inferred.

## 1. Stair-step turquoise / dark bands (depends on camera orientation)

### Root cause
Two ocean levels were drawn on top of each other. They lie in the same plane, so they z-fight:
- The island ocean `ibiza-o` (kind 2, colour map `ibiza-c` covering the 43 km island, so it looks dark, brown and speckled)
- The area / sector oceans (kinds 1 / 0, colour maps `area-X-Y-c` / `sector-…-c`, so they look turquoise with seagrass patches)

All of them have y = 0 and are lowered by the same 0.01 in the VS [DATA, SHADER], so their depths are equal. Which one wins
depends on the rasterizer's depth rounding: on Apple TBDR GPUs (MoltenVK) it changes in rectangular tile and row blocks
and with the view direction. That gives the stair-steps in the user's screenshot.

`EdenWorld::updateOceanVisibility` intended to hide the island `AREA-x-y` tiles of loaded areas (finest level wins,
ocean_spec §5.2). But it called `setNodeVisible` on the `AREA-x-y` node itself. [DATA ibiza.bnk ibiza-o.3dd, logged]:
`AREA-x-y` nodes are dummies with 0 items. The mesh is on their child node (`AREA-0-0` → `OCEAN`, `AREA-1-0` →
`OCEAO`, …, 1 item each). `EdenModel::draw` only tests the item's own node, so nothing was hidden and the whole
island ocean was always drawn under the area and sector oceans.

### Fix (source/world/eden_world.cpp, `updateOceanVisibility`)
- An island `AREA-x-y` tile is hidden when the area `X-Y` is loaded **and has at least one TD_Ocean item**.
- Hiding applies to every island item whose node descends from `AREA-x-y` (`Render3DD::isDescendantOf`).
- `BORDER-*` (the open sea to the horizon) and the island tiles of areas without their own ocean stay visible.
- Optional log `OTDU_OCEAN_LOG=1`. In walk mode: `24/44 island items hidden, 9 area tiles hidden`.

Evidence: same camera over the bay (house terrace, looking at the schooner beach):
`/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad/oc/ocean_bay_before_after.png`.
- Top (before): the island ocean wins everywhere on lavapipe, so the whole bay is the dark, brown, speckled kind-2
  colour map (the dark area in the user's image).
- Bottom (after): only the sector and area oceans remain: turquoise shallows with seagrass patches.

## 2. Shore foam not animated

### Data
- The shore foam is the TD_backwash material `Ecume` (GUID 0x73078865ca31234d, bucket handler 0x00d70cb0).
  - On Ibiza it exists only in `sector-5-0-6-7-o` (the beach under the house) and `sector-5-1-6-1-o` [DATA].
  - Its COLOR texture is `plage.2db` (commonworld.bnk, 256² DXT5). DETAIL3 is `D3_SAND`.
- The same banks contain `ecume_0.uva` (ANM!/MAT!) [DATA]:
  - Its id 0x049c2e1c84816bae equals the `UVA.` entry of the -o.2dm UVAA section.
  - Duration 8 s. Channels: U offset 0, V offset −2 → −0.25 (t = 4 s) → −2, U scale 1, V scale 3 → 1.2 → 3 (13 keys).
- TD_backwash vs_0 [SHADER] (CTAB: `__World_View`, `__World_View_Proj`, `__CamPos_ObjSpace`, `__DirLight_Dir_0/1`, `__UVA_0`):
  - `TC0.x = dp3((u,v,1), c71.xyw)`, `TC0.y = dp3((u,v,1), c72.xyw)`, so the UV comes from `__UVA_0`.
  - `TC1 = v0.xz * (1/768, 1/768, 0.25, 0.25)`.
  - The vertex is lowered by viewZ/256.
- TD_backwash ps_0 [SHADER]: foam = tex(s0, TC0 * (6, 1)). Output alpha = foam.R.
- Strip mesh [DATA sector-5-0-6-7-o.3dg PRIM@3ad0]: v ∈ {0, 1} across the about 13 m wide strip. u goes 0 → 1 per 4 quads
  along the shore, restarting on duplicated seam vertices.

### Root cause
`EdenModel::bindUVAnimations` was only called for intro-cinematic set models. For world sectors and areas it was never
called, so `__UVA_0` = identity and the foam was static.

### Fix (source/world/eden_world.cpp)
- `loadSector`: `Objects.bindUVAnimations(SectorBank)`. `loadArea`: same for the area bank. The existing `__UVA_k`
  writer evaluates the animation at scene time (8 s loop).
- Sampler of the TD_backwash COLOR slot = linear **mirror**:
  - 2DM slot bytes +0x18/+0x19 of `Ecume` COLOR are 1 / 2. Every other slot has equal values (2/2 or 1/1) [DATA].
  - With the D3DTADDRESS numbering, V is MIRROR. The numbering is confirmed by the ocean runtime material [CODE
    FUN_008be1e0]: 1 for the world-tiled NoiseNMap (needs wrap) and 3 for the projected reflection (clamp). [INF]
  - EdenRenderer has no per-axis sampler. Mirror on both axes is exact on V and seamless on u·6, because u restarts at 0
    on the strip seams.
  - Wrap (the renderer default) gave hard opaque/transparent seams at v' = 0 and −1 (plage R = 255 at v = 0, 0 at v = 1).

Evidence: `…/scratchpad/oc/foam_before_after.png`.
- Top row (before): frames 5 and 125 show identical static foam (the only differences are in the water normal map).
- Bottom row (after): frames 5, 65 and 125 (t ≈ 0.08 / 1.08 / 2.08 s). Two crests merge and run up the strip and back
  on the 8 s loop.

## Left open
- The island / area / sector swap policy itself is [ENC] (ocean_spec §5.2). The fix keeps the existing [INF] policy and
  makes it effective.
- 2DM per-slot address bytes are ignored renderer-wide (every material uses LinearWrap). Only TD_backwash is overridden
  here. A general mapping (D3DTADDRESS per axis) would need per-axis samplers in EdenRenderer.
  **Update**: EdenRenderer now applies the per-axis slot addressing to every 2DM material (skin_zones_spec §1.7);
  the TD_backwash override in eden_world.cpp (mirror on both axes) still runs after it and could now be dropped
  (the slot's (1, 2) gives wrap U / mirror V exactly).
