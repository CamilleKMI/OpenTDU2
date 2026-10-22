#include "shared.h"
#include "vmf_file.h"

namespace {
template <typename T> T Read( const std::vector<uint8_t> &data, uint32_t offset ) {
    T value{};
    if ( offset + sizeof( T ) <= data.size() ) {
        memcpy( &value, data.data() + offset, sizeof( T ) );
    }
    return value;
}
} // namespace

bool VMFFile::load( const uint8_t *pBuffer, uint32_t size ) {
    data.assign( pBuffer, pBuffer + size );
    tags.clear();
    fonts.clear();
    bitmaps.clear();
    exports.clear();
    frameLabels.clear();

    if ( size < 0x13 || memcmp( pBuffer, "FMV", 3 ) != 0 ) {
        OTDU_LOG_WARN( "Invalid VMF header\n" );
        return false;
    }

    version = data[3];
    stageWidth = Read<uint16_t>( data, 0x0a ) / 20.0f;
    stageHeight = Read<uint16_t>( data, 0x0c ) / 20.0f;
    frameRate = Read<uint16_t>( data, 0x0e ) / 256.0f;
    frameCount = Read<uint16_t>( data, 0x10 );

    uint32_t offset = 0x13;
    if ( data[0x12] != 0 ) {
        uint32_t numPreload = Read<uint32_t>( data, offset );
        offset += 4 + numPreload * 4;
    }

    tagsStart = offset;
    if ( !parseTags( offset, size, tags ) ) {
        return false;
    }

    // Definition tags may live in Eden sprites too; fonts and bitmaps are only
    // at the root level in every file checked so far.
    for ( const FlashTag &tag : tags ) {
        switch ( tag.Code ) {
        case FT_DefineFontEden:
            parseFont( tag );
            break;
        case FT_DefineBitmapEden:
            parseBitmap( tag );
            break;
        case FT_ExportAssets:
            parseExports( tag );
            break;
        case FT_FrameLabel: {
            const char *pLabel = reinterpret_cast<const char *>( data.data() + tag.Offset );
            frameLabels.emplace_back( pLabel, strnlen( pLabel, tag.Length ) );
            break;
        }
        default:
            break;
        }
    }
    return true;
}

bool VMFFile::parseTags( uint32_t start, uint32_t end, std::vector<FlashTag> &outTags ) const {
    uint32_t offset = start;
    while ( offset + 2 <= end ) {
        uint16_t header = Read<uint16_t>( data, offset );
        offset += 2;
        FlashTag tag;
        tag.Code = header >> 6;
        tag.Length = header & 0x3f;
        if ( tag.Length == 0x3f ) {
            tag.Length = Read<uint32_t>( data, offset );
            offset += 4;
        }
        tag.Offset = offset;
        if ( offset + tag.Length > end ) {
            OTDU_LOG_WARN( "Truncated VMF tag %u\n", tag.Code );
            return false;
        }
        outTags.push_back( tag );
        if ( tag.Code == FT_End ) {
            break;
        }
        offset += tag.Length;
    }
    return true;
}

void VMFFile::parseFont( const FlashTag &tag ) {
    FlashFontDesc font;
    uint32_t offset = tag.Offset;
    const uint32_t end = tag.Offset + tag.Length;
    font.CharacterId = Read<uint16_t>( data, offset );
    font.TextureId = Read<uint16_t>( data, offset + 2 );
    font.Flags = data[offset + 4];
    offset += 5;

    const char *pName = reinterpret_cast<const char *>( data.data() + offset );
    font.Name.assign( pName, strnlen( pName, end - offset ) );
    offset += static_cast<uint32_t>( font.Name.size() ) + 1;

    const uint16_t numGlyphs = Read<uint16_t>( data, offset );
    const uint16_t numTexturedGlyphs = Read<uint16_t>( data, offset + 2 );
    offset += 4;

    auto readGlyphs = [&]( uint32_t count, std::vector<FlashGlyph> &outGlyphs ) {
        for ( uint32_t i = 0; i < count && offset + 26 <= end; i++ ) {
            FlashGlyph glyph;
            glyph.Code = Read<uint16_t>( data, offset );
            glyph.U0 = Read<float>( data, offset + 2 );
            glyph.U1 = Read<float>( data, offset + 6 );
            glyph.V0 = Read<float>( data, offset + 10 );
            glyph.V1 = Read<float>( data, offset + 14 );
            glyph.Width = Read<int16_t>( data, offset + 18 );
            glyph.Height = Read<int16_t>( data, offset + 20 );
            glyph.OffsetX = Read<int16_t>( data, offset + 22 );
            glyph.OffsetY = Read<int16_t>( data, offset + 24 );
            outGlyphs.push_back( glyph );
            offset += 26;
        }
    };
    readGlyphs( numTexturedGlyphs, font.Glyphs );
    if ( font.TextureId != 0 ) {
        readGlyphs( numGlyphs, font.SecondaryGlyphs );
    }
    // Layout table: shape offsets, codes, metrics and advances.
    offset += numGlyphs * 2u;
    const uint32_t codesOffset = offset;
    offset += numGlyphs * 2u;
    if ( offset + 6u + numGlyphs * 2u <= end ) {
        font.Ascent = Read<int16_t>( data, offset );
        font.Descent = Read<int16_t>( data, offset + 2 );
        font.Leading = Read<int16_t>( data, offset + 4 );
        const uint32_t advancesOffset = offset + 6;
        std::unordered_map<uint16_t, int16_t> advances;
        for ( uint32_t i = 0; i < numGlyphs; i++ ) {
            advances[Read<uint16_t>( data, codesOffset + i * 2 )] = Read<int16_t>( data, advancesOffset + i * 2 );
        }
        for ( FlashGlyph &glyph : font.Glyphs ) {
            auto it = advances.find( glyph.Code );
            if ( it != advances.end() ) {
                glyph.Advance = it->second;
            }
        }
    }
    fonts.push_back( std::move( font ) );
}

void VMFFile::parseBitmap( const FlashTag &tag ) {
    if ( tag.Length < 22 ) {
        return;
    }
    FlashBitmapDesc bitmap;
    bitmap.CharacterId = Read<uint16_t>( data, tag.Offset );
    bitmap.Width = Read<uint16_t>( data, tag.Offset + 2 );
    bitmap.Height = Read<uint16_t>( data, tag.Offset + 4 );
    bitmap.U0 = Read<float>( data, tag.Offset + 6 );
    bitmap.V0 = Read<float>( data, tag.Offset + 10 );
    bitmap.U1 = Read<float>( data, tag.Offset + 14 );
    bitmap.V1 = Read<float>( data, tag.Offset + 18 );
    bitmaps.push_back( bitmap );
}

void VMFFile::parseExports( const FlashTag &tag ) {
    uint32_t offset = tag.Offset;
    const uint32_t end = tag.Offset + tag.Length;
    const uint16_t count = Read<uint16_t>( data, offset );
    offset += 2;
    for ( uint32_t i = 0; i < count && offset + 2 < end; i++ ) {
        const uint16_t characterId = Read<uint16_t>( data, offset );
        offset += 2;
        const char *pName = reinterpret_cast<const char *>( data.data() + offset );
        std::string name( pName, strnlen( pName, end - offset ) );
        offset += static_cast<uint32_t>( name.size() ) + 1;
        exports[name] = characterId;
    }
}

const FlashFontDesc *VMFFile::findFont( const char *pName ) const {
    for ( const FlashFontDesc &font : fonts ) {
        if ( StringCmpI( font.Name.c_str(), pName ) == 0 ) {
            return &font;
        }
    }
    return nullptr;
}

const FlashBitmapDesc *VMFFile::findBitmap( uint16_t characterId ) const {
    for ( const FlashBitmapDesc &bitmap : bitmaps ) {
        if ( bitmap.CharacterId == characterId ) {
            return &bitmap;
        }
    }
    return nullptr;
}

const FlashFontDesc *VMFFile::findFont( uint16_t characterId ) const {
    for ( const FlashFontDesc &font : fonts ) {
        if ( font.CharacterId == characterId ) {
            return &font;
        }
    }
    return nullptr;
}
