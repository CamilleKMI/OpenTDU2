#pragma once

#include "core/math.h"
#include "render/render_api.h"
#include "render/render_file.h"

// Vertex format used by the OpenTDU2 mesh renderer (converted at load time).
struct MeshVertex {
    float Position[3];
    float Normal[3];
    float Texcoord[2];
    uint32_t Color; // RGBA8 (baked ambient occlusion / vertex color)
};
OTDU_SIZE_MUST_MATCH( MeshVertex, 36 );

// Attribute of the original vertex streams (kept for the original .sob shaders).
struct RawVertexAttribute {
    uint8_t Usage = 0;      // D3DDECLUSAGE
    uint8_t UsageIndex = 0;
    uint8_t Stream = 0;
    eVertexAttributeFormat Format = VAF_Invalid;
    uint16_t Offset = 0;
};

struct GeometryPrimitive {
    uint16_t MaterialIndex = 0; // Index in the instance material bank (3DD MBNK)
    // Original vertex streams (D3D layout, not mirrored).
    std::vector<uint8_t> RawStreams[2];
    uint32_t RawStrides[2] = { 0, 0 };
    std::vector<RawVertexAttribute> RawAttributes;
    uint32_t NumRawVertex = 0;
    uint16_t PrimitiveType = 0; // low byte: ePrimitiveType (7: heightfield, no index buffer)
    std::vector<MeshVertex> Vertices;
    std::vector<uint32_t> Indices; // Triangle list
    std::vector<uint8_t> BoneIndices; // First bone of each vertex (skinned primitives only)
    // Skinned primitives: PRIM type high byte = palette size, child BONE
    // section { u32 skeleton bone index[] } (palette local blend indices).
    std::vector<uint32_t> BonePalette;
};

struct Geometry {
    uint64_t Hashcode = 0;
    std::vector<GeometryPrimitive> Primitives;
    Vector3f BoundsMin;
    Vector3f BoundsMax;
};

// .3DG (TDU2, version 3.4): geometry library.
//
//   GEOA (container)
//     HASH: { u64 Hashcode; u32 pGeometry (file offset of GEOM); u32 pad }[]
//     GEOM (one per geometry) -> PRIM children (0x40 bytes payload):
//        +0x00 u32 pIndexBuffer (file offset of a DXIB section)
//        +0x04 u32 pVertexBuffer (file offset of a DXVB section)
//        +0x10 u16 MaterialIndex
//        +0x12 u16 PrimitiveType (4: triangle strip)
//        +0x1C u32 NumVertex
//        +0x28 u32 NumIndex
//        +0x38 packed bounding data
//   VBA. (container) DXVB: +0x10 u32 NumVertex, +0x14 u8[12][2] { count, eVertexAttributeFormat }
//        per semantic (position, normal, diffuse, specular, uv, tangent,
//        binormal, bone indices, bone weights...), +0x2C u32 flags, +0x30 data.
//        Stream 0 holds the positions, stream 1 every other attribute
//        (interleaved, semantic order).
//   IBA. (container) DXIB: +0x10 u32 NumIndex, +0x14 u32 IndexSize (1: 16 bits), +0x20 data
//   MORA (morph targets), REAL (relocations)
class Render3DG {
public:
    inline const std::unordered_map<uint64_t, Geometry> &getGeometries() const {
        return geometries;
    }

public:
    // bMirrorX converts from the original left handed space (D3D) to the
    // right handed OpenTDU2 world space.
    bool parse( const uint8_t *pFile, uint32_t fileSize, bool bMirrorX = true );
    const Geometry *find( uint64_t hashcode ) const;

private:
    std::unordered_map<uint64_t, Geometry> geometries;

    bool parsePrimitive( const uint8_t *pFile, uint32_t fileSize, uint32_t primitiveOffset,
                         bool bMirrorX, GeometryPrimitive &outPrimitive );
};
