#pragma once

#include "flash/flash_definitions.h"

class FlashMovie;
class Renderer2D;
class Font;

// Runtime of an Eden flash movie (.vmf): display list, timelines (root and
// sprites), a subset of the ActionScript 1 virtual machine (timeline control,
// variables, user functions, movie clip properties) and rendering through the
// 2D renderer (original TD_Flash.sob shaders).
class FlashPlayer {
public:
    using FSCommandCallback = std::function<void( const std::string &command, const std::string &args )>;
    // '#command:<ACTION>#' markup of read only text fields (FUN_00849dd0:
    // the global resolver DAT_013e281c, vtable 0x00ee6ff0, vfunc +4 =
    // FUN_00d87cc0 returns a markup string laid out in place, recursively).
    // Empty result: the markup is dropped.
    using CommandResolver = std::function<std::string( const std::string &action )>;
    static void SetCommandResolver( CommandResolver resolver );
    // Pad form of FUN_00d87cc0 for a key map entry whose DevSubType (+0x179)
    // is 3 (X360 pad): channel name -> "#bitmap:x360_..#" (table
    // FUN_00d877b0, strings 0x00ee6270..0x00ee64b8). Empty for an unknown
    // channel (0x00f35018 "").
    static std::string PadChannelMarkup( const std::string &channel );
    // Keyboard form of FUN_00d87cc0 (format 0x00ee61f4, key map entry +0x60
    // ChannelLocName).
    static std::string KeyboardChannelMarkup( const std::string &channelLocName );

    // '#bitmap:<name>#' inline images: global registry DAT_013e2720 filled
    // from the named children of a "$$bitmaps$$" sprite (FUN_0085ad40 /
    // FUN_00dd8830; GENERAL.vmf ch612: x360_* and PS3_* button icons).
    struct TextBitmap {
        const FlashPlayer *pOwner = nullptr;
        GPUTexture *pTexture = nullptr;
        float U0 = 0.0f, V0 = 0.0f, U1 = 1.0f, V1 = 1.0f;
        float Width = 0.0f, Height = 0.0f; // bitmap pixels (object +0xa / +0xc)
        float ScaleX = 1.0f, ScaleY = 1.0f; // placement scale in "$$bitmaps$$"
    };
    static const TextBitmap *FindTextBitmap( const std::string &name );

    struct Clip;
    struct Object;

    struct Value {
        enum class eType : uint8_t { Undefined, Null, Bool, Number, String, Clip, Object, Function };
        eType Type = eType::Undefined;
        double Number = 0.0;
        std::string String;
        Clip *pClip = nullptr;
        std::shared_ptr<Object> pObject;
        int32_t FunctionIndex = -1;

        static Value MakeNumber( double v ) { Value r; r.Type = eType::Number; r.Number = v; return r; }
        static Value MakeString( std::string v ) { Value r; r.Type = eType::String; r.String = std::move( v ); return r; }
        static Value MakeBool( bool v ) { Value r; r.Type = eType::Bool; r.Number = v ? 1.0 : 0.0; return r; }
        double toNumber() const;
        std::string toString() const;
        bool toBool() const;
    };

    struct Object {
        std::map<std::string, Value> Members;
    };

    struct Instance {
        uint16_t CharacterId = 0;
        uint16_t Depth = 0;
        FlashMatrix Matrix;
        FlashColorTransform ColorTransform;
        uint16_t Ratio = 0;
        uint16_t ClipDepth = 0;
        std::string Name;
        bool bVisible = true;
        std::unique_ptr<Clip> pClip; // sprite instances
    };

    struct Clip {
        const FlashTimeline *pTimeline = nullptr;
        Clip *pParent = nullptr;
        Instance *pInstance = nullptr; // owning display object (nullptr for the root)
        uint32_t CurrentFrame = 0;
        bool bPlaying = true;
        std::map<uint16_t, std::unique_ptr<Instance>> DisplayList;
        std::map<std::string, Value> Variables;
    };

public:
    inline bool isLoaded() const { return pMovie != nullptr; }
    inline FlashMovie *getMovie() const { return pMovie; }
    inline Clip *getRoot() { return &root; }
    void setFSCommandCallback( FSCommandCallback callback ) { fsCommandCallback = std::move( callback ); }

public:
    FlashPlayer();
    ~FlashPlayer();

    bool load( FlashMovie *pFlashMovie );
    void unload();

    // Advances the movie (at its own frame rate).
    void tick( float deltaTime );
    // Draws the stage onto the destination rect of the screen ratio mode
    // (SystemPC.cpr SetScreenRatio; 16:9: stage y 120..840, full width).
    void draw( Renderer2D &renderer );
    // Draws one clip only (with its parents' transforms), e.g. the letterbox
    // "/black_stripes" of GENERAL.vmf.
    void drawClipOnly( Renderer2D &renderer, const std::string &path );

    // Scripting interface used by the game (paths: "/:var", "/clip/sub:var").
    void setVariable( const std::string &path, const std::string &value );
    // Typed variants (edFlash variant types 5 = bool, 7 = int).
    void setVariableBool( const std::string &path, bool bValue );
    void setVariableNumber( const std::string &path, double value );
    std::string getVariable( const std::string &path );
    bool gotoLabel( const std::string &clipPath, const std::string &label, bool bPlay );
    Clip *findClip( const std::string &path );

    // Button events (DefineButton2 condition actions): key press (SWF key
    // code: 1 left, 2 right, 13 enter...) and mouse release on a button
    // (OverDownToOverUp) at a backbuffer position. True when handled.
    bool sendKey( uint8_t swfKey );
    // Calls an ActionScript function of a clip ("/" root, "/cursor"...) with
    // string arguments (game -> movie calls such as ItemType('over')).
    bool invoke( const std::string &clipPath, const std::string &function, const std::vector<std::string> &args );
    // MovieClip _visible (edFlash vfunc +0x20 on a clip).
    bool setClipVisible( const std::string &clipPath, bool bVisible );
    bool sendMouseRelease( float screenX, float screenY );

private:
    struct Function {
        uint32_t Offset = 0;
        uint32_t Length = 0;
        std::vector<std::string> Parameters;
        uint8_t NumRegisters = 4;
        std::vector<std::pair<uint8_t, std::string>> RegisterParameters; // DefineFunction2
        uint16_t Flags = 0;
        std::vector<std::string> Constants; // constant pool at definition
    };

    struct PendingAction {
        Clip *pClip;
        uint32_t Offset;
        uint32_t Length;
    };

    FlashMovie *pMovie;
    FlashDefinitions definitions;
    Clip root;
    float frameTime;
    float accumulator;
    std::vector<PendingAction> pendingActions;
    std::vector<std::unique_ptr<Instance>> graveyard;
    std::vector<Function> functions;
    std::map<std::string, Value> globals;
    FSCommandCallback fsCommandCallback;
    int32_t actionDepth;
    uint32_t maskLevel = 0; // nesting of the masks being drawn
    uint32_t maskDrawing = 0;

private:
    // Timeline
    void advanceClip( Clip &clip );
    void gotoFrame( Clip &clip, uint32_t frame, bool bQueueActions );
    void applyControlTags( Clip &clip, uint32_t frame, bool bQueueActions, bool bQueueChildActions = false );
    void placeObject( Clip &clip, const FlashPlaceInfo &place, bool bQueueActions );
    void removeObject( Clip &clip, uint16_t depth );
    void initClip( Instance &instance, Clip *pParent, bool bQueueActions );
    void registerTextBitmap( const Instance &instance );
    void runPendingActions();
    bool resolveFrame( Clip &clip, const Value &frame, uint32_t &outFrame );

    // ActionScript
    Value execute( Clip *pClip, uint32_t offset, uint32_t length, std::map<std::string, Value> *pLocals,
                   std::vector<Value> *pRegisters, Value *pThis,
                   const std::vector<std::string> *pConstants = nullptr );
    Value callFunction( int32_t index, Clip *pClip, const std::vector<Value> &args, Value *pThis );
    Value getMember( const Value &object, const std::string &name );
    void setMember( Value &object, const std::string &name, const Value &value );
    Value getProperty( Clip *pClip, int32_t index );
    void setProperty( Clip *pClip, int32_t index, const Value &value );
    Value lookupVariable( Clip *pClip, const std::string &name, std::map<std::string, Value> *pLocals );
    void assignVariable( Clip *pClip, const std::string &name, const Value &value,
                         std::map<std::string, Value> *pLocals );
    Clip *resolveTarget( Clip *pClip, const std::string &target );
    Value callMethod( Value &object, const std::string &name, const std::vector<Value> &args, Clip *pClip );

    bool sendKeyToClip( Clip &clip, uint8_t swfKey );
    bool sendMouseToClip( Clip &clip, const FlashMatrix &matrix, float stageX, float stageY );
    void stageRect( float &x0, float &y0, float &width, float &height ) const;

    // Rendering
    void beginStage( Renderer2D &renderer ) const;
    void drawClip( Renderer2D &renderer, const Clip &clip, const FlashMatrix &matrix,
                   const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas );
    void drawInstance( Renderer2D &renderer, const Instance &instance, const FlashMatrix &matrix,
                       const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas );
    void drawShape( Renderer2D &renderer, const FlashShape &shape, const FlashMatrix &matrix,
                    const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas );
    void drawEditText( Renderer2D &renderer, const FlashEditText &text, const Clip &owner, const FlashMatrix &matrix,
                       const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas );
    const Font *findFont( uint16_t fontId ) const;
};
