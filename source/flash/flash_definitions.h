#pragma once

#include "flash/flash_types.h"
#include "flash/vmf_file.h"

struct GPUTexture;
class FlashMovie;

// Character definitions of a VMF movie, decoded once at load time.

enum class eFlashFillType : uint8_t { FF_None = 0, FF_Solid, FF_Bitmap, FF_LinearGradient, FF_RadialGradient };

struct FlashFill {
    eFlashFillType Type = eFlashFillType::FF_None;
    uint32_t Color = 0xffffffff; // RGBA8 (solid fills)
    uint16_t BitmapId = 0;
    bool bRepeat = false;
    FlashMatrix InverseMatrix;   // shape space -> bitmap pixels / gradient square
    GPUTexture *pTexture = nullptr;
    float U0 = 0.0f, V0 = 0.0f, U1 = 1.0f, V1 = 1.0f; // bitmap UV rectangle
    float BitmapWidth = 1.0f, BitmapHeight = 1.0f;
    std::vector<std::pair<uint8_t, uint32_t>> GradientStops; // ratio, RGBA
};

// Triangulated region of a shape sharing one fill style.
struct FlashShapeMesh {
    uint32_t FillIndex = 0;
    std::vector<float> Positions; // x, y pairs (twips)
    std::vector<uint32_t> Indices;
};

struct FlashShape {
    float XMin = 0.0f, XMax = 0.0f, YMin = 0.0f, YMax = 0.0f;
    std::vector<FlashFill> Fills;
    std::vector<FlashShapeMesh> Meshes;
};

enum class eFlashControlType : uint8_t { CT_Place, CT_Remove, CT_Action, CT_Label };

struct FlashPlaceInfo {
    uint16_t Depth = 0;
    bool bMove = false;
    bool bHasCharacter = false;
    uint16_t CharacterId = 0;
    bool bHasMatrix = false;
    FlashMatrix Matrix;
    bool bHasColorTransform = false;
    FlashColorTransform ColorTransform;
    bool bHasRatio = false;
    uint16_t Ratio = 0;
    bool bHasName = false;
    std::string Name;
    uint16_t ClipDepth = 0;
};

struct FlashControlTag {
    eFlashControlType Type;
    FlashPlaceInfo Place;          // CT_Place
    uint16_t Depth = 0;            // CT_Remove
    uint32_t ActionOffset = 0;     // CT_Action (offset in the VMF data)
    uint32_t ActionLength = 0;
    std::string Label;             // CT_Label
};

struct FlashTimeline {
    uint16_t FrameCount = 0;
    std::vector<std::vector<FlashControlTag>> Frames;
    std::map<std::string, uint32_t> Labels; // lower case
};

struct FlashEditText {
    float XMin = 0.0f, XMax = 0.0f, YMin = 0.0f, YMax = 0.0f;
    uint16_t FontId = 0;
    float FontHeight = 240.0f; // twips
    uint32_t Color = 0xffffffff;
    uint8_t Align = 0;         // 0 left, 1 right, 2 center, 3 justify
    float Leading = 0.0f;
    bool bMultiline = false; // line breaking on (FUN_00849730; WordWrap is not tested)
    bool bWordWrap = false;
    bool bReadOnly = false;  // #markup# and "..." on overflow
    bool bPassword = false;
    uint16_t LeftMargin = 0, RightMargin = 0, Indent = 0;
    std::string VariableName;
    std::string Text;          // default text (leading '_' = localized key)
};

struct FlashButtonRecord {
    uint8_t States = 0; // 1 up, 2 over, 4 down, 8 hit test
    uint16_t CharacterId = 0;
    uint16_t Depth = 0;
    FlashMatrix Matrix;
    FlashColorTransform ColorTransform;
};

// BUTTONCONDACTION: u16 Size; u16 Conditions (bits 0..8 mouse
// transitions, bits 9..15 key code); actions.
struct FlashButtonCondition {
    uint16_t Conditions = 0;
    uint32_t ActionOffset = 0;
    uint32_t ActionLength = 0;
};

struct FlashButton {
    std::vector<FlashButtonRecord> Records;
    std::vector<FlashButtonCondition> Conditions;
};

enum class eFlashCharacterType : uint8_t { CT_None, CT_Shape, CT_Sprite, CT_EditText, CT_Button, CT_Bitmap, CT_Font };

class FlashDefinitions {
public:
    inline const FlashTimeline &getRootTimeline() const { return root; }
    eFlashCharacterType getType( uint16_t id ) const;
    const FlashShape *getShape( uint16_t id ) const;
    const FlashTimeline *getSprite( uint16_t id ) const;
    const FlashEditText *getEditText( uint16_t id ) const;
    const FlashButton *getButton( uint16_t id ) const;

public:
    bool build( const VMFFile &vmf, FlashMovie *pMovie );
    void destroy();

private:
    FlashTimeline root;
    std::unordered_map<uint16_t, FlashShape> shapes;
    std::unordered_map<uint16_t, FlashTimeline> sprites;
    std::unordered_map<uint16_t, FlashEditText> editTexts;
    std::unordered_map<uint16_t, FlashButton> buttons;
    std::unordered_map<uint16_t, eFlashCharacterType> types;
    std::vector<GPUTexture *> gradientTextures;

private:
    void buildTimeline( const VMFFile &vmf, const std::vector<FlashTag> &tags, FlashTimeline &outTimeline,
                        FlashMovie *pMovie );
    void parseShape( const VMFFile &vmf, const FlashTag &tag, FlashMovie *pMovie );
    void parseEditText( const VMFFile &vmf, const FlashTag &tag );
    void parseButton( const VMFFile &vmf, const FlashTag &tag );
    FlashPlaceInfo parsePlace( const VMFFile &vmf, const FlashTag &tag );
    GPUTexture *createGradientTexture( const FlashFill &fill );
};
