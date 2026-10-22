# TDU2 skeletal-animation looping: what the readable code and the data establish

Scope: why intro PNJ clips "pop" at the loop in OpenTDU2, and which loop rule the game uses.
Tags: [CODE] read from code at or above 0x6c1000 (decompiled or disassembled); [DATA] measured on files; [INF] inferred;
[ENC] lives in the encrypted range (< 0x6c1000) and was not touched. No encrypted code was decrypted, emulated or dumped.
Every address below is in the readable range unless it is marked [ENC].

Tools and outputs of this investigation: `/tmp/claude-0/loop/`
- `measure.py`, `m2.py` … `m6.py`: .anm measurements.
- `vis.py`: wrap and cut events in the scenes. `frust.py`: camera frustum test.
- `harness/t.cpp`: a test harness around the unmodified OpenTDU2 `EdenAnimation::sample`.
- `s1.c` … `s13.c`: Ghidra decompiles. They come from a private copy of the Ghidra project (`/tmp/claude-0/loop/gh`, with
  `loop/dec.sh`), because `/tmp/claude-0/bm/gh` was locked by another job.

---------------------------------------------------------------------------------------------------
## 0. Verdict (short)

1. **The edAnim time controller is readable** (0x7cb2f0–0x7cf9xx). Its loop rule is a plain modulo wrap, with no
   crossfade and no root-motion accumulation [CODE]:
   - loop mode: `t' = fmod(t − D, D)` with **D = DATA+0x28 (`dur2`)**;
   - non-loop mode: clamp at **DATA+0x24 (`dur`)** (hold the last frame).
2. **OpenTDU2 already does the same**: `fmod(t, dur)`, last key included, Pelvis translation not accumulated. The
   harness confirms that every closing clip is seamless across the wrap in OpenTDU2 (§6).
   - The only mismatch is `dur` versus `dur2`. They differ only on the 4 "0x6xx-exporter" clips (buoy, sitstand-w,
     DJ turntables), and there the gap is a frozen tail of 0.04–0.3 s.
3. **The pops are in the data.** All stand-dance clips close exactly (last key = first key).
   - The walk clips (`fi_dancewalk*`), the DJ dancers (`fi_dancedjm/djw_npc`), `fi_intro_fiesta_01` and the buoy dancer do
     **not** close: 16–152° and up to 2.07 m between the last and first frame. No time-wrap rule can make them seamless.
   - The scene still asks for them to loop (f5 ≫ clip length), so the original game pops on them too [CODE + DATA].
   - The cinematic hides most of these pops by framing [DATA]:
     - In the generique, the camera stays 300 m+ away over the sea until scene time 34, and every wrap happens before 33.4.
     - In the Default scene, the wraps are mostly out of frustum (§5).
4. **Other visible discontinuities are authored hard cuts**: .sce Animation keys with blend-in f7 = 0 that jump clip time
   (for example Dance_Walk_03_MAN at 15.84 s, 59°, 1.3 m from the camera in c_intro_12). These are not loop pops.
5. **Most data-supported rule**:
   - sample at `fmod(u, dur2)`;
   - hold each channel at its last key;
   - no root motion and no crossfade at the wrap;
   - keep OpenTDU2's two-key crossfade, which the cinematic drives (§3).

   Any smoothing of the non-closing clips would be a cosmetic deviation, not a fidelity fix (§7).

---------------------------------------------------------------------------------------------------
## 1. .anm header as seen by the runtime [CODE + DATA]

The runtime animation pointer (`data`) points to **file + 0x20** (the u32 right after "DATA"+size). Proof:
- FUN_007cd290 returns `*p + 0x18` as the first track, and file+0x38 is the first track.
- The track walker in FUN_007cf640 reads the u16 kind (`& 0xfff8`: 0 = normal, 0x10 = root/mover, 8 = IK), the u16 at
  +2, the bone hash at +8, and advances by the u32 size at +4. That is exactly the file track layout.
- FUN_007cf640 / FUN_007cf970 test `*data != 0`, i.e. numTracks.

| file off | data+ | field | used by readable code | tag |
|---|---|---|---|---|
| 0x20 | +0x00 | numTracks | track loop (FUN_007cf640) | [CODE] |
| 0x24 | +0x04 | `dur` | **non-loop length**: clamp point (FUN_007cdfd0 → D); phase length when the loop bit is clear (FUN_007cba30/7cba00) | [CODE] |
| 0x28 | +0x08 | `dur2` | **loop length**: wrap period (FUN_007cdfa0 → D); phase length when the loop bit is set | [CODE] |
| 0x2C | +0x0c | loopFlag 0 / 0xFFFFFFFF | **not read by any readable function found** (searched 0x7c8000–0x7d4000 and 0xdb0000–0xdf0000) | [UNK]/[ENC] |
| 0x30 | +0x10 | `dur2 − dur` | redundant; the runtime recomputes `data+8 − data+4` and stores it in cursor +0x1c and in state +0x44 | [CODE] |

Measured on all 40 clips of new_intro.bnk [DATA] (`measure.py`, `m2.py`, `m5.py`, `m6.py`):
- **Frame count versus duration**:
  - Classic exporter (fmt 1/2/3/5/9, loopFlag 0, dur2 = dur): 25 fps grid, keyframe-reduced. 100% of key times are on
    the 1/25 s grid for most clips; walk2m and walk3m are resampled off-grid. dur is an integer number of 25 fps frames
    (1.92 = 48 frames, 9.52 = 238 frames).
  - 0x6xx exporter (buoy, sitstand-w, dj_01/02): 20 fps grid. dur = 129 frames (6.45 s), dur2 = 132 frames (6.60 s).
- **Key time encoding**:
  - fmt & 1: u16 times for keys 1..n−1, normalized by the channel's subDuration (= dur2); key 0 is implicit at 0.
  - Otherwise f32 seconds.
  - Every channel starts at t = 0 and ends at exactly `dur` or `dur2`.
  - No multi-key channel has a subDuration of 0.
  - The sampler dispatcher FUN_00dec2a0 [CODE] matches this: it skips (n−1)·2 bytes of u16 times rounded up to 4, or
    n·4 bytes of f32 times. fmt & 0xe0: 0 = linear keys, 0x20 = another evaluator FUN_005a8020 [ENC], 0x80 = another
    evaluator [ENC].
- **Last key equals first key**: true for every stand-dance clip. Table in §4.
- **The [dur, dur2] tail of the 0x6xx clips is a hold, not overlap data**:
  - Only static 2-key channels run to dur2 (9/50 on buoy, 39/86 on sitstand-w).
  - Every animated channel ends at dur.
  - So sampling in [dur, dur2) gives the frozen pose of `dur`.
- **Root / translation tracks**:
  - Human clips have no kind-0x10 (root) track. Travel is in the **Pelvis** (bone hash 0x766cd8b9) local translation.
  - Only `cf_buoy_*` has a root track (kind 0x12).

---------------------------------------------------------------------------------------------------
## 2. edAnim time controller (readable) [CODE]

### 2.1 Cursor ("anim node") object

Initialised by **FUN_007cbc80(anim, arg2, time, bNormalized, flags[, …])**. The node type is the switch value in EAX;
−1 means "read it from res+4". The type selects the vtable pointer:

| type | vtable | role |
|---|---|---|
| 0 | 0xf42e18 (via 0xf8f660) | clip through a resource (`data = *(res+8)`) |
| 5 | 0xf42e28 | clip with data directly |
| 1, 4 | — | weighted blend nodes; the length is the weighted mean of the children's lengths (FUN_007cb530 / 7cb300: phase sync) |
| 2, 3, 6, 10, 0x14 | — | other node kinds |

Cursor fields (dword index):

| field | meaning |
|---|---|
| [1] | event bits: 0x80000000 = ended/wrapped, 0x40000000 = in tail, 0x20000000 = entered tail this tick |
| [2] | flags; **bit 0 = loop** |
| [3] | normalized phase |
| [4] | time |
| [5] | previous time |
| [6] | length |
| [7] | tail length |
| [8] | anim/resource |

The clip-node init (FUN_007cba30 / FUN_007cba00) sets:

```
length [6] = (flags & 1) ? data->dur2 : data->dur
tail   [7] = data->dur2 - data->dur
```

FUN_007cbc80 then sets either `time = start; phase = start/length`, or, for normalized starts,
`phase = start; time = start*length`.

### 2.2 Advance: FUN_007cbba0(dt)

```
phase += dt/length;  prevTime = time;  time += dt;
evaluate();                                   // vfunc +0xc, clip node = FUN_007cbae0
if ((ev & 0x40000000) && prevTime < length - tail)  ev |= 0x20000000;   // just entered the tail
```

### 2.3 Evaluate (clip node): FUN_007cbae0

```
if (flags & 1)  FUN_007cdfa0(data);   // st.mode=1 (loop), st.D = data->dur2, st.L = 0, st.tail = dur2-dur
else            FUN_007cdfd0(data);   // st.mode=0 (clamp), st.D = data->dur,  st.L = 0
FUN_007ce340(time);                   // wrap/clamp, see 2.4
FUN_007cf640(&state);                 // sample all tracks at state.time
time  = state.time;  phase = state.phase;      // the cursor stores the WRAPPED time back
ev |= state.ended ? 0x80000000 : 0;  ev |= state.inTail ? 0x40000000 : 0;
```

### 2.4 Wrap / clamp: FUN_007ce070(t, bNorm, D, L, mode)

Called by FUN_007ce340. Read from the decompile and checked against the x87 disassembly.

```
if (bNorm) t *= D;
if (D <= 0)              { out = {0,0,0,0}; return ended=1; }
if (0 <= t && t < D)     { time=t; phase=t/D; ltime=t-L; lphase=(t-L)/(D-L); return 0; }
if (mode == 0)           { time=D; phase=1; ltime=D-L; lphase=1; return 1; }      // clamp / hold last frame
if (mode == 1) {                                                                   // loop
    r = fmod(t >= 0 ? t - D : t, D - L);     // FUN_004061d0 [ENC], argument shape = fmod  [INF]
    ltime = r; time = r + L; lphase = r/(D-L); phase = (r+L)/D; return 1;
}
```

- L is always 0: both init paths do `fldz; fstp [state+0x48]`. **The loop is the whole clip [0, D)**.
- At exactly t == D the time maps to 0 (`t < D` is strict), the same as `fmod`.
- FUN_007ce340, loop mode only: `ended = inTail = (state.time >= D − tail)`, i.e. time ≥ dur. In non-loop mode,
  ended = the return value of FUN_007ce070 and inTail = 0.
- The function returns only the wrapped time: no loop counter and no carried delta. Nothing in readable code reads
  `ltime`/`lphase` (0x144b668/0x144b66c have no readers in 0x6c1000–0xe40000).
- **So there is no root-motion accumulation across loops** [CODE].

### 2.5 Layer state machine (per avatar anim layer)

| function | what it does |
|---|---|
| FUN_007cd340 | reset: speed +0xd4 = 1, state +0x10 = 0, pending +0x70 = −1 |
| FUN_007cdeb0(dt) | `dt *= speed`; state 1 → FUN_007cd5e0; state 2 → FUN_007cd930 |
| FUN_007cd5a0(desc) | play request: state 0 → start now. **State 1 with the same anim id → keep playing (no restart, no self-crossfade)**. Otherwise FUN_007cd4c0 (queue a transition) |
| FUN_007cd4c0 | looks up the transition parameters for the new anim id in the table at +0x2c (16-byte entries `{id, mode/blend +0xc4, newStart +200, dur +0xcc}`; id −2 = default) |
| FUN_007cd400 | starts the transition: second cursor at +0x88, state 2 |
| FUN_007cd930 | transition update: elapsed +0xc0 += dt; weight = elapsed / dur; FUN_007cf2b0 blends the two poses; at the end FUN_007cd3a0 promotes the new cursor |

- The +0xc4 sentinels in FUN_007cd930: a value > 0 is a crossfade time; −1 = both clocks run; −3 = synced rate;
  −4 = "transition when the current clip ends" (handled in FUN_007cd5e0 on event 0x80000000); −5, −6 = special.
- Layer events +0xd0: |2 = ended/wrapped, |4 = in tail, |8 = entered tail. Read by FUN_00d96e30, FUN_00d96e60 and
  FUN_00d97180 ("is finished?") for game logic only. **No readable consumer crossfades on these events**.

---------------------------------------------------------------------------------------------------
## 3. How the cinematic drives PNJ animations [CODE + ENC]

- **FUN_00b8b140** (avatar update while a cinematic owns it, `+0x1bc2`) reads the Entity__Cinematic at `slot+0x2c`.
  - With B = **AvatarsBlendDuration** (+0x5f8) and t = cinematic time (+0x600), it calls FUN_00dc19a0(…, w):
    - w = `t/B` while `B > 0 && t < B` (and +0x1e41 set);
    - otherwise w = 1.0.
  - So AvatarsBlendDuration is a **blend-in from the pre-cinematic pose at the start of the cinematic**. It is not a
    loop crossfade.
  - It is **0 for all 26 cinematics, C_INTRO included** (`/tmp/claude-0/intro/cinematics.txt`), so w = 1 for the intro
    [DATA].
- **FUN_00dc19a0** gets up to two clips from **additionalsAnims** (entity +0xb38/+0xb3c, the m/w style arrays; count
  +0xb48; resolved to resources at +0xb40 by FUN_00b3e940, read by FUN_00b3e6a0).
  - The indices come from the per-avatar cinematic slot: +0x74/+0x78 = clip A (style, index), +0x84/+0x88 = clip B.
  - The clip times are +0x7c (A) and +0x8c (B). The weight is +0x80.
  - **One clip**: FUN_007cbc80(A, …, time = slot+0x7c, bNorm = 0, flags), layer state 1.
  - **Two clips**: cursors A (time +0x7c) and B (time +0x8c), state 2, mode +0xc4 = −2.0 (0xf49d98), newStart = 0,
    dur = 1.0, elapsed = slot+0x80.
    - Mode −2 freezes both clocks: FUN_007cbba0(0).
    - So the blend weight is exactly the cinematic-supplied +0x80.
  - The descriptor (including the cursor flags, so the loop bit) is filled by FUN_0048b190 [ENC].
- **So the cinematic code (CinScene.cpp, [ENC]) computes clip time A, clip time B and the weight every frame from
  the .sce Animation keys.** edAnim only wraps or clamps and samples.
  - This is exactly OpenTDU2's model (`u = f2 − f7 + (t − T_k)`, linear crossfade over f7).
  - The exact field semantics remain [ENC].
- **Loop mode for cinematic clips** [INF, strong]: the .sce asks for 17–98 s of 1.7–20 s clips (f5 ≫ dur; key2 of
  Dance_Walk_03_MAN even covers clip time 1.82 → 10.16 on a 6.68 s clip).
  - In non-loop mode edAnim would freeze them at `dur`. So either the descriptor has flags bit 0 set (wrap on dur2),
    or CinScene wraps the time itself before passing it.
  - For every human clip dur2 = dur, so both give the same result.

---------------------------------------------------------------------------------------------------
## 4. Measurements on the intro clips [DATA]

40 ms is one 25 fps frame. "Jump" is the largest per-bone difference between pose(dur) and pose(0). Script: `m2.py`,
`m3.py`; cross-checked with the C++ harness (§6).

| clip | dur | last = first? | jump at wrap | normal 1-frame step | Pelvis Δ per cycle |
|---|---|---|---|---|---|
| fi_dancestand1..5-m/-w, fi_dancew1..4, fi_dancem1/2/4, fi_dancesitstand-m, transatstand4w, co_stand-m/-w | 1.72–9.32 | **yes** (dot ≥ 0.99999, Δpos ≤ 0.6 mm) | ≤ 0.1° | 2–15° | 0 |
| fi_dancesitstand-w (0x6xx) | 4.56 / 4.60 | ≈ (1.2°, 1.4 mm) | 1.2° | 1.8° | 0 |
| fi_dancedj_dj (the DJ himself) | 10.2 | ≈ | 3.4° | 5° | 1 mm |
| fi_intro_fiesta_01_npc | 10.68 | no | 12° | – | (−0.07, −0.01, −0.05) |
| fi_dancebuoyw_npc (0x6xx) | 6.45 / 6.60 | no | 16° | 3° | 4 mm |
| fi_dancedjw_npc | 12.08 | no | 51° | 4° | (0.03, −0.01, 0.09) |
| fi_dancedjm_npc | 12.08 | no | 85° | 1.6° | (−0.04, −0.03, 0.05) |
| fi_dancewalk1w_npc | 9.52 | no | 55° | 11° | **(+2.05, −0.04, −0.28) m** |
| fi_dancewalk1m_npc | 9.52 | no | 93° | 6° | (−0.28, −0.04, −0.06) |
| fi_dancewalk2w_npc | 10.96 | no | 49° | 16° | (−0.23, 0.04, −0.54) |
| fi_dancewalk2m_npc | 10.96 | no | **152°** (pelvis yaw) | 10° | (0.41, 0.04, 0.04) |
| fi_dancewalk3w_npc | 20.24 | no | 118° | 15° | (−0.20, 0.03, 0.78) |
| fi_dancewalk3m_npc | 6.68 | no | 88° | 24° | (0.58, −0.05, 0.22) |
| dj_01 / dj_02 turntables (0x6xx) | 10.1 / 10.2 | no | 36° / 64° | 11–12° | – |

- In 20–52 channels per walk or DJ clip the end pose differs by more than 5° or 5 cm. These are whole-body pose
  differences, not only travel.
- The pelvis yaw also changes (35–152°). **Root-motion accumulation cannot make them loop**: the dancer would spin and
  drift by metres per cycle. Pure wrap (teleport back) is the only rule consistent with the code.
- With a wrap on `dur2` instead of `dur`, the 0x6xx clips freeze for 0.04 s (sitstand-w, about 1 frame), 0.15 s (buoy)
  or 0.1 s (turntables) before jumping. The jump size does not change.

---------------------------------------------------------------------------------------------------
## 5. Where the pops are visible (C_INTRO) [DATA]

- Script `vis.py`: wraps and f7 = 0 key switches per actor, with the u-mapping of avatar_spec §4.1.
- Script `frust.py`: camera frustum test on M_INTRO_Default.sce.
  - The camera convention was calibrated on c_intro_13 → c_intro_13_Target: forward = rotate(+Z) by conj(q),
    dot = 1.000.
  - The aspect assumed for the horizontal FOV is 16:9; the test ignores occlusion.

**Generique** (shot 0, scene time 6.28 → 35.36):
- 36 wraps of non-closing clips at t = 6.45 … 33.40.
- The camera is over the sea, 300 m to 1.7 km from the house, until t ≈ 34. It only enters the house at 34 → 35.36.
- **None of the generique wraps is on screen at a visible size.**

**Default scene** (shots 29.08 → 52.62 s). The camera is in the house. Events whose actor is inside the frustum:

| scene t | actor | event | jump | shot (camera time range) | dist |
|---|---|---|---|---|---|
| 6.45 | Pool_WOMAN_02 | wrap of fi_dancebuoyw | 16° | c_intro_05 (5.35–6.96), c_intro_10 | 2.8 m / 5.5 m |
| 12.08 | Dance_Dj_WOMAN_02 | wrap of fi_dancedjw | 51° | c_intro_11 (11.88–13.14) | 9.1 m |
| 6.09 | Dance_Walk_03_MAN | authored cut (clip 6.09 → 0) | 102° | c_intro_10 starts exactly at 6.09 (hidden by the cut) | – |
| 7.50 | Dance_Walk_03_MAN | authored cut (1.41 → 1.82) | 112° | end of c_intro_10; c_intro_06 shows it 0.04 s before its end | 7.5 m |
| 15.84 | Dance_Walk_03_MAN | authored cut (3.48 → 2.11) | 59° | c_intro_12 (15.04–16.76) | 1.3 m |
| 16.76 | Dance_Sit_MAN | authored cut | 44° | boundary of c_intro_12 / 13 | – |
| 6.44 | Dance_Dj_WOMAN_01 | authored cut | 36° | c_intro_10 | 15 m |
| 5.06 | Pool_WOMAN_01 | authored cut | 100° | c_intro_11bis | 6.5 m |
| 12.11 | CF_Platine_01/02 | authored cut (spinning disc) | – | c_intro_11 | 11 m |

- The other wraps are out of frustum: walk1w at 7.52, walk1m at 9.52, walk3m at 12.36, buoy at 8.83/15.28/12.9/19.35,
  the turntables at 19.38, and the authored cuts at 13.14, 16.73, 16.81, 17.18 and 17.32.
- The many hard cuts at camera-cut times (6.09, 7.50, 13.14, 16.76) show that the animators placed clip switches on
  cuts. They tolerated pure-wrap pops elsewhere.

---------------------------------------------------------------------------------------------------
## 6. OpenTDU2 versus the game

Code: `source/render/eden/eden_animation.cpp` and `source/game/avatarchoice/intro_cinematic.cpp:426`.

| aspect | game | OpenTDU2 | match |
|---|---|---|---|
| wrap | `fmod(t − D, D)`, t = D → 0 [CODE] | `fmod(t, duration)` | yes |
| period | `dur2` (DATA+0x28) in loop mode [CODE]; `dur` clamp in non-loop | `dur` (DATA+0x24) | equal for every human clip; differs on 4 × 0x6xx clips |
| last key | included; t ∈ [lastKey, D) holds (the time is never outside [0, D]) [CODE]; the per-key interpolator is [ENC] | `time >= times.back()` → last value | yes |
| channels shorter than D | hold the last value [INF] | hold | yes |
| root motion | no kind-0x10 track in human clips; Pelvis is an ordinary bone; nothing accumulated per loop [CODE + DATA] | Pelvis replaced, not accumulated | yes |
| crossfade at loop | none in edAnim; a same-id replay does not restart [CODE] | none | yes |
| key-to-key crossfade | the weight comes from the cinematic (slot +0x80), times +0x7c/+0x8c [CODE]; formula [ENC] | linear over f7, both clocks running | plausible ([INF], avatar_spec §4.1) |
| AvatarsBlendDuration | blend-in at cinematic start; 0 for C_INTRO [CODE + DATA] | not used | OK (no-op) |

Harness: `/tmp/claude-0/loop/harness/t` links the **unmodified** `eden_animation.cpp` and samples ±2 ms around 3
successive wraps.
- Closed clips: ≤ 1.4° and ≤ 1.5 mm, which is the same float-acos floor as a static clip (co_stand-w 1.06°/1.06°). So
  **there is no sampler-induced pop**.
- Non-closing clips: the same jumps as §4, from 16° (buoy) to 152° (walk2m), and 2.07 m (walk1w).

So if a stand-dance PNJ appears to pop in OpenTDU2, the cause is **not** the loop. Check the other candidates:
- an authored f7 = 0 key switch (§5);
- the scene-time jump at a shot cut (each Default shot replays an overlapping scene-time range);
- the actor being one of the non-closing clips listed in §4.

---------------------------------------------------------------------------------------------------
## 7. Fix plan (no OpenTDU2 sources were modified)

1. **Fidelity (small, do it)**: parse `dur2` (DATA+0x28) in `EdenAnimation::parse` and wrap looping clips on it.
   - `sample(..., bLoop=true)`: `time = fmod(time, dur2)`, with negative times folded into [0, dur2).
   - Keep `dur` for non-loop and clamp use (`bLoop=false` → `Min(time, dur)`).
   - Effect: only fi_dancebuoyw_npc, cf_buoy_*, fi_dancesitstand-w and fi_dancedj_dj_01/02 change. They gain the
     game's 0.04–0.3 s hold before the wrap.
   - Alternative, if one prefers to stay neutral on the [ENC] CinScene part: keep `dur`. The difference is invisible
     except on the buoy close-up at 6.45 (c_intro_05).
2. **Do not** add root-motion accumulation or "last frame excluded" logic.
   - Root-motion accumulation contradicts the code (no loop delta) and the data (yaw changes, metres of drift).
   - Excluding the last frame is wrong too: closed clips already have last = first and fmod already skips the duplicate.
3. **Do not** add a self-crossfade at the wrap if the goal is fidelity. The original pops identically and hides it by
   framing (§5).
   - If a cosmetic option is wanted anyway (off by default), crossfade over the last `w = 0.25 s` of each cycle. Blend
     `pose(dur − w + x)` → `pose(x − w)` clamped at 0, or simply pose(fmod) with pose(0) weight x/w.
   - Apply it only when the closure error is above about 5°/5 cm (precompute at parse time: max |q_last·q_first|,
     max |p_last − p_first|).
   - Expect the walk clips to still visibly slide 0.3–2 m in 0.25 s.
4. **Make sure the intro uses the right scene time per shot** (`intro_cinematic.cpp:433`):
   `sceneTime = CameraStart + (time − Start)`. Keep it: the pose discontinuities at shot cuts are expected.
5. **Optional check of the u-mapping** (not loop-related, [ENC]): f1 is the in-point. It differs from f2 − f7 only on
   the first keys whose f2 > 0 (WOMAN_05 f2 = 0.2, Dance_04_MAN_01 0.6, Dance_Walk_01_WOMAN 2.0). Both readings are
   continuous, so this cannot cause pops.
6. **Optional diagnostic**: log per PNJ which clip and key is active and flag `closes = false` clips (§4 table). Then a
   pop report can be classified as data wrap, authored cut or shot cut.

---------------------------------------------------------------------------------------------------
## 8. Still unknown [ENC]

- CinScene.cpp: the exact mapping of the .sce key fields f1..f9 to (time A, time B, weight).
- Whether CinScene sets descriptor flags bit 0 (edAnim wrap on dur2) or wraps the time itself (unknown period).
- The descriptor builder FUN_0048b190, and whether .anm loopFlag (+0x2C) feeds the flags.
- The per-key interpolators FUN_005a7d00 / 5a8020 / 5a8520 / 5a8660 / 5a8bb0 (slerp/lerp and the fmt & 0x20 / 0x80
  variants), and FUN_004061d0 (assumed fmod from its call shape).
