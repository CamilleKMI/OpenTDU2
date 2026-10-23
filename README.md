# OpenTDU2

[![macOS](https://github.com/CamilleKMI/OpenTDU2/actions/workflows/macos.yml/badge.svg)](https://github.com/CamilleKMI/OpenTDU2/actions/workflows/macos.yml)
[![Linux](https://github.com/CamilleKMI/OpenTDU2/actions/workflows/linux.yml/badge.svg)](https://github.com/CamilleKMI/OpenTDU2/actions/workflows/linux.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Vulkan](https://img.shields.io/badge/Vulkan-MoltenVK-red)

OpenTDU2 is an open source re-implementation (C++17 / Vulkan / Jolt Physics) of the engine of
**Test Drive Unlimited 2** (Eden Games, 2011).

The project is built on the structure of
[OpenTestDriveUnlimited](https://github.com/opentestdriveunlimited/OpenTestDriveUnlimited) (OpenTDU, the TDU1
re-implementation). It uses the same conventions: `GS*` game services, `GM*` game modes, original file formats
read as they are, the Jolt physics library, and D3D9 → SPIR-V shader translation through DXVK's DXSO compiler.

The original game is **not** included. OpenTDU2 reads the *unpacked* files of a legal copy (`Euro/Bnk`,
`Euro/Shaders`, ...: the layout used by the TDU2 Universal Launcher, not the `.big` archives).

Everything is read from the game files or from the **readable** code of `TestDrive2.exe`. About 3 MB of the
executable is encrypted by SecuROM; that part is never decrypted or bypassed. When a behaviour lives only there,
OpenTDU2 follows the data and marks the choice as inferred (`[INF]` in the code and in `docs/re/*.md`). See
[Grey areas](#grey-areas-what-is-approximate-and-why).

## Screenshots

| | |
|---|---|
| ![Title screen](docs/images/title_screen.png) Press START screen (real-time cinematic of `press_start.bnk`) | ![Select your character](docs/images/select_character.png) "Select your character" (C_AVATAR party in slow motion) |
| ![Pool party](docs/images/pool_party.png) C_INTRO: pool water (refraction), reflections, avatar skin and faces | ![Tess](docs/images/tess.png) C_TESS: Tess talking (FaceFX lip sync), lighting from the house lamps |
| ![Walk mode](docs/images/walk_mode.png) First-person walk mode, goal "Talk to Tess", reticle | ![Ocean](docs/images/ocean_bay.png) Ocean and shore foam over the bay |
| ![Garage](docs/images/garage_door.png) FIRST_WM garage: the California, roof down, driver door opening, untinted glass | ![Cockpit](docs/images/cockpit.png) Seated in the cockpit, hands on the wheel |
| ![Freeride](docs/images/freeride.png) First Freeride: driving the California in Ibiza (Bienvenidos village) | ![World viewer: Ibiza](docs/images/world_ibiza.png) World viewer, Ibiza: village, beach and tall ship near the intro party house (spot `EPH_L3DI`, weather `19.30`) |
| ![World viewer: Casino island](docs/images/world_casino.png) World viewer, Casino island: the bay and pier at sunset (weather `19.30`) | ![World viewer: Casino](docs/images/world_casino_resort.png) World viewer, Casino island: the casino (weather `19.30`) |
| ![World viewer: Hawai](docs/images/world_hawai.png) World viewer, Hawai (Oahu): houses and palms near spot `ETUN_G`, mountains behind (weather `18`) | |

## Build and run on macOS (Apple Silicon or Intel)

Requirements:

* Xcode Command Line Tools: `xcode-select --install`
* CMake ≥ 3.24 and Ninja: `brew install cmake ninja`
* The **LunarG Vulkan SDK** (provides MoltenVK): <https://vulkan.lunarg.com/sdk/home#mac>. It can simply be
  unpacked into `~/VulkanSDK/<version>`; the build script and the game find it there.
* Network access for the first `cmake` run: SDL3, volk, Vulkan-Headers, VMA, Jolt Physics, Dear ImGui and stb
  are fetched with `FetchContent`.

Build:

```sh
cd OpenTDU2
./build_macos.sh                 # configure + build in Release (folder build/)
```

or by hand:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run:

```sh
./build/bin/opentdu2                          # game data found automatically (see below)
./build/bin/opentdu2 -game /path/to/TestDrive2
```

The game data folder is looked up in this order: `-game <folder>`, the `OTDU2_GAME_DIR` environment variable,
the current folder, then the parent folders of the executable that contain `Euro/Bnk`. If the project sits
inside the game folder (`TestDrive2/OpenTDU2`), nothing has to be set.

Linux works too (tested with Mesa / llvmpipe). Windows should compile with MSVC but has not been tested.

### World viewer (developer tool)

A free-fly camera over an island, like OpenTDU's world viewer (game mode `WORLD_VIEWER`, not a TDU2 mode):

```sh
./launch_world_viewer.sh                 # Ibiza
./launch_world_viewer.sh casino          # Casino island directly (also: hawai)
./launch_world_viewer.sh hawai -game /path/to/TestDrive2 -weather 18
```

The script finds the game data like the game does (`-game`, `OTDU2_GAME_DIR`, the parent folders of the
repository, a `TestDrive2` folder next to them, `~/TestDrive2`) and passes any other option to `opentdu2`
(`./build/bin/opentdu2 -gm WORLD_VIEWER -island casino` is the same thing).

* Look: hold the right mouse button (or the left one outside the panel); Tab = capture the mouse (look without a
  button, Tab again to release).
* Move: W / S (or Up / Down) = forward / backward along the view, A / D (or Left / Right) = strafe,
  E / Space / Page Up = up, Q / Ctrl / Page Down = down, **Shift = speed x2**, mouse wheel = base speed (x1.1 per
  notch, like OpenTDU), the camera stays 1.5 m above the loaded terrain ("ground clamp").
* Panel (ImGui, F2 hides it): island Ibiza / Hawai / Casino (also F5 / F6 / F7; the world is unloaded and the
  new island loaded), position / speed, base speed, FOV, sector radius, weather (`game_weathers.xmb`), spot
  exteriors, and the `spots.bin` list (click = teleport; the number is how many of the 9 sector banks around the
  spot are installed).
* Streaming follows the camera: the sectors around it (radius 1, `-sectorradius n`) are loaded and the ones two
  rows away unloaded; every area bank, the island ocean and the island level heightfield (`<island>.bnk`, drawn
  where no area is loaded) stay loaded; spot exteriors (`exterior/<spot bank>.bnk`) within 1.5 km.
* Missing banks are skipped with a warning (partial game data). The start position is the `spots.bin` spot with the
  most sector banks installed around it (`-spot <bank name>` to choose, `-campos x,y,z -camyaw deg -campitch deg`
  to place the camera, `-flyspeed m/s`, `-noexteriors`).

### Command line options

| Option | Effect |
|---|---|
| `-game <folder>` | game root folder (the one that contains `Euro/`) |
| `-lang fr` | text language (`db_<lang>.cpr`); default: system language |
| `-w`, `-fullscreen`, `-width N`, `-height N`, `-novsync` | video (otherwise `SystemPC.cpr`) |
| `-gm <mode>` / `-intro` | start in a game mode: `LOGIN`, `AVATAR_CHOICE`, `FIRST_WM`, `FREERIDE`, `WORLD_VIEWER` (tool) |
| `-island <name>` | WORLD_VIEWER: `ibiza` (default), `hawai`, `casino` |
| `-skipintro` | AVATAR_CHOICE: skip C_INTRO, straight to "Select your character" |
| `-walkmode n` | AVATAR_CHOICE: avatar n (1..6) chosen, straight to the house walk mode |
| `-avatar <config>` | FIRST_WM / FREERIDE started directly: DBPNJ avatar config (default `AVT_Male_Intro`) |
| `-allowskip` | FIRST_WM: the garage intro can be skipped |
| `-skipcine` | FREERIDE: skip C_FIRST_OUTRO, start on the road |
| `-playername <name>` | name shown in "Starring" (no profile yet; default: system user name) |
| `-envquality n` | `ENVIRONMENT_QUALITY_LEVEL_n` of `Quality_Settings.cpr` (0..3, default 3 = max; the game without a save uses 1) |
| `-FORCELOGINCINEINDEX n` / `-cine n` | title screen cinematic (0..3, `entities.xmb` order); R key = next one |
| `-nosob` | do not use the original `.sob` shaders for the 2D layer |
| `-vk_enable_validation_layer` | Vulkan validation layers |
| `-verbose` | detailed log in `opentdu2.log` |
| F1 | debug UI (ImGui) |

### Controls

Bindings come from the game's `Default_KB.xmb` (plus WASD); a gamepad (SDL) is supported.

* Menus: arrows, Enter = validate, Backspace = back, Esc = pause. "Select your character": left/right, Enter or
  mouse click.
* Walk mode (house, garage): arrows / WASD = move and strafe, mouse or right stick = look, Enter = act on what the
  reticle points at (the "Garage" door, the car). Open the car door: **numpad 4** (`ACCESSORIES_LEFT` in
  `Default_KB.xmb`) or the d-pad left.
* Car: Up / W = throttle, Down / S = brake / reverse, Left / Right = steer, Left Shift = handbrake, C = camera.

## Status: done and to do

| Area | Done | To do |
|---|---|---|
| Game files | `.bnk` banks, XMB (`XMBF`, both endians), encrypted `.cpr` (XTEA), database `db_data.cpr`, localisation `db_<lang>.cpr`, `SystemPC/GamePC/Physics/Quality_Settings.cpr`, `Default_KB.xmb` | `.big` archives, save games (format is in encrypted code) |
| Rendering | Vulkan (volk + VMA, MoltenVK); original `.sob` shaders translated D3D9 → SPIR-V for the 3D and the 2D layer; `.2db` (incl. cube maps), `.3dg`, `.3dd`, `.2dm`; `.UVA` texture animations; shadows (cascades + combine), post FX, haze, sky, clouds, weather presets; ocean with shore foam; GSReflection pool water (refraction / mirror projectors); interior day/night lightmaps | Rain, night lighting of the world, traffic lights, more post FX variants |
| World | Ibiza sectors and areas streaming, `.pmi` instances with the game's LOD factor, house spot placement, exteriors and interiors; area grid from `worldconfig.cpr` (Ibiza 7 x 6, Hawai / Casino 11 x 9), sector unloading, island level heightfield | Map mode, LOD meshes below LOD0, asynchronous loading |
| WORLD_VIEWER (tool) | Free-fly camera over Ibiza, Hawai or Casino (`launch_world_viewer.sh`), mouse look, WSAD, Shift x2, ImGui island switch, `spots.bin` teleport, spot exteriors, weathers | Traffic, roads / collision debug views |
| Flash UI | VMF reader, Eden fonts, display list, AVM1 ActionScript, buttons (keys + mouse), masks, EditText layout from the exe, `sbdd` localisation, font shader (TD_Flash PS #7), inline `#bitmap:` icons (`$$bitmaps$$` registry), `#command:` keys (orange `[key]` text on keyboard, like the original) | Pad key maps (the default X360 button binding is not in the data), some rare AVM1 opcodes |
| Audio | Eden mixer rebuilt from `audiosetup.xmb` (clients, routing matrices, mix presets, ducking), 3D positional sounds, cinematic Fx, voice banks per language | User volume sliders, ambiance emitters, engine sound model |
| Avatars | DBPNJ configs, bodies, heads, clothes, hair colours, body zones, skin constants (freckles, age, tone), skeleton animation with crossfades, style animations (m/w), avatar lighting (house lamps, camera light, shadow influence), driver seating (`IKOffsetDB.bin`, `stance_turn`, hand/foot/back IK targets), FaceFX facial animation in the intro / garage cinematics (`.fxa` actors, `.fxe` lip-sync curves, face graph, face shape sliders, expression normal maps) | FaceFX look-at, FaceFX outside the intro cinematics, finger grip and steering blend while driving |
| LOGIN | 3D press start cinematics, LOG_IN2.vmf, press start, "no progression data" popup | Profile / save game loading |
| AVATAR_CHOICE | C_INTRO with credits and slow motion, "Select your character" (6 candidates, transitions, slow-motion party and music), C_FIESTA_INTRO, first-person walk mode with Havok collisions, "Talk to Tess" → C_TESS → "Go to the garage", garage door | — |
| FIRST_WM (garage) | Garage intro, Tess and her voice lines, the California (roof down, cockpit LOD), tips ("Astuce") from `DBTutoMess`, door opening, enter the car, cockpit view with hands on the wheel, engine start → C_OUT / C_OUTRO | — |
| FREERIDE | Story step 2: C_FIRST_OUTRO, car on the road, driving (edVehicle drive train, controllers and aids from the exe, 90 Hz), collisions (roads, terrain, world objects), chase camera from `camerasingame.xmb`, HUD (goal, zone badge, minimap, GAUGES movie with the California rev counter, speed, gear, odometer), 90 s then white fade | Story step 3 "Meeting Tess", traffic, other cars, shops, houses |
| Not started | | Online, multiplayer, Casino and Hawai gameplay (both can be flown over in WORLD_VIEWER), challenges, races, map, phone, dealerships |

## Grey areas: what is approximate and why

The rule of the project is to read behaviour from the data or from the readable code. The items below could not be
read completely, mostly because the code that decides them is in the SecuROM-encrypted part of the executable.
Each one is marked `[INF]` (inferred) in the sources and documented in `docs/re/`.

| Topic | What is approximate | Why |
|---|---|---|
| Car driving | The chassis constraint solver (contact / suspension force law, rigid body integration), the RK4 combination of the drive train stages, the contact coupling of the tyres to the chassis, a few predicates (idle / limiter thresholds, ABS / TC activation, shift speed getters), wheel and chassis inertia, ground materials (grass = terrain) | TDU2 cars use Eden's own `edVehicle` library, not the Havok vehicle SDK. The 90 Hz step, the drive train (engine torque / damping / speed clamp, clutch, gearbox, open differential with its bias limit, wheel axles with the car inertia share and the tyre torque at every stage), the engine / clutch / gearbox controllers (automatic shifts at the red / blue lines), ABS / traction control / hand brake regulation, aero anchors and damping curves are read from the code; the chassis core of `edVehicle` is in the encrypted part (see `docs/re/vehicle_physics_spec.md` §6 - 7) |
| Chase camera | Camera motion (lag, lean, shake) | Positions and FOV come from `camerasingame.xmb`; the update code was not found |
| HUD | Minimap road drawing, zone lookup, which gauge-bank texture is the dial / the needle, in-car DASHBRD screen not drawn | Map drawing and zone lookup are encrypted; texture arguments are lost in the readable code |
| Walk mode | Mouse sensitivity, body radius (0.3 m), step height, strafing on left/right | The walk controller and its camera are encrypted; speeds and head limits come from `avatarsdriver.bnk` |
| Tips and goals | When each tip opens and closes; "Talk to Tess" shown when walk mode starts | Texts and delays come from `DBTutoMess`; the trigger conditions are encrypted |
| Seated avatar | The IK solver (analytic two-bone), seat adjustment added to the seat node, head hidden in the cockpit view | Seat node, `IKOffsetDB.bin` offsets, driving clip and IK targets come from the data and readable code; the Havok IK solver, the seat move and the cockpit render branch are encrypted |
| Avatar lighting | Lamp distance fall-off (linear from DropOff to CutOff), light sample point (feet + 1 m), camera axis | Small helper functions are encrypted; everything else follows the readable code |
| 3D sound | Attenuation curve and panning law | The edAudio spatializer is encrypted; distances, clients and matrices come from the data |
| Cinematics | Shot fade parameters, start-of-cinematic skip input, roof state in the garage (open, from the general convertible rule) | Read from data and partial code; the shot / scene code (CinScene) is encrypted |
| Pool reflections | Avatars in the mirror layers are on | The intro-mode check is encrypted; the original layer setup includes them |
| Ocean | Swap rule between island, area and sector ocean tiles; foam texture addressing (mirror) | The tile selection and the 2DM sampler byte meaning are not readable |
| World viewer | Island level heightfield (`<island>.bnk`, 384 m grid) drawn where no area is loaded; spot exteriors placed from `spots.bin` (bank name -> `exterior/<name>.bnk`, position, heading); streaming distances and camera speeds | The level LOD selection is encrypted; the tool values (speeds, distances, sensitivity) are OpenTDU2's own, the wheel steps come from OpenTDU |
| Animations | Walking clips "jump" at their loop point | This is the game data itself: the readable time controller wraps exactly like OpenTDU2 |

## Code layout

```
source/
  core/          logger, assert, maths, memory, XMB, CPR crypto
  system/        command line
  filesystem/    GSFile (game root, case-insensitive paths), Bank (.bnk)
  config/        GSConfig (SystemPC / GamePC / Physics / Quality_Settings .cpr)
  database/      GSDatabase (db_data.cpr, localisation)
  input/         GSInput (Default_KB.xmb, keyboard, mouse, gamepad)
  audio/         GSAudio (Eden mixer, audiosetup.xmb, 3D sound)
  render/        GSRender, Vulkan device, 2DB/3DG/3DD/2DM, texture cache, Renderer2D, Eden renderer
    eden/        original-shader renderer, scene view, shadows, post FX, ocean, sky, planar reflections
    shaders/     ShaderLibrary (.sob), DXSO -> SPIR-V, FlashShaderPipeline (TD_Flash.sob)
  flash/         VMF reader, FlashMovie, FlashPlayer (AVM1), localisation
  physics/       GSPhysics (Jolt), Havok .shk/.dhk collision reader, vehicle data / dynamics / collision
  world/         EdenWorld (sectors, .pmi instances, ocean tiles)
  game/          game modes: bootmenu (LOGIN), avatarchoice (AVATAR_CHOICE), firstwalkmode (FIRST_WM),
                 freeride (FREERIDE), worldviewer (WORLD_VIEWER tool); walkmode, cinematics, cars, avatars,
                 player profile
  tdu_instance.* main loop, services, game mode switching
third_party/dxvk DXVK's DXSO compiler (zlib licence), adapted (see OpenTDU)
tools/           otdu2_bnk_tool (list/extract/decrypt/xmb), otdu2_sob_tool (dump + SPIR-V translation)
docs/            file formats (docs/formats.md) and reverse-engineering notes (docs/re/*.md)
```

Tools:

```sh
./build/bin/otdu2_bnk_tool list    Euro/Bnk/Vehicules/High/california.bnk
./build/bin/otdu2_bnk_tool extract Euro/Bnk/FrontEnd/HiRes/main.bnk out/
./build/bin/otdu2_bnk_tool decrypt Euro/Bnk/Database/db_data.cpr db_data.xmb
./build/bin/otdu2_sob_tool Euro/Shaders/TD_CarPaint.sob out/      # .dxso + .spv per shader
```

## Licence

OpenTDU2 code: to be defined by the repository owner. `third_party/dxvk`: zlib (see `third_party/dxvk/LICENSE`).
Test Drive Unlimited 2 and its data belong to their respective owners; no game data is included.
