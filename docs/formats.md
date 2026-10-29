# Test Drive Unlimited 2 — file formats (as implemented in OpenTDU2)

All structures are little endian unless stated otherwise. Offsets are relative to the start of the
structure. Everything below was validated against the PC "Euro" data (unpacked layout).

## Hashcodes

Names are hashed with `TestDriveConcatToString8` (same as TDU1): uppercase characters are summed into 8 byte
slots (`GetIdentifier64bit` in `shared.h`). Names of 8 characters or less are recoverable
(`IdentifierToString`), longer names are lossy (e.g. `BUMP_F_HR` shows as `?UMP_F_H`).

## BNK (banks)

* `KNAB` magic at +8, TDU1-like header (0x40 bytes), absolute section offsets.
* Section descriptor: 0x14 bytes `{ u32 Offset, u32 Size, u32 Date, u32 CRC, u32 Alignment }`
  (TDU1 used 0x10 bytes; both are supported).
* File types: `{ u32 SubType, u32 Group }`.
* Directory: prefix tree. Inner node = negative length + varint child count, leaf = positive length.
  Components are joined with `\`, the `.ext` component is moved to the end of the path.
* File indexes are u8/u16/u32 depending on the number of files.

## XMB (`XMBF`)

Binary XML-like container used by the database, key maps, shader libraries...

* Header: `+8 strings offset, +0xC types offset, +0x10 data offset, +0x18 root type ref (index * 2)`.
* Type records (8 bytes) `{ u32 typeWord, u32 nameOffset }`. Composite when `(lo & 0xF) == 0`:
  `numFields = (lo >> 4) & 0x7FF`, `size = hi * 2 + (lo & 0x8000 ? 1 : 0)`.
* Field records reference types by `index * 2`, bit 31 = array. In little-endian files the two words of
  field records are swapped.
* Primitive codes 0..13: OBJECT, BOOL, SINT8, SINT16, SINT32, SINT64, UINT8, UINT16, UINT32, UINT64, FLOAT,
  DOUBLE, STRING (u32 data relative offset), VIRTUAL. Arrays are `{ u32 count, u32 offset }`.
  Data is packed (unaligned). Padding records are `0x41414141`.
* `.sob` files are big endian XMB, the others little endian.

## CPR (encrypted files)

XTEA, 32 rounds (`sum = 0xC6EF3720`, `delta = 0x61C88647`), used as a keystream:
`Plain[i] = Cipher[i] ^ XTEA_decipher(Cipher[i + 1])` on 64-bit blocks, the last 8 bytes are dropped.
Key `{ 0x4FE23C4A, 0x80BAC211, 0x6917BD3A, 0xF0528EBD }` (TestDrive2.exe `DAT_00f763bc`,
`FUN_0095eb40` / `FUN_0095ea60`).

* `GamePC.cpr`, `SystemPC.cpr`, `Physics.cpr`: INI text.
* `db_data.cpr`: XMB `DBBase` (e.g. `DBCarPhysicsData`, 286 entries).
* `db_<lang>.cpr`: XMB `DBData { Line db_data[] { Int32 HashCode; String Text; } }`.
  Useful ids: 938 "Press START", 1394 "New profile", 1386 "Profiles", 1219 "Player profile",
  772 "Invalid name", 4024 "Avatar creation", 4025/4026/1011 gender / ethnic group / character.

## Render files (2DB, 3DG, 3DD, 2DM)

Common header `{ u16 major, u16 minor, u32 flags, u32 size, u32 magic }` then sections
`{ u32 type, u16, u16, u32 size, u32 dataSize }` (children when `size > dataSize`).

### 2DB (textures)

`BMAP` section, texture header (0x30) at file offset 0x20: `u64 hash, u16 width, height, depth, u8 mips,
u8 packed, u32 flags, ..., u32 format (+0x1C)`. Pixel data at 0x50. Formats follow TDU1's `eViewFormat`
(51 = SRGB_DXT1, 55 = SRGB_DXT5...).

### 3DG v3.4 (geometry)

`GEOA { HASH (u64 hash, u32 ptr GEOM)[], GEOM { PRIM ... } }`, `VBA { DXVB }`, `IBA { DXIB }`, `MORA`, `REAL`.

* `PRIM` (0x40): `+0 IB offset, +4 VB offset, +0x10 u16 material index, +0x12 u16 type`
  (low byte 4 = triangle strip; high byte = bone palette size of skinned primitives),
  `+0x1C numVertex, +0x28 numIndex`.
* `DXVB`: `+0x10 numVertex`, `+0x14` 12 × `{ u8 count, u8 format }` for the semantics Position, Normal,
  Diffuse, Specular, Texcoord, Tangent, Binormal, BoneIndices, BoneWeights..., `+0x2C` layout flags,
  data at `+0x30` as **two consecutive streams** (interleaved inside each stream, semantic order):
  * `0x0100`: stream 0 = position
  * `0x0200`: stream 0 = position + bone indices + weights (skinned, bind pose)
  * `0x1000`: stream 0 = position + texcoords
  * `0x2000`: stream 0 = position + texcoords + bone indices + weights
  * stream 1 = every other attribute.
  Formats: 0x00 float3 position, 0x0B normal half4, 0x02 BGRA8 color (baked AO), 0x0C float2 uv,
  0x17 tangent half4, 0x11 u8×4 bone indices, 0x12 float weight.
* `DXIB`: `+0x10 numIndex`, u16 indices at `+0x20`.

Geometry is D3D left handed (+Z forward). OpenTDU2 mirrors X to get a right handed space (and swaps the
triangle winding).

### 3DD v2.0 (scene)

`MBNA { MBNK { u64 materialHash, u64 pad }[] }`, `OBJA { HASH, OBJ. }`, `INSA { HASH, INS. }`,
`HIEA { HASH, HIE. }`, `REAL`.

* `INS.`: 4×4 row-major matrix, `+0x44` material bank (MBNK) pointer, `+0x50` pointer to the OBJA hash entry.
* `HIE.`: `{ u32, u32 INS pointer, u32 flags (low 16 bits = descendant count), u32 parent HIE pointer }`.
  Simple scenes (rims) have no `HIEA`: every instance is a root node.
* Car hierarchy: `D_CAR > BODY_HR/MR/LR, COCK_LR, DOOR_*, FORK_xx > DISC_xx > D_RIM_xx (wheel centers), HANIM_S
  (skinned convertible roof), D_SBOX (physics helpers)...`

### 2DM v4.0 (materials)

`MAT.` payload: `+0 hash, +0x10 name (32), +0x30 render states (depth test/write, cull (1 none, 2 back,
3 front after the LH→RH conversion), alpha test/ref, src/dst blend, blend op), +0x100 8 texture slots
{ u64 slot type, u64 texture hash }, +0x340 parameters { u64 name, u16 type, u16 flags, u32 size }`
followed by float4 values (e.g. `RAMPCLR1` paint color).

### PRT v0x1b (particle systems) and BFX (fx graphs)

`.prt`: a `.PRT` section (force block, generator, sprite renderer, curve set found by their type identifiers in
the name table) + a `MAT.` section (u64 material hash of the PrtMat `.2DM`, shader TD_Particle). `.bfx`: the edFx
node tree of an `Fx__SourceData` (sound " DNS" nodes, particle nodes with a transform and the .prt system id).
Layout and the meaning of each field: `docs/re/particles_spec.md` (the engine itself is encrypted).

## VMF (Eden flash movies, `FrontEnd/<Res>/*.bnk`)

`FMV` + version, stage size in twips, fps 8.8, frame count, optional preload table, then SWF style records.
Eden specific tags:

* 125 `DefineFontEden`: `u16 id, u16 textureId, u8 flags, name\0, u16 numGlyphs, u16 numTextured`,
  `numTextured` × 26-byte glyphs `{ u16 code, f32 u0, u1, v0, v1, i16 w, h, xOffset, yOffset }` mapped on
  `fla_<id>.2db` (1024 units per em), then (if `textureId`) a second padded glyph table for
  `fla_<textureId>.2db`, then a DefineFont3-like layout: shape offsets, codes, `i16 ascent, descent,
  leading`, advances.
* 126 `DefineBitmapEden`: `u16 id, u16 w, u16 h, f32 u0, v0, u1, v1` (texture `fla_<id>` or an atlas page).

## SOB (shader libraries, `Euro/Shaders`)

Big endian XMB `ShaderSet { name, guid, versions, Profile profArr[] }`, profile 2 = vs_3_0, 6 = ps_3_0,
`Attrib { frame, scene, skin, matID[], dynID[], symID[], litID[], bin[] }` (`bin` = D3D9 bytecode with CTAB),
`Item { u64 permutation, u16 attrib, u8 spec[48] }`.

OpenTDU2 translates the bytecode with DXVK's DXSO compiler. Binding model of the resulting SPIR-V:
set 0: b0 VS constants (`int4 i[16]; float4 c[256]`), b3 clip planes, b10 PS constants
(`int4 i[16]; float4 c[224]`), b12 shared PS state, b13+ sampled images, b31 specialization state UBO;
set 1: sampler heap; push constants: D3D9 render state.

`TD_Flash.sob` (2D layer): VS #2 = position/color/texcoord, `c60/c61` 2D transform rows,
`c17..c20` projection; PS #3 = `texld s0 * color`, PS #7 = alpha-only text.
