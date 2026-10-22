# FIRST_WM garage: "Tip" boxes, driver door, entering the California, cockpit, engine start, exit to FREERIDE

Tags: [CODE] readable x86 (VA >= 0x6c1000) · [DATA] game files · [INF] inferred (reason given) · [ENC] SecuROM range
or not present in readable code/data (not read).
Complements docs/re/tess_garage_spec.md §3–§5 and docs/re/california_spec.md.
Decompiles: /tmp/claude-0/gc/d1.c … d16.c (Ghidra copy /tmp/claude-0/al/gh). Flash dumps: /tmp/claude-0/gc/gen_647.txt
(GENERAL `pop_tutorial`), gen_37/632/634/643.txt.

---------------------------------------------------------------------------------------------------------------
## 1. The "Tip" box (screenshots 1 and 3)

### 1.1 Flash: GENERAL.vmf `pop_tutorial` [DATA]
- Sprite 647 (288 frames) on the GENERAL root at (66.8, 432.9), depth 995, name `pop_tutorial`.
- Labels: `default` 0 (defines `close_popup()`: `close_tuto_WM` if `_root.IsWalkMode` else `close_tuto`, sound
  `sndp:mclose`), **`open_tuto` 7** (→ `open_tuto_WM` 85 when `_root.IsWalkMode`), `close_tuto` 65 (→ default at 84),
  `open_tuto_WM` 85, `close_tuto_WM` 190, `open_tuto_block` 210 (modal version with an OK button, `a_valid()` →
  FSCommands `gein` … `geou`, `fccl`), `close_tuto_block` 268.
- Children: dark panel (ch616, 50% black + bitmap corners 2492/2494/2501), sprite 37 (the **book icon**, pulsing
  alpha), sprite 632 (EditText `/:m_Tutorial`, DBLoc 0x297 category common = "Tip"/"Astuce"), sprite 634
  (EditText `/:TutText`, 394 × 65 px, multiline). Non-WM variant: text at y −218 → upper-left of the screen (matches
  the screenshots, `/:IsWalkmode` false).

### 1.2 Engine: tutorial manager (object `PTR_PTR_00f80198`, vtable 0x00eec7f4) [CODE]
- Tutorials: array `DAT_00f801ac[DAT_00f801a4]` of objects created by the fourcc factory at 0x0042xxxx
  (`FUN_00503c60(0x44, 'opdo')` → FUN_004284c0 …) **[ENC]** — their condition vfuncs (+4 active, +8 can show, +0xc
  re-arm, +0x10 completed, +0x18 close, +0x24 on-show) are encrypted.
- Object fields: +4 id, +8 DBTutoMess row index, +0xc timer, +0x10 condition timer, +0x14 last-on-screen timer,
  +0x18 done, +0x19 on screen, +0x1a completed, +0x1c "needs cooldown", +0x1d text set, +0x20 NbTimeAllowed
  (save XMB keys `Cancel`, `NbTimeAllowed`, FUN_00d61bb0).
- Tick FUN_00d63240: manager cooldown +0x218 -= dt; per tutorial (not done): if (NbTimeAllowed == −1 or > 0) and
  active(): timer += dt, show when timer > conditiontimer, canShow(), no tutorial on screen (+0x10 of the manager) and
  (no +0x1c flag or cooldown elapsed); else timer = 0. On screen: timer += dt; close (FUN_00d61600) when
  (lastonscreentimer ≠ 0 and timer > it) or close(); FUN_00d61600: +0x19 = 0, +0x18 = 1, timer 0, `/:IsWalkmode` =
  (walk manager present and story step ≥ 2), `pop_tutorial.close_popup()`.
- Show FUN_00d62ed0: +0x19 = 1, timer 0, vfunc +0x24, NbTimeAllowed-- (unless −1); `/:IsWalkmode` = true only for
  "invi"; FUN_00d62a90 writes `/:TutText` from the DB row (copied 0x58 bytes from `DAT_01024d84 + idx*0x58`):
  first 64-bit loc id = `_mess_hc`, second = `_messpc_hc`; the PC text is used when it is non-zero and the device
  name (FUN_0050fe80(2,0)+0x30) is not "Microsoft X360 Pad"; then `gotoLabel("open_tuto", "/pop_tutorial")` (except
  "frog", and "conn" uses `open_tuto_block`). Cooldown = 10.0 (5.0 at story step 0).

### 1.3 Data: DBTutoMess (db_data.cpr) [DATA]

| _tutomess | _mess_hc | text (US) | condition | on screen | NbTimeAllowed | _soundfilename |
|---|---|---|---|---|---|---|
| `opdo` | 0x13ac = 5036 | Position the reticle over the car, and press #command:ACCESSORIES_LEFT# to open the door. | 2 s | 0 (until closed) | −1 | Intro_FiestaGarage_04 |
| `stca` | 0x13ab = 5035 | Start the car with #command:CAR_ACCEL# | 2 s | 0 | 1 | Intro_FiestaGarage_06 |
| `updw` | 0x13ad = 5037 | Press #command:CAR_ACCEL# to crouch / CAR_BRAKE to stand up | 10 s | 15 | 1 | — (not a garage one) |

5034 "Go next to the car" is in no DBTutoMess row (unused here). Fourcc strings at 0x00eec634/0x00eec63c are not
referenced by readable code → which tutorial objects exist in FIRST_WM and their conditions: **[ENC]**.
Implemented conditions [INF]: opdo = walk mode, door closed, not in the car; closes when the door opens. stca =
seated, engine not started; closes when the throttle starts the timer. The tutorials' own sounds (vfunc +0x24) are
not played (the same voices are played by C_VOICE_04/06) [INF].

### 1.4 `#command:XXX#` [CODE]
- EditText layout FUN_00849dd0 (read-only fields only): markups `color:`, `endcolor`, `nocolor`, `underline`,
  `nounderline`, `bitmap:`, **`command:`** (strings 0x00ecb8bc…). `command:` copies the name (≤ 0x7f chars) and calls
  the global resolver `DAT_013e281c` (vtable 0x00ee6ff0, set by FUN_00d82010) vfunc +4 = **FUN_00d87cc0**, then lays
  out the returned string recursively.
- FUN_00d87cc0: LEFT_PAD / RIGHT_PAD special cases (all 4 AVT_MOVE_* / AVT_LOOK_* on a pad stick → `#bitmap:x360_LS#`
  / `#bitmap:x360_RS#`, else the keyboard form `#color:ff6600#[%s]#endcolor#` with key-map entry +0x6c); every other
  action goes through the encrypted thunk `PTR___break_00e4141c(keymap, name)` **[ENC]**. Pad channel → bitmap table
  next to it (FUN_00d87930): BUTTON_0..11 → x360_A, B, X, Y, LB, RB, LT, RT, Start, Back, LS_clic, RS_clic;
  STICK_0_LEFT/RIGHT/UP/DOWN → LS…; LEFT/RIGHT/UP/DOWN → x360_DPad_*.
- Key map (Default_KB.xmb) [DATA]: ACCESSORIES_LEFT = PAD_4, ChannelLocName "4 (PAVE NUM.)"; CAR_ACCEL = UP, "HAUT".
- Implemented: keyboard form `#color:ff6600#[<ChannelLocName>]#endcolor#` for every action [INF: same format as the
  readable keyboard branch]. Pad bitmaps (`#bitmap:`) are not drawn by the OpenTDU2 flash player (screenshots 1/3 are
  X360-pad: DPad_Left and RT glyphs).

---------------------------------------------------------------------------------------------------------------
## 2. Driver door (screenshot 2)

### 2.1 Who opens it
- FIRST_WM command handler FUN_009a9c4e [CODE]: FSCommand **`opdo`** → unless the player is in a car
  (player->vfunc5c()->+0xd0 && FUN_00c3f360() && car+0xa794 ≠ 0) → **FUN_00a48770(2.0, 1, 1, 1)** on the door object
  `DAT_00fd9290 + 0x8510`.
- intro.vmf `ItemType`: only SelItemType `vehicle` + 'over' sends `opdo` (and sets `Input` true, cursor_anim 'on')
  [DATA]. The garage selectable is `car` (FST_EnterCar). What makes the walk mode send `opdo` for the California is
  not in readable code [ENC]; loc 5036 says the player presses ACCESSORIES_LEFT with the reticle on the car.
  Input ids: 0x34/0x35/0x36/0x37 are used as left/right/up/down (door left/right toggles in the in-car door code
  0x00a498bf, menu left/right in FUN_008f9f60) → [INF] 0x34 = ACCESSORIES_LEFT.
- Implemented [INF]: ACCESSORIES_LEFT (key PAD_4 of Default_KB, or the pad d-pad left = IA_MenuLeft without the
  strafe key) while the reticle ray (eye, forward, 50 m) hits the BODY_HR bounds of the car and the door is closed →
  `opdo`.

### 2.2 Door object (vtable 0x00f20214, constructor FUN_00a489f0) [CODE]
- Channels: left door +0x14 elapsed / +0x18 duration / +0x1c opening (param 2 = 1), right door +0x24/+0x28/+0x2c;
  windows +0x34.. / +0x44.. Cinematic messages handled in FUN_00dc5a40: `opdl`/`opdr`/`opda` (open left/right/all,
  duration in the argument), `cldl`/`cldr`/`clda`, `opwl`/`opwr`/`opwa`, `clwl`/`clwr`/`clwa`.
- FUN_00a48770(duration, channel, open, keep): duration 0 → elapsed = duration = 1.0 (instant end state); otherwise
  starts / reverses the channel (reversal maps the progress through the inverse ease FUN_00a483f0).
- Tick (0x00a49400): elapsed = min(elapsed + dt, duration); FUN_00a484c0: f = elapsed/duration (1 − f when closing),
  **angle = −record(+0x14c) · ease(f)** for the left door (+ for the right); FUN_00c67230(angle) → car+0xa794 and the
  node pose of slot 0x1d (DOOR_L); FUN_00c67200 → car+0xa790, slot 0x1e (DOOR_R).
- Ease FUN_00a48310 (constants 0x00f49af0 0.1, 0x00f49748 0.5, 0x00f49ad8/0x00f49ad0 1.0555556/0.0555556,
  0x00f49ae8/0x00f49ae0 0.8888889/0.1111111): opening: f < 0.1 ? 0.5 f : 1.0555556 f − 0.0555556; closing:
  f < 0.1 ? 2 f : 0.8888889 f + 0.1111111.
- record +0x14c: the max angle of the car record (FUN_00d8bb90 on the car id); DB `DBCarPhysicsData._doormaxangle`
  = 1 for the California [DATA] [INF: field mapping]. Entering requires `car+0xa794 == −1.0` (0x00ede404) = fully
  open.
- In-car (not used here): input 0x34 toggles the left door, 0x35 the right one (2.0 s).

### 2.3 Door pose FUN_00c67020 [CODE]
- Key-node channels at car+0xaa40 (left) / +0xa7a0 (right), 3 × 0xe0 bytes, built by FUN_00c6a610 through
  FUN_00c66b50 with the names `DOOR_L_A_x` / `DOOR_R_A_x` (strings 0x00ef5d38 / 0x00ef5d2c; letters A..C, x = 1..4):
  value/duration → eased → segment interpolation between the key nodes (FUN_00451560 [ENC]).
- **california.3dd has no `DOOR_x_A_n` node** [DATA: folded hashes absent] → fallback branch: FUN_0042afe0 [ENC],
  **FUN_00ddbcd0(angle)** (sin/cos [ENC] written to matrix entries [0][0],[0][2],[2][0],[2][2] = rotation about Y),
  FUN_0042b050 applies it to the node slot record car+0x6d0+0x60·0x1d [ENC].
- Implemented: DOOR_L local frame rotated about its Y axis by the angle (−1 rad = outwards, +X is the car's left);
  the DOOR_L subtree (mirror, windows, INT_LR_L) and the interior bank panel INT_HR_L (instanced on the door,
  FUN_00c69e30) follow [INF: post-multiplied local rotation].
- Car nodes (california.3dd) [DATA]: DOOR_L at (0.903, 0.545, 0.506) (front edge = hinge), its parent `DOOR_L_COL`
  (folded "??OR_L_C") at (0.878, 0.494, −0.145); seats `DVRSEAT_LR` (0.365, 0.247, −0.382) / `PSGSEAT_LR`
  (−0.355, 0.245, −0.368); STEER_LR (0.369, 0.724, 0.068) with HAND_LR_L (0.550, 0.762, 0.064) / HAND_LR_R
  (0.187, 0.732, 0.054); FOOT_LR_*; BACK_LR_*.

### 2.4 Low bar "Entrer dans la voiture"
- FUN_009aa560 [CODE]: enter needs the selected item string "car" (FST_EnterCar), carReady (door angle == −1.0) and
  input 0x38 or FUN_00d5e4d0(3). intro.vmf `ItemType('over')` for `car`: `cursor.disp_low_bar('over', true)` →
  low_bar `open_down` (icon above, `/:t_wm_destination` = `LowBarText()` = `_root.m_EnterCar_` below) [DATA].
- Screenshot 1 (door closed, reticle on the car) has no low bar → [INF] the "car" item is only picked once the door is
  fully open (implemented as a selection filter).

---------------------------------------------------------------------------------------------------------------
## 3. Seated view (screenshot 3)

- C_ENTER_CAR (3.99 s, camera c_enter, FxC_ENTER_CAR sound) then FUN_008c1880 (enter car, seat 0) [ENC body].
- Cockpit camera [DATA]: `database/camerasingame.xmb` (`CameraInGame."Car 141"`, id_car 387, 6 cameras; DB
  `_camera = 387`). Camera 1, **mode 23**: fixed_pos (0.37, 1.01, −0.36), fixed_targ (0.37, 0.90, 1.353186), fov 55°
  (0.959931), near 0.05, show_flags 262185, steering_wheel_offset 150, stick_head ±130 / up 30 / down 15. Its eye is
  above DVRSEAT_LR → [INF] mode 23 = cockpit. (Camera 0 mode 20 chase fov 45; 2 mode 24 bonnet; 3 mode 43, 4 mode
  44 rear views; 5 mode 40.)
- Car frame: +X left, +Y up, +Z forward (front bumper z = 1.94) [DATA].
- Driver avatar seating: decoded in **docs/re/driver_seat_spec.md** (avatarsdriver.bnk driveiklayer0..6.bas
  blend / IK graph, `stance_turn.anm` pose, IKOffsetDB.bin per-car / per-sex node offsets, seat placement
  FUN_00b8ade0 / FUN_00b82450 and IK goals FUN_00b7e940 on the car nodes DvrSeat / Hand / Foot / Back) [CODE+DATA].
  Implemented: avatar root at DvrSeat + offsets, stance_turn pose, IK of back / arms / legs onto the car nodes;
  the head is not drawn in the cockpit view [INF].
- C_OUTRO's .sce has `Avatar` with message `avm4:CIN_CAR` [DATA] → the avatar stays seated in CIN_CAR while it drives
  out [INF]; it is drawn seated through C_OUT / C_OUTRO.

---------------------------------------------------------------------------------------------------------------
## 4. Engine start and exit [CODE]

- FUN_009aa560: throttle (car+0x79bc → +0x380) > 0.7 (0x00eddb08) with enteredCar → timer = 3.0 (0x00edf3b8),
  DAT_01179620 saved / = 0x1e, **FUN_009bddf0("C_OUTRO")**: finds the `__FX` entity named "C_OUTRO" (Entity__Fx, Fx
  0x40a58c5b68a6f9de, AutoStart 0) and starts it (FUN_00b17b70(fx, −1.0)) → the engine/outro sound
  (c_outro_CINEMATIC.wav 11.5 s) starts at the throttle press.
- timer < 0: out of the car (FUN_008c11f0), FUN_00d80340(1,0,0,1), C_OUT; end of C_OUT → C_OUTRO; end of C_OUTRO →
  story step 2 (FREERIDE). Implemented: `StoryStep = 2`, `setNextGameMode(GM_FreeRide)`.

---------------------------------------------------------------------------------------------------------------
## 5. Open points
- Tutorial classes / conditions, the walk-mode `opdo` trigger, FUN_008c1880 seat entry (seating / IK: see
  driver_seat_spec.md), the X360 bitmap glyphs,
  the door rotation helper internals (FUN_0042b050), record +0x14c ↔ _doormaxangle: see tags above.
