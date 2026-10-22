#pragma once

#include "core/math.h"

// .PMI instance placement lists of the island sectors (TestDrive2.exe
// FUN_00988d10 static V/O/S lists, FUN_009613e0 / FUN_00960f60 actors, see
// docs/re/pmi_spec.md).
//
//   RenderFile header ".PMI" 2.1, "MOBA" section (dataSize 0x1c0):
//     +0x20 u32 numMOB, f32 heightMin, f32 heightMax, pad, u8 cellCount[409]
//   numMOB x "MOB." sections:
//     +0x10 u64 model hashcode (library.3dd node or Entity__Actor.__Hc)
//     +0x18 u16 count, +0x1a u16 extraSize, +0x1c u16 ?, +0x1e u8 type
//           (1 static, 2 actor, 3 skinned static, 4 actor + payload)
//     +0x20 count x { u16[16] record, payload (extraSize rounded to 16) }
//   Record: x, z = w * 0.0156252384 - 128 (sector local), y = heightMin +
//   w1 (max - min) / 65535, scale = w * 4 / 65535, angles X / Y / Z =
//   w * 2 pi / 65535, w9 quadtree cell, AABB = w * 0.0625009537 - 1024
//   (relative to the position).
//   Matrix (D3D row vectors): Scale * RotX * RotZ * RotY, translation =
//   sector origin + position.
struct PmiInstance {
    uint64_t Hashcode = 0;
    uint8_t Type = 0;
    Matrix4x4f World; // OpenTDU2 convention (column c = D3D row c)
    Vector3f Position;
    Vector3f BoxMin;  // world space AABB (degenerate for actors)
    Vector3f BoxMax;
    const uint8_t *pPayload = nullptr;
    uint32_t PayloadSize = 0;
};

bool ParsePmi( const uint8_t *pFile, uint32_t fileSize, const Vector3f &sectorOrigin,
               std::vector<PmiInstance> &outInstances );
