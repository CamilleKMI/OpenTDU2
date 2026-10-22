#include "shared.h"
#include "font.h"

#include "flash/vmf_file.h"


namespace {
constexpr float kEdenFontUnitsPerEm = 1024.0f;
} // namespace

uint32_t Font::DecodeUTF8( const char *&pText ) {
    const uint8_t c = static_cast<uint8_t>( *pText++ );
    if ( c < 0x80 ) {
        return c;
    }
    auto continuation = [&]() -> uint32_t {
        const uint8_t next = static_cast<uint8_t>( *pText );
        if ( ( next & 0xc0 ) != 0x80 ) {
            return 0;
        }
        pText++;
        return next & 0x3f;
    };
    if ( ( c & 0xe0 ) == 0xc0 ) {
        return ( ( c & 0x1f ) << 6 ) | continuation();
    }
    if ( ( c & 0xf0 ) == 0xe0 ) {
        uint32_t c1 = continuation();
        return ( ( c & 0x0f ) << 12 ) | ( c1 << 6 ) | continuation();
    }
    if ( ( c & 0xf8 ) == 0xf0 ) {
        uint32_t c1 = continuation();
        uint32_t c2 = continuation();
        return ( ( c & 0x07 ) << 18 ) | ( c1 << 12 ) | ( c2 << 6 ) | continuation();
    }
    return c; // Latin-1 fallback
}

bool Font::createFromFlash( const FlashFontDesc &desc,
                            const std::vector<FlashGlyph> &sourceGlyphs,
                            GPUTexture *pAtlas ) {
    if ( pAtlas == nullptr || sourceGlyphs.empty() ) {
        return false;
    }
    name = desc.Name;
    pTexture = pAtlas;
    glyphs.clear();

    float maxAscent = 0.0f;
    for ( const FlashGlyph &source : sourceGlyphs ) {
        if ( source.Code == ' ' || source.Width < 0 || source.Height < 0 || source.Width > 4096 ||
             source.Height > 4096 ) {
            continue;
        }
        Glyph glyph;
        glyph.U0 = source.U0;
        glyph.U1 = source.U1;
        glyph.V0 = source.V0;
        glyph.V1 = source.V1;
        // Quad at pen - OffsetX (FUN_0083de00).
        glyph.X0 = -source.OffsetX / kEdenFontUnitsPerEm;
        glyph.Y0 = -source.OffsetY / kEdenFontUnitsPerEm;
        glyph.X1 = glyph.X0 + source.Width / kEdenFontUnitsPerEm;
        glyph.Y1 = glyph.Y0 + source.Height / kEdenFontUnitsPerEm;
        // (short) advance of the font table, no letter spacing (FUN_00849dd0).
        glyph.Advance = source.Advance / kEdenFontUnitsPerEm;
        glyphs[source.Code] = glyph;
        maxAscent = Max( maxAscent, source.OffsetY / kEdenFontUnitsPerEm );
    }
    ascent = desc.Ascent > 0 ? desc.Ascent / kEdenFontUnitsPerEm : ( maxAscent > 0.0f ? maxAscent : 0.8f );
    lineHeight = desc.Ascent > 0 ? ( desc.Ascent + desc.Descent + desc.Leading ) / kEdenFontUnitsPerEm
                                 : ascent + 0.35f;
    emLeading = desc.Leading / kEdenFontUnitsPerEm;
    spaceAdvance = 80.0f / kEdenFontUnitsPerEm; // no space glyph: 80 units (FUN_00849a70)
    for ( const FlashGlyph &source : sourceGlyphs ) {
        if ( source.Code == ' ' && source.Advance > 0 ) {
            spaceAdvance = source.Advance / kEdenFontUnitsPerEm;
        }
    }
    bAlphaOnly = false;
    OTDU_LOG_INFO( "Font '%s' (id %u): %zu glyphs\n", name.c_str(), desc.CharacterId,
                   glyphs.size() );
    return !glyphs.empty();
}

const Font::Glyph *Font::findGlyph( uint32_t codepoint ) const {
    auto it = glyphs.find( codepoint );
    if ( it != glyphs.end() ) {
        return &it->second;
    }
    return nullptr;
}

float Font::measureText( const char *pText, float size, float letterSpacing ) const {
    float width = 0.0f;
    float lineWidth = 0.0f;
    while ( *pText != '\0' ) {
        uint32_t codepoint = DecodeUTF8( pText );
        if ( codepoint == '\n' ) {
            width = Max( width, lineWidth );
            lineWidth = 0.0f;
            continue;
        }
        const Glyph *pGlyph = findGlyph( codepoint );
        lineWidth += ( pGlyph != nullptr ? pGlyph->Advance : spaceAdvance ) * size + letterSpacing;
    }
    return Max( width, lineWidth );
}
