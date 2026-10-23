# TDU2 facial animation: FaceFX actors, animations, cinematic Morph track, Eden bone glue

Tags: [CODE] read from readable x86 (VA >= 0x6c1000) · [DATA] read from game files · [INF] inferred (reason given) ·
[ENC] the logic is in SecuROM-encrypted code (< 0x6c1000) or not present in readable code/data; not read.

Decompiled code (Ghidra, private copy `/tmp/claude-0/ffx/gh`): `/tmp/claude-0/ffx/fx_all.c` (every function of
0x6e3000–0x70a000 = the FaceFX SDK), `/tmp/claude-0/ffx/glue.c` (0xb73000–0xb7c000 = Eden FaceFX animator),
`d1.c`…`d4.c` (helpers). Python reference parsers / evaluator: `/tmp/claude-0/ffx/py/fxa.py` (archive),
`eval.py` (graph + bones), `scemorph.py` (.sce Morph tracks).

OpenTDU2: `source/game/avatar/facefx.{h,cpp}` (parser + evaluation), `IntroCinematic::applyFaceFx`
(`source/game/avatarchoice/intro_cinematic.cpp`), `EdenAvatar::getFaceParams / setFacialCoefficients`.

---------------------------------------------------------------------------------------------------
## 1. What the game uses

- The runtime is the **OC3 Entertainment FaceFX SDK 1.7** statically linked in the readable `.text`
  (0x6e3000–0x70a000) [CODE]: RTTI `.?AVFxActor@Face@OC3Ent@@`, `FxActorInstance`, `FxAnim`, `FxAnimCurve`,
  `FxAnimGroup`, `FxAnimSet`, `FxMasterBoneList`, `FxCompiledFaceGraph`, 11 link functions, 8 node types, archive
  stores (vtables listed in `/tmp/claude-0/ffx/vtables.txt`; the 4th vfunc of every FxObject is `Serialize`).
- Licensee strings "Eden Games" / "evaluation use only", archive version 1720 [DATA].
- **No morph targets drive the mouth**: speech is purely bone based (42 facial bones of the avatar skeleton) plus
  two TD_Skinface constants that weight the expression normal map [CODE+DATA].
- No phoneme-level lip-sync is computed at runtime: each voice line has a pre-baked FaceFX animation (curves on
  phoneme targets `open W ShCh PBM FV wide tBack tRoof tTeeth` and on action units) [DATA].

### 1.1 Files [DATA]

| file | bank | content |
|---|---|---|
| `avatar/fxa/female.fxa` (164810 B), `male.fxa` (164304 B) | `Euro/Bnk/avatar/avatar_face_fx.bnk` (group 0, sub 34) | FxActor `Female_Avatar` / `Male_Avatar`: 42 master bones, 445 / 448 compiled nodes, phoneme map |
| `entities.xmb` of that bank | idem | `Entity__FxasRef` (`FASX`): `FaceFxActorMan`, `FaceFxActorWoman` rsc (read by FUN_00cb76f0 → +0xcc30 / +0xcc34) |
| `*.fxe` (group 0, sub 35) | cinematic banks | FxAnimSet. new_intro.bnk: `thedream-w.fxe`, `thedream-m.fxe`, `intro-w.fxe`; garage.bnk: `intro-w.fxe`, `common-m/w.fxe`; meeting_tess: `intronew-e-m/w.fxe`; fade_freeride / pack_firstfreeride: `islands-welcome-m.fxe`, `realtor-e-m/w.fxe` … |

`intro-w.fxe` (actor `Female_Avatar`, group `Intro_`), 12 anims [DATA]:

| anim | keys | key time range (s) | used by |
|---|---|---|---|
| Intro_FiestaGarage_01_NPC_debut | 290 | 0 – 8.93 | garage C_INTRO Tess (before voice 01) |
| Intro_Fiesta_Tess_debut | 84 | −0.001 – 0.703 | C_TESS sce 0.80 |
| Intro_Tess_stand | 110 | 0 – 4.0 | C_WALKMODE Tess (idle) |
| Intro_FiestaGarage_01 … _06 | 152 … 261 | −0.30 – 5.4 | garage voices 01–06 |
| Intro_Fiesta_01 | 202 | 0 – 3.37 | C_TESS sce 1.50 (voice intro_fiesta_01) |
| Intro_Fiesta_02 | 298 | 0 – 5.30 | C_TESS sce 4.87 (voice intro_fiesta_02) |
| Intro_Fiesta_Tess_call_avatar | 117 | 0 – 3.0 | C_FIESTA_INTRO_xx sce 3.45 |

Blend in / out stored 0.16 / 0.22 s in every anim (FaceFX defaults); the Eden tick does not use them [CODE: no read
of FxAnim +0x20/+0x24 in FUN_006ece00 / FUN_00b77b20].

---------------------------------------------------------------------------------------------------
## 2. Archive format (bit exact, both file kinds parse to the last byte) [CODE+DATA]

Little endian. `FxString` = u32 length + bytes (licensee strings keep their NUL). `FxName` = u32 index in the name
table (FUN_006e4e20 with FxName class version 1). Class versions come from the class table (FUN_006fa1a0 →
FUN_006fae90) because the archive version is ≥ 0x6a4.

```
Header (FUN_006f9630):  "FACE" u32 version(=1720 0x6b8) u32 0 (must be 0 when version>=0x6b8)
                        FxString licensee "Eden Games\0", FxString project "evaluation use only\0", u32 1000
Directory (FUN_006f9490, version > 0x63f):
  u16 1 (class table present)
  class table (FUN_006fbe60): u32 nBuckets { u32 bucket, u32 n { FxString class, u16 version } }
  name table  (FUN_006fbc90): u32 n { FxString }
  u32 0
Object data
```

Class versions [DATA]: fxa: FxBone 0, FxCompiledFaceGraph 0, FxObject 0, **FxActor 3**, FxFaceGraphNodeUserProperty 0,
**FxMasterBoneList 2**, FxNamedObject 0, FxName 1, FxPhonemeMap 1, FxAnimGroup 0, FxLinkFnParameters 1.
fxe: FxObject 0, **FxAnim 6**, FxAnimSet 0, FxNamedObject 0, FxName 1, **FxAnimCurve 1**, FxAnimGroup 0.

### 2.1 FxActor (FUN_006f1950, v3)

```
FxNamedObject : FxName name                              ("Female_Avatar")
FxMasterBoneList (FUN_006f38d0 v2 -> FUN_006f4900): u32 n, n x entry (0x58 bytes in memory):
   FxBone ref (FUN_006feed0): FxName, f32 pos[3], f32 rot[4] (w x y z), f32 scale[3]     (+0x04..+0x30)
   f32 invRot[4]  (+0x30; = conjugate of ref.rot)                                         (FUN_007093d0)
   u32 clientIndex (+0x40; file values 36..77, see §4.2)
   f32 weight (+0x44; 1.0)
   u32 n, n x { u32 node (compiled graph index), FxBone pose }   (0x34 bytes, FUN_006f2ed0)
FxCompiledFaceGraph (FUN_006e9860): 
   u32 n, n x node (FUN_006e87a0, 0x48 bytes in memory):
      u32 type, FxName name, f32 min, f32 1/min, f32 max, f32 1/max, u32 inputOperation,
      u32 n x input { u32 inputNode, u32 linkFunction, FxLinkFnParameters = u32 n, f32[n] }   (FUN_006e8320)
      u32 n x FxFaceGraphNodeUserProperty { FxName, u32, u32, f32, u32 n x FxString }      (FUN_006ec130)
   name map (FUN_006eb3d0): u32 nBuckets { u32 bucket, u32 n { FxString name, u32 node } }
FxAnimGroup "Default" (+0x6058): FxName, u32 nAnims (0)
FxAnimGroup[] (+0x606c): u32 n (0)
FxPhonemeMap (+0x6088, v1, FUN_007063b0): u32 n { u32 phoneme, FxName target, f32 amount } (128 entries),
   u32 n x FxName targets (open W ShCh PBM FV wide tBack tRoof tTeeth)
```

Memory layout of the compiled node (used by the evaluation): +0x00 type, +0x04 name, +0x08 min, +0x0c 1/min,
+0x10 max, +0x14 1/max, +0x18 track value (curve, reset after use), +0x1c register value, +0x20 **final value**,
+0x24 input operation, +0x28 register operation, +0x30 inputs, +0x3c user properties [CODE FUN_006e8a70].

Node types (table 0xe52ad4 {name, factory, id}) [CODE]: 0 FxCombinerNode, 1 FxDeltaNode, 2 FxCurrentTimeNode,
3 FxGenericTargetNode, 4 FxBonePoseNode, 5 FxMorphTargetNode, 6 FUnrealFaceFXMaterialParameterNode,
7 FUnrealFaceFXMorphNode. female.fxa: 241 combiners, 202 bone poses, 2 morph targets (`Ethnic_ASIAN`,
`Ethnic_AFRIC`, user property "Morph Target Name"; nothing in the readable glue consumes them) [DATA].
The graph is topologically sorted (every input index < node index) [DATA, both actors].

### 2.2 FxAnimSet (.fxe; FUN_006f5430, FxAnimGroup FUN_006eefa0, FxAnim v6 FUN_006ee680)

```
FxName setName, FxName actorName ("Female_Avatar"), FxName groupName ("Intro_"), u32 nAnims, nAnims x FxAnim:
   FxName name
   u32 nCurves x { FxName curveName (= face graph node name), u32 interpolation (FxAnimCurve +0x1c: 0 hermite,
                   1 linear) }                                  (FxAnimCurve v1: no keys inside)
   u32 nKeys, nKeys x { f32 time, f32 value, f32 slopeIn, f32 slopeOut }   (key = 0x14 bytes + user data 0)
   u32 nCurves x u32 keyCount                                   (keys distributed in curve order)
   f32 blendInTime (+0x20), f32 blendOutTime (+0x24)
   u32 n x FxAnimBoneWeight { FxName, f32 }   (0 here)
   FxString audioPath, FxString ? (empty), u32 (+0x4c, 0xffffffff)
```
After load FUN_006eeb90 sets **start (+0x18) = min first-key time, end (+0x1c) = max last-key time** of the curves
((0, 1) when there is no key).

---------------------------------------------------------------------------------------------------
## 3. Evaluation [CODE]

### 3.1 Curves (FxAnimCurve::EvaluateAt FUN_007033c0)
- no key → 0; `!(t > k0.time)` → k0.value; `!(t < kLast.time)` → kLast.value; one key → its value.
- Bracket search FUN_00703a00 (starting at the cached index when `key[cache].time <= t`): first i with
  `k[i].t <= t < k[i+1].t`; i == last → (last−1, last).
- Hermite FUN_007038e0 (x87, double constants 3.0 @0xf49a50, −3.0 @0xf49d58, 2.0 @0xf497f8):
  `dt = t1−t0, u = (t−t0)/dt, so = k0.slopeOut·dt, si = k1.slopeIn·dt,
  v = (((2v0 − 2v1 + so + si)·u + (3v1 − 3v0 − 2so − si))·u + so)·u + v0`.
- Linear FUN_007039a0: `(v1−v0)·u + v0`.

### 3.2 Actor instance tick (FUN_006ece00) with the Eden call (FUN_00b77b20)
- Eden passes appTime = DAT_00fdb8cc and **audioTime = animator time** (+0x1ea8, starts at the PlayAnim time
  argument, += frame dt each tick; −1 once ≥ anim end).
- PlayAnim (FUN_006eccf0(anim, group, 1)) sets +0x31 = 1 → start offset 0 → **anim time = audioTime**; negative key
  times simply lie before 0.
- Every curve whose name is a node (`FUN_006e9780` name map) writes `curve(t)` into node +0x18.
- Then FUN_006e8a70 (graph); the anim stops (FUN_006ecd60, state +0x34 = 2) when `end <= t`.

### 3.3 Compiled face graph (FUN_006e8a70), per node in index order
```
acc = inputs ? {0, 1, -FLT_MAX, FLT_MAX}[inputOperation] : 0          (table 0xe52b84)
corr = 0
for input (node j, fn, params p):
   x = value[j]
   if fn == 9 (corrective):  corr = min(1, max(x*node_j.(1/max), -x*node_j.(1/min)) * p0 + corr); continue
   y = LinkFn(fn, x, p)                                                  (FUN_006e9b80)
   op 0: acc += y; 1: acc *= y; 2: acc = (NaN||acc>y) ? acc : y (FUN_006eacc0); 3: acc = acc<y ? acc : y
acc += track (+0x18, then reset)
register op (+0x28): 0 add, 1 multiply, 2 replace (+0x1c), 3 none (initial value, FUN_006e8460)
acc = (1 - corr) * acc
value = max(min(acc, max), min)                                         (FUN_006ea320)
```
Link functions (FUN_006e9b80 switch): 0 → 0; 1 linear `p1 + x·p0` (2 params) / `x·p0` / x; 2 quadratic
`(p0)·x²·sign`; 3 cubic `(p0)·x³`; 4 sqrt `(p0)·sqrt(|x|)·sign` (fabs/sqrt FUN_0040eb80/FUN_00481570 [ENC]);
5 negate −x; 6 inverse (x≈0 → 0, else 1/x; FxRealEquality FUN_00481530 [ENC], epsilon [INF]); 7 one-clamp
(x ≥ 1 → 1/x else 1); 8 constant (p0 or 1); 9 corrective (above); 10 clamped linear: 4 params
`((p3>0 && p1<x) || (p3<=0 && x<p1)) ? p2 + (x−p1)·p0 : p2`, 1 param `x<=0 ? 0 : x·p0`.
Used by the TDU2 actors: 1, 5, 6, 8, 9, 10 [DATA].
Node types 1 (delta: input0 link value minus previous) and 2 (current time) have their own branches; neither is
present in the TDU2 actors [DATA].

### 3.4 Registers set by Eden (FUN_00b76550, every tick before Tick)
For i = 0..48: node `kFaceFxCustomNodes[i]` (FUN_00b770b0 order, FxName members +0x1ee4…+0x1fa4:
`CustPos Jaw thin, CustPos Nose pushed, C_Nose_Tall_or_Small, C_Brow_push, … C_LowerEyeLid_Up`) gets
`FUN_006ed2d0(node, 2, (char)avatar[+0x2690 + 0xa2 + i] / 100.0, 0, -1, 0, 0)` = **replace**, no
interpolation (1/0 → 0 → the value applies at once). `avatar+0x2690` = the AvatarConfiguration (`.avt`); +0xa2 =
**FaceParamArray** (key 9d93). FUN_00cbd030 copies the array with `memcpy(+0xa2, value, 0x31)` [CODE]; in the file
it is 49 child nodes `{u16 3, u16 key, u8 value}` [DATA], element i = i-th child [INF].
Example STO_Tess: 15, 0, 6, 8, 15, 10, 7, 8, 8, 5, 8, 8, 5, 6, 15, 8, 8, 0, 10, 8, 8, 8, 7, 7, 6, 10, −31, −35, 8, 8,
15, 1, 10, 7, 9, 7, 8, 0, 4, 9, 6, 0 × 8 [DATA].
=> the face **shape** customisation is part of the FaceFX pose; it is visible only while a FaceFX anim plays (§4.3).

Look-at: FUN_00b75f50 / FUN_00b76070 set registers `LookAt_T2_side` / `LookAt_T2_vertical` (clamped to the node
range) from the look-at system: §6.

### 3.5 Bones (inlined in FUN_00b77b20)
For each master bone (0x58 stride), skeleton bone `boneMap[i]` (≠ −1):
```
pos = ref.pos; scale = ref.scale; r = ref.rot (copy); acc = ref.rot            (FaceFX (w,x,y,z), cm)
for pose (node n, P) in file order:
   w = node[n].value (+0x20); skip unless w >= 1.192e-7 (DAT_00f49b08) or w <= -1.192e-7 (DAT_00f49b60)
   pos += P.pos * w
   if |r + P.rot|² < |r − P.rot|²: r = −r       (persistent for the next poses)
   d = r·P.rot
   if d <= 0.55 (float @0xf90738, read only): θ = acos(d), s = 1/sin θ, S = sin((1−w)θ)s·r + sin(wθ)s·P.rot
                                             (acos/sin FUN_00429330/FUN_00541260 [ENC], INF: CRT)
   else S = w·P.rot + (1−w)·r                  (no normalisation)
   acc = acc ⊗ (invRot ⊗ S)                     (Hamilton, w first)
   scale += P.scale * w
if | |acc|² − 1 | >= 1.192e-7: acc /= sqrt(|acc|²) (FUN_00448620 [ENC]), identity when the length < 1.192e-7
Eden local transform: rot = (−acc.x, acc.y, acc.z, acc.w), pos = (−pos.x, pos.y, pos.z) / 100.0 (double @0xf49a00),
scale as is. Written into the skeleton pose buffer (skeleton->[9] + bone·0x30: quat, pos, scale) and into the
animator buffer, weight 1.0.
```
Check [DATA]: the converted reference pose of the 42 bones equals the bind locals of `woman_body.xsb` (positions to
1e-5 m, rotations equal up to sign; e.g. Head_Facial (0.7019, 0.7019, 0.0857, −0.0857)), so pose positions / scales
are **deltas** and rotations absolute.

Example (python reference, Intro_Fiesta_01): L_LowerLip at t = 0.3 s rot (−0.004, 0.003, 0.031, −0.999), pos
(0.0108, −0.0135, 0.0493) vs bind (0.0107, −0.0131, 0.0509): lips move by 1–3 mm and rotate by up to ~7°.

### 3.6 Normal map weights
Node indices of `Anim_Nmap_Nose, _Mouth_R, _Mouth_L, _Brow, _Eye_R, _Eye_L` (PTR 0xf7da3c, FUN_00b76180,
FUN_00b78a20 → animator +0x1ec4…) → FUN_00caf700(material set, (n0, n1, n2, 0), (n3, n4, n5, 0)): every material of
GUID d5013ed0dbcddbca (TD_Skinface) gets parameter 0x4e72928698868e8f = `facialAnimCoef1` and
0x4e73928698868e8f = `facialAnimCoef2` (= GetIdentifier64bit of those names) [CODE]. Zeros are written in the
fade branch after the anim stopped.

---------------------------------------------------------------------------------------------------
## 4. Eden glue: the FaceFX animator (object at avatar+…, members +0x1e90…+0x1fa4) [CODE]

### 4.1 Creation (FUN_00b78a20, constructor FUN_00b770b0)
- Actor `DAT_011badb0[FUN_00cabec0()]` (male / female actor by the avatar description); `new FxActorInstance`
  (FUN_006eca80), bind (FUN_006ecbc0).
- `boneMap[i] = FUN_007cfd10(masterBone[i].+0x40)`: binary search of a 4-byte value in the skeleton's sorted
  `hash4` table (XSB +0x20: u32 n, parents, sorted hashes, sorted indices) → bone index.
- Pose buffers of 0x96 bones × 0x30, weights; skipped for skeletons of ≥ 150 bones.

### 4.2 Bone map key [INF]
The file stores +0x40 = 36…77 (an exporter index), but FUN_007cfd10 looks up a **name hash**. All 42 FaceFX bone
names hash (EdenNameHash4) to bones of `woman_body.xsb` / `man_body.xsb` [DATA], so the loader must rewrite +0x40
with the name hash; the function that does it was not found in the readable code (FUN_006f3680 SetClientIndex
has no direct caller) [INF]. OpenTDU2 maps by name hash.

### 4.3 Per-frame update (FUN_00b78970 → FUN_00b77530, FUN_00b77b20)
- dt = DAT_00fdb8e4 (0 when DAT_00fdb8d8 <= 0).
- Ticks when (dt > 0 and time ≠ −666) or "running" flag (+0x1ec0… byte 0x7b0); time = +0x1ea8; if
  `anim.end <= time` the audio time passed is −1 (last tick, then stop).
- FUN_00b76550 (registers), FUN_006ece00 (tick), vfunc +0x10, bones (§3.5) only if the skeleton is < 150 bones,
  FUN_00caf700 (coefficients), vfunc +0x14, `time += dt`.
- When stopped (state 2): time = −666, fade +0x1ebc = 0.2 (DAT_00ede428); the fade timer decreases by dt; while it
  is non zero FUN_00480d10 [ENC] blends the bone outputs; the animator is registered on the skeleton
  (`*(skeleton+0x1c) = animator`) only while ticking or fading → **without a FaceFX anim the face is the body
  animation pose** (bind for the facial bones).

### 4.4 Play (FUN_00b77a50 → FUN_00b77600)
- Name split: anim = after the last ':', group = between the last two ':' (path prefix ignored).
- Group: actor default group (+0x6058), groups (+0x606c, 0x14 each), or the mounted list (+0x6078: the .fxe
  anim sets); anim by name (0x54 stride).
- If a different anim (name, curves, times compared) → `FUN_006eccf0(anim, group, 1)`, fade = 0.2 when the 5th
  argument is set, time (+0x1ea8) = argument.

### 4.5 Cinematic link
- `.sce` actor track **Morph** (id 0x70726fb5, kind 0, flags 1): keys of 5 dwords
  `{u32 REFS index, u64 (hash; e.g. f3d9c0b0 13f5b44b), f32 clip length, u32 0}`; REFS type 0x0306 =
  `d:\…\avatar\fxa\female.fxa::Intro_:Intro_Fiesta_01` [DATA].
- The CinScene track parser is [ENC] (intro_spec §2). In readable code the actor controller vtable 0xeabc8c
  (installed on the animator +0x1e94) slot 6 FUN_00d96ab0(name, time) calls `FUN_00b77a50(name, time, 1)`
  [CODE] → the cinematic starts the anim with fade.
- C_TESS (M_TESS_Tess.sce): Tess keys 0.80 Intro_Fiesta_Tess_debut (0.70), **1.50 Intro_Fiesta_01 (3.37)**,
  **4.87 Intro_Fiesta_02 (5.30)** — same times as the voice Fx keys (Intro_Fiesta_01 at 1.50, _02 at 4.87).
  C_WALKMODE: Tess Intro_Tess_stand (key length 241.33, anim 4 s), the party PNJ TheDream_* (thedream-w/m.fxe).
  C_FIESTA_INTRO_xx: Tess Intro_Fiesta_Tess_call_avatar at 3.45. Garage C_INTRO: FiestaGarage_01_NPC_debut / 01 / 02;
  C_VOICE_03…06: Intro_FiestaGarage_03…06 [DATA].

---------------------------------------------------------------------------------------------------
## 5. OpenTDU2 implementation

- `FaceFxActor::parse`, `FaceFxAnimSet::parse`: §2, rejected unless the archive is consumed exactly (anim sets) and
  the graph is topologically sorted.
- `FaceFxLibrary::Evaluate(actor, anim, t, faceParams)`: §3.1, §3.3, §3.4, §3.5, §3.6 (x87 expressions computed in
  double where the original uses double constants).
- `IntroCinematic` (AVATAR_CHOICE and FIRST_WM): loads `avatar_face_fx.bnk` and every `.fxe` of the cinematic bank;
  `applyFaceFx` per PNJ / player actor after the body pose: latest Morph key ≤ scene time, anim time = scene time −
  key time **[INF: stateless; the original advances its own clock with the frame dt, identical at rate 1, differs
  in the 0.2× slow motion of C_AVATAR]**, active while anim time ≤ end, linear 0.2 s blend in (from the previous
  key's anim if still active) and 0.2 s blend out [INF: FUN_00480d10 is ENC].
  The bones replace the skeleton locals (name hash lookup); TD_Skinface `facialAnimCoef1/2` set per avatar.
- Flags: `-nofacefx` (disable), `-logfacefx` (per-frame log), `-tess` (AVATAR_CHOICE test: C_TESS at once).

## 6. Look-at (eyes) [CODE unless tagged]

Decompiled: `/tmp/claude-0/f2py/look.c` (FUN_00b89730, FUN_00b871a0, FUN_00b77530, FUN_00b8dcd0; Ghidra loses most
of the x87 data flow, the description below is from the asm in `/tmp/claude-0/text.asm`).

### 6.1 Registers (FUN_00b75f50 / FUN_00b75e10 / FUN_00b76070)
- FUN_00b75f50(side, vertical) (ESI = animator; skipped when `*(+0x1ee0)+0x1fc == 1`): register
  `LookAt_T2_side` = side, `LookAt_T2_vertical` = vertical; with the debug byte DAT_00fd9011 set, the debug floats
  DAT_00fd9014 / DAT_00fd9018 replace them. Then `+0x1ec0 = 1` (the animator ticks even without an animation, §4.3)
  and `+0x1ec2 = 1`.
- FUN_00b75e10(node, v): `v = min(v, node.max)` (+0x10), `v = max(v, node.min)` (+0x08), then
  `FUN_006ed2d0(node, 2 /*replace*/, v, 0, -1, 0, 0)` (same call as the face parameters, §3.4).
- FUN_00b78970 (per-frame update, after the tick): when `+0x1ec2` is set, FUN_00b76070 resets both registers to 0,
  clears their curve state (FUN_006ed260), sets `+0x1ec0 = 1` (one more tick) and `+0x1ec2 = 0` ⇒ the look-at lasts
  one frame and must be set again every frame.
- Graph [DATA female/male.fxa]: `LookAt_T2_side` (range −0.45 …, input `ANM_Look_side`·0.52) → `Eyes_left` = side·0.57,
  `Eyes_right` = −side·0.57 (+ `Gaze Eye Yaw`·±0.005) → eye bone poses, `Focus`; `LookAt_T2_vertical` (range −0.26 …,
  input `ANM_look_vertical`·0.26) → `Eye_up` = vertical, `Eye_down` = −vertical (+ `Gaze Eye Pitch`·±0.005) → eye
  bones, lower lids (`CustPos LowerEyeLid Up/Forward`), `Cor_Asian_look_up/down`.

### 6.2 Eye target → (side, vertical): FUN_00b871a0 (avatar update, EBX = avatar)
```
if avatar+0x210 (cinematic object, see 6.3) and its vfunc+4() == 1:
     target(+0x1e30) = object+0x40 (position), +0x1e40 = 1, fromObject = 1
if !+0x1e40: return
PL, PR = world positions of L_Eye_All / R_Eye_All (hash names @0xf0b598 / 0xf0b58c; FUN_007d01d0 bone matrix ×
         avatar matrix +0x10, FUN_00ddb740; when avatar byte +0xc == 0 the avatar translation is first moved by the
         world origin (0x1367a10 / 0x1178130, FUN_00422a00 [ENC]))
mid  = (PL + PR) · 0.5 (double @0xf49748)
up   = fromObject ? (0, 1, 0, 0) : row 1 (Y axis) of the R_Eye_All world matrix
axis = PL − PR
A    = op(up, axis); normalise(A); normalise(up)         (0x42c2a0, 0x433510 [ENC]: cross product /
                                                          normalisation [INF from use])
dir  = normalise(target − mid) (target.w = 0)
side = dot(normalise(axis), dir)
U    = normalise(op(axis, A))                              (= up without its component along the eye axis,
                                                          whichever operand order 0x42c2a0 uses)
vertical = dot(U, dir)
if !fromObject: vertical = min(vertical, 0.07 @0xedf1f4); if (−0.2 @0xf49c98) > vertical: vertical = −0.2 @0xf7db64
animator = avatar->vfunc+0xbc(); FUN_00b75f50(side, vertical)
avatar+0x210 = 0; +0x1e40 = 0
```
Positive side = target on the L_Eye_All side (→ `Eyes_left`), positive vertical = target above (→ `Eye_up`).

### 6.3 Target sources
1. **Actor property `LookAtTargetFaceFx`** (.sce, see 6.4): setter FUN_00b854d0 (strcmp with the string @0xf0b5ac)
   copies the value to avatar+0x1c58 (256 chars). FUN_00b869e0 (called by the avatar's cinematic attach FUN_00b8b3xx,
   b8b407): copy, `strstr("-m")` → `"-w\0"` for a woman (FUN_00cabec0 == 1), FUN_00b3e110(cinematic+0x370, name) →
   avatar+0x210 (the scene object of that name). The eyes follow that object every frame (`up` = world Y, no
   vertical clamp). Data: Tess in `M_TESS_Tess.sce` → `lookAt_Tess` (key 0.667 s), Tess in garage
   `M_INTRO_intro.sce` → `LookAt_Tess` (key 7.78 s).
2. **Messages `lkon` / `lkof`** (actor Message track, avatar message handler FUN_00b8dcd0):
   `lkon` (unless DAT_00fd51b0 == 0 or avatar +0x220/+0x224 == 8193c347 9d785ed9) →
   FUN_00b858f0(avatar, DAT_01195b80 (the view position vec4, also the Tess-volume test point, tess_garage_spec), 1):
   `+0x1d70 = target, +0x1d80 = 0, +0x1d68 = 0, +0x1d81 = 1, +0x1d69 = 1`. `lkof` → `+0x1d68 = 1`.
   Data: Tess in `M_WALKMODE_walkmode.sce` (lkon at 0), garage `M_LIFE_TESS_life.sce`, `M_VOICE_03..06`. The
   actor Message track keys are `{u32 REFS index, f32 length}`; a message REFS entry is `{u16 0x020b, u16 size,
   char text[]}` (no u64 hashcode, unlike the 0x0302 / 0x0306 entries) [DATA]; `CinematicScene` reads it so.
3. **Walk mode, 0x9520b9** [CODE, asm 0x9519a0–0x952234, decompiled `/tmp/claude-0/-home-claude/44d890d5-0ce4-567e-a88c-c2f12ec3a4a4/scratchpad/lookat/d1.c`]: the call is
   **not** a "hovered character looks at you". It is in **FUN_009519a0, the state machine of the player's walk
   avatar** (`this` = manager+0x94, `this+0xc9c` = manager+0xd30 = the player's walk avatar, the one FUN_00950470 puts
   under the camera; state `this+0xc` = manager+0xa0, changed flag manager+0xa4; arg 1 = event 1 enter / 0 update /
   2 leave). `FUN_00b858f0(walkAvatar, copy of DAT_01195b80, 0)` runs **every frame of states 6 and 8**:
   - state 1 (first person) → FUN_00951440 when input action 0x27 is pressed (FUN_0094f7d0 → FUN_00d5e9f0(0x27),
     unless DAT_00fd960c+0x345), or with action 0x22 + the emote menu DAT_00fd95f8 (+0xccc = 1, FUN_0094e540).
     FUN_00951440: camera **type 10** (FUN_00ca0840(10) → manager+0xd54) aimed at the avatar position (+0x40) + (0,
     0.25 (double 0xf49800), 0) (+0x380), switch FUN_00ca0df0(2, 0.2 (DAT_00ede428)) [INF: 0.2 s blend], cursor
     hidden, selection cleared (FUN_0094f440(−1)), **state 5**;
   - state 5: `avatar+0x9c |= 1`, fade `+0xcc4 += dt / 0.2` (double 0xf49b58) clamped to 1 (FUN_0041c400 [INF]) →
     FUN_00b85460 (avatar +0x1a8 = v, +0x1a4 = min(v, +0x1b0)) [INF: avatar opacity]; at 1 → **state 6**;
   - **state 6** (self view): FUN_00950760 (avatar under the walk camera, camera 10 re-aimed at avatar + (0, 1.6
     (0xf49c18), 0) via FUN_00c7e3a0), then the look-at call; action 0x27 again → FUN_00950250 (camera back
     FUN_00ca0df0(2, 0.2), cursor shown, **state 4** = fade out by dt/0.2 then `+0x9c &= ~1`, state 1); an emote
     chosen in the menu (DAT_00fd95f8+0xa4 > 0) → state 7 (emote animation, back to 4 at its end);
   - **state 8** (set by FUN_0094be60 and FUN_009a3490: a camera type 10 built for another mode, FSCommand `open`
     of the game mode at 0x94e131 [INF: shop / dressing preview]): b85460(1.0), the look-at call every frame.
   So the target is the camera position (DAT_01195b80, [INF: view position, see 6.3.2]) copied **every frame**
   (the camera orbits), flag +0x1d69 = 0 (so FUN_00b89730 also needs `!(avatar+0x9c & 8)`), and the head turn is
   requested too (same FUN_00b89730 path as lkon: 0.2 cone on the avatar forward, head angles [ENC], eyes 6.2).
   There is no stop call: leaving states 6/8 just stops refreshing +0x1d70 (+0x1d81 stays 1; the avatar is faded
   out / back under the first-person camera).
   **No hover look-at exists** [CODE]: FUN_00b858f0 has 5 callers (0x9520fc here, lkon 0xb8e140, 0xb55bf4,
   0xb6aae8/0xb6ab0d, 0xb922ad: other avatar systems), the direct writers of +0x1d70 are 0xb82a95 / 0xb907a2 (car
   seat avatars) and the init 0xb8d629; the selection code (FUN_00952410, FUN_0094f440) and the FpSelectable
   enter/leave functions (vtables 0xf2db30 / 0xf2db78 vf14 FUN_009534c0: /:AvatarId, LockMyPhotos, LockSell;
   0xf2da10 vf14 FUN_00953a30 / vf18 FUN_009536b0: club / friend flash vars) never touch the look-at. In the house
   (AVATAR_CHOICE) Tess is not even a selectable: her `Tess` box volume triggers C_TESS (tess_garage_spec 2.5). Tess
   looks at the player in walk mode only through the `lkon` of `M_WALKMODE_walkmode.sce` (6.3.2): one snapshot of
   the camera position when the key is reached (C_WALKMODE start), applied while inside her 0.2 cone.
4. Head controller **FUN_00b89730** (per frame, avatar EBX): `Head` world matrix (FUN_00b93290, name @0xf0b2d4);
   when `+0x1d81` and (`dot(normalise(+0x1d70 − Head), avatar matrix row 2) > +0x1e04 (0.2 @0xede428)` or
   `+0x1d80`) and `!+0x1d68` and (`!(avatar+0x9c & 8)` or `+0x1d69`): head angles +0x1d60 / +0x1d64 (FUN_00954720),
   and the eye target `+0x1e30 = +0x1d70` moved by the world origin (FUN_00421980 [ENC]), `+0x1e40 = 1`; else the
   head angles are 0. Then the angles are limited (yaw ±0.8727 rad +0x1df4/+0x1df8, pitch −0.6109 … 0.3491 rad
   +0x1dfc/+0x1e00), smoothed (rates 2.5 +0x1e08, 6.0 +0x1e0c) and spread over the spine/neck/head bone range
   +0x1e10…+0x1e28 with the weights 0.15 / 0.2 (+0x1de4/+0x1de8) and 0.4 / 0.3 (+0x1dec/+0x1df0) (init FUN_00b8d4xx,
   b8d67d…). The rotation math (0x40dae0, 0x40d9e0, 0x403620, 0x41f0e0, 0x42b4f0, 0x44c560) is [ENC].

### 6.4 .sce property track [DATA]
Actor track id 0xd3593709: `{u16 0, u16 1, u32 size, u32 id, u32 nSub, char name[128]}` then nSub sub tracks
`{u16 3, u16 1, u32 size, u32 0xa58b238b, u32 nKeys, f32 duration, f32 times[nKeys], keys {u32 0, f32 length,
char value[]}}` (one key in every file of the flow). Parsed by `CinematicScene` (`Object::Properties`).

### 6.5 OpenTDU2
`IntroCinematic::computeLookAt` (before `applyFaceFx`, per PNJ): property target (6.3.1, every frame, up = Y, no clamp)
else lkon target captured when the key is reached (6.3.2) gated by the 0.2 cone of 6.3.4 (up = R_Eye_All Y, vertical
clamp); the registers go to `FaceFxLibrary::Evaluate(…, pLookAt)`; without a FaceFX animation the graph is evaluated
with the registers only (face parameters + look-at) and written at full weight, as the forced tick does.
`-logfacefx` logs the values.
`IntroCinematic::setLookAtTarget(name, worldTarget)` / `stopLookAt(name)` = FUN_00b858f0 / lkof, used by the
lkon handling; the lkon / lkof state is only changed by a message key, a scene without a message track no longer
clears it (the original only writes it in FUN_00b858f0 and lkof).
Test (walk mode, `-walkat 9.0,33.0,4.0,30`, outside the Tess box, in her cone): `Tess_Wintory lkon side −0.60
vertical 0.06` every frame, with the reticle on her and after turning away (unchanged: no hover dependency).
**Not reproduced**: the head turn of FUN_00b89730 (ENC math; the eyes alone follow the target, limited by the node
ranges), the self-view camera of 6.3.3 (states 5–8 of FUN_009519a0, camera type 10, avatar fade; no caller of
setLookAtTarget yet), the one-frame fade of FUN_00b76070 (the registers simply stop), the `avatar+0x9c & 8` /
+0x1d69 gate, whether a looping cinematic re-sends its key-0 lkon on each loop [INF: OpenTDU2 captures once].

## 7. FaceFX outside the intro / garage cinematics [DATA]
Every `.sce` of the cinematic banks with a Morph track or an lkon message: new_intro (C_AVATAR, C_FIESTA_INTRO_01..06,
C_INTRO, C_TESS, C_WALKMODE), garage (C_INTRO, C_VOICE_03..06, C_LIFE_TESS), meeting_tess (M_MEETING_TESS_*; bank not
used by the current flow). All are played by `IntroCinematic`. `M_FIRST_OUTRO_first_outro.sce` (FREERIDE, eph_l3di.bnk)
has no Morph track and no look-at (its bank carries realtor-e-m/w.fxe for the other cinematics of the spot). The exe
has no blink / idle facial system (no string, the only FaceFX drivers are the Morph track, §3.4 and §6) [CODE].

## 8. Remaining
- [ENC] CinScene Morph track parsing (key semantics of the u64 and of the clip length), FUN_00480d10 fade blend,
  acos/sin/sqrt/fabs/FxRealEquality helpers (CRT-like, low addresses); head-turn math of FUN_00b89730; the vector
  helpers 0x42c2a0 / 0x433510 of FUN_00b871a0 (cross / normalise [INF]); the dispatch of the .sce property track to
  FUN_00b854d0 (OpenTDU2 applies the value from its key time).
- [INF] the loader that rewrites the master bone +0x40 with the bone name hash; FaceParamArray element order;
  the avatar id 8193c347 9d785ed9 excluded from lkon (not identified; no avatar of the flow is excluded).
- Not done: the `Ethnic_*` morph target nodes (no readable consumer), head turn, walk-mode hover look-at.
