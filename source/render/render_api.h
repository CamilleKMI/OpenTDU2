#pragma once

#include "core/math.h"

enum class eShaderType : int32_t {
    ST_Vertex = 0,
    ST_Pixel = 1,

    ST_Count,
    ST_Invalid = -1
};

// Texture formats (same enumeration as TDU1, stored in .2DB headers).
enum eViewFormat : uint32_t {
    VF_R8G8B8 = 0,
    VF_R8G8B8A8 = 1,
    VF_X8R8G8B8 = 2,
    VF_A8R8G8B8 = 3,
    VF_R5G6B5 = 4,
    VF_A1R5G5B5 = 5,
    VF_A8 = 6,
    VF_A8R3G3B2 = 7,
    VF_A2R10G10B10 = 8,
    VF_G16R16 = 9,
    VF_A16B16G16R16 = 10,
    VF_L8 = 11,
    VF_L16 = 12,
    VF_V8U8 = 13,
    VF_Q8W8V8U8 = 14,
    VF_V16U16 = 15,
    VF_Q16W16V16U16 = 16,
    VF_R16F = 17,
    VF_G16FR16F = 18,
    VF_A16B16G16R16F = 19,
    VF_R32F = 20,
    VF_G32R32F = 21,
    VF_A32B32G32R32F = 22,
    VF_DXT1 = 23,
    VF_DXT2 = 24,
    VF_DXT3 = 25,
    VF_DXT4 = 26,
    VF_DXT5 = 27,
    VF_SRGB_R8G8B8 = 28,
    VF_SRGB_R8G8B8A8 = 29,
    VF_SRGB_X8R8G8B8 = 30,
    VF_SRGB_A8R8G8B8 = 31,
    VF_SRGB_R5G6B5 = 32,
    VF_SRGB_A1R5G5B5 = 33,
    VF_SRGB_A8 = 34,
    VF_SRGB_A8R3G3B2 = 35,
    VF_SRGB_A2R10G10B10 = 36,
    VF_SRGB_G16R16 = 37,
    VF_SRGB_A16B16G16R16 = 38,
    VF_SRGB_L8 = 39,
    VF_SRGB_L16 = 40,
    VF_SRGB_V8U8 = 41,
    VF_SRGB_Q8W8V8U8 = 42,
    VF_SRGB_V16U16 = 43,
    VF_SRGB_Q16W16V16U16 = 44,
    VF_SRGB_R16F = 45,
    VF_SRGB_G16FR16F = 46,
    VF_SRGB_A16FB16FG16FR16F = 47,
    VF_SRGB_R32F = 48,
    VF_SRGB_G32R32F = 49,
    VF_SRGB_A32B32G32R32F = 50,
    VF_SRGB_DXT1 = 51,
    VF_SRGB_DXT2 = 52,
    VF_SRGB_DXT3 = 53,
    VF_SRGB_DXT4 = 54,
    VF_SRGB_DXT5 = 55,
    VF_HDR = 56,
    VF_Envmap1 = 57,
    VF_Envmap0 = 58,
    VF_ATI2 = 61,

    VF_Count,
    VF_Invalid = 0xffffffff
};

// Per-attribute storage format of a .3DG vertex stream.
enum eVertexAttributeFormat : uint8_t {
    VAF_POSITION_R32G32B32_SFLOAT = 0x0,
    VAF_POSITION_R16G16B16A16_SFLOAT = 0x1,
    VAF_COLOR_B8G8R8A8_UNORM = 0x2,
    VAF_COLOR_B8G8R8A8_UNORM_2 = 0x3,
    VAF_COLOR_B8G8R8A8_UNORM_3 = 0x4,
    VAF_COLOR_B8G8R8A8_UNORM_4 = 0x5,
    VAF_NORMAL_R32G32B32_SFLOAT = 0x6,
    VAF_POSITION_R32G32B32A32_SFLOAT = 0x7,
    VAF_UNUSED_2 = 0x8,
    VAF_NORMAL_A2B10G10R10_SNORM_PACK32 = 0x9,
    VAF_NORMAL_R16G16B16A16_UNORM = 0xa,
    VAF_NORMAL_R16G16B16A16_SFLOAT = 0xb,
    VAF_TEXCOORD_R32G32_SFLOAT = 0xc,
    VAF_TEXCOORD_R16G16_SNORM = 0xd,
    VAF_TEXCOORD_R16G16_SFLOAT = 0xe,
    VAF_POSITION_R32_SFLOAT = 0xf,
    VAF_UNUSED_3 = 0x10,
    VAF_BONEINDEX_R8G8B8A8_USCALED = 0x11,
    VAF_BONEWEIGHT_R32_SFLOAT = 0x12,
    VAF_BONEWEIGHT_R32G32_SFLOAT = 0x13,
    VAF_BONEWEIGHT_R32G32B32_SFLOAT = 0x14,
    VAF_TANGENT_R32G32B32_SFLOAT = 0x15,
    VAF_TANGENT_A2B10G10R10_SNORM_PACK32 = 0x16,
    VAF_TANGENT_R16G16B16A16_SFLOAT = 0x17,
    VAF_NORMAL_R16G16B16A16_SNORM = 0x18,
    VAF_TANGENT_R16G16B16A16_SNORM = 0x19,

    VAF_Count,
    VAF_Invalid = 0xff
};

// Size in bytes of a single vertex attribute (FUN_005072f0 in TDU1).
uint32_t GetVertexAttributeSize( eVertexAttributeFormat format );

inline bool IsBlockCompressedFormat( eViewFormat format ) {
    return ( format >= VF_DXT1 && format <= VF_DXT5 ) ||
           ( format >= VF_SRGB_DXT1 && format <= VF_SRGB_DXT5 ) ||
           format == VF_ATI2;
}

inline bool IsSRGBFormat( eViewFormat format ) {
    return format >= VF_SRGB_R8G8B8 && format <= VF_SRGB_DXT5;
}

uint32_t GetFormatBlockSize( eViewFormat format );
uint32_t CalcMipSize( uint32_t width, uint32_t height, eViewFormat format );

enum class ePrimitiveType : uint8_t {
    PT_Point = 0,
    PT_Line = 1,
    PT_LineStrip = 2,
    PT_Triangle = 3,
    PT_TriangleStrip = 4,
    PT_TriangleFan = 5,
};
