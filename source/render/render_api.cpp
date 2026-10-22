#include "shared.h"
#include "render_api.h"

uint32_t GetVertexAttributeSize( eVertexAttributeFormat format ) {
    // FUN_00507250 (component count) * FUN_005072f0 (component size), TDU1.
    static constexpr uint32_t kComponentSize[VAF_Count] = {
        4, 2, 4, 4, 4, 2, 4, 4, 0, 4, 2, 2, 4,
        2, 2, 4, 2, 4, 4, 4, 4, 4, 4, 2, 2, 2,
    };
    static constexpr uint32_t kComponentCount[VAF_Count] = {
        3, 4, 1, 1, 4, 4, 3, 4, 0, 1, 4, 4, 2,
        2, 2, 1, 1, 1, 1, 2, 3, 3, 1, 4, 4, 0 };
    if ( format >= VAF_Count ) {
        return 0;
    }
    return kComponentSize[format] * kComponentCount[format];
}

uint32_t GetFormatBlockSize( eViewFormat format ) {
    switch ( format ) {
    case VF_DXT1:
    case VF_SRGB_DXT1:
        return 8;
    case VF_DXT2:
    case VF_DXT3:
    case VF_DXT4:
    case VF_DXT5:
    case VF_SRGB_DXT2:
    case VF_SRGB_DXT3:
    case VF_SRGB_DXT4:
    case VF_SRGB_DXT5:
    case VF_ATI2:
        return 16;
    case VF_A8:
    case VF_SRGB_A8:
    case VF_L8:
    case VF_SRGB_L8:
        return 1;
    case VF_R5G6B5:
    case VF_A1R5G5B5:
    case VF_L16:
    case VF_V8U8:
    case VF_R16F:
    case VF_SRGB_R5G6B5:
    case VF_SRGB_A1R5G5B5:
    case VF_SRGB_L16:
        return 2;
    case VF_R8G8B8:
    case VF_SRGB_R8G8B8:
        return 3;
    case VF_A16B16G16R16:
    case VF_A16B16G16R16F:
    case VF_G32R32F:
    case VF_SRGB_A16B16G16R16:
    case VF_SRGB_A16FB16FG16FR16F:
        return 8;
    case VF_A32B32G32R32F:
    case VF_SRGB_A32B32G32R32F:
        return 16;
    default:
        return 4;
    }
}

uint32_t CalcMipSize( uint32_t width, uint32_t height, eViewFormat format ) {
    width = Max( width, 1u );
    height = Max( height, 1u );
    if ( IsBlockCompressedFormat( format ) ) {
        return Max( 1u, ( width + 3 ) / 4 ) * Max( 1u, ( height + 3 ) / 4 ) *
               GetFormatBlockSize( format );
    }
    return width * height * GetFormatBlockSize( format );
}
