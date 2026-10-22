#include "shared.h"
#include "eden_material_handlers.h"

#include "render/eden/eden_renderer.h"

namespace {
constexpr uint64_t kRuntimeTexture = 1ull;
constexpr uint64_t kRuntimeTextureAlt = ~0ull;
constexpr int32_t kBucketDefault = 0x39;

struct Context {
    EdenMaterial &Material;

    // Key byte (handler offset 0x10..0x3f -> switch 0..47)
    uint8_t &K( uint32_t offset ) { return Material.Spec[offset - 0x10]; }
    // MAT. render state byte (0x30..0x3f)
    uint8_t &M( uint32_t offset ) { return Material.States[offset - 0x30]; }
    // Texture hashcode of slot i (MAT. +0x108 + 0x20 i)
    uint64_t &Slot( uint32_t index ) {
        if ( index >= Material.NumSlots ) {
            Material.NumSlots = index + 1;
        }
        return Material.TextureHashes[index];
    }
    uint64_t slotHash( uint32_t index ) const {
        return index < Material.NumSlots ? Material.TextureHashes[index] : 0;
    }

    // Common tail of the car handlers (e.g. 0x00d716b0)
    int32_t carTail() {
        int32_t bucket = 2;
        if ( M( 0x35 ) == 0 ) {
            K( 0x25 ) = 1;
            bucket = 0xb;
        }
        if ( M( 0x3a ) != 6 ) {
            bucket = M( 0x39 ) == 2 ? 0x2c : 0x2b;
        }
        return bucket;
    }
};

// 0x00d713f0
int32_t TD_CarPaint( Context &c ) {
    c.K( 0x18 ) = 0;
    const uint32_t gloss = c.K( 0x31 ) != 0 ? 1u : c.K( 0x12 );
    c.K( 0x11 ) = static_cast<uint8_t>( c.K( 0x39 ) + c.K( 0x38 ) + c.K( 0x35 ) + gloss );
    if ( c.K( 0x3a ) + c.K( 0x33 ) + c.K( 0x34 ) == 0 ) {
        c.M( 0x35 ) = 1;
    }
    if ( c.K( 0x12 ) == 0 ) {
        c.K( 0x34 ) = 0;
    }
    c.K( 0x32 ) = 0;
    c.K( 0x16 ) = 0;
    c.K( 0x1b ) = 0;
    return c.carTail();
}

// 0x00d71480
int32_t TD_CarPaint2( Context &c ) {
    c.K( 0x18 ) = 0;
    c.K( 0x1e ) = 0;
    c.K( 0x1b ) = 0;
    return c.carTail();
}

// 0x00d714c0
int32_t TD_CarDeform( Context &c ) {
    c.K( 0x18 ) = 0;
    c.K( 0x1b ) = 0;
    return c.carTail();
}

// 0x00d71500
int32_t TD_CarVelour( Context &c ) {
    if ( c.slotHash( 2 ) != 0 ) {
        c.K( 0x35 ) = 1;
    }
    if ( c.K( 0x35 ) == 1 ) {
        c.K( 0x1f ) = 1;
    }
    c.K( 0x1b ) = 0;
    return c.carTail();
}

// 0x00d71560
int32_t TD_DashBoard( Context &c ) {
    c.K( 0x1d ) = 0;
    c.K( 0x1e ) = 0;
    c.K( 0x1f ) = 0;
    c.K( 0x1b ) = 0;
    return c.carTail();
}

// 0x00d715b0 (TD_CarMat_NM, TD_Tire)
int32_t TD_CarMat_NM( Context &c ) {
    if ( c.K( 0x13 ) == 0 ) {
        c.K( 0x34 ) = 0;
    }
    c.K( 0x32 ) = 0;
    c.K( 0x1b ) = 0;
    return c.carTail();
}

// 0x00d71600 (69f07f72719bf862: leathers, brake discs)
int32_t CarMatTangentSpace( Context &c ) {
    c.Material.ShaderSetGUID = 0xca00000000000020ull;
    c.K( 0x1f ) = 1;
    return TD_CarMat_NM( c );
}

// 0x00d71620 (b39c35015789e65c: low resolution cockpits)
int32_t CarMatNormalMapped( Context &c ) {
    c.Material.ShaderSetGUID = 0xca00000000000020ull;
    c.K( 0x36 ) = 0;
    c.K( 0x1c ) = 1;
    c.K( 0x1d ) = 1;
    c.K( 0x1e ) = 1;
    c.K( 0x1f ) = 1;
    return TD_CarMat_NM( c );
}

// 0x00d71650
int32_t TD_CarPaint_NM( Context &c ) {
    c.Material.ShaderSetGUID = 0xca00000000000010ull;
    if ( c.K( 0x12 ) == 0 ) {
        c.K( 0x34 ) = 0;
    }
    c.K( 0x32 ) = 0;
    c.K( 0x16 ) = 1;
    c.K( 0x1b ) = 0;
    return c.carTail();
}

// 0x00d71850 (1b38348331d3942a: car details, lights, logos)
int32_t CarPaintTangentSpace( Context &c ) {
    c.K( 0x1f ) = 1;
    return TD_CarPaint_NM( c );
}

// 0x00d716b0 (td_carplate, TD_STICKERMAP, TD_CarPainting_helper), 0x00d713b0 (TD_Belt)
int32_t CarNoFog( Context &c ) {
    c.K( 0x1b ) = 0;
    return c.carTail();
}

// 0x00d70c30
int32_t TD_Mirror( Context &c ) {
    c.K( 0x31 ) &= c.K( 0x33 );
    c.K( 0x34 ) &= static_cast<uint8_t>( ~c.K( 0x33 ) );
    c.K( 0x12 ) &= c.K( 0x34 );
    c.K( 0x1b ) = 0;
    int32_t bucket = 2;
    if ( c.M( 0x35 ) == 0 ) {
        c.K( 0x25 ) = static_cast<uint8_t>( ( c.K( 0x34 ) | c.K( 0x33 ) ) & ( c.K( 0x31 ) | c.K( 0x12 ) ) );
        bucket = 0xb;
    }
    if ( c.M( 0x3a ) != 6 ) {
        bucket = c.M( 0x39 ) == 2 ? 0x2c : 0x2b;
    }
    return bucket;
}

// 0x00d70ea0: projected box (car ambient occlusion under the body, light
// beams). Slot 1 is the scene depth (runtime texture).
int32_t TD_ShadowBox( Context &c ) {
    c.Material.NumSlots = Max<uint32_t>( c.Material.NumSlots, 2 );
    c.Slot( 1 ) = kRuntimeTexture;
    c.M( 0x31 ) = 2;
    c.M( 0x3a ) = 1;
    if ( c.K( 0x30 ) != 0 ) {
        c.M( 0x38 ) = 2; // ONE
        c.M( 0x39 ) = 2; // ONE
        return 0x2f;
    }
    c.M( 0x38 ) = 1; // ZERO
    c.M( 0x39 ) = 3; // SRCCOLOR (multiply)
    c.M( 0x3b ) = 7;
    return 0x36;
}

// 0x00d70bb0
int32_t TD_HardwareInstance( Context &c ) {
    c.K( 0x25 ) = c.K( 0x11 );
    c.K( 0x1b ) = 0;
    c.M( 0x37 ) = 0xa;
    c.M( 0x3a ) = 6;
    c.M( 0x35 ) = c.K( 0x11 ) == 0 ? 1 : 0;
    return 0x15;
}

// 0x00d70be0 (TD_Gouraud, td_checkpoint)
int32_t TD_Gouraud( Context &c ) {
    if ( c.K( 0x33 ) == 0 && c.K( 0x30 ) != 0 ) {
        c.K( 0x1b ) = 0;
    }
    return kBucketDefault;
}

// 0x00d70d10 / 0x00d70d20 (TD_GPS, TD_digit, TD_Lamp, td_actors...)
int32_t NoFog( Context &c ) {
    c.K( 0x1b ) = 0;
    return kBucketDefault;
}

// Water buckets (layer table 0x00f7f604): 0x19 WATER, 0x2e WATER_FULLALPHA,
// 0x17 WATER_RIVER, 0x18 WATER_RIVER_FULLALPHA.
// 0x00d70cf0 (TD_Water)
int32_t TD_Water( Context &c ) {
    c.K( 0x1b ) = 0;
    return c.M( 0x31 ) == 2 ? 0x2e : 0x19;
}

// 0x00d70cb0 (TD_WaterRiver, TD_backwash)
int32_t TD_WaterRiver( Context &c ) {
    c.K( 0x1b ) = 0;
    return c.M( 0x31 ) == 2 ? 0x18 : 0x17;
}

// 0x00d70cd0 (0123210123210123)
int32_t UVMask3( Context &c ) {
    c.K( 0x18 ) &= 7;
    return c.M( 0x31 ) == 2 ? 0x18 : 0x17;
}

// 0x00d70d30 (then 0x00422eb0, encrypted)
int32_t TD_VegetationSPR( Context &c ) {
    c.K( 0x25 ) = 1;
    c.K( 0x10 ) = 1;
    c.K( 0x16 ) = 0;
    return kBucketDefault;
}

// 0x00d70fa0 (TD_Vegetation and 37078865c0ca336e, then 0x00422eb0, encrypted)
int32_t TD_Vegetation( Context &c ) {
    c.Material.ShaderSetGUID = 0x73078865caca336dull;
    if ( c.K( 0x34 ) == 0 ) {
        c.K( 0x1d ) = 0;
        c.K( 0x1e ) = 0;
        c.K( 0x16 ) = 0;
    }
    return kBucketDefault;
}

// 0x00d70d50 (TD_Accessories2, TD_Visor)
int32_t TD_Accessories( Context &c ) {
    const uint8_t u1 = c.K( 0x30 );
    c.K( 0x11 ) = ( u1 != 0 || c.K( 0x35 ) != 0 ) ? 1 : 0;
    if ( c.K( 0x39 ) != 0 ) {
        c.K( 0x11 )++;
    }
    c.K( 0x1b ) = 0;
    int32_t bucket = 0xc;
    if ( u1 == 0 ) {
        c.M( 0x35 ) = 1;
    } else if ( c.M( 0x35 ) == 0 ) {
        bucket = 0xe;
        c.K( 0x25 ) = 1;
    }
    return c.M( 0x3a ) != 6 ? 0x29 : bucket;
}

// 0x00d70db0
int32_t TD_Ground_HM( Context &c ) {
    c.K( 0x1b ) = 0;
    if ( c.K( 0x1f ) == 0 ) {
        c.K( 0x1d ) = 0;
        c.K( 0x1e ) = 0;
    }
    return kBucketDefault;
}

// 0x00d70f30
int32_t TD_ShadowCaster( Context &c ) {
    c.K( 0x21 ) = 1;
    c.M( 0x35 ) = 1;
    c.M( 0x3a ) = 6;
    c.M( 0x3b ) = 0xe;
    c.K( 0x1a ) = 0;
    return c.Material.ShaderSetGUID == 0xac78df32bc4589dfull ? 0x38 : 0x11;
}

// 0x00d70f80
int32_t TD_Paper( Context &c ) {
    c.Slot( 1 ) = kRuntimeTexture;
    return kBucketDefault;
}

// 0x00d70fd0
int32_t TD_Furnitures( Context &c ) {
    c.K( 0x1b ) = 0;
    if ( c.K( 0x16 ) == 0 || c.K( 0x36 ) == 1 ) {
        if ( c.K( 0x12 ) != 0 ) {
            c.K( 0x12 ) = 0;
            c.Slot( 4 ) = 0;
        }
        if ( c.K( 0x36 ) == 1 ) {
            c.Slot( 6 ) = kRuntimeTexture;
        }
    }
    if ( c.K( 0x34 ) == 0 ) {
        c.K( 0x1d ) = 0;
        c.K( 0x1e ) = 0;
    }
    if ( c.M( 0x3a ) != 6 ) {
        c.K( 0x25 ) = 1;
        return 0x21;
    }
    return 0;
}

// 0x00d71030
int32_t TD_Building( Context &c ) {
    c.K( 0x1b ) = 0;
    if ( c.K( 0x16 ) != 0 && c.K( 0x12 ) == 0 ) {
        c.K( 0x16 ) = 0;
    }
    c.K( 0x34 ) = c.K( 0x38 );
    c.K( 0x1a ) = 0;
    if ( c.K( 0x32 ) != 0 ) {
        c.K( 0x32 ) = c.K( 0x12 );
    }
    const uint64_t normalMap = c.slotHash( 5 );
    if ( normalMap == 0 && c.K( 0x39 ) != 1 ) {
        c.K( 0x1d ) = 0;
        c.K( 0x1e ) = 0;
    } else {
        if ( normalMap != 0 ) {
            c.K( 0x3e ) = 1;
            c.K( 0x39 ) = 0;
        }
        c.K( 0x1f ) = 1;
        c.K( 0x1d ) = 1;
        c.K( 0x1e ) = 1;
    }
    int32_t bucket = 0;
    if ( c.M( 0x35 ) == 0 ) {
        c.K( 0x25 ) = 1;
        bucket = 0xf;
    }
    const bool bAdditive = c.M( 0x3a ) == 1 && c.M( 0x39 ) == 2;
    if ( c.M( 0x3a ) != 6 ) {
        bucket = 0x21;
        if ( !bAdditive ) {
            c.K( 0x1b ) = 1;
        }
    }
    if ( c.M( 0x31 ) == 2 ) {
        if ( !bAdditive ) {
            c.K( 0x1b ) = 1;
            return bucket;
        }
        return 0x22;
    }
    return bucket;
}

// 0x00d71220 (TD_Avatar, TD_Skin2, TD_Clothe2, TD_Eye, TD_HELMET...)
int32_t TD_Avatar( Context &c ) {
    c.K( 0x1b ) = 0;
    int32_t bucket = 0xc;
    if ( c.M( 0x35 ) == 0 ) {
        bucket = 0xe;
        c.K( 0x25 ) = 1;
    }
    return c.M( 0x3a ) != 6 ? 0x1f : bucket;
}

// 0x00d712c0
int32_t TD_Ground( Context &c ) {
    c.K( 0x1b ) = 0;
    if ( c.K( 0x1f ) == 0 ) {
        c.K( 0x1d ) = 0;
        c.K( 0x1e ) = 0;
    }
    c.K( 0x32 ) = c.K( 0x36 ) != 0 ? 1 : 0;
    c.K( 0x36 ) = 0;
    if ( c.slotHash( 2 ) == 0 ) {
        c.Slot( 2 ) = kRuntimeTextureAlt;
    }
    int32_t bucket = 1;
    if ( c.M( 0x35 ) == 0 ) {
        bucket = 8;
        c.K( 0x25 ) = 1;
    }
    if ( c.M( 0x3a ) != 6 ) {
        return c.M( 0x31 ) == 2 ? 0xa : 0x9;
    }
    return bucket;
}

// 0x00d710f0 (TD_Hair): hair layers. The runtime hair shadow maps of slots
// 1..4 (DAT_011c9ed0) are not rendered: their switches stay off.
int32_t TD_Hair( Context &c ) {
    c.M( 0x30 ) = 1;
    c.K( 0x1b ) = 0;
    if ( c.M( 0x3a ) != 6 ) {
        c.K( 0x34 ) = 0;
        c.K( 0x35 ) = 0;
        c.K( 0x36 ) = 0;
        c.K( 0x37 ) = 0;
        c.K( 0x3f ) = 0;
        const int32_t b31 = c.K( 0x31 );
        const int32_t b30 = c.K( 0x30 );
        c.K( 0x31 ) = 1;
        c.K( 0x30 ) = 1;
        return 0x25 + 2 * b30 + b31; // AVATAR_FULLALPHA_0..3
    }
    c.K( 0x25 ) = c.M( 0x35 ) != 1 ? 1 : 0;
    if ( c.M( 0x35 ) == 1 || c.M( 0x37 ) == 0 ) {
        c.M( 0x35 ) = 1;
    }
    c.K( 0x31 ) = 1;
    c.K( 0x30 ) = 1;
    return 0xd; // AVATAR_HAIR
}

// 0x00d71340 (TD_Road, TD_Tunnel)
int32_t TD_Road( Context &c ) {
    c.K( 0x1b ) = 0;
    c.K( 0x1e ) = 0;
    if ( c.M( 0x35 ) == 0 ) {
        c.K( 0x25 ) = 1;
    }
    if ( c.K( 0x33 ) == 1 ) {
        // Road decals: blended on the road, no depth write.
        c.M( 0x35 ) = 1;
        c.M( 0x31 ) = 2;
        c.K( 0x25 ) = 0;
        c.M( 0x3a ) = 1;
        c.M( 0x38 ) = 5;
        c.M( 0x39 ) = 6;
        return 7;
    }
    const int32_t bucket = c.M( 0x35 ) == 0 ? 4 : 3;
    c.M( 0x3a ) = 6;
    return bucket;
}

// 0x00d716f0
int32_t TD_HeightMap( Context &c ) {
    c.K( 0x30 ) = 0;
    c.K( 0x1b ) = 0;
    c.K( 0x12 ) = 1;
    if ( c.K( 0x36 ) == 0 && c.K( 0x33 ) == 0 ) {
        c.K( 0x36 ) = c.slotHash( 6 ) != 0 ? 7 : 3;
    }
    c.M( 0x35 ) = 1;
    return 5;
}

using Handler = int32_t ( * )( Context & );
struct HandlerEntry {
    uint64_t Guid;
    Handler pHandler;
};

// 0x00f8fa10 (entries whose handler is in the encrypted part of the
// executable are left out: td_dome, clouds, flash, shop...).
const HandlerEntry kHandlers[] = {
    { 0x948a357330c13b48ull, TD_HeightMap },
    { 0x183157b81c6c7357ull, TD_CarPaint },
    { 0x413ab646426c5358ull, CarNoFog },
    { 0x183157b81c6c5377ull, TD_CarPaint2 },
    { 0xca00000000000010ull, TD_CarPaint_NM },
    { 0x1b38348331d3942aull, CarPaintTangentSpace },
    { 0xb39c35015789e65cull, CarMatNormalMapped },
    { 0xca00000000000020ull, TD_CarMat_NM },
    { 0x69f07f72719bf862ull, CarMatTangentSpace },
    { 0x1b38348331cacacaull, CarNoFog },
    { 0xca0000ee00000020ull, TD_CarMat_NM },
    { 0x213157b8bc6c7363ull, TD_CarVelour },
    { 0x6638348331d66466ull, TD_CarDeform },
    { 0x100100100100dcbaull, CarNoFog },
    { 0x01234567891010caull, CarNoFog },
    { 0xc8ad93c95455291bull, TD_Road },
    { 0xe8ad93c00455291bull, TD_Road },
    { 0x183157b81c6cff57ull, TD_Building },
    { 0xb0f1234b0f123473ull, TD_Ground },
    { 0xa0a0a1a1a2a2a3a3ull, TD_Ground_HM },
    { 0xd5013ed0db79afafull, TD_Furnitures },
    { 0x73078865caca336dull, TD_Vegetation },
    { 0x37078865c0ca336eull, TD_Vegetation },
    { 0x73078865cacacacaull, TD_VegetationSPR },
    { 0xd5013ed0db79dadbull, TD_Avatar },
    { 0xd5013ed0dbdededeull, TD_Hair },
    { 0xd5013ed0dbcdcdcdull, TD_Avatar },
    { 0xd5013ed0dbcddbcaull, TD_Avatar },
    { 0xd5013ed0db79aeaeull, TD_Avatar },
    { 0xd5013ed0db79cacaull, TD_Avatar },
    { 0xd5013ed0dba9dacaull, TD_Accessories },
    { 0xd5013ed0db79baedull, TD_Avatar },
    { 0xd5013ed0db79bbbbull, TD_Avatar },
    { 0xd50130aedb79dadbull, TD_Avatar },
    { 0x7dad16b47eb57b95ull, NoFog },
    { 0x7dad16b47eb51234ull, NoFog },
    { 0xcaca133713371337ull, NoFog },
    { 0xfafafafafafafafaull, NoFog },
    { 0x0123000000000000ull, TD_Water },
    { 0x73078865ca37336dull, TD_Gouraud },
    { 0xcd453201027c8abaull, TD_HardwareInstance },
    { 0x0123210123210123ull, UVMask3 },
    { 0x0123210456210123ull, TD_WaterRiver },
    { 0x73078865ca31234dull, TD_WaterRiver },
    { 0x643cdcb28a4efd79ull, TD_Paper },
    { 0xac78df32bc4589ffull, TD_ShadowCaster },
    { 0xac78df32bc4589dfull, TD_ShadowCaster },
    { 0x132435465768798aull, TD_ShadowBox },
    { 0xc913741cbe17490dull, NoFog },
    { 0xcaca000000000010ull, TD_DashBoard },
    { 0x7dad16b47eb51337ull, NoFog },
    { 0xd5013ed0dcacafafull, NoFog },
    { 0x08041973fadeffffull, TD_Mirror },
    { 0xabbaffffffffabbaull, TD_Gouraud },
    { 0x183157b81c6ccacaull, TD_Avatar },
    { 0xd5013ed0cacadacaull, TD_Accessories },
};
} // namespace

int32_t ApplyEdenMaterialHandler( EdenMaterial &material ) {
    Context context { material };
    int32_t bucket = kBucketDefault;
    for ( const HandlerEntry &entry : kHandlers ) {
        if ( entry.Guid == material.ShaderSetGUID ) {
            bucket = entry.pHandler( context );
            break;
        }
    }
    // FUN_00d71900
    if ( bucket == kBucketDefault ) {
        if ( context.M( 0x35 ) == 0 ) {
            bucket = 0x12;
            context.K( 0x25 ) = 1;
        }
        if ( context.M( 0x3a ) != 6 ) {
            bucket = context.M( 0x31 ) == 2 ? 0x2f : 0x2d;
        } else if ( bucket == kBucketDefault ) {
            bucket = 0x11;
        }
    }
    if ( context.M( 0x3a ) != 6 && bucket != 0x2b && bucket != 0x2c ) {
        context.M( 0x31 ) = 2;
    }
    return bucket;
}
