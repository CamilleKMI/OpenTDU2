#include "shared.h"
#include "2db.h"

static constexpr uint32_t k2DBMagic = MakeFourCC( '.', '2', 'D', 'B' );
static constexpr uint32_t kBitmapMagic = MakeFourCC( 'B', 'M', 'A', 'P' );

bool Texture2DB::parse( const uint8_t *pFile, uint32_t fileSize ) {
    if ( !RenderFile::IsValid( pFile, fileSize, k2DBMagic ) ||
         fileSize < 0x20 + sizeof( Texture2DBHeader ) ) {
        return false;
    }

    RenderFile::Section section;
    memcpy( &section, pFile + sizeof( RenderFile::Header ), sizeof( section ) );
    if ( section.Type != kBitmapMagic ) {
        return false;
    }

    Texture2DBHeader header;
    const uint32_t headerOffset = sizeof( RenderFile::Header ) + sizeof( RenderFile::Section );
    memcpy( &header, pFile + headerOffset, sizeof( header ) );

    Hashcode = header.Hashcode;
    Name = IdentifierToString( header.Hashcode );
    Width = header.Width;
    Height = header.Height;
    Depth = Max<uint32_t>( header.Depth, 1u );
    NumMips = Max<uint32_t>( header.NumMipmap, 1u );
    Flags = header.Flags;
    Format = header.Format;

    const uint32_t texelOffset = headerOffset + sizeof( Texture2DBHeader );
    pTexels = pFile + texelOffset;
    TexelsSize = fileSize - texelOffset;

    // Clamp the mip chain to the available data.
    uint32_t total = 0;
    for ( uint32_t i = 0; i < NumMips; i++ ) {
        uint32_t mipSize = getMipSize( i );
        if ( total + mipSize > TexelsSize ) {
            NumMips = Max( i, 1u );
            break;
        }
        total += mipSize;
    }
    return Width > 0 && Height > 0 && getMipSize( 0 ) <= TexelsSize;
}

uint32_t Texture2DB::getMipSize( uint32_t mipIndex ) const {
    return CalcMipSize( Max( Width >> mipIndex, 1u ), Max( Height >> mipIndex, 1u ),
                        Format ) * Depth;
}

uint32_t Texture2DB::getMipOffset( uint32_t mipIndex ) const {
    uint32_t offset = 0;
    for ( uint32_t i = 0; i < mipIndex; i++ ) {
        offset += getMipSize( i );
    }
    return offset;
}

namespace {
void DecodeColorBlock( const uint8_t *pBlock, uint8_t outColors[16][4], bool bForce4Colors ) {
    uint16_t c0, c1;
    memcpy( &c0, pBlock, 2 );
    memcpy( &c1, pBlock + 2, 2 );
    uint32_t indices;
    memcpy( &indices, pBlock + 4, 4 );

    uint8_t palette[4][4];
    auto expand = [&]( uint16_t c, uint8_t *pOut ) {
        pOut[0] = static_cast<uint8_t>( ( ( c >> 11 ) & 31 ) * 255 / 31 );
        pOut[1] = static_cast<uint8_t>( ( ( c >> 5 ) & 63 ) * 255 / 63 );
        pOut[2] = static_cast<uint8_t>( ( c & 31 ) * 255 / 31 );
        pOut[3] = 255;
    };
    expand( c0, palette[0] );
    expand( c1, palette[1] );
    if ( c0 > c1 || bForce4Colors ) {
        for ( int i = 0; i < 3; i++ ) {
            palette[2][i] = static_cast<uint8_t>( ( 2 * palette[0][i] + palette[1][i] ) / 3 );
            palette[3][i] = static_cast<uint8_t>( ( palette[0][i] + 2 * palette[1][i] ) / 3 );
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for ( int i = 0; i < 3; i++ ) {
            palette[2][i] = static_cast<uint8_t>( ( palette[0][i] + palette[1][i] ) / 2 );
            palette[3][i] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }
    for ( int i = 0; i < 16; i++ ) {
        memcpy( outColors[i], palette[( indices >> ( i * 2 ) ) & 3], 4 );
    }
}
} // namespace

void DecodeBlockCompressed( const uint8_t *pSource,
                            uint32_t width,
                            uint32_t height,
                            eViewFormat format,
                            uint8_t *pDestinationRGBA ) {
    const bool bDXT1 = ( format == VF_DXT1 || format == VF_SRGB_DXT1 );
    const bool bDXT3 = ( format == VF_DXT2 || format == VF_DXT3 ||
                         format == VF_SRGB_DXT2 || format == VF_SRGB_DXT3 );
    const uint32_t blockSize = bDXT1 ? 8 : 16;
    const uint32_t blocksX = Max( 1u, ( width + 3 ) / 4 );
    const uint32_t blocksY = Max( 1u, ( height + 3 ) / 4 );

    for ( uint32_t by = 0; by < blocksY; by++ ) {
        for ( uint32_t bx = 0; bx < blocksX; bx++ ) {
            const uint8_t *pBlock = pSource + ( by * blocksX + bx ) * blockSize;
            uint8_t colors[16][4];
            DecodeColorBlock( pBlock + ( bDXT1 ? 0 : 8 ), colors, !bDXT1 );

            if ( bDXT3 ) {
                for ( int i = 0; i < 16; i++ ) {
                    uint8_t a = ( pBlock[i / 2] >> ( ( i & 1 ) * 4 ) ) & 0xf;
                    colors[i][3] = static_cast<uint8_t>( a * 17 );
                }
            } else if ( !bDXT1 ) {
                uint8_t a0 = pBlock[0];
                uint8_t a1 = pBlock[1];
                uint64_t bits = 0;
                for ( int i = 0; i < 6; i++ ) {
                    bits |= static_cast<uint64_t>( pBlock[2 + i] ) << ( 8 * i );
                }
                uint8_t alphas[8];
                alphas[0] = a0;
                alphas[1] = a1;
                if ( a0 > a1 ) {
                    for ( int k = 2; k < 8; k++ ) {
                        alphas[k] = static_cast<uint8_t>( ( ( 8 - k ) * a0 + ( k - 1 ) * a1 ) / 7 );
                    }
                } else {
                    for ( int k = 2; k < 6; k++ ) {
                        alphas[k] = static_cast<uint8_t>( ( ( 6 - k ) * a0 + ( k - 1 ) * a1 ) / 5 );
                    }
                    alphas[6] = 0;
                    alphas[7] = 255;
                }
                for ( int i = 0; i < 16; i++ ) {
                    colors[i][3] = alphas[( bits >> ( 3 * i ) ) & 7];
                }
            }

            for ( int py = 0; py < 4; py++ ) {
                for ( int px = 0; px < 4; px++ ) {
                    uint32_t x = bx * 4 + px;
                    uint32_t y = by * 4 + py;
                    if ( x < width && y < height ) {
                        memcpy( pDestinationRGBA + ( y * width + x ) * 4, colors[py * 4 + px], 4 );
                    }
                }
            }
        }
    }
}
