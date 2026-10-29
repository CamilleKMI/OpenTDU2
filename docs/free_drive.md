# FREE_DRIVE: free drive sandbox (developer tool)

Tags: [CODE] readable exe code (VA >= 0x6c1000), [DATA] game files, [INF] inference / tool choice, [ENC] SecuROM
range (not read).

`FREE_DRIVE` is an OpenTDU2 tool, **not a TDU2 game mode** (like the sandbox of OpenTDU, `GMSandbox`, and the
OpenTDU2 `WORLD_VIEWER`): you are on a road of the chosen island in a car of the database, you can switch car at
any time, and you just drive. No story, no timer, no traffic. The story mode `FREERIDE` (story step 2 / 8) is a
separate game mode and is not changed.

```sh
./launch_free_drive.sh                       # Ibiza, Ferrari California
./launch_free_drive.sh casino                # Casino island
./launch_free_drive.sh ibiza mustang         # a word of the car name, a _car_file_name or an _id_car
./launch_free_drive.sh hawai 371 -time 19:30 -spot <spot bank>
./build/bin/opentdu2 -gm FREE_DRIVE -island ibiza -car California -color 3   # same thing by hand
```

Code: `source/game/freedrive/gm_free_drive.{h,cpp}` (`GMFreeDrive`), `source/game/worldviewer/weather_controls.{h,cpp}`
(time of day / weather panel shared with the World Viewer), `launch_free_drive.sh`.

## 1. What it is made of

Everything is the existing OpenTDU2 code of the story modes and of the World Viewer, put together:

| Part | Source | Data |
|---|---|---|
| Island world (areas, sectors streamed around the car and dropped two rings away, island heightfield, ocean, sky, clouds, ambient effects) | `EdenSceneView` / `EdenWorld`, as `WORLD_VIEWER` | `islands/<island>/level/*` [DATA] |
| Spot exteriors (houses, hotels, the Casino) | `SpotExterior`, as `WORLD_VIEWER` (within 1.5 km) | `islands/<island>/exterior/<spot bank>.bnk` [DATA] |
| Driving collision | `VehicleWorldCollision` (sector `.shk` / `-R.shk`, `commonworld.bnk` `Library.dhk` placed by the sector `.pmi`, heightfield) + the `<spot>_sc_*.shk` of each loaded exterior in every spot frame (as FREERIDE adds the EPH_L3DI house collision) | [DATA] |
| Car model | `CinematicCar`: `vehicules/high/<_car_file_name>.bnk`, interior `vehicules/interiors/<car>_i.bnk`, rims `vehicules/rim/<DBRims _rsc_path>/<_rsc_file_name_front / rear>.bnk` of `_default_rims`, tyres of `common_car.bnk` (radius from the DBRims rim diameter and tyre `_largeur` x `_hauteur`) | [DATA] |
| Colours | `DBCarsAndColors` rows of the car in table order (the shop list, `docs/re/paint_spec.md` 1), `_color_id_1` = material of `<car>_color.2dm` (`CinematicCar::applyColor`) | [DATA] |
| Physics | `VehicleDynamics` with the car's `DBCarPhysicsData` row (`LoadVehicleSetup`, `docs/re/vehicle_physics_spec.md`), wheels on the model `D_RIM_xx` nodes | [CODE + DATA] |
| Camera | `ChaseCamera`: `CamerasInGame.xmb` cameras of the car's `_camera`, `CAR_CAMERA` cycles them | [DATA] |
| HUD | `FreeRideHud`: HUD.vmf, minimap (road triangles), GAUGES.vmf + `frontend/hires/gauges/<_hud_file_name>.bnk` of the car | [DATA] |
| Car lights, accessories | `CinematicCar` lights (brake, reverse, blinkers, position lights / headlights from the weather), `CarAccessories` (roof, windows, blinkers, `docs/re/accessories_spec.md`) | [CODE + DATA] |
| Time of day / weather | `WeatherControls` (GSWeather evaluation of `fx/game_weathers.xmb`, intemperies, fixed presets, rain), same panel and options as `WORLD_VIEWER` | `docs/re/weather_spec.md` |

## 2. Car list

`DBCarPhysicsData` rows whose `vehicules/high/<_car_file_name>.bnk` is installed. Name: `DBBrands
_manufacturer_name` (row of `_car_brand`) + `_car_model` + `_car_version` ("##" = empty text). Several rows can
share a car file (e.g. 371 / 501 / 502 Mustang Fastback 68): only the first row in table order is listed [INF: tool
choice]. The panel marks with `*` the cars whose rims, interior or gauge bank is missing (the car is still drawn
and driven). `-listcars` logs every database car with the banks it needs (to stage partial game data).

The default car is the California (387, the story car); `-car` takes a `_car_file_name`, an `_id_car` or a word of
the name; `-color` an index, a `_color_id_1` or a `_color_name`.

## 3. Start position and "back on the road"

* `spots.bin` record (0x110 bytes): `+0x10` position, `+0x20` heading, **`+0x28` / `+0x38` second transform**
  (the street side of the spot, the TESTCAR start: `IntroCinematic::ReadSpot`) [DATA]. The start spot is
  `-spot <bank>`, else the spot with the most sector banks installed around it (partial game data), else the
  first one. `-carpos x,y,z` / `-carheading deg` place the car anywhere.
* The game puts a car on the road with the road network (FUN_00a39630 → FUN_00cfb280 / FUN_00cfab80 on the road
  graph DAT_0119b420, `docs/re/story_spec.md`) [CODE]; that graph is not loaded by OpenTDU2
  (`roadinspection.pth` only holds 16-bit segment indices of it [DATA]). FREE_DRIVE snaps the point instead on the
  **nearest road surface** [INF]: the up-facing triangles of the sector `-R.shk` files ("sector roads") and of the
  spot collision (driveways, car parks, squares), score = horizontal distance + 1/4 of the height difference (the
  level of the car first on bridges). Search passes, the first hit wins: spot start / teleport: sector roads
  within 60 m of the street side transform, then any road surface within 300 m (the spot heading is kept when the
  point is within 15 m); back on the road: sector roads within 50 m, any surface within 50 m, sector roads within
  400 m, any surface within 400 m.
* Heading along the road: sum of the longest horizontal edge of the road triangles within 12 m (road strips are
  long along the road), the sign closest to the car's heading. Lane: the road surface is measured across (0.5 m
  steps, up to 15 m each side) and the car is put a quarter of the width right of the middle when the road is
  wider than 6 m (right-hand traffic), else in the middle [INF].
* **F3** or B (the game's `BACK_ON_TRACK` key, `Default_KB.xmb`): the car is put back, stopped, on the nearest road. (In the game
  `BACK_ON_TRACK` is held with a `HUD/BOT` gauge, FUN_009292c0 [CODE]; here a press is enough [INF: tool].)
* Teleport: the panel lists the spots (street side position); a click puts the car on the road there.

## 4. Switching car

F4 / Shift+F4 (next / previous in the list) or a click in the panel: the GPU is idled, the car, its HUD gauges
and accessories are unloaded, the renderer's Z pre-pass / shadow caster material clones are dropped (they are keyed by material address + hashcode, which the next car's "Paint"... materials can reuse: a crash on a second switch before this), the new car is loaded (model, rims, interior, colour 0 or the chosen one, physics
row, cameras, gauges, accessories), and the vehicle is reset at the **same position, orientation and velocity**
(raised to the ground if needed) [INF: tool behaviour, the game changes car only in garages / dealers]. F8 cycles
the colours (applied in place). Changing island (panel, F5 / F6 / F7) reloads the world and the current car at the
new island's start spot.

## 5. Controls (no numpad needed: Mac keyboard)

| Action | Keys |
|---|---|
| Throttle / brake-reverse / steer | Up / Down / Left / Right (`Default_KB.xmb`), W / S / A / D |
| Handbrake | Left Shift |
| Camera | C (`CAR_CAMERA`) |
| Accessories menu (roof 8, windows 2, blinkers 4 / 6) | 4 then 8 / 2 / 4 / 6 (main row, `docs/re/accessories_spec.md` 2) |
| Back on the road | F3 or B (`BACK_ON_TRACK` of `Default_KB.xmb`) |
| Next / previous car | F4 / Shift+F4 |
| Next colour | F8 |
| Lights off / auto (weather) / high beam | F9 or Enter (`CAR_LIGHT` of `Default_KB.xmb`; light mode +0x1d4, `docs/re/car_lights_spec.md`) [INF: the `CAR_LIGHT` handler is not identified] |
| Ibiza / Hawai / Casino | F5 / F6 / F7 |
| Panel / debug UI | F2 / F1 |

On a Mac laptop the F keys may need the `fn` key (or the "use F1, F2... as standard function keys" setting);
everything is also in the panel (mouse). The panel: island, position / speed / camera, back on the road, HUD on /
off, car (installed banks), colour, lights, hazard lights, car list with a filter, time of day / weather (same as
the World Viewer), spot teleport list, controls.

## 6. Command line

`-gm FREE_DRIVE`, `-island ibiza|hawai|casino`, `-car <name|id>`, `-color <index|id|name>`, `-spot <bank>`,
`-carpos x,y,z`, `-carheading deg`, `-sectorradius n` (default 1), `-noexteriors`, `-nopanel`, `-noworldfx`,
`-listcars`, `-logdrive`, and the weather options of the World Viewer: `-time hh:mm`, `-timespeed x`,
`-intempery <w>`, `-nextintempery <w>`, `-weather <key or preset>`, `-rain`. Tests: autoplay `key=F3` / `key=F4`
/ `key=F8`, `press=backontrack`.

## 7. Limits

No traffic, no engine sound model, no driver avatar in the car, no road network (see 3), Hawai and Casino have no
gameplay data of their own here (they are driven like Ibiza; the Ibiza zone badge of the HUD is hidden there).
The HUD minimap shows the road triangles of the loaded sectors.
