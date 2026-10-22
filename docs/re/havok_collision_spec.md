# TDU2 Havok collision files (.shk / .dhk): spec

Tags: [DATA] read directly from the file bytes, including the hkClass reflection embedded in each file. [INF] inferred, e.g. from Havok SDK naming conventions or because the data is consistent with it. [UNK] not decoded.

Files checked:
- `intro_lph_l3di_57E5C829CCD9F2C6.shk` (house, 116448 B, from new_intro.bnk)
- `garage_int_57E5C829CCD9F2C6.shk` (64064 B) and `garage_int_57E5C829CCD9F2C6.dhk` (36656 B), from garage.bnk

Prototype scripts are in `/tmp/claude-0/havok/`:
- `hkpack.py`: packfile, sections and fixups
- `hkclass.py`: dumps the embedded hkClass reflection
- `dump.py`: walks bodies and shapes, writes OBJ
- `plot.py`, `occ.py`: top-down plots

Outputs: `house.obj`, `garage_s.obj`, `garage_d.obj`, plus `house.png`, `occ_house.png`, `floor_house.png` and `garage.png`.

## 1. Eden wrapper
| off | type | meaning |
|---|---|---|
| 0x00 | u8[2] | `02 02` [DATA] |
| 0x02 | u16 | entry count N [DATA]. For .shk, N equals the number of rigid bodies (house 114, garage 44). |
| 0x04 | u16 | 0 in .shk; 0x0280 in .dhk [DATA] [UNK] |
| 0x08 | u32 | 0 in .shk; 0x510 in .dhk (size of the extra block, see below) [DATA] [UNK] |
| 0x18 | u32 | **packfile size**. The packfile is the end of the file, so packfile offset = fileSize − this value [DATA] (holds for all 3 files: 0xe60, 0x5a0, 0x970). |
| other | | 0xcd fill |

Entries start at 0x20, 0x20 bytes each: { u64 hash, u32 index (0..N-1 in sequence), u32 type, 0xcd×4, 0×12 } [DATA].
- In .shk, every entry has type 3.
- In .dhk, the 14 entries have types 0..3, and several entries carry `"Phys"` plus a small id in their hash field [DATA].
- What the hashes mean: [UNK]. They are probably entity/name hashes per body [INF].

The .dhk has an extra block between the entries and the packfile (0x1e0..0x970): u32 0, u32 1, …, u32 6 (= the number of bodies), followed by 0x80-byte records. Each record looks like:
- a 3×vec4 identity rotation
- a vec4 centre of mass
- a 3×vec4 inertia tensor
- a float mass (for example 1301.0)
- u32 4

This layout is [INF]. The reader does not need this block.

## 2. Packfile (Havok-5.5.0-r1) [DATA]
Header (0x40):

| off | field | value |
|---|---|---|
| 0x00 | magic | 57e0e057 10c0c010 |
| 0x08 | userTag | 0 |
| 0x0c | fileVersion | 5 |
| 0x10 | layoutRules | 4,1,0,1 (ptr=4, little endian, reusePadding=0, emptyBaseOpt=1) |
| 0x14 | numSections | 3 |
| 0x18 / 0x1c | contentsSectionIndex / contentsSectionOffset | 2 / 0 (the root object is the hkpPhysicsSystem at data+0) |
| 0x20 / 0x24 | contentsClassNameSectionIndex / contentsClassNameSectionOffset | 0 / 0x9a or 0xe4 (the classname "hkpPhysicsSystem") |
| 0x28 | contentsVersion | "Havok-5.5.0-r1\0", then 0xff padding |

Section headers start at 0x40, 0x30 bytes each:
- char tag[19], u8 0
- u32 absoluteDataStart (relative to the packfile start)
- localFixupsOffset, globalFixupsOffset, virtualFixupsOffset, exportsOffset, importsOffset, endOffset (all relative to absoluteDataStart)

The sections are `__classnames__`, `__types__` and `__data__`.
- Object data runs from section start to localFixupsOffset.
- Local fixups are {i32 src, i32 dst}, both in the same section. A src of −1 is padding.
- Global fixups are {i32 src, i32 dstSection, i32 dst}.
- Virtual fixups are {i32 objectOffset, i32 classnameSection, i32 classnameOffset}. Each one gives the class of the object at that offset.
- In `__classnames__`, each entry is {u32 signature, u8 0x09, char name[]\0}. The classname offset points at name[].
- In these files, pointers inside `__data__` go through local and global fixups whose targets are always section 2.

`__types__` holds the full **hkClass / hkClassMember reflection**:
- hkClass is 0x30 bytes: name*, parent*, objectSize, numIfaces, enums*, numEnums, members*, numMembers, defaults*, attributes*, flags, version.
- hkClassMember is 0x18 bytes: name*, class*, enum*, u8 type, u8 subtype, i16 cArraySize, u16 flags, u16 offset, attributes*.
- Member flag 0x400 is serialize-ignored [INF from usage].

**All member offsets below come from this reflection [DATA].** Run `hkclass.py <file> <Class>` to dump any class.

## 3. Object layouts (32-bit)
Notes that apply to all the layouts below:
- hkArray = {T* data, i32 size, i32 capAndFlags} (0xc bytes). hkSimpleArray = {T*, i32 size} (8 bytes).
- hkTransform = 3 **column** vec4 (rotation) followed by a translation vec4 (0x40 bytes). The column-major order is [DATA]: it was validated by hinge pivots. pivotA in body A and pivotB in body B coincide in world space to 1e-6 with column-major; with row-major they are off by metres.
- hkVector4 is 16 bytes.

### hkpPhysicsSystem (0x44)
| off | member |
|---|---|
| +0x08 | rigidBodies hkArray<hkpRigidBody*> |
| +0x14 | constraints hkArray<hkpConstraintInstance*> |
| +0x20 | actions |
| +0x2c | phantoms |
| +0x38 | name cstring (null here) |
| +0x3c | userData |
| +0x40 | active |

### hkpRigidBody (0x200)
| off | member |
|---|---|
| +0x10 | collidable (hkpLinkedCollidable). **+0x10 = collidable.shape (hkpShape\*)** |
| +0x70 | name (null in all three files) |
| +0x80 | material {i8 responseType, +0x84 friction, +0x88 restitution} |
| +0xd0 | motion (hkpMaxSizeMotion, 0x120). **+0xd8 = motion.type (u8)** |
| +0xe0 | **motion.motionState.transform (hkTransform)** |
| +0x120 | sweptTransform |

Motion type values:
- 7 for every body in the .shk files, which is FIXED in Havok's enum [INF].
- 4 in the .dhk, which is BOX_INERTIA [INF].

### hkpShape base (0x10)
vtable, memSizeAndFlags, refCount, +0x08 userData, +0x0c type (not serialized).

hkpConvexShape adds +0x10 radius (the convex radius, 0.01 everywhere).

### hkpBoxShape (0x30)
+0x20 halfExtents (vec4).

### hkpCapsuleShape (0x40)
+0x20 vertexA, +0x30 vertexB, +0x10 radius.

### hkpMoppBvTreeShape (0x40)
- +0x10 code (hkpMoppCode\*)
- +0x30 child: an hkpSingleShapeContainer {vtable, childShape\*}. So **+0x34 = child shape**.

### hkpConvexTransformShape (0x60)
- +0x14 childShape container. **+0x18 = child shape\***.
- **+0x20 transform (hkTransform)**

### hkpListShape (0x70)
- +0x18 childInfo hkArray<hkpListShapeChildInfo>. Each child info is 0x10 bytes: shape\* at +0, filterInfo at +4, shapeSize at +8, numChildShapes at +0xc.
- +0x30 aabbHalfExtents
- +0x40 aabbCenter
- +0x50 enabledChildren u32[8]

### hkpExtendedMeshShape / hkpStorageExtendedMeshShape (0xe0)
| off | member |
|---|---|
| +0x18 | numBitsForSubpartIndex (12) |
| +0x20 | scaling vec4 (1,1,1) |
| +0x30 | aabbHalfExtents |
| +0x40 | aabbCenter |
| +0x50 | trianglesSubparts hkSimpleArray<TrianglesSubpart>. Serialized right after the object at shape+0xe0. |
| +0x58 | shapesSubparts (0 in all files) |
| +0x60 | weldingInfo hkArray<u16> |
| +0x6c | weldingType |
| +0x70 | embeddedTrianglesSubpart |
| +0xb0 | triangleRadius |
| +0xc0 | **meshstorage hkArray<MeshSubpartStorage\*>** (one per triangle subpart, in the same order) |
| +0xcc | shapestorage |

### hkpExtendedMeshShape::TrianglesSubpart (0x40)
| off | member |
|---|---|
| +0x00 | type (0 = triangles) |
| +0x01 | materialIndexStridingType |
| +0x02 | materialStriding |
| +0x04 | materialIndexBase\* (not serialized) |
| +0x08 | materialIndexStriding |
| +0x0a | numMaterials |
| +0x0c | materialBase\* (not serialized) |
| +0x10 | **numTriangleShapes** |
| +0x14 | vertexBase\* (**not serialized**) |
| +0x18 | **vertexStriding (16)** |
| +0x1c | **numVertices** |
| +0x20 | extrusion vec4 (0) |
| +0x30 | indexBase\* (**not serialized**) |
| +0x34 | **indexStriding (8)** |
| +0x38 | **stridingType (1 = INT16** [INF name], data: indices16 is filled and indices32 is empty**)** |
| +0x39 | flipAlternateTriangles (0) |
| +0x3c | triangleOffset (0) |

There is **no per-subpart transform in 5.5** [DATA: no such member].

### hkpStorageExtendedMeshShape::MeshSubpartStorage (0x50)
| off | member |
|---|---|
| +0x08 | vertices hkArray<hkVector4> |
| +0x14 | indices16 hkArray<u16> |
| +0x20 | indices32 hkArray<u32> |
| +0x2c | materialIndices hkArray<u8> |
| +0x38 | materials hkArray<u32> |
| +0x44 | materialIndices16 |

Indices are 4 u16 per triangle (stride 8). The 4th index is always 0 (padding) [DATA]. Storage vertex count equals subpart numVertices, and every index is less than numVertices [DATA, checked].

### hkpConstraintInstance (0x2c)
- +0x0c data\*
- +0x10 modifiers\*
- +0x14 entities[2] (A, B)
- +0x1c priority
- +0x20 name

### hkpLimitedHingeConstraintData (0xe0)
+0x10 atoms (hkpLimitedHingeConstraintDataAtoms, 0xd0):
| off | member |
|---|---|
| +0x00 | transforms (hkpSetLocalTransformsConstraintAtom 0x90: transformA at +0x10, transformB at +0x50) |
| +0x90 | angMotor |
| +0xa4 | angFriction |
| +0xb0 | angLimit {u16 type, u8 isEnabled, u8 limitAxis, f32 minAngle, f32 maxAngle, f32 tau} |
| +0xc0 | 2dAng |
| +0xc4 | ballSocket |

The hinge axis is column 0 of transformA/B, and the pivot is their translation [INF: Havok convention; the pivots match in world space].

## 4. Contents (verified)
| file | bodies | triangle meshes (MOPP→StorageEMS) | boxes | capsules | other |
|---|---|---|---|---|---|
| house .shk | 114 (all fixed) | 5 meshes, 195 verts / 207 tris | 97 | 12 | — |
| garage .shk | 44 (all fixed) | 3 meshes, 99 verts / 116 tris | 41 | 0 | — |
| garage .dhk | 6 (type 4) | 0 | 17 (1 hkpListShape of 1 box + 10 hkpConvexTransformShape→box, + 5 boxes) | 0 | 6 limited hinges |

Shapes are shared: several bodies point to the same hkpBoxShape.

## 5. Coordinate frame [DATA]
Vertices are stored in **body-local** space. Each rigid body has its own hkTransform (motionState.transform).
- Several mesh bodies have identity transforms, for example the house floor mesh body 36 at T=(0,0,0).
- Others are translated, for example house body 35 T=(10.15, 11.07, −17.71) and garage floor T=(−9.19, −0.02, 17.50).

After applying the body transform, every file is in **one common frame equal to the scene-local frame**:
- **House**: the floor mesh is at y = 31.12 (25 vertices at 31.12, the main floor) [DATA]. That matches the cinematic floor 31.11, so this is house-local, the same frame as the .sce and the entities. Additional details:
  - The east wing floor is at ≈31.42–31.57.
  - Wall boxes span y 31.05..35.45.
  - Some exterior geometry (fences, terrain block) sits around y ≈ 0..1.3, far outside the house at z ≈ −20..−43.
- **Garage**: the floor quad is at y = −0.018 [DATA], so garage-local with the floor at ≈0. The car spot (−4.385, 0.084, 15.553) lies inside the open floor area:
  - FindHavokFloor finds y = −0.018 there.
  - Horizontal rays from that spot hit walls at +x 17.8 m, −x 17.5 m, +z 23.0 m and −z 9.0 m.
- **.dhk**: its 6 bodies form a car-shaped compound at (−4.17, 0.56, 15.88), rotated about 146° around Y. This is the car on its spot. The hinged parts are 2 doors and the hood/trunk (limits 0..0.175 / 0..1.484 rad etc.). It is the same garage-local frame.

## 6. Plausibility checks
- **House layout**: see `occ_house.png` (solid cross-section at y = 32.8) and `floor_house.png`.
  - Outer walls are at x ≈ −20 / +9.5…25 and z ≈ −16 / +13.5.
  - The two large solid boxes (x −12..1.2, z −3.6..4 and z −9.5..−6, height 31.13..36.18) sit **exactly over the two holes of the floor mesh**. They are invisible blockers over an open patio or void, not geometry errors.
- **Garage door entity (20.24, 33.11, 3.21)**:
  - It sits at the end of the wall box x 20.30..25.17 / z 3.23..4.57. On its other side is the gap at x 18.7..20.3 between that wall and the wall x 17.5..18.7 / z 1.1..2.2, so it is at a wall opening, at the jamb.
  - Directly under it is a step box with its top at y = 32.02. That is consistent with a door whose centre is at 33.11 [INF].
- **Garage**: walls enclose about x −22..15, z −1..39. The floor quad covers x −22..17.6, z −1.4..36.4.

## 7. C++ (OpenTDU2)
Files: `source/physics/havok_collision.h` and `source/physics/havok_collision.cpp`. They are picked up by the CONFIGURE_DEPENDS glob, so there was no CMake change.

- `LoadHavokCollision(data, size, out, bStaticOnly=false)`: everything is flattened to the file frame. Output contains:
  - Vertices/Indices (triangle list)
  - Boxes {Transform, HalfExtents, ConvexRadius, BodyIndex}
  - Capsules {A, B, Radius}
  - Bodies {Transform, MotionType, Friction, primitive ranges}
  - Hinges
  - Bounds
- `RaycastHavokCollision`: brute force over triangles (two-sided), OBB slab test and capsules.
- `FindHavokFloor`: a downward ray.
- `ResolveSphereHavokCollision`: iterative push-out using closest points on triangles, OBBs and capsule segments.

The C++ counts match the Python ones (114/195/207/97/12 and 44/99/116/41; dhk 6 bodies, 17 boxes, 6 hinges).
