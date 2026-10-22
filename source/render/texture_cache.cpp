#include "shared.h"
#include "texture_cache.h"

#include "filesystem/bank.h"
#include "render/2db.h"
#include "system/cmd_line.h"

namespace {
VkFormat GetVulkanFormat( eViewFormat format, bool bSupportsBC ) {
    switch ( format ) {
    case VF_DXT1:
    case VF_SRGB_DXT1:
        return bSupportsBC ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case VF_DXT2:
    case VF_DXT3:
    case VF_SRGB_DXT2:
    case VF_SRGB_DXT3:
        return bSupportsBC ? VK_FORMAT_BC2_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case VF_DXT4:
    case VF_DXT5:
    case VF_SRGB_DXT4:
    case VF_SRGB_DXT5:
        return bSupportsBC ? VK_FORMAT_BC3_UNORM_BLOCK : VK_FORMAT_R8G8B8A8_UNORM;
    case VF_A8R8G8B8:
    case VF_X8R8G8B8:
    case VF_SRGB_A8R8G8B8:
    case VF_SRGB_X8R8G8B8:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case VF_R8G8B8A8:
    case VF_SRGB_R8G8B8A8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    default:
        return VK_FORMAT_R8G8B8A8_UNORM; // Converted on the CPU
    }
}

// Converts uncommon formats to RGBA8.
bool ConvertToRGBA8( const uint8_t *pSource,
                     uint32_t width,
                     uint32_t height,
                     eViewFormat format,
                     std::vector<uint8_t> &output ) {
    output.resize( static_cast<size_t>( width ) * height * 4 );
    const size_t numPixels = static_cast<size_t>( width ) * height;
    switch ( format ) {
    case VF_L8:
    case VF_SRGB_L8:
        for ( size_t i = 0; i < numPixels; i++ ) {
            output[i * 4 + 0] = output[i * 4 + 1] = output[i * 4 + 2] = pSource[i];
            output[i * 4 + 3] = 255;
        }
        return true;
    case VF_A8:
    case VF_SRGB_A8:
        for ( size_t i = 0; i < numPixels; i++ ) {
            output[i * 4 + 0] = output[i * 4 + 1] = output[i * 4 + 2] = 255;
            output[i * 4 + 3] = pSource[i];
        }
        return true;
    case VF_R5G6B5:
    case VF_SRGB_R5G6B5:
        for ( size_t i = 0; i < numPixels; i++ ) {
            uint16_t c;
            memcpy( &c, pSource + i * 2, 2 );
            output[i * 4 + 0] = static_cast<uint8_t>( ( ( c >> 11 ) & 31 ) * 255 / 31 );
            output[i * 4 + 1] = static_cast<uint8_t>( ( ( c >> 5 ) & 63 ) * 255 / 63 );
            output[i * 4 + 2] = static_cast<uint8_t>( ( c & 31 ) * 255 / 31 );
            output[i * 4 + 3] = 255;
        }
        return true;
    case VF_A1R5G5B5:
    case VF_SRGB_A1R5G5B5:
        for ( size_t i = 0; i < numPixels; i++ ) {
            uint16_t c;
            memcpy( &c, pSource + i * 2, 2 );
            output[i * 4 + 0] = static_cast<uint8_t>( ( ( c >> 10 ) & 31 ) * 255 / 31 );
            output[i * 4 + 1] = static_cast<uint8_t>( ( ( c >> 5 ) & 31 ) * 255 / 31 );
            output[i * 4 + 2] = static_cast<uint8_t>( ( c & 31 ) * 255 / 31 );
            output[i * 4 + 3] = ( c & 0x8000 ) ? 255 : 0;
        }
        return true;
    case VF_R8G8B8:
    case VF_SRGB_R8G8B8:
        for ( size_t i = 0; i < numPixels; i++ ) {
            output[i * 4 + 0] = pSource[i * 3 + 2];
            output[i * 4 + 1] = pSource[i * 3 + 1];
            output[i * 4 + 2] = pSource[i * 3 + 0];
            output[i * 4 + 3] = 255;
        }
        return true;
    case VF_L16:
    case VF_SRGB_L16:
        for ( size_t i = 0; i < numPixels; i++ ) {
            output[i * 4 + 0] = output[i * 4 + 1] = output[i * 4 + 2] = pSource[i * 2 + 1];
            output[i * 4 + 3] = 255;
        }
        return true;
    default:
        return false;
    }
}
} // namespace

TextureCache::TextureCache()
    : pDevice( nullptr ), pWhiteTexture( nullptr ), pCheckerTexture( nullptr ) {}

TextureCache::~TextureCache() {}

void TextureCache::initialize( RenderDevice *pRenderDevice ) {
    pDevice = pRenderDevice;

    const uint32_t white[16] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
                                 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
                                 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
                                 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
    pWhiteTexture = createFromRGBA( 4, 4, reinterpret_cast<const uint8_t *>( white ),
                                    "OTDU_White" );

    std::vector<uint32_t> checker( 64 * 64 );
    for ( uint32_t y = 0; y < 64; y++ ) {
        for ( uint32_t x = 0; x < 64; x++ ) {
            bool bDark = ( ( x / 8 ) + ( y / 8 ) ) & 1;
            checker[y * 64 + x] = bDark ? 0xff505050 : 0xff707070;
        }
    }
    pCheckerTexture = createFromRGBA( 64, 64, reinterpret_cast<const uint8_t *>( checker.data() ),
                                      "OTDU_Checker" );
}

void TextureCache::destroy() {
    for ( OwnerTextures &owner : owners ) {
        for ( auto &entry : owner.Textures ) {
            pDevice->destroyTexture( entry.second );
        }
    }
    owners.clear();
    pDevice->destroyTexture( pWhiteTexture );
    pDevice->destroyTexture( pCheckerTexture );
    pWhiteTexture = nullptr;
    pCheckerTexture = nullptr;
}

TextureCache::OwnerTextures &TextureCache::getOwner( const char *pOwner ) {
    const std::string name = pOwner != nullptr ? pOwner : "";
    for ( OwnerTextures &owner : owners ) {
        if ( owner.Owner == name ) {
            return owner;
        }
    }
    owners.push_back( OwnerTextures() );
    owners.back().Owner = name;
    return owners.back();
}

GPUTexture *TextureCache::createFromRGBA( uint32_t width,
                                          uint32_t height,
                                          const uint8_t *pPixels,
                                          const char *pName ) {
    const void *pData = pPixels;
    const uint32_t size = width * height * 4;
    return pDevice->createTexture2D( width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, &pData, &size,
                                     pName );
}

GPUTexture *TextureCache::createFrom2DB( const Texture2DB &texture, const char *pName ) {
    const bool bSupportsBC = pDevice->supportsBC();
    const VkFormat format = GetVulkanFormat( texture.Format, bSupportsBC );
    const bool bBlockCompressed = IsBlockCompressedFormat( texture.Format );
    const bool bNativeLayout =
        ( bBlockCompressed && bSupportsBC ) || format == VK_FORMAT_B8G8R8A8_UNORM ||
        texture.Format == VF_R8G8B8A8 || texture.Format == VF_SRGB_R8G8B8A8;

    // Only keep mips down to the 4x4 block size for compressed formats.
    uint32_t numMips = texture.NumMips;
    static const bool bNoCube = CmdLine::HasArg( "-nocube2db" ); // comparisons: face 0 as 2D (old behaviour)
    const bool bCube = texture.isCube() && !bNoCube;
    if ( texture.Depth > 1 && !bCube ) {
        numMips = 1; // Volume textures: first slice only for now
    }

    // Cube maps (cubemap, env_lph_l3d, cubemap01, cubecarb...: Depth 6,
    // Flags 0xC4 / 0xC8; 2D textures: Depth 1, 0x84 / 0x88) [DATA]: the
    // texels are face major (face 0 with all its mips, then face 1...;
    // checked on env_lph_l3d: mip 1 matches the downsampled mip 0 with this
    // layout only), faces in the D3D order +X -X +Y -Y +Z -Z [INF: standard
    // D3D]. Vulkan cube faces use the same order and texel orientation as
    // D3D9, and the Eden renderer draws in the original (unmirrored) D3D
    // space: the faces are uploaded as they are.
    const uint32_t numLayers = bCube ? 6u : 1u;
    uint32_t faceSize = 0;
    for ( uint32_t i = 0; i < numMips; i++ ) {
        faceSize += CalcMipSize( Max( texture.Width >> i, 1u ), Max( texture.Height >> i, 1u ), texture.Format );
    }
    if ( bCube && faceSize * 6 > texture.TexelsSize ) {
        OTDU_LOG_WARN( "Cube texture '%s': truncated data\n", pName );
        return nullptr;
    }
    auto getSource = [&]( uint32_t layer, uint32_t mip ) -> const uint8_t * {
        if ( !bCube ) {
            return texture.pTexels + texture.getMipOffset( mip );
        }
        uint32_t offset = layer * faceSize;
        for ( uint32_t i = 0; i < mip; i++ ) {
            offset += CalcMipSize( Max( texture.Width >> i, 1u ), Max( texture.Height >> i, 1u ), texture.Format );
        }
        return texture.pTexels + offset;
    };

    const uint32_t numSubresources = numMips * numLayers;
    std::vector<const void *> mipData( numSubresources );
    std::vector<uint32_t> mipSizes( numSubresources );
    std::vector<std::vector<uint8_t>> converted;

    if ( bNativeLayout ) {
        for ( uint32_t s = 0; s < numSubresources; s++ ) {
            const uint32_t i = s % numMips;
            mipData[s] = getSource( s / numMips, i );
            mipSizes[s] = CalcMipSize( Max( texture.Width >> i, 1u ),
                                       Max( texture.Height >> i, 1u ), texture.Format );
        }
    } else {
        converted.resize( numSubresources );
        for ( uint32_t s = 0; s < numSubresources; s++ ) {
            const uint32_t i = s % numMips;
            const uint32_t width = Max( texture.Width >> i, 1u );
            const uint32_t height = Max( texture.Height >> i, 1u );
            const uint8_t *pSource = getSource( s / numMips, i );
            if ( bBlockCompressed ) {
                converted[s].resize( static_cast<size_t>( width ) * height * 4 );
                DecodeBlockCompressed( pSource, width, height, texture.Format, converted[s].data() );
            } else if ( !ConvertToRGBA8( pSource, width, height, texture.Format, converted[s] ) ) {
                OTDU_LOG_WARN( "Unsupported texture format %u for '%s'\n", texture.Format, pName );
                return nullptr;
            }
            mipData[s] = converted[s].data();
            mipSizes[s] = static_cast<uint32_t>( converted[s].size() );
        }
    }

    if ( bCube ) {
        return pDevice->createTexture( VK_IMAGE_VIEW_TYPE_CUBE, texture.Width, texture.Height, 1, 6, numMips, format,
                                       mipData.data(), mipSizes.data(), pName );
    }
    return pDevice->createTexture2D( texture.Width, texture.Height, numMips, format,
                                     mipData.data(), mipSizes.data(), pName );
}

uint32_t TextureCache::loadBankTextures( const Bank &bank, const char *pOwner ) {
    OwnerTextures &owner = getOwner( pOwner );
    uint32_t numLoaded = 0;
    for ( const BankEntry *pEntry : bank.findEntriesByExtension( "2db" ) ) {
        Texture2DB texture;
        if ( !texture.parse( pEntry->pData, pEntry->Size ) ) {
            OTDU_LOG_WARN( "Failed to parse texture '%s'\n", pEntry->Filename.c_str() );
            continue;
        }

        const uint64_t hashcode = GetIdentifier64bit( pEntry->Name.c_str() );
        // Already loaded for this owner (e.g. shared banks loaded by several
        // vehicles): keep the existing texture, models may reference it.
        if ( owner.Textures.find( hashcode ) != owner.Textures.end() ) {
            continue;
        }

        GPUTexture *pTexture = createFrom2DB( texture, pEntry->Name.c_str() );
        if ( pTexture == nullptr ) {
            continue;
        }
        owner.Textures[hashcode] = pTexture;
        if ( texture.Hashcode != hashcode ) {
            // Alias with the hashcode stored in the resource (used by materials).
            owner.Aliases[texture.Hashcode] = pTexture;
        }
        numLoaded++;
    }
    OTDU_LOG_DEBUG( "Loaded %u textures from '%s'\n", numLoaded, bank.getPath().c_str() );
    return numLoaded;
}

void TextureCache::releaseOwner( const char *pOwner ) {
    const std::string name = pOwner != nullptr ? pOwner : "";
    for ( auto it = owners.begin(); it != owners.end(); ++it ) {
        if ( it->Owner == name ) {
            for ( auto &entry : it->Textures ) {
                pDevice->destroyTexture( entry.second );
            }
            owners.erase( it );
            return;
        }
    }
}

GPUTexture *TextureCache::find( uint64_t hashcode ) const {
    for ( auto it = owners.rbegin(); it != owners.rend(); ++it ) {
        auto texture = it->Textures.find( hashcode );
        if ( texture != it->Textures.end() ) {
            return texture->second;
        }
        auto alias = it->Aliases.find( hashcode );
        if ( alias != it->Aliases.end() ) {
            return alias->second;
        }
    }
    return nullptr;
}

GPUTexture *TextureCache::find( const char *pName ) const {
    return find( GetIdentifier64bit( pName ) );
}

GPUTexture *TextureCache::find( const char *pOwner, const char *pName ) const {
    const uint64_t hashcode = GetIdentifier64bit( pName );
    for ( const OwnerTextures &owner : owners ) {
        if ( owner.Owner != pOwner ) {
            continue;
        }
        auto texture = owner.Textures.find( hashcode );
        return texture != owner.Textures.end() ? texture->second : nullptr;
    }
    return nullptr;
}
