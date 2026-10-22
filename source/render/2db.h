#pragma once

#include "render/render_api.h"
#include "render/render_file.h"

// .2DB: Eden bitmap (texture) resource.
//
//   RenderFile::Header      ".2DB", version 2.0
//   RenderFile::Section     "BMAP"
//   Texture2DBHeader        (0x30 bytes, same layout as TDU1)
//   Texel data              (mips stored consecutively)
#pragma pack( push, 1 )
struct Texture2DBHeader {
    uint64_t Hashcode;
    uint16_t Width;
    uint16_t Height;
    uint16_t Depth;
    uint8_t NumMipmap;
    uint8_t NumPackedMipmap;
    uint32_t Flags;
    uint32_t pTexture; // Runtime (x86 pointer)
    uint32_t __PADDING__;
    eViewFormat Format;
    uint8_t bUploaded;
    uint8_t Type;
    uint16_t StreamOffset;
    uint8_t __PADDING2__[0xc];
};
OTDU_SIZE_MUST_MATCH( Texture2DBHeader, 0x30 );
#pragma pack( pop )

struct Texture2DB {
    std::string Name;
    uint64_t Hashcode = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Depth = 1;
    uint32_t NumMips = 1;
    uint32_t Flags = 0;
    eViewFormat Format = VF_Invalid;
    const uint8_t *pTexels = nullptr;
    uint32_t TexelsSize = 0;

    bool parse( const uint8_t *pFile, uint32_t fileSize );
    // Cube map: 6 faces, face major (each face holds its whole mip chain).
    // Flags bit 0x40 [DATA: cube 2DBs have 0xC4 / 0xC8, 2D ones 0x84 / 0x88].
    inline bool isCube() const { return Depth == 6 && ( Flags & 0x40 ) != 0; }
    uint32_t getMipOffset( uint32_t mipIndex ) const;
    uint32_t getMipSize( uint32_t mipIndex ) const;
};

// Software decoding of DXT1/3/5 blocks to RGBA8 (fallback for devices
// without BC support).
void DecodeBlockCompressed( const uint8_t *pSource,
                            uint32_t width,
                            uint32_t height,
                            eViewFormat format,
                            uint8_t *pDestinationRGBA );
