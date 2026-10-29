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
  - OpenTDU2 binds it to the material slot (`__UVA_k`) through the 2DM slot +0x1c -> `MTP.` -> `UVA.` reference
    (spot_exterior_spec 2); the file name rule is only a fallback. Checked on the Casino sectors: `Ecume` slot 0 ->
    `ecume_0` / `ecume_1` -> `__UVA_0` through the slot reference [DATA, OpenTDU2 -verbose log].
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

## 3. Ocean out of the loaded sectors: no continuity (beige band, dark tiles, horizon seams)

User report: the sea is not continuous outside the loaded sectors. World Viewer: a beige band on the Casino horizon,
lighter / darker rectangles and a seam line on the horizon on Ibiza and Hawai.

### How the game covers the sea at every distance [DATA + CODE]
There is no camera-following plane and no sea in the sky dome. The sea is the three mesh levels of ocean_spec §5, all at
the water height (y = 0, Hawai -1.8 [DATA]):

| level | mesh | coverage |
|---|---|---|
| island (`<island>.bnk` `<island>-o`, kind 2) | `Area-x-y` (one 6144 m tile per area, under a dummy) + `Border-i-j` (8 dummies, the 3 x 3 ring minus the centre) | the island grid + the open sea out to about -98 km / +104..166 km |
| area (`area-x-y.bnk` `area-x-y-o`, kind 1) | one 768 m tile per sector cell (staggered rows), under a dummy `Sector-x-y-sx-sy`, with the far objects of that sector (e.g. Casino `CITY` under `Sector-0-0-3-1`) | the area |
| sector (`sector-…-o`, kind 0) | one 768 m quad | the sector |

- [DATA] node names are stored as 8-byte folded hashes (GetIdentifier64bit). `o?RDER-0` = `BORDER-0-0`… and the area
  groups `?up?|?-0` = `SECTOR-0-0-sx-sy` (all 64 matched by hash in casino `area-0-0-o.3dd`). Ibiza / Casino / Hawai
  island `-o` borders checked: complete coverage, consistent winding (strips, 4 to 21 triangles each).
- [CODE FUN_00992890 / FUN_00992820] the area loader formats `Sector-%d-%d-%d-%d` (string 0xf2c650) for the 8 x 8
  sectors of the area, finds the node in the area -o scene and stores it in a per sector table (`area+0x2f8 + i*0x14`,
  index sy * 8 + sx). On odd rows, the first cell also looks up `Sector-(x-1)-y-7-sy` (the staggered sector of the
  previous area overlapping this one by 384 m). FUN_009927a0 then runs the TD_Ocean material setup for the node
  subtree.
- [CODE FUN_008be5c0] same for the island: `Area-%d-%d` / `Border-%d-%d` (0xf3527c / 0xf35288) node tables.
- [ENC] the per frame code that reads those tables (show / hide) is not in readable code. [INF] finest loaded level
  wins: the island `Area-x-y` tile is hidden when the area is loaded (and has its own ocean tiles), an area
  `Sector-x-y-sx-sy` group (ocean tile + far objects) when that sector is loaded; `Border-*` always drawn.
- The far end is the camera far plane (40 km in OpenTDU2: chase camera `farProjPlane`, cinematics). The haze
  post-process covers the distance [CODE postfx_spec]. Beyond the far plane the sky dome shows (td_dome below the
  horizon = `GroundColor` (0.7, 0.6, 0.5) [CODE FUN_00930d70]: beige).

### Root causes in OpenTDU2
1. `EdenWorld::draw` loaded the area `-o` objects but **never drew them** (only the area heightfields). With the island
   `Area-x-y` tile hidden under every loaded area (§1 fix), each area was a hole in the sea:
   - Casino (heightfield -100 m everywhere off the island [DATA casino.3dg]): the hole showed the sky dome lower
     hemisphere = the beige band.
   - Ibiza / Hawai: lighter / darker rectangles (island heightfield -100 m seen through the hole, or the area
     heightfield) and a dashed seam line on the horizon (holes of the far areas).
   - Proof: top view of Casino at 20 km with `-noareas`: a 6144 m beige square = area 0-0, sea all around it.
2. The island ocean `AreaSize` (vs_2 colour UV = v0.xz / (6144 AreaSize)) was hard-coded to Ibiza's (7, 6) for every
   island. [CODE FUN_0098e060 → levelObj+0xf98/+0xf9c → FUN_008be1e0] it is the worldconfig.cpr area grid:
   Hawai 11 x 9. Casino's worldconfig.cpr is a copy of Hawai's (`level="Hawai"`, 11 x 9 [DATA]) and the static
   default of DAT_01024a8c/90 is also 11 / 9 [CODE 0xaee9d1], so the game uses 11 x 9 on Casino too: casino-c (which
   shows the 6144 m casino area [DATA]) is stretched over 67.5 km under the island ocean, which gives the island
   shapes far out on the Casino horizon. Reproduced as is.
3. The island heightfield (TD_HeightMap island permutation) had no `AreaSize`: colour UV = grid * AreaSize / 256
   [SHADER vs_14/15] was 0. [CODE FUN_0098e450 → FUN_0098cc90] AreaSize = (16 / NbColumns, 16 / NbRows) (16.0 =
   double 0xf49c08), i.e. the colour map spans the area grid.
4. The reflection plane was y = 0 on every island. [CODE FUN_00963010] h = -1.8 when DAT_00fe5028 == 1 (level
   "Hawai"), matching the Hawai ocean meshes at y = -1.8 [DATA].

### Fix
- `source/world/eden_world.cpp`:
  - `loadArea`: per item, the sector of its `Sector-x-y-sx-sy` group (findNode by name, plus the odd row
    `Sector-(x-1)-y-7-sy`, as FUN_00992890); `Area::ItemSectors`.
  - `draw`: area objects drawn (`Translation(area.Origin)`), after the area heightfields. `-noareaobjects` turns them
    off (before / after comparisons).
  - `updateOceanVisibility`: an area item is hidden while the sector of its group is loaded (ocean tiles and far
    objects alike); ocean items also follow the ocean on / off switch. Items outside any group use the sector under
    their centre (previous rule).
  - Island heightfield `AreaSize` = (16 / columns, 16 / rows); island ocean `AreaSize` = (columns, rows) (set in
    `forEachOceanMaterial`, the hard-coded value is removed from `eden_ocean.cpp`).
  - `getWaterHeight()`: -1.8 on Hawai, else 0.
- `source/render/eden/eden_ocean.*`: `setWaterHeight` (mirror camera and clip plane at y = h), set by
  `EdenSceneView::initialize`.
- Logs: `OTDU_OCEAN_LOG=1` (`area-0-0: 65 items, 64 sector groups`, hidden counts), `OTDU_OCEAN_DUMP=1` (island and
  area -o nodes / items with world bounds).
- The wave UVs were already continuous: TC0 = (World·p).xz · NMapLR/HR + ShiftWorld (0) on every level [SHADER], all
  levels use the same NoiseNMap and constants, so tiles only differ by their colour map (sector / area / island
  `-c`), as in the game.

### Evidence (`/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad/ocean2/`)
- Casino (2700, 120, 1500), 4 yaws: `cas_before_grid.png` (beige band, darker tile edges) vs `cas_after_grid.png`
  (sea to the horizon). Top view 20 km: `cas_top.png` (`-noareas`, hole of area 0-0) / `cas_top_after.png`.
- Ibiza (35900, 700, 5400): `ibz2_before_grid.png` (`-noareaobjects`: rectangles in the bay, seam line on the
  horizon) vs `ibz2_after_grid.png`; crops `ibz_zoom.png`, `ibz_zoom2.png`.
- Hawai (5000, 500, 26500): `haw_before_grid.png` (tile rectangles, dashed horizon) vs `haw_after_grid.png`.
- Note: the "before" shots of Ibiza / Hawai use the new build with `-noareaobjects` (the island heightfield and ocean
  AreaSize are already the fixed values there); the Casino "before" is the old build.

## Left open
- The island / area / sector swap policy itself is [ENC] (ocean_spec §5.2): the node tables are filled in readable
  code (§3), the per frame use is not. The fix keeps the [INF] finest-level-wins policy and makes it effective.
- Partial game data: a sector bank that is missing keeps its area group (tile + far objects) visible.
- 2DM per-slot address bytes are ignored renderer-wide (every material uses LinearWrap). Only TD_backwash is overridden
  here. A general mapping (D3DTADDRESS per axis) would need per-axis samplers in EdenRenderer.
  **Update**: EdenRenderer now applies the per-axis slot addressing to every 2DM material (skin_zones_spec §1.7);
  the TD_backwash override in eden_world.cpp (mirror on both axes) still runs after it and could now be dropped
  (the slot's (1, 2) gives wrap U / mirror V exactly).
