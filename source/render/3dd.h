#pragma once

#include "core/math.h"
#include "render/render_file.h"

// .3DD (TDU2, version 2.0): scene description (hierarchy of instances).
//
//   MBNA: MBNK sections = material banks { u64 MaterialHashcode; u64 pad }[]
//   OBJA: HASH { u64 Hashcode; u32 pObject; u32 pad }[] + OBJ. (one per object,
//         the object hashcode is also the hashcode of its .3DG geometry)
//   INSA: HASH + INS. instances:
//         +0x00 float[16] local matrix (D3D row-major, row vectors)
//         +0x44 u32 pMaterialBank (file offset of a MBNK section)
//         +0x50 u32 pObjectHashEntry (file offset of an OBJA HASH entry)
//   HIEA: HASH + HIE. hierarchy nodes { u32; u32 pInstance; u32 flags
//         (low 16 bits: number of descendants); u32 pParentNode }
//   REAL: relocations
struct SceneNode {
    uint64_t Hashcode = 0;
    std::string Name;
    Matrix4x4f Local;
    Matrix4x4f World; // Rest pose (parent chain applied)
    int32_t Parent = -1;
    uint64_t ObjectHashcode = 0;   // 0: dummy node
    std::vector<uint64_t> Materials;
    // INS. +0x4e u16 numLOD, +0x50 + k * 0x14 { u32 pObjectHashEntry; f32
    // distance (squared, x (16/9)^2 at runtime: FUN_00da0440) }.
    std::vector<float> LodDistances;
};

class Render3DD {
public:
    inline const std::vector<SceneNode> &getNodes() const { return nodes; }

public:
    bool parse( const uint8_t *pFile, uint32_t fileSize, bool bMirrorX = true );
    int32_t findNode( const char *pName ) const;
    bool isDescendantOf( int32_t nodeIndex, int32_t ancestorIndex ) const;

private:
    std::vector<SceneNode> nodes;
};
