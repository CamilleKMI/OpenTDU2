#pragma once

#include "filesystem/bank.h"
#include "flash/vmf_file.h"
#include "render/font.h"
#include "render/renderer_2d.h"

struct GPUTexture;

// A frontend bank (Euro/Bnk/FrontEnd/<Res>/<Name>.bnk): flash movie (.vmf),
// its materials (.2dm) and textures (.2db: glyph atlases, standalone bitmaps
// "fla_<characterId>" and atlas pages "page_<n>").
//
// The SWF display list / ActionScript runtime is not implemented yet: game
// modes currently compose their screens from the movie resources (bitmaps,
// fonts) natively.
class FlashMovie {
public:
    struct BitmapRef {
        GPUTexture *pTexture = nullptr;
        Rect2D UV;
        float Width = 0.0f;
        float Height = 0.0f;
        inline bool isValid() const { return pTexture != nullptr; }
    };

    inline const VMFFile &getVMF() const { return vmf; }
    inline const std::string &getName() const { return name; }
    inline bool isLoaded() const { return bLoaded; }

public:
    FlashMovie();
    ~FlashMovie();

    // pName: "intro", "log_in", "main"... pResolution: "HiRes" / "MedRes" / "LowRes"
    bool load( const char *pName, const char *pResolution = "HiRes" );
    void unload();

    BitmapRef getBitmap( uint16_t characterId ) const;
    // Raw texture of the bank ("fla_0012", "page_001"...).
    BitmapRef getTexture( const char *pTextureName ) const;

    // Fonts: "Berlin Sans FB", ... (bRegular selects the regular rendition
    // when the font ships both).
    const Font *getFont( const char *pFontName, bool bRegular = true ) const;
    const Font *getDefaultFont() const;

private:
    struct FontEntry {
        std::string Name;
        std::unique_ptr<Font> pRegular;
        std::unique_ptr<Font> pBold;
    };

    std::string name;
    std::string owner;
    Bank bank;
    VMFFile vmf;
    std::vector<FontEntry> fonts;
    std::vector<std::pair<std::string, GPUTexture *>> pages;
    bool bLoaded;

private:
    void createFonts();
    GPUTexture *findTexture( const std::string &textureName ) const;
};
