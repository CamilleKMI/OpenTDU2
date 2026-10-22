#pragma once

// .vmf: Eden flash movie (TDU2 frontend). This is a SWF 6 tag stream with a
// custom header and a few Eden specific tags:
//
//   0x00 char[3] "FMV"
//   0x03 u8      SWF version (4 or 6)
//   0x04 u16     Unknown (6)
//   0x06 u32     File size
//   0x0A u16     Stage width (twips)
//   0x0C u16     Stage height (twips)
//   0x0E u16     Frame rate (8.8 fixed point)
//   0x10 u16     Frame count
//   0x12 u8      Has preload table
//   [u32 count; { u16 characterId; u16 flags }[count]]  if preload table
//   Tags (standard SWF RECORDHEADER: u16 code << 6 | length, long length if 0x3f)
//
// Eden tags:
//   125 DefineFontEden   textured font (glyph atlas in a .2db of the bank)
//   126 DefineBitmapEden bitmap = UV rectangle in a texture page (.2db)
//   127 DefineSpriteEden sprite (replaces SWF DefineSprite / 39)
enum eFlashTag : uint16_t {
    FT_End = 0,
    FT_ShowFrame = 1,
    FT_DefineShape = 2,
    FT_PlaceObject = 4,
    FT_RemoveObject = 5,
    FT_SetBackgroundColor = 9,
    FT_DefineText = 11,
    FT_DoAction = 12,
    FT_DefineShape2 = 22,
    FT_PlaceObject2 = 26,
    FT_RemoveObject2 = 28,
    FT_DefineShape3 = 32,
    FT_DefineText2 = 33,
    FT_DefineButton2 = 34,
    FT_DefineEditText = 37,
    FT_DefineSprite = 39,
    FT_FrameLabel = 43,
    FT_DefineMorphShape = 46,
    FT_ExportAssets = 56,
    FT_DefineFontName = 88,
    FT_DefineFontEden = 125,
    FT_DefineBitmapEden = 126,
    FT_DefineSpriteEden = 127,
};

struct FlashTag {
    uint16_t Code = 0;
    uint32_t Offset = 0; // Offset of the tag payload in the file
    uint32_t Length = 0;
};

struct FlashGlyph {
    uint16_t Code = 0;
    float U0 = 0.0f, U1 = 0.0f, V0 = 0.0f, V1 = 0.0f;
    int16_t Width = 0;   // Font units (1024 per em)
    int16_t Height = 0;
    int16_t OffsetX = 0;
    int16_t OffsetY = 0; // Distance from the baseline to the top of the glyph
    int16_t Advance = 0; // Font units (from the DefineFont3-like layout table)
};

// DefineFontEden (tag 125) layout:
//   u16 id, u16 textureId, u8 flags, name\0, u16 numGlyphs, u16 numTextured
//   numTextured x 26B glyphs -> texture fla_<id>.2db (1024 units per em)
//   [textureId != 0] numGlyphs x 26B glyphs -> texture fla_<textureId>.2db
//     (large, padded rendition; different unit scale, unused for now)
//   numGlyphs x u16 shape offsets (empty), numGlyphs x u16 codes,
//   i16 ascent, i16 descent, i16 leading, numGlyphs x i16 advances, ...
struct FlashFontDesc {
    uint16_t CharacterId = 0;
    uint16_t TextureId = 0;
    uint8_t Flags = 0;
    std::string Name;
    int16_t Ascent = 0;
    int16_t Descent = 0;
    int16_t Leading = 0;
    std::vector<FlashGlyph> Glyphs;          // fla_<CharacterId>
    std::vector<FlashGlyph> SecondaryGlyphs; // fla_<TextureId>
};
struct FlashBitmapDesc {
    uint16_t CharacterId = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;
};

class VMFFile {
public:
    inline float getStageWidth() const { return stageWidth; }
    inline float getStageHeight() const { return stageHeight; }
    inline float getFrameRate() const { return frameRate; }
    inline uint16_t getFrameCount() const { return frameCount; }
    inline uint8_t getVersion() const { return version; }
    inline const std::vector<FlashTag> &getTags() const { return tags; }
    inline const std::vector<FlashFontDesc> &getFonts() const { return fonts; }
    inline const std::vector<FlashBitmapDesc> &getBitmaps() const { return bitmaps; }
    inline const std::map<std::string, uint16_t> &getExports() const { return exports; }
    inline const std::vector<std::string> &getFrameLabels() const { return frameLabels; }
    inline const std::vector<uint8_t> &getData() const { return data; }

public:
    bool load( const uint8_t *pBuffer, uint32_t size );

    const FlashFontDesc *findFont( const char *pName ) const;
    const FlashFontDesc *findFont( uint16_t characterId ) const;
    bool parseTags( uint32_t start, uint32_t end, std::vector<FlashTag> &outTags ) const;
    inline uint32_t getTagsStart() const { return tagsStart; }
    const FlashBitmapDesc *findBitmap( uint16_t characterId ) const;

private:
    std::vector<uint8_t> data;
    std::vector<FlashTag> tags;
    std::vector<FlashFontDesc> fonts;
    std::vector<FlashBitmapDesc> bitmaps;
    std::map<std::string, uint16_t> exports;
    std::vector<std::string> frameLabels;
    float stageWidth = 0.0f;
    float stageHeight = 0.0f;
    float frameRate = 0.0f;
    uint16_t frameCount = 0;
    uint8_t version = 0;

private:
    uint32_t tagsStart = 0;
    void parseFont( const FlashTag &tag );
    void parseBitmap( const FlashTag &tag );
    void parseExports( const FlashTag &tag );
};
