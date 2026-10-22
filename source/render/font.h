#pragma once

#include "render/vulkan/render_device.h"

struct FlashFontDesc;
struct FlashGlyph;

// Bitmap font used by the 2D renderer, built from a TDU2 textured flash font
// (DefineFontEden tag + glyph atlas .2db of the same bank).
class Font {
public:
    struct Glyph {
        float U0, V0, U1, V1;
        float X0, Y0, X1, Y1; // Quad relative to the pen (em units, Y down)
        float Advance;        // em units
    };

    inline GPUTexture *getTexture() const { return pTexture; }
    inline const std::string &getName() const { return name; }
    inline bool isAlphaOnly() const { return bAlphaOnly; }

public:
    bool createFromFlash( const FlashFontDesc &desc,
                          const std::vector<FlashGlyph> &sourceGlyphs,
                          GPUTexture *pAtlas );

    const Glyph *findGlyph( uint32_t codepoint ) const;
    float measureText( const char *pText, float size, float letterSpacing = 0.0f ) const;
    float getLineHeight() const { return lineHeight; }
    float getAscent() const { return ascent; }
    float getEmLeading() const { return emLeading; }       // font Leading (em)
    float getSpaceAdvance() const { return spaceAdvance; } // em

    static uint32_t DecodeUTF8( const char *&pText );

private:
    std::string name;
    GPUTexture *pTexture = nullptr;
    std::unordered_map<uint32_t, Glyph> glyphs;
    float lineHeight = 1.2f;
    float ascent = 0.8f;
    float emLeading = 0.0f;
    float spaceAdvance = 0.28f;
    bool bAlphaOnly = false;
};
