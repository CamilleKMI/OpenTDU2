# TDU2 intro cinematic: animated avatars (PNJ dancers) reimplementation spec

Scope: the characters of `C_INTRO` (`Euro/Bnk/Scenarics/Cinematics/intro/new_intro.bnk`, scenes `M_INTRO_generique.sce`
and `M_INTRO_Default.sce`). The goal is to draw them the way the game does in OpenTDU2, using the original D3D9 shaders
translated to SPIR-V and the original .2DM materials.

Tags:
- **[CODE]** read from readable x86 (VA 0x6c1000–0xe41000).
- **[DATA]** read from or verified on game files.
- **[SHADER]** read from the .sob bytecode.
- **[INF]** inferred; the reason is given.
- **[ENC]** the code is in the SecuROM-encrypted range (< 0x6c1000) and was not read. No DRM was touched.

Files produced:
- Decompiled code: `/tmp/claude-0/avatar_dec1.c` … `avatar_dec6.c`.
- Python helpers, in `/tmp/claude-0/py/`. Run each with `python3 -I`.

| script | does |
|---|---|
| `anm.py` | `.anm` decoder and sampler |
| `anm_dump.py` | full decoded dump of a clip against a skeleton |
| `anm_validate.py` | unit-length and continuity checks on every clip |
| `xsb.py` | `.XSB` skeleton parser |
| `skel_check.py` | `CONJ=1`: verifies the inverse-bind matrices |
| `pose.py` | evaluates a world pose at time t |
| `bonenames.py` | recovers bone names from the two hashes |
| `skin3dg.py` | 3DG skinned primitives: palettes and vertex layout |
| `avt.py` | `.avt` tree |
| `mat2dm.py` | 2DM materials and parameters |
| `dbtab.py` | DB table loader |
| `intro_pnj.py` | builds the PNJ list (the assembly table of §3.4) |
| `sceanim.py` | .sce Animation tracks with their REFS |

Outputs: `/tmp/claude-0/av/`

| file | content |
|---|---|
| `fi_dancestand1-w_decoded.txt` | the validation clip, fully decoded |
| `woman_bones_named.txt` | bone list with recovered names |
| `def_anim.txt`, `gen_anim.txt` | all Animation keys of the two scenes |
| `intro_pnj.txt` | the assembly of every intro PNJ |
| `db_avatar.json` | DBPNJ and DBClothe tables as JSON |
| `bodies/`, `pnj/`, `styles/`, `cl/`, `cc/` | extracted banks |
| `sob/` | split avatar shaders |

Banks staged from the Mac: 52 clothes banks plus `common_clothes.bnk`, `common_bodies_light.bnk` and
`avatar_face_fx.bnk`, under `/mnt/user-data/uploads/TestDrive2/Euro/Bnk/avatar/...`.

---------------------------------------------------------------------------------------------------
## 1. `.anm` animation format ("ANM!")

### 1.1 Where the decoder is

| what | where | tag |
|---|---|---|
| Animation library (`edAnim`) | Its RTTI (`edAnmControler`, `edFxAnimator<…>`) has vtables pointing into 0x5e…, the **encrypted** range. The track decoder could not be found in readable code (no use of 1/65535, no matching quaternion unpack) | [ENC] |
| The `CinScene.cpp` / `AnmProperty.h` assert strings (0xf42f94…) | not referenced from readable code | [ENC] |

Everything in §1 is therefore decoded **from the data**. It was validated on all 40 clips of new_intro.bnk:
- the byte accounting of every track matches its stored size, except the IK tracks (kind 8);
- the quaternions are unit length and continuous;
- the poses are anatomically plausible (§1.7).

### 1.2 File layout [DATA]

All little endian.

```
0x00 char[4] "ANM!"
0x04 u32     0x10
0x08 f32     1.6            (format version)
0x0C u32     0x12f (303)
0x10 u32     0, u32 0
0x18 char[4] "DATA"; u32 dataSize
0x20 u32     numTracks
0x24 f32     duration       ("dur": loop/usable length, see 1.6)
0x28 f32     duration2      (length of the keyed data; = dur for "classic" clips)
0x2C u32     loopFlag       0 or 0xFFFFFFFF (−1 only on the 0x6xx-format clips: buoy, sitstand-w, dj_01/02)
0x30 f32     dur2 − dur     (0 for classic clips; 0.15 fi_dancebuoyw, 0.2/0.7 fi_selectavatar-m/w)
0x34 u32     0
0x38 tracks[numTracks]      (each track is self-sized)
     "EOF!" u32 0           trailer
```

### 1.3 Track [DATA]

```
+0 u16 kind      bit mask of the sub-blocks that follow: 1 = rotation, 2 = translation, 4 = scale.
                 bit 0x10 = flag without data (seen only on the buoy ROOT, kind 18 = 0x12).
                 kind 8 = IK / constraint track (see below).
+2 u16 0xFFFF
+4 u32 trackSize (from +0, including this header)
+8 u32 boneHash  4-byte bone hash, case-sensitive (§2.3): "Hips" = 0x73706948, Pelvis = 0x766cd8b9
+12 sub-blocks in the order rotation, translation, scale (only the ones whose kind bit is set)
```

Kinds seen: 1, 2, 3, 5 (rotation+scale, women's Foot/ToeBase), 6, 7, 18, 8.

**kind 8**: only in `fi_selectavatar-m/w`, which C_INTRO does not use. Its payload names three bones (UpLeg_R, Leg_R,
Foot_R), then holds float weight keys. It is an IK leg constraint. It can be ignored for the intro.

**Tracks whose hash is not in the skeleton** are ignored. There are 10–13 per clip, for example 0xd2fb2d1d and
0xc9d2ff20. They hold identity rotations and are the 4th (tip) finger joints that the game skeleton does not have [INF].

### 1.4 Sub-block (key array) [DATA]

```
+0 u16 numKeys (n)
+2 u16 fmt
+4 f32 subDuration (= header duration of the clip)
+8 times:
     if (fmt & 1): u16 times[n-1], key 0 is implicitly at t=0, t_k = times[k-1] / 65535 * subDuration
                   (the last stored value is always 0xFFFF)
     else:         f32 times[n] (seconds; includes key 0)
   then align to 4 bytes
   if (translation/scale and fmt & 4): f32 rangeMin, f32 rangeExtent
values[n]:
   per key, if (fmt & 0x20): a reserved prefix that is always zero — 12 bytes before a float quaternion,
   4 bytes before a 6-byte value and 2 bytes after it (records of 28 or 12 bytes)
   rotation: fmt & 2 ? 6 bytes (see below) : f32 x,y,z,w (16 bytes)
   translation/scale: fmt & 4 ? u16 x,y,z quantized : fmt & 2 ? s16 x,y,z : f32 x,y,z
then align to 4
```

The `fmt` values seen are 1, 2, 3, 5, 9, 0x400, 0x402, 0x421, 0x423, 0x425, 0x600, 0x602, 0x621, 0x623 and 0x625.
- The high byte (4 or 6) has no observable effect on the layout.
- Bit 8 (seen in fmt 9) has no layout effect either: it is float quaternions with u16 times.
- Bits 0x400 and 0x200 only occur on clips exported with the second exporter, the ones that have loopFlag = −1 or a
  non-zero `dur2 − dur`.

Value encodings:

| value | encoding | tag |
|---|---|---|
| **Compressed quaternion** (6 bytes) | `s16 a,b,c`; `x = a/32767`, `y = b/32767`, `z = (c & ~1)/32767`, `w = sqrt(max(0, 1 − x² − y² − z²))`, and **`w = −w` when `c & 1`** | [DATA] |
| **Compressed vector, fmt & 2** | `s16 / 16384` (range ±2 m). Pelvis height ≈ 0.86–0.91 in every dance clip; the Breasts/Head translations match the bind pose to the millimetre | [DATA] |
| **Quantized vector, fmt & 4** | `v = rangeMin + u16/65535 · rangeExtent` per component, with the two floats stored after the time array. On fi_dancewalk1w the pelvis gives y = 0.894 and x travelling −2.88 → −0.53 m, and the s16 reading wraps. The sitting pelvis is at y = 0.368 | [DATA] |
| **Scale** | uses the same encodings. The women's Foot/ToeBase scale keys are ≈ (1.0, 0.75, 0.73) and (1.0, 0.91, 1.0): high-heel foot deformation baked in the clip | [DATA] |

Why the LSB of c is the sign of w: z's LSB is 0 on 57 037 of 57 073 keys. On the spinning turntable (`fi_dancedj_dj_01`),
consecutive keys stay continuous within the runs that have the bit set only when it is read as w < 0.

### 1.5 Bone binding [DATA]

- A track drives the skeleton bone whose `hash4` (§2.2) equals `boneHash`. Bones without a track keep their **bind
  local pose** (XSB local q/t/s).
- Track values **replace** the bind local values; they are not additive. Evidence: pelvis q ≈ bind (0.707, 0.707, 0, 0);
  Head translation 0.098 versus bind 0.092.
- The same clip works on both skeletons. The man has 124 bones and the woman 125 (with "Breasts"), so the bone *indices*
  differ but the hashes are identical.
- Root motion: there is **no root track** in the avatar clips.
  - The dancer's offset and travel are in the **Pelvis** (bone 8) translation, in ROOT space. For example
    fi_dancestand1-w has its pelvis at (−0.705, 0.900, 0.048).
  - ROOT (bone 0) stays at the actor origin, so the actor transform of the .sce (§4.3) places the ROOT.
  - Props are different: `cf_buoy_*` animates the ROOT itself ("Buoy", kind 7), and `fi_dancedj_dj_01/02` animates
    the turntables.

### 1.6 Sampling [INF; the sampler is ENC]

- For each sub-block, find k with t_k ≤ t < t_{k+1} and u = (t − t_k)/(t_{k+1} − t_k).
  - Rotation: shortest-path slerp. Negate q_{k+1} when dot < 0; nlerp is visually identical, because the minimum
    |dot| between consecutive keys is 0.93 on humans.
  - Translation and scale: lerp.
- Before key 0 or after the last key, clamp to the end values.
- **Looping**: the .sce plays clips much longer than they are (`fi_dancestand1-w`, 1.92 s, is played for 98.12 s).
  - Every dance clip closes exactly: the first and last keys are equal, with dot = 1.0000 and Δpos = 0.
  - So sample at `fmod(clipTime, dur)`, with `dur` taken from DATA+0x24.
  - For the clips with loopFlag = −1 the data runs to `dur2` = dur + (dur2 − dur). The extra tail (0.04–0.3 s) is
    most likely a crossfade margin for the loop [INF].
  - The walk clips (`fi_dancewalk*`) do **not** close (Δpos 0.3–2 m), but the .sce still asks for 17–68 s of them.
    How the engine handles that (root-delta accumulation or a hold) is unknown [ENC]. Using fmod gives a visible pop
    every cycle on those actors only.

### 1.7 Validation (fi_dancestand1-w) [DATA]

The full dump is `/tmp/claude-0/av/fi_dancestand1-w_decoded.txt`.

- **Clip**: 66 tracks, duration 1.92 s, 1348 rotation keys.
  - The large joints use float quaternions (fmt 1); the hands and fingers use compressed ones (fmt 3).
  - The Pelvis has rotation fmt 1 (49 keys) and translation fmt 3 (21 keys).
  - The Breasts track is translation only (kind 2). Foot/ToeBase are rotation + scale (kind 5).
- **Quaternion checks**:
  - max | ‖q‖ − 1 | = 7·10⁻⁸ (float keys) and exactly 0 (compressed keys).
  - min |dot(q_k, q_{k−1})| = 0.993.
  - Over all 40 clips: min |dot| ≥ 0.93 on every human clip. The rapidly spinning turntables reach 0.02.
- **World pose at t = 0.5 s** on woman_body.xsb (`pose.py`): Pelvis (−0.64, 0.90, 0.05), Head (−0.63, 1.54, 0.09),
  hands (−0.47, 0.92) / (−0.70, 1.17), feet y = 0.145 (heels), toes y = 0.016. The man clip gives head y = 1.57.
  The sitting clip gives pelvis y = 0.37 and head y = 0.95.

---------------------------------------------------------------------------------------------------
## 2. Skeletons (.XSB, "SKEL")

### 2.1 Files [DATA]

| bank | files | skeleton |
|---|---|---|
| `avatar/common_bodies.bnk`, sub 7 | `man_body.xsb` and `man_head_{occ,asia,afrik}.xsb` are identical | **124 bones** |
| `avatar/common_bodies.bnk`, sub 7 | `woman_body.xsb` and `woman_head_*.xsb` are identical | **125 bones** (adds bone 12 "Breasts") |
| `new_intro.bnk` | `cf_buoy_*.XSB`, `cf_plat_*.XSB` | 2 bones (Root, Buoy/…) |

### 2.2 Layout [DATA, verified on all 4 distinct XSB]

```
0x00 u32 1, u32 0, u32 fileSize-0x? (0x40a0), char[4] ".XSB"
0x10 char[4] "SKEL", u32 0, u32 size, u32 size
0x20 u32 n                                    number of bones
0x24 s32 parent[n]                            −1 for the root; parents always precede children
     u32 sortedHash4[n]                       ascending bone hash4
     u32 sortedIndex[n]                       bone index of sortedHash4[j]
     (align to 16)
     Local[n] (48 bytes each): f32 q[4] (x,y,z,w); f32 t[3], pad; f32 s[3], pad   — bind pose, local to parent
     f32 invBind[n][16]                       inverse of the bind world matrix (D3D row-vector, translation in row 3)
     u8  id8[n][8]                            in **sortedHash4 order** (id8[j] belongs to bone sortedIndex[j])
```

- Each bone has two names. `hash4` is the 4-byte fold (§2.3), case-sensitive. `id8` is the engine's 64-bit
  `GetIdentifier64bit`: uppercase, char i added into byte i&7.
- There is no explicit per-bone hash4 array: `hash4[sortedIndex[j]] = sortedHash4[j]`.

**Quaternion convention** (verified): with D3DX row-vector math, the local matrix is

```
Local = S · R(q*) · T(t), with R(q*) = D3DXMatrixRotationQuaternion(conjugate(q))
World_i = Local_i · World_parent(i)
```

`invBind` equals `inverse(World_bind)` to 8·10⁻⁵ for every bone of both skeletons. With the non-conjugated quaternion
the error is up to 2.4. This matches the camera convention found for the .sce (intro_spec §7.8: R(q*)). In
column-vector terms, the engine's quaternions rotate by q*.

### 2.3 Bone hash [DATA]

```
hash4(name): b[4] = 0; for i, c in name: b[i & 3] += c   →  u32 little endian
```

It is **not** case-folded: "Hips" = 0x73706948, "ROOT" = 0x544f4f52, "Root" (buoy) = 0x746f6f52. `id8` is the
uppercase 8-byte fold.

Names were recovered by solving the two hashes (`bonenames.py`); 90 of 125 are named in
`/tmp/claude-0/av/woman_bones_named.txt`.

Woman skeleton (index: name, parent):

| bones | names (parent) |
|---|---|
| 0, 1–7 | ROOT; 1–7 unnamed helpers |
| 8–16 | Pelvis(0), Hips(8), Spine(9), Spine1(10), Breasts(11), Spine2(11), IK_Back(13), Neck(13), Head(15) |
| 17–60 | 17 = face root (unnamed), then the face bones (L_/R_ Ear, EyeBall, Eye_All, InnerBrow, OuterBrow, Cheek, Cheekbone, Chin, Nostril, UpperLip, LowerLip; M_Jaw, M_tongue, M_Throat, M_Forehead, M_NoseBase, M_NoseEnd …); 59 = IK_Head |
| 61–83 | Shoulder_L(13), Arm_L, ArmRoll_L, ForeArm_L, ForeArmRoll_L, Hand_L(65), IK_Left_Hand, Index1-3_L, Middle1-3_L, Pinky1-3_L, Ring1-3_L, Thumb1-3_L |
| 84–110 | the same chain on the right side (Shoulder_R … Thumb3_R); 90 = IK_Right_Hand |
| 111–120 | UpLeg_L(8), Leg_L, Foot_L, IK_Left_Foot, ToeBase_L, then UpLeg_R … ToeBase_R |
| 121–124 | belt helpers; 123 = SideBelt |

The man skeleton is the same without Breasts, so every index from 12 on is shifted by −1.

### 2.4 Use [DATA + INF]

- The skin palettes (§3.5) index **this skeleton** (bone index, not hash). So a body, head or clothes mesh of sex S must
  use the S skeleton. Clothes banks have no XSB.
- `invBind` is stored ready to use. You can also rebuild it from the local bind pose.

---------------------------------------------------------------------------------------------------
## 3. Avatar assembly

### 3.1 Where a PNJ's look comes from [CODE + DATA]

The flow is FUN_00da8bb0 (AVATAR_CHOICE start), then for every `'CPNJ'` entity:

1. FUN_00cbd680 initialises an `AvatarConfiguration` (0xe4 bytes, §3.2).
2. **FUN_00dc2070(config)**, with EAX = the entity's **ConfigName** (entity +0xe0), searches the runtime DBPNJ table
   (`DAT_01024c94`, stride 0xf8, std::string `_configid` at +0x60).
   - Found: FUN_00cb5bd0 does two things.
     - FUN_00cb5b10 loads `<_fileconfigid>.avt` from `Avatar/CONFIG/PNJ.bnk` (".avt" is appended; the bank is
       loaded by FUN_00cb7470 together with STYLES.bnk).
     - It then **overwrites the clothes and colours** with the DBPNJ fields. Each `_clothe_*` hashcode goes through
       the DBClothe hash→index map (`DAT_01024ca0/ca4`, stride 0xa0, u16 at +0x68) into the two 11-slot ClotheID
       arrays, and the `_*_color` bytes go into the two ColorID arrays.
   - Not found: FUN_00445920 [ENC].
3. The intro only does this for names starting with **'W' or 'M'**. The other PNJ (Dance_*, Pool_*, Tess, DJ) get the
   same lookup through the normal PNJ path.
4. Casino-avatar case (`DAT_00fd954a` set, i.e. a TDU2 Casino Online profile was imported): the PNJ named `WOMAN_0n` /
   `MAN_0n`, built by FUN_00da7b90 from `DAT_00fdb570`, gets the casino avatar's clothes instead (FUN_00da7b40, then
   FUN_00445920 [ENC]). Otherwise the string "NoCasinoAvatar" is used.
5. FUN_00b84590(2000, id64(name), config, …) creates the avatar instance with priority 2000.

**So there is no random choice** for WOMAN_xx / MAN_xx in the normal case. This corrects intro_spec §0, which said
"random avatar". The config is fully deterministic, from the six DBPNJ "Intro Player" entries:

| entity | ConfigName | .avt | body |
|---|---|---|---|
| WOMAN_04 | AVT_Female_Intro | AVT_Female | woman occ |
| WOMAN_05 | AVT_Female_Afrik_Intro | AVT_Female_Afrik | woman afrik |
| WOMAN_06 | AVT_Female_Asia_Intro | AVT_Female_Asia | woman asia |
| MAN_01 | AVT_Male_Intro | AVT_Male | man occ |
| MAN_02 | AVT_Male_Afrik_Intro | AVT_Male_Afrik | man afrik |
| MAN_03 | AVT_Male_Asia_Intro | AVT_Male_Asia | man asia |

These six are the selectable player avatars shown next in C_AVATAR. The other appearance data comes from the .avt
(§3.2): **no RNG is involved**. The DB field `_hair_rand_intro` exists in DBClothe (57 entries = 1), but none of the
readable code paths used here reads it [INF]. A randomiser, if any, is in FUN_00445920 [ENC].

### 3.2 `.avt` = AvatarConfiguration [DATA + CODE]

**Binary format** (big endian): `u16 0x5ee9`, then a tree of nodes `{u16 size, u16 key, value[size−2]}`.
- A value of 1/2/4 bytes is u8/u16/u32-or-f32.
- A value of 8/12/16 bytes is f32×2/3/4.
- Larger values are child lists.

The keys are 16-bit hashes. Their mapping to the xmb names below (FUN_00cbd030 reader, FUN_00cbd7d0 writer, field
names at 0xef295c…) follows the read order and the value types [INF]:

| key | field | struct offset | example (AVT_W_Random01) |
|---|---|---|---|
| cd26 | BodyID u16 | +0x00 | 1 |
| 01f9 | SkinID u16 | +0x02 | 20 |
| 6cb0 | SkinAmbiant RGB | +0x10 | 1,1,1 |
| 1a6c | SkinDiffuse RGB | +0x20 | 1,1,1 |
| e173 | SkinSpecular RGB | +0x30 | 1,1,1 |
| cbfd | SpecCoeff, SpecPow, RIMCoeff | +0x40 | 0.3, 3.0, 0.7 (= the SPECPARM of the body 2DM) |
| e9ce | DiffWrap, ScatteringCoeff | +0x50 | 0.6, 0.85 (≈ COEFFS) |
| 82b9 | EyeID u16 | +0x58 | 41 |
| c881 | EyebrowsID u16 | +0x5a | 7 |
| c643 | EyebrowsColor | +0x5c | 0.235, 0.039, 0.039, 1 |
| d448 | ClotheIDArray: 22 × {u16 id, u8 color} (2 sets × 11 slots) | +0x60 / +0x8c | overridden by DBPNJ for PNJ |
| 9d93 | FaceParamArray (49 × u8, the morph sliders `C_*` at 0xef2e50…) | +0xa2 | ignore (morphs) |
| 8d6d / 3f89 / 5182 | age f32, freckle f32, ClotheSetType | +0xd4 / +0xd8 / +0xdc | |

**BodyID** comes from the table built in FUN_00cb43e0 [CODE]. Each entry is a {mesh, material variant, head}:

| BodyID | body 3DG | body 2DM | head |
|---|---|---|---|
| 0 | man_body | man_body | man_head_occ |
| 1 | woman_body | woman_body | woman_head_occ |
| 2 | man_body | man_body_asia | man_head_asia |
| 3 | woman_body | woman_body_asia | woman_head_asia |
| 4 | man_body | man_body_afrik | man_head_afrik |
| 5 | woman_body | woman_body_afrik | woman_head_afrik |

The intro .avt files agree: AVT_Female 1, Female_Asia 3, Female_Afrik 5, Male 0, Male_Asia 2, Male_Afrik 4.

Bank choice: FUN_00ca9040(hires), called by FUN_00cb7ce0 [CODE].
- `true` loads `Avatar/COMMON_BODIES.bnk`; `false` loads `COMMON_BODIES_LIGHT.bnk`. If the load fails it retries with
  the other one.
- The LIGHT bank has the same meshes with ¼-size textures. Use COMMON_BODIES.
- `Avatar/CLOTHES/COMMON_CLOTHES.bnk` is always loaded (FUN_00ca90b0). It holds the shared maps and `hair_mat.2dm`.

### 3.3 Clothes [CODE + DATA]

There are 10 DBPNJ slots: hair, top, bottom, dress, shoes, glasses, gloves, helmet, accessories, beard.

**Resolving a slot**: `DBClothe[_hashcode == DBPNJ._clothe_<slot>]._file` gives the bank
`Avatar/CLOTHES/<dir>/<file>.bnk`.
- The path format is `"%sAvatar/%s/%s/"` (FUN_00ca8730) [CODE].
- `<dir>` follows from the code inside the file name [DATA, on the Mac tree]:

| code | dir |
|---|---|
| HR | hair |
| PN | pnj |
| SP | sportswear |
| SW | streetwear |
| FH | fashion |
| HL | highlife |
| CA | casino |
| ED | eden |

**Slots that draw nothing**: `M_/W_NO_<SLOT>` files and `*_NO_*`.

**Bank contents** [DATA]: `<file>.3dd`, `.3dg`, `.2dm`, and usually `<file>_mat.2dm`.
- `<file>_mat.2dm` holds the **colour variants**: N materials with the same name, differing in BASECLR / CLRL1..3.
- **The colour index (`_<slot>_color`) selects variant #index of `_mat.2dm`.** [INF] The reason:
  - FUN_00cb2080 does exactly this for hair, with `HAIR_MAT`: if `colorIndex < materialCount(HAIR_MAT)`, it copies the
    parameters of material #colorIndex onto every hair material through FUN_00cb4dd0.
  - The clothes `_mat.2dm` banks have the same structure.

**Hair colour palette** = `COMMON_CLOTHES.bnk/hair_mat.2dm`, 14 TD_Hair materials in file order. The index is 0-based
and 14 = keep the hair's own colours: Tess and Todd use 14 [INF].

| idx | material | PRIMCLR | SCDCLR |
|---|---|---|---|
| 0 | A | 0, 0, 0 | .373, .373, .373 |
| 1 | J | .145, .133, .122 | .976, .776, .416 |
| 2 | B | 0, 0, 0 | .714, .651, .596 |
| 3 | F | 0, 0, 0 | .949, .929, .922 |
| 4 | D | .227, .184, .141 | .698, .612, .525 |
| 5 | E | 0, 0, 0 | 1, 1, 1 |
| 6 | G | .024, .02, .016 | .965, .808, .655 |
| 7 | C | .384, .322, .255 | .769, .749, .749 |
| 8 | I | 0, 0, 0 | .588, .502, .427 |
| 9 | N | 0, 0, 0 | .867, .859, .859 |
| 10 | M | .373, .176, .039 | .796, .663, .557 |
| 11 | L | .125, 0, 0 | 1, .447, 0 |
| 12 | H | 0, 0, 0 | 1, 1, 1 |
| 13 | K | .306, .173, .063 | .953, .882, .788 |

Their SPECPARM is (0.5, 75) instead of the per-hair (0.5, 400): FUN_00cb4dd0 copies the colours. Whether it copies
every parameter is not verified.

**Body-part hiding** [CODE + DATA + INF]: the body 2DM has one material per body zone.
- FUN_00cada30 (avatar constructor) stores the 21 zone ids (8-byte fold of the full name) at +0x24c0, stride 0x10:
  BASE, BOTTOM_FEET, BOTTOM_ANKLES, BOTTOM_CALVES, BOTTOM_KNEES, BOTTOM_THIGHS, BOTTOM_PELVIS, TOP_LOWER_ABDOMEN,
  TOP_ABDOMEN, TOP_LOWER_BACK, TOP_BACK, TOP_UPPER_BACK_CENTER, TOP_UPPER_BACK_SIDES, TOP_CLEAVAGE_LARGE, TOP_CLEAVAGE,
  TOP_CLEAVAGE_SIDES, TOP_UPPER_CHEST, TOP_ARMS_SHOULDERS, TOP_ARMS, TOP_ARMS_FOREARMS, TOP_ARMS_HANDS.
  The 2DM `Name` is truncated to 15 characters, but the material hashcode is the fold of the full name.
- DBClothe has one flag per zone: `_base _feet _ankles _calves _knees _thighs _pelvis _lower_abdomen _abdomen
  _lower_back _back _upper_back_center _upper_back_sides _cleavage_large _cleavage _cleavage_sides _upper_chest
  _shoulders _arms _forearms _hands`.
- **0 = this garment covers the zone**. Draw a body zone only if every worn garment has 1 for it [INF].
  - Long sleeves clear arms/forearms/shoulders.
  - Pants clear thighs/knees/calves/ankles/pelvis.
  - A swimsuit clears only BASE (BASE is the "underwear" mesh).
  - The NO_* items are all 1.

### 3.4 The 31 intro PNJ [DATA]

Full table: `/tmp/claude-0/av/intro_pnj.txt`, produced by `py/intro_pnj.py`.

| entity | DBPNJ config | .avt | hair (col) | top | bottom | shoes | other |
|---|---|---|---|---|---|---|---|
| Dance_04_WOMAN_01 | STO_Party_Woman02 | AVT_W_Random01 | W_HR_R_Jane(13) | W_PN_T_SWIMSUIT_01(0) | W_PN_B_SWIMSUIT_01(0) | W_PN_S_Shoes_Low_02(0) | |
| Dance_04_WOMAN_02 | STO_Party_Woman03 | AVT_W_Random02 | W_HR_R_Miami(2) | W_SP_T_Sleeveless_Short_01(2) | W_SP_B_Short_01(3) | W_PN_S_Shoes_Low_02(3) | |
| Dance_05_WOMAN_01 | STO_Party_Woman04 | AVT_W_Random03 | W_HR_R_Tess(14) | W_SW_T_ShortSleeves_Long_01d(4) | W_SP_B_Short_Skirt_01(2) | W_PN_S_Shoes_Low_02(2) | glasses W_HL_G_Glass_01(0) |
| Dance_05_WOMAN_02 | STO_Party_Woman10 | AVT_W_Random09 | W_HR_R_Tess(1) | W_PN_T_SWIMSUIT_01(4) | W_PN_B_SWIMSUIT_01(4) | W_PN_S_Shoes_Low_02(5) | glasses W_FH_G_Glass_03(3) |
| Dance_Walk_01_WOMAN | STO_Party_Woman05 | AVT_W_Random04 | W_HR_R_Miami(8) | W_PN_T_SWIMSUIT_01(3) | W_SP_B_Short_01(3) | W_PN_S_Shoes_Low_02(3) | glasses W_FH_G_Glass_03(1) |
| Dance_Walk_02_WOMAN | STO_Party_Woman11 | AVT_Misc_W04 | W_HR_R_Jane(14) | W_SW_T_ShortSleeves_Long_01d(3) | W_SP_B_Short_01(0) | W_PN_S_Shoes_Low_02(0) | |
| Dance_Walk_03_WOMAN | STO_Party_Woman12 | AVT_Misc_W01 | W_HR_R_AFRO_01(0) | W_SP_T_Sleeveless_Short_01(1) | W_SP_B_Short_01(1) | W_SP_S_Shoes_ExtraHigh_01(1) | |
| Dance_Dj_WOMAN_01 | STO_Party_Woman06 | AVT_W_Random05 | W_HR_R_Miami(1) | W_SP_T_Sleeveless_Short_01(2) | W_SP_B_Short_01(4) | W_SP_S_Shoes_ExtraHigh_01(3) | |
| Dance_Dj_WOMAN_02 | STO_Party_Woman07 | AVT_W_Random06 | W_HR_R_Jane(4) | W_SP_T_Sleeveless_Short_01(2) | W_PN_B_SWIMSUIT_01(5) | W_PN_S_Shoes_Low_02(2) | |
| Dance_Transat_02_WOMAN | STO_Party_Woman08 | AVT_W_Random07 | W_HR_R_Jane(1) | W_PN_T_SWIMSUIT_01(4) | W_SP_B_Short_Skirt_01(4) | W_SP_S_Shoes_ExtraHigh_01(2) | |
| Dance_Transat_04_WOMAN | STO_Party_Woman09 | AVT_W_Random08 | W_HR_R_Host_Rpt(6) | W_SP_T_Sleeveless_Short_01(2) | W_SP_B_Short_Skirt_01(4) | W_PN_S_Shoes_Low_02(7) | |
| Dance_03_WOMAN | STO_Party_Woman01 | STO_Miami | W_HR_R_Host_Rpt(3) | W_PN_T_LongSleeves_Miami(0) | W_FH_B_MidPant_01(2) | W_PN_S_Shoes_Miami_02(0) | acc W_PN_A_Jewels_Miami |
| Pool_WOMAN_01 | STO_Party_Woman13 | AVT_Misc_W03 | W_HR_R_Tess(0) | W_PN_T_SWIMSUIT_01(2) | W_PN_B_SWIMSUIT_01(2) | – | |
| Pool_WOMAN_02 | STO_Party_Woman14 | AVT_Misc_W02 | W_HR_R_Jane(1) | W_PN_T_SWIMSUIT_01(3) | W_PN_B_SWIMSUIT_01(3) | – | |
| WOMAN_04 | AVT_Female_Intro | AVT_Female | W_HR_R_Jane(2) | W_SP_T_Sleeveless_Short_01(4) | W_SP_B_Short_Skirt_01(2) | W_PN_S_Shoes_Low_02(7) | |
| WOMAN_05 | AVT_Female_Afrik_Intro | AVT_Female_Afrik | W_HR_R_AFRO_02(0) | W_SW_T_ShortSleeves_Long_01d(0) | W_SP_B_Short_Skirt_01(3) | W_PN_S_Shoes_Low_02(0) | |
| WOMAN_06 | AVT_Female_Asia_Intro | AVT_Female_Asia | W_HR_R_Miami(0) | W_SW_T_ShortSleeves_Long_01d(2) | W_SP_B_Short_01(3) | W_PN_S_Shoes_Low_02(3) | |
| Tess_Wintory | STO_Tess_Wintory | STO_Tess | W_HR_R_Tess(14) | dress W_PN_D_Dress_TESS(0) | | W_PN_S_Shoes_TESS(0) | acc W_PN_A_Jewels_TESS |
| Dance_04_MAN_01 | STO_Party_Man02 | SPT_Todd | M_HR_R_Todd(14) | M_PN_T_LongSleeves_Tbishop(0) | M_CA_B_Pant_00(5) | M_PN_S_Shoes_High_Tbishop(0) | beard M_HR_M_BEARD_1DAY |
| Dance_04_MAN_03 | STO_Party_Man03 | STO_Stuart | M_HR_R_Groom(3) | M_FH_T_ShortSleeves_Long_01(5) | M_CA_B_Pant_00(2) | M_SP_S_Shoes_Middle_01(2) | |
| Dance_04_MAN_04 | STO_Party_Man04 | AVT_M_Random01 | M_HR_R_HAIRLESS_01(1) | – | M_SW_B_Short_01(4) | – | glasses M_HL_G_Glass_01(2), beard M_HR_M_BEARD_3DAY |
| Dance_03_MAN | STO_Party_Man01 | SPT_Jude | M_HR_R_Jude(0) | M_PN_T_Sleeveless_Jude_01(0) | M_PN_B_Pants_Jude_01(0) | M_PN_S_Shoes_Jude_01(0) | |
| Dance_Dj_MAN | STO_Party_Man05 | AVT_Misc_M01 | M_HR_R_HAIRLESS_01(1) | M_SW_T_ShortSleeves_Long_01(2) | M_FH_B_MidPant_01(3) | M_FH_S_Shoes_Low_01(1) | beard M_HR_M_BEARD_5DAY |
| Dance_Sit_MAN | STO_Party_Man06 | AVT_Misc_M02 | M_HR_R_Todd(2) | – | M_SW_B_Short_01(3) | – | glasses M_HL_G_Glass_01(3), beard M_HR_M_COMBI_01 |
| Dance_Walk_01_MAN | STO_Party_Man09 | AVT_M_Random03 | M_HR_R_Groom(5) | M_SW_T_MidSleeves_Long_01a(2) | M_SW_B_Short_01(2) | M_FH_S_Shoes_Low_01(1) | beard M_HR_M_RING_COMBI_01 |
| Dance_Walk_02_MAN | STO_Party_Man08 | AVT_M_Random04 | M_HR_R_Jude(11) | M_FH_T_ShortSleeves_Long_01(2) | M_FH_B_MidPant_01(0) | M_FH_S_Shoes_Low_01(4) | |
| Dance_Walk_03_MAN | STO_Party_Man10 | AVT_M_Random02 | M_HR_R_Stuart(0) | M_SW_T_ShortSleeves_Long_01(3) | M_FH_B_MidPant_01(4) | M_SP_S_Shoes_Middle_01(1) | |
| DJ | STO_Party_DJ | AVT_M_Random03 | M_HR_R_HAIRLESS_02(2) | M_PN_T_Shirt_SoundOperator | M_PN_B_Pants_SoundOperator | M_PN_S_Shoes_SoundOperator | acc M_PN_A_HP_SoundOperator_01, beard 1DAY |
| MAN_01 | AVT_Male_Intro | AVT_Male | M_HR_R_Groom(0) | M_SW_T_ShortSleeves_Long_01(1) | M_CA_B_Pant_00(4) | M_SP_S_Shoes_Middle_01(1) | beard M_HR_M_RING_COMBI_01 |
| MAN_02 | AVT_Male_Afrik_Intro | AVT_Male_Afrik | M_HR_R_HAIRLESS_01(0) | M_FH_T_ShortSleeves_Long_01(3) | M_FH_B_MidPant_01(5) | M_FH_S_Shoes_Low_01(3) | |
| MAN_03 | AVT_Male_Asia_Intro | AVT_Male_Asia | M_HR_R_Todd(0) | M_SW_T_ShortSleeves_Long_01(0) | M_SW_B_Short_01(2) | M_FH_S_Shoes_Low_01(4) | |

The STO_Party_* configs are **not** files: they are DBPNJ rows (`db.txt` DBPNJ[135…]). Only the `_fileconfigid` .avt
exists in PNJ.bnk.

### 3.5 Skinned meshes (3DG) [DATA + SHADER]

All body, head, hair and clothes primitives are skinned (only `shadow.3dg` is not).

**PRIM** layout:

| field | value |
|---|---|
| `+0x12 u16 type` | low byte 4 = strip; **high byte = palette size n** (1…31 seen, so at most 32) |
| child `BONE` section (right after the PRIM payload) | `u32 palette[ceil4(n)]`; entries ≥ n are padding garbage |
| `palette[i]` | **skeleton bone index** (§2.2) |

**DXVB stream 0** (flags 0x0240 or 0x2040):

| attribute | format |
|---|---|
| position | f32×3, in bind (model) space |
| (flags 0x2040) | uv f32×2 |
| BLENDINDICES | u8×4, fmt 0x11 = D3DDECLTYPE_UBYTE4; **palette-local** indices 0…n−1 |
| BLENDWEIGHT | f32×3, fmt 0x14; **w3 = 1 − w0 − w1 − w2** |

Stream 1 holds normal (half4), tangent (half4) and binormal (half4); the existing OpenTDU2 3DG parser reads it.

Skin VS (TD_Skin2 vs_0 and the same block in every skinned family) [SHADER]:

```
a0 = 3 * blendIndices                         (mova from mul by c0.y=3)
P  = Σ_k w_k * ( dot(p, c[148+a0.k]), dot(p, c[149+a0.k]), dot(p, c[150+a0.k]) ),  p = (pos,1)
     with w = (v5.x, v5.y, v5.z, 1−v5.x−v5.y−v5.z) pairing index k with weight k
N, T, B: same with dp3 (no inverse transpose; renormalised with nrm)
oPos = P · __World_View_Proj (c60..c63)
```

`Var_Skin_VS` = **`__Skin_Matrix[96]` at c148…c243**: 32 bones × 3 float4. For palette entry i of the primitive being
drawn:

```
M_i (row-vector 4x4, model space) = invBind[palette[i]] · World[palette[i]]     (World = skeleton world, §2.2,
                                                                                  relative to the actor ROOT space)
c[148+3i+r] = ( M_i[0][r], M_i[1][r], M_i[2][r], M_i[3][r] )   for r = 0,1,2     (columns of M_i = 3x4 transposed)
```

- The order (inverse bind then world) is [INF]; it is the only one that gives identity in the bind pose.
- The model→world matrix of the actor goes into `__World_View_Proj`. `__CamPos_ObjSpace` and `__DirLight_Dir_0` are
  given in that same object space.
- The palette is re-uploaded per primitive [INF]: there are 32 slots and a palette per PRIM.

---------------------------------------------------------------------------------------------------
## 4. .sce actor tracks

### 4.1 Animation track (`0xdcd2e210`, 40 bytes per key) [DATA]

Key times come from the track (common layout). Each value is:

| field | type | meaning | tag |
|---|---|---|---|
| f0 | u32 | REFS index; the entry must be ANIM (type 0x0302) | [DATA] |
| f1 | f32 | clip "in" point, normally f2 − f7 (0 on some first keys whose f2 > 0) | [INF] |
| f2 | f32 | clip time at which the full-weight part starts | [DATA] |
| f3 | f32 | = f5 when another key follows, else 0 (exception: Dance_Walk_03_MAN key 0) | [UNK] |
| f4 | f32 | 0 on first keys, = f2 on later keys | [UNK] |
| f5 | f32 | clip time at which the segment ends (can be ≫ clip length, which means looping) | [DATA] |
| f6 | f32 | f5 + f9 (end of the blend-out, in clip time; 0 if none) | [DATA] |
| f7 | f32 | blend-in duration in seconds; equals the previous key's f9 | [DATA] |
| f8 | f32 | **f5 − f2**, holds on every key; the scene time to the next key equals f8 | [DATA] |
| f9 | f32 | blend-out duration in seconds | [DATA] |

**REFS entry**: `{u16 type (0x0302 ANIM / 0x0306 MORPH), u16 entrySize, u64 hash, char name[], padding to entrySize}`.
- The ANIM hash is the bank hashcode of the .anm. It is the same value as the PNJ entity `CinAnimTracks[].Anim[].rsc`
  (sub 8).
- The old `scedump` parser misread the variable-size entries. `sceanim.py` is correct.

**Recommended sampling at scene time t** [INF, consistent with every key of the two scenes]:
1. Pick the last key k with T_k ≤ t.
2. Compute the clip time `u = (f2 − f7) + (t − T_k)`. Check against the data:
   - At T_k + f8 + f7, u reaches f5, which is when the next key starts.
   - Dance_03_MAN: key 1 at 16.76 starts at u = 0.03 and is at full weight at u = f2 = 0.3944 by t = 17.12.
   - Key 0 runs from u = f5 = 16.76 to f6 = 17.12 over the same interval.
3. During `[T_k, T_k + f7]` crossfade linearly with the previous key's clip.
   - The previous clip keeps advancing: `u_prev = (f2_prev − f7_prev) + (t − T_{k−1})`, up to its f6.
   - Weights: `w_new = (t − T_k)/f7`, `w_old = 1 − w_new`. Blend per bone with slerp/lerp.
4. Sample the .anm at `fmod(u, dur)` (§1.6).

Example (WOMAN_05, Default): key `[ref0 fi_dancestand1-w, f2 0.2, f5 98.12, f8 97.92]`, so u = 0.2 + t.

Full key lists: `/tmp/claude-0/av/def_anim.txt` (Default) and `gen_anim.txt` (generique).
- Generique: every dancer has one key at t = 0, f2 = 0 (Dance_Walk_01_WOMAN has f2 = 2), and f5 ≈ 34–68.
- Default: 1 to 4 keys per actor, with blends of 0.0024–0.364 s.

Exact engine semantics: [ENC] (CinScene.cpp).

### 4.2 Other actor tracks [DATA]

- Every actor object has exactly one key per track:
  - `position` (`d7e2d8c4`) = 3 floats;
  - `rotation` (`64c8d3b1`) = quaternion x,y,z,w (a pure Y rotation for every dancer);
  - `scale` = (1, 1, 1).
- Track `d2df4b2c` (1 float) always holds the bit pattern 0x34343434. It is unused filler.
- Dancer positions are y ≈ 30.7–31.4 in scene space (house floor). The PNJ entities themselves have world y ≈ 64.8, so the
  cinematic places them.

### 4.3 World placement [DATA + INF]

```
ActorWorld = S · R(q*) · T(pos) · Origin          (Origin = DATA+0x28 matrix, identity for C_INTRO)
skeleton ROOT space = actor space; pelvis offset comes from the clip (§1.5)
```

R(q*) uses the same convention as the skeleton and the camera (§2.2, intro_spec §7.8).

---------------------------------------------------------------------------------------------------
## 5. "Random" avatars

**Not random** (§3.1, [CODE] FUN_00da8bb0 / FUN_00dc2070 / FUN_00cb5bd0 / FUN_00b84590). In detail:

- **FUN_00cbd680**: config defaults. All skin colours are 1.0, the ClotheID entries are 0xFFFF, the colours are 0, and
  +0x2e = 0xff000000.
- **FUN_00dc2070**: deterministic lookup of the entity's ConfigName in DBPNJ. The only fallback is FUN_00445920 [ENC],
  and it is never reached for the six intro configs (all six exist).
- **FUN_00b84590(2000, id, cfg, …)**: creates and registers the avatar.
  - It sets the LOD and "BANDAGE" state, the visibility flag at +0x17c (|0x1e), and the update mode.
  - It draws no random number in the readable part.
- **Casino override**: the slot named `"WOMAN_"/"MAN_"` + digit (FUN_00da7b90: index from `DAT_00fdb570`), only for
  imported Casino profiles. It is irrelevant to OpenTDU2.

To get a deterministic look, use the DBPNJ rows of §3.4. The only data with randomness in its name is
`AVT_W_Random01…13` / `AVT_M_Random01…11`. Those are fixed .avt files that the DBPNJ rows reference.

---------------------------------------------------------------------------------------------------
## 6. Shaders and materials

### 6.1 Families, handlers and buckets [CODE + DATA]

| family (.sob) | GUID | handler (0x00f8fa10) | used by |
|---|---|---|---|
| TD_Skin2 | d5013ed0dbcdcdcd | 0x00d71220 | body zones (21 materials) |
| TD_Skinface | d5013ed0dbcddbca | 0x00d71220 | head skin (OCC/ASIA/AFRIK_HEAD) |
| TD_Avatar | d5013ed0db79dadb | 0x00d71220 | lashes, eyebrows, teeth, tongue, OCC_EYES_S |
| TD_Eye | d5013ed0db79bbbb | 0x00d71220 | EYES, OCC_EYES_B |
| TD_Clothe2 | d5013ed0db79aeae | 0x00d71220 | clothes |
| TD_Accessories2 | d5013ed0dba9daca | 0x00d70d50 | UNCHANGEABLE parts (buttons, metal), glasses |
| TD_Hair | d5013ed0dbdedede | **0x00d710f0** (missing from OpenTDU2's table) | hair, beards, hair_mat palette |
| TD_HELMET | 183157b81c6ccaca | 0x00d71220 | material looked up for the hair layer (FUN_00cac920) |

**FUN_00d71220** [CODE; matches OpenTDU2 `TD_Avatar`]:
- `K(0x1b) = 0`.
- Bucket 0xc AVATAR; with alpha test (M 0x35 == 0) bucket 0xe AVATAR_ALPHATEST and `K(0x25) = 1`.
- If blended (M 0x3a ≠ 6), bucket 0x1f AVATAR_ALPHA.

**FUN_00d710f0, TD_Hair** [CODE, `avatar_dec4.c`] (M = material states at MAT.+0x30, K = spec−0x10, as in OpenTDU2):

```
M(0x30) = 1;  K(0x1b) = 0;  NumTextureSlots(+0xfc) = 5
for slot 1..4: slot hash := 0xFFFFFFFF_FFFFFFFF (runtime shadow textures from DAT_011c9ed0[i]) when present,
               sampler bytes +0xe = (shadows on ? 2 : 1), +0xf = 1, +0x10 = 2, +0x11 = 2
if M(0x3a) != 6 (alpha blended):
    K(0x34) = shadowRT0 != 0; K(0x35) = shadowRT1 != 0; K(0x36) = 0; K(0x37) = shadowRT3 != 0;
    K(0x3f) = shadowsEnabled; b31 = K(0x31); b30 = K(0x30); K(0x31) = 1; K(0x30) = 1
    return 0x25 + 2*b30 + b31          → AVATAR_FULLALPHA_0..3 (buckets 0x25..0x28)
else:
    K(0x25) = (M(0x35) != 1); if (M(0x35) == 1 || M(0x37) == 0) M(0x35) = 1; K(0x31) = 1; K(0x30) = 1
    return 0xd                         → AVATAR_HAIR
```

The bucket ids index the layer-name table at 0xf7f604 (stride 0x10) [CODE/DATA]. In draw order:

| id | layer |
|---|---|
| 12 | AVATAR |
| 13 | AVATAR_HAIR |
| 14 | AVATAR_ALPHATEST |
| 31 | AVATAR_ALPHA |
| 37–40 | AVATAR_FULLALPHA_0..3 |
| 41 | OT_ACESSORIES_FULLALPHA |

Example: jane hair has D_* = spec 32+33 → bucket 0x28; B_01 = spec 32 → 0x27; A_01 → 0x25. That gives layered
hair-card sorting.

### 6.2 Constants [SHADER + DATA]

**Vertex shader** (all skinned families):
- `Var_Skin_VS` c148[96] (§3.5).
- `__World_View_Proj` c60–63, `__DirLight_Dir_0` c64, `__CamPos_ObjSpace` c65.
- `carinteg` c66 (x, y = height range for oD4.w = (y_obj − x)/(y − x); z), `atteFact` c67.
- TD_Avatar and TD_Eye VS also take `__AmbLight/Diffuse/Specular`, `__SpotLight_Pos_0` and `uvoff`.

**Pixel shader**:

| family | constants | samplers |
|---|---|---|
| TD_Skin2 | `__AmbLight_Ambient_0`, `__DirLight_Diffuse_0`, `__DirLight_Specular_0` (c9–11); `specParm`, `coeffs`, `skinclr`, `dclclr` (c12–15) | `gSkinSampler` s0, `gNormalSampler` s1, `gSpectralScatteringSampler` s3, `gGlossDetailCutSampler` s4, shadow s15 |
| TD_Skinface | adds `facialAnimCoef1/2` (from material symbols `COEFF1`/`COEFF2`) | adds `gNormalAgeExpressionSampler` s5, `gMask1/2Sampler` s6/s7 |
| TD_Hair | `specParm`, `primClr`, `scdClr` | `gHairSampler` |
| TD_Avatar | spot-light set, `specparm`, `color` | `gTexSampler` |

The `Var_Scene_PS` block is the usual one (fog, shadow slices, RT size).

**Where the values come from**:

| value | source |
|---|---|
| material symbols (`sym[]`: SPECPARM, COEFFS, SKINCLR, DCLCLR, UVSCALE, CARINTEG, ATTEFACT, PRIMCLR, SCDCLR, COLOR, UVOFF, BASECLR, CLRL1–3…) | **the 2DM parameter block** (MAT.+0x340), already parsed by `Render2DM` [DATA] |
| hair PRIMCLR/SCDCLR | the palette material (§3.3) [CODE FUN_00cb2080/FUN_00cb4dd0] |
| clothes BASECLR/CLRLn | the colour variant (§3.3) [INF] |
| TD_Skinface COEFF1/COEFF2 and SPECPARM | written at runtime by FUN_00cab140 for materials of GUID d5013ed0db79baba (facial animation and specular from the avatar) [CODE]; the details are not decoded; use the 2DM values |

Example body values (woman_body.2dm, TD_Skin2): SPECPARM (0.3, 3, 0.7), COEFFS (0.6, 0, 1), SKINCLR (1, .863, .867, 1),
DCLCLR (.431, .176, .039, 1), CARINTEG (0.35, 0.5, 1), UVSCALE (40, 48). The head (TD_Skinface) has SKINCLR
(.91, .28, .24, 1).

**.avt skin colours**: all intro .avt files have SkinAmbiant = SkinDiffuse = SkinSpecular = (1, 1, 1), except
AVT_Misc_W03 (0.95, 1, 1). Their SpecCoeff/Pow/RIM equal the 2DM SPECPARM. **Using the 2DM values as they are is
therefore exact for the intro**, up to the 5 % ambient tint of Pool_WOMAN_01 [DATA].

### 6.3 Runtime skin textures [CODE, partial]

- FUN_00cbe42f: a material texture slot of type `"SKIN"` (u64 'SKIN') is redirected to a runtime render target.
- FUN_00cb0d00 creates per-avatar layers `AVT_F_0x%p` / `AVT_B_0x%p` with an "Unnamed depth-buffer". FUN_00cac920
  creates `AVT_HAIR_0x%p` / `AVT_HAIR_PREPASS_0x%p`, with sort 0x1e0 and the TD_HELMET material.
- So the game **composites the skin texture at runtime** (face and body: base skin plus beard, make-up, age and
  freckle). The compositing pass was not decoded.

For the intro PNJ the inputs are the default ones: no tattoos; beard meshes are separate TD_Hair meshes. Binding the
2DM's own SKIN texture (`w_body_c`, `w_head_c`…) directly is the best available approximation [INF]. Freckles (.avt
non-zero on 4 women) and age (non-zero on 4 men) are lost. The head's `MASK1/MASK2` (`*_head_b1/b2`) and `N2`
(wrinkles) samplers are still bound from the 2DM.

Eyes: EyeID / EyebrowsID most likely select a UV offset in `eye_switch.2db` / `eyebrows_*.2db` (TD_Eye and TD_Avatar
take `uvOff`) [INF, not decoded]. Use the 2DM UVOFF.

---------------------------------------------------------------------------------------------------
## 7. Summary

**Fully known** [DATA / CODE / SHADER]:
- **.anm layout**: header, track kinds (1/2/4 bit mask, 8 = IK), u16/f32 time arrays, float, 48-bit and quantized
  values, and the 0x20 padded records. The byte accounting is exact on 40 of 40 clips, apart from the 4 IK tracks.
  Validated on fi_dancestand1-w (§1.7).
- **Bone binding**: by case-sensitive hash4; tracks replace the local values. Root motion lives in Pelvis.
- **XSB layout**, the quaternion convention (R(q*)) and the inverse-bind matrices (verified to 1e-4). Man 124 bones,
  woman 125. Bone names recovered (90 of 125).
- **3DG skinning**: palette in a BONE child of PRIM, UBYTE4 palette-local indices, 3 float weights plus an implicit
  4th, at most 31 bones. The c148 layout (3 columns per bone) and the shader math.
- **Avatar assembly**:
  - DBPNJ(ConfigName) gives the .avt (BodyID, eyes, skin) and the DBPNJ clothes and colours.
  - DBClothe._file gives the clothes bank `Avatar/CLOTHES/<dir>/<file>.bnk`.
  - The body-zone material ids and the zone flags.
  - The BodyID table; the hair colour palette in COMMON_CLOTHES/hair_mat.2dm.
- **Not random**: WOMAN_xx / MAN_xx are the six fixed DBPNJ "Intro Player" configs.
- **Shaders**: families, GUIDs and handlers (TD_Hair handler decoded; it is missing from OpenTDU2), render buckets,
  constants and samplers.
- **.sce Animation track**: f0 = REFS ANIM index, f8 = f5 − f2, f6 = f5 + f9, f7/f9 = blend in/out, and the clip-time
  mapping u = f2 − f7 + (t − T). REFS entry layout.

**Inferred** (reason given in the text):
- slerp/lerp interpolation; fmod looping with period `dur`; the w-sign bit (turntable evidence);
- zone flag 0 = hidden; colour index = `_mat.2dm` variant order; hair palette in file order with 14 = none;
- the meaning of f1, f3 and f4.

**Unknown or encrypted**:
- the sampler and blending code itself (edAnim, CinScene.cpp) [ENC];
- how non-closing walk clips loop [ENC];
- the runtime skin-texture compositing (AVT_F/AVT_B), which is readable but not decoded;
- the EyeID/EyebrowsID atlas mapping and the .avt key hashes (mapped by order);
- the TD_Skinface runtime COEFF1/2 values (FaceFX, can be ignored).

**Files needed by OpenTDU2**:
- `Euro/Bnk/Scenarics/Cinematics/intro/new_intro.bnk`: 40 .anm, 2 .sce, entities.xmb.
- `Euro/Bnk/avatar/common_bodies.bnk`: bodies, heads, XSB, skin maps (or `common_bodies_light.bnk`).
- `Euro/Bnk/avatar/config/pnj.bnk`: .avt files.
- `Euro/Bnk/avatar/clothes/common_clothes.bnk`: shared maps and hair_mat.2dm.
- The 52 clothes banks of §3.4 (`clothes/{hair,pnj,sportswear,streetwear,fashion,highlife,casino}/*.bnk`, list in
  `intro_pnj.txt`). They are now staged under `/mnt/user-data/uploads/TestDrive2/Euro/Bnk/avatar/clothes/`.
- `Euro/Shaders/TD_Skin2, TD_Skinface, TD_Avatar, TD_Eye, TD_Clothe2, TD_Accessories2, TD_Hair, td_helmet .sob`.
- The game database DBPNJ and DBClothe tables (`/tmp/claude-0/db.txt`, or `/tmp/claude-0/av/db_avatar.json`).
- Not needed: `avatar_face_fx.bnk`, `*.fxe` (FaceFX/morphs) and `styles.bnk` (only used for avatar creation).
