#include "shared.h"
#include "flash_player.h"

#include "config/gs_config.h"

#include "flash/flash_movie.h"
#include "flash/gs_flash.h"
#include "system/cmd_line.h"
#include "render/font.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"

#include <SDL3/SDL_timer.h>

namespace {
template <typename T> T Read( const std::vector<uint8_t> &data, uint32_t offset ) {
    T value{};
    if ( offset + sizeof( T ) <= data.size() ) {
        memcpy( &value, data.data() + offset, sizeof( T ) );
    }
    return value;
}

std::string ReadString( const std::vector<uint8_t> &data, uint32_t &offset, uint32_t end ) {
    std::string result;
    while ( offset < end && data[offset] != 0 ) {
        result += static_cast<char>( data[offset++] );
    }
    offset++;
    return result;
}

enum eAction : uint8_t {
    A_End = 0x00,
    A_NextFrame = 0x04,
    A_PrevFrame = 0x05,
    A_Play = 0x06,
    A_Stop = 0x07,
    A_ToggleQuality = 0x08,
    A_StopSounds = 0x09,
    A_Add = 0x0a,
    A_Subtract = 0x0b,
    A_Multiply = 0x0c,
    A_Divide = 0x0d,
    A_Equals = 0x0e,
    A_Less = 0x0f,
    A_And = 0x10,
    A_Or = 0x11,
    A_Not = 0x12,
    A_StringEquals = 0x13,
    A_StringLength = 0x14,
    A_StringExtract = 0x15,
    A_Pop = 0x17,
    A_ToInteger = 0x18,
    A_GetVariable = 0x1c,
    A_SetVariable = 0x1d,
    A_SetTarget2 = 0x20,
    A_StringAdd = 0x21,
    A_GetProperty = 0x22,
    A_SetProperty = 0x23,
    A_CloneSprite = 0x24,
    A_RemoveSprite = 0x25,
    A_Trace = 0x26,
    A_StartDrag = 0x27,
    A_EndDrag = 0x28,
    A_StringLess = 0x29,
    A_RandomNumber = 0x30,
    A_MBStringLength = 0x31,
    A_CharToAscii = 0x32,
    A_AsciiToChar = 0x33,
    A_GetTime = 0x34,
    A_MBStringExtract = 0x35,
    A_Delete = 0x3a,
    A_Delete2 = 0x3b,
    A_DefineLocal = 0x3c,
    A_CallFunction = 0x3d,
    A_Return = 0x3e,
    A_Modulo = 0x3f,
    A_NewObject = 0x40,
    A_DefineLocal2 = 0x41,
    A_InitArray = 0x42,
    A_InitObject = 0x43,
    A_TypeOf = 0x44,
    A_TargetPath = 0x45,
    A_Enumerate = 0x46,
    A_Add2 = 0x47,
    A_Less2 = 0x48,
    A_Equals2 = 0x49,
    A_ToNumber = 0x4a,
    A_ToString = 0x4b,
    A_PushDuplicate = 0x4c,
    A_StackSwap = 0x4d,
    A_GetMember = 0x4e,
    A_SetMember = 0x4f,
    A_Increment = 0x50,
    A_Decrement = 0x51,
    A_CallMethod = 0x52,
    A_NewMethod = 0x53,
    A_BitAnd = 0x60,
    A_BitOr = 0x61,
    A_BitXor = 0x62,
    A_BitLShift = 0x63,
    A_BitRShift = 0x64,
    A_BitURShift = 0x65,
    A_StrictEquals = 0x66,
    A_Greater = 0x67,
    A_StringGreater = 0x68,
    A_GotoFrame = 0x81,
    A_GetURL = 0x83,
    A_StoreRegister = 0x87,
    A_ConstantPool = 0x88,
    A_WaitForFrame = 0x8a,
    A_SetTarget = 0x8b,
    A_GoToLabel = 0x8c,
    A_DefineFunction2 = 0x8e,
    A_With = 0x94,
    A_Push = 0x96,
    A_Jump = 0x99,
    A_GetURL2 = 0x9a,
    A_DefineFunction = 0x9b,
    A_If = 0x9d,
    A_Call = 0x9e,
    A_GotoFrame2 = 0x9f,
};

const char *kPropertyNames[] = { "_x",       "_y",       "_xscale",  "_yscale",   "_currentframe",
                                 "_totalframes", "_alpha", "_visible", "_width",  "_height",
                                 "_rotation", "_target", "_framesloaded", "_name", "_droptarget",
                                 "_url",      "_highquality", "_focusrect", "_soundbuftime", "_quality",
                                 "_xmouse",   "_ymouse" };

int32_t PropertyIndex( const std::string &name ) {
    const std::string lower = ToLower( name );
    for ( int32_t i = 0; i < static_cast<int32_t>( std::size( kPropertyNames ) ); i++ ) {
        if ( lower == kPropertyNames[i] ) {
            return i;
        }
    }
    return -1;
}
} // namespace

// --- Values -----------------------------------------------------------------

double FlashPlayer::Value::toNumber() const {
    switch ( Type ) {
    case eType::Number:
    case eType::Bool:
        return Number;
    case eType::String: {
        char *pEnd = nullptr;
        const double v = strtod( String.c_str(), &pEnd );
        return ( pEnd != String.c_str() ) ? v : 0.0;
    }
    default:
        return 0.0;
    }
}

std::string FlashPlayer::Value::toString() const {
    switch ( Type ) {
    case eType::String:
        return String;
    case eType::Number: {
        if ( Number == std::floor( Number ) && std::fabs( Number ) < 1e15 ) {
            return std::to_string( static_cast<int64_t>( Number ) );
        }
        char buffer[64];
        snprintf( buffer, sizeof( buffer ), "%.15g", Number );
        return buffer;
    }
    case eType::Bool:
        return Number != 0.0 ? "true" : "false";
    case eType::Null:
        return "null";
    case eType::Clip:
        return pClip != nullptr && pClip->pInstance != nullptr ? "_level0." + pClip->pInstance->Name : "_level0";
    case eType::Object:
        return "[object Object]";
    default:
        return "";
    }
}

bool FlashPlayer::Value::toBool() const {
    switch ( Type ) {
    case eType::Number:
    case eType::Bool:
        return Number != 0.0;
    case eType::String:
        return !String.empty() && toNumber() != 0.0;
    case eType::Clip:
    case eType::Object:
    case eType::Function:
        return true;
    default:
        return false;
    }
}

// --- Loading ------------------------------------------------------------------

FlashPlayer::FlashPlayer()
    : pMovie( nullptr ), frameTime( 1.0f / 30.0f ), accumulator( 0.0f ), actionDepth( 0 ) {}

FlashPlayer::~FlashPlayer() { unload(); }

bool FlashPlayer::load( FlashMovie *pFlashMovie ) {
    unload();
    if ( pFlashMovie == nullptr || !pFlashMovie->isLoaded() ) {
        return false;
    }
    pMovie = pFlashMovie;
    definitions.build( pMovie->getVMF(), pMovie );
    frameTime = 1.0f / Max( pMovie->getVMF().getFrameRate(), 1.0f );

    root = Clip();
    root.pTimeline = &definitions.getRootTimeline();
    root.CurrentFrame = 0;
    applyControlTags( root, 0, true );
    runPendingActions();
    return true;
}

void FlashPlayer::unload() {
    root.DisplayList.clear();
    root.Variables.clear();
    graveyard.clear();
    pendingActions.clear();
    functions.clear();
    globals.clear();
    definitions.destroy();
    pMovie = nullptr;
}

// --- Timeline -------------------------------------------------------------------

void FlashPlayer::tick( float deltaTime ) {
    if ( pMovie == nullptr ) {
        return;
    }
    accumulator += deltaTime;
    int32_t numFrames = 0;
    while ( accumulator >= frameTime && numFrames < 4 ) {
        accumulator -= frameTime;
        advanceClip( root );
        runPendingActions();
        numFrames++;
    }
    if ( accumulator > frameTime ) {
        accumulator = 0.0f;
    }
    graveyard.clear();
}

void FlashPlayer::advanceClip( Clip &clip ) {
    if ( clip.bPlaying && clip.pTimeline != nullptr && clip.pTimeline->FrameCount > 1 ) {
        uint32_t next = clip.CurrentFrame + 1;
        if ( next >= clip.pTimeline->FrameCount ) {
            next = 0;
        }
        gotoFrame( clip, next, true );
    }
    // Children (copy the list: actions may modify it)
    std::vector<Clip *> children;
    for ( auto &entry : clip.DisplayList ) {
        if ( entry.second->pClip != nullptr ) {
            children.push_back( entry.second->pClip.get() );
        }
    }
    for ( Clip *pChild : children ) {
        advanceClip( *pChild );
    }
}

void FlashPlayer::gotoFrame( Clip &clip, uint32_t frame, bool bQueueActions ) {
    if ( clip.pTimeline == nullptr ) {
        return;
    }
    frame = Min<uint32_t>( frame, clip.pTimeline->FrameCount > 0 ? clip.pTimeline->FrameCount - 1 : 0 );
    if ( frame == clip.CurrentFrame ) {
        return;
    }
    if ( frame < clip.CurrentFrame ) {
        // Rebuild the display list state of the target frame, keeping the
        // instances that are still the same character at the same depth.
        std::map<uint16_t, FlashPlaceInfo> state;
        for ( uint32_t f = 0; f <= frame; f++ ) {
            for ( const FlashControlTag &control : clip.pTimeline->Frames[f] ) {
                if ( control.Type == eFlashControlType::CT_Place ) {
                    FlashPlaceInfo &target = state[control.Place.Depth];
                    const FlashPlaceInfo &place = control.Place;
                    if ( place.bHasCharacter && !place.bMove ) {
                        target = place;
                        continue;
                    }
                    if ( place.bHasCharacter ) {
                        target.CharacterId = place.CharacterId;
                        target.bHasCharacter = true;
                    }
                    if ( place.bHasMatrix ) {
                        target.Matrix = place.Matrix;
                        target.bHasMatrix = true;
                    }
                    if ( place.bHasColorTransform ) {
                        target.ColorTransform = place.ColorTransform;
                        target.bHasColorTransform = true;
                    }
                    if ( place.bHasRatio ) {
                        target.Ratio = place.Ratio;
                    }
                    if ( place.bHasName ) {
                        target.Name = place.Name;
                        target.bHasName = true;
                    }
                    if ( place.ClipDepth != 0 ) {
                        target.ClipDepth = place.ClipDepth;
                    }
                    target.Depth = place.Depth;
                } else if ( control.Type == eFlashControlType::CT_Remove ) {
                    state.erase( control.Depth );
                }
            }
        }
        for ( auto it = clip.DisplayList.begin(); it != clip.DisplayList.end(); ) {
            auto found = state.find( it->first );
            if ( found == state.end() || found->second.CharacterId != it->second->CharacterId ) {
                graveyard.push_back( std::move( it->second ) );
                it = clip.DisplayList.erase( it );
            } else {
                ++it;
            }
        }
        clip.CurrentFrame = frame;
        for ( auto &entry : state ) {
            FlashPlaceInfo place = entry.second;
            if ( !place.bHasCharacter ) {
                continue;
            }
            place.bMove = clip.DisplayList.count( entry.first ) != 0;
            placeObject( clip, place, bQueueActions );
        }
        if ( bQueueActions ) {
            for ( const FlashControlTag &control : clip.pTimeline->Frames[frame] ) {
                if ( control.Type == eFlashControlType::CT_Action ) {
                    pendingActions.push_back( { &clip, control.ActionOffset, control.ActionLength } );
                }
            }
        }
        return;
    }
    for ( uint32_t f = clip.CurrentFrame + 1; f <= frame; f++ ) {
        clip.CurrentFrame = f;
        // The frame actions of the skipped frames do not run, but the
        // sprites placed on the way run their first frame (a clip placed
        // during a goto is constructed: e.g. intro.vmf pop_avert stop()).
        applyControlTags( clip, f, bQueueActions && f == frame, bQueueActions );
    }
}

void FlashPlayer::applyControlTags( Clip &clip, uint32_t frame, bool bQueueActions, bool bQueueChildActions ) {
    if ( clip.pTimeline == nullptr || frame >= clip.pTimeline->Frames.size() ) {
        return;
    }
    for ( const FlashControlTag &control : clip.pTimeline->Frames[frame] ) {
        switch ( control.Type ) {
        case eFlashControlType::CT_Place:
            placeObject( clip, control.Place, bQueueActions || bQueueChildActions );
            break;
        case eFlashControlType::CT_Remove:
            removeObject( clip, control.Depth );
            break;
        case eFlashControlType::CT_Action:
            if ( bQueueActions ) {
                pendingActions.push_back( { &clip, control.ActionOffset, control.ActionLength } );
            }
            break;
        default:
            break;
        }
    }
}

void FlashPlayer::placeObject( Clip &clip, const FlashPlaceInfo &place, bool bQueueActions ) {
    auto it = clip.DisplayList.find( place.Depth );
    Instance *pInstance = nullptr;
    const bool bReplace = it != clip.DisplayList.end() && place.bHasCharacter &&
                          it->second->CharacterId != place.CharacterId;
    if ( it == clip.DisplayList.end() || ( !place.bMove && place.bHasCharacter ) || bReplace ) {
        if ( !place.bHasCharacter ) {
            return;
        }
        if ( it != clip.DisplayList.end() && it->second->CharacterId == place.CharacterId && place.bMove ) {
            pInstance = it->second.get();
        } else {
            if ( it != clip.DisplayList.end() ) {
                graveyard.push_back( std::move( it->second ) );
            }
            auto pNew = std::make_unique<Instance>();
            pNew->CharacterId = place.CharacterId;
            pNew->Depth = place.Depth;
            pInstance = pNew.get();
            clip.DisplayList[place.Depth] = std::move( pNew );
            if ( place.bHasName ) {
                pInstance->Name = place.Name;
            }
            if ( definitions.getType( place.CharacterId ) == eFlashCharacterType::CT_Sprite ) {
                initClip( *pInstance, &clip, bQueueActions );
            }
        }
    } else {
        pInstance = it->second.get();
    }
    if ( place.bHasMatrix ) {
        pInstance->Matrix = place.Matrix;
    }
    if ( place.bHasColorTransform ) {
        pInstance->ColorTransform = place.ColorTransform;
    }
    if ( place.bHasRatio ) {
        pInstance->Ratio = place.Ratio;
    }
    if ( place.bHasName ) {
        pInstance->Name = place.Name;
    }
    if ( place.ClipDepth != 0 ) {
        pInstance->ClipDepth = place.ClipDepth;
    }
}

void FlashPlayer::initClip( Instance &instance, Clip *pParent, bool bQueueActions ) {
    instance.pClip = std::make_unique<Clip>();
    Clip &clip = *instance.pClip;
    clip.pTimeline = definitions.getSprite( instance.CharacterId );
    clip.pParent = pParent;
    clip.pInstance = &instance;
    clip.CurrentFrame = 0;
    applyControlTags( clip, 0, bQueueActions );
}

void FlashPlayer::removeObject( Clip &clip, uint16_t depth ) {
    auto it = clip.DisplayList.find( depth );
    if ( it != clip.DisplayList.end() ) {
        graveyard.push_back( std::move( it->second ) );
        clip.DisplayList.erase( it );
    }
}

void FlashPlayer::runPendingActions() {
    for ( int32_t guard = 0; guard < 16 && !pendingActions.empty(); guard++ ) {
        std::vector<PendingAction> actions;
        actions.swap( pendingActions );
        for ( const PendingAction &action : actions ) {
            execute( action.pClip, action.Offset, action.Length, nullptr, nullptr, nullptr );
        }
    }
}

FlashPlayer::Clip *FlashPlayer::findClip( const std::string &path ) { return resolveTarget( &root, path ); }

FlashPlayer::Clip *FlashPlayer::resolveTarget( Clip *pClip, const std::string &target ) {
    if ( target.empty() ) {
        return pClip;
    }
    Clip *pCurrent = pClip;
    size_t start = 0;
    std::string path = target;
    for ( char &c : path ) {
        if ( c == '.' ) {
            c = '/';
        }
    }
    if ( path[0] == '/' ) {
        pCurrent = &root;
        start = 1;
    }
    while ( start <= path.size() && pCurrent != nullptr ) {
        size_t end = path.find( '/', start );
        std::string element = path.substr( start, end == std::string::npos ? std::string::npos : end - start );
        start = end == std::string::npos ? path.size() + 1 : end + 1;
        if ( element.empty() || element == "this" ) {
            continue;
        }
        if ( element == "_root" || element == "_level0" ) {
            pCurrent = &root;
            continue;
        }
        if ( element == ".." || element == "_parent" ) {
            pCurrent = pCurrent->pParent != nullptr ? pCurrent->pParent : pCurrent;
            continue;
        }
        Clip *pNext = nullptr;
        for ( auto &entry : pCurrent->DisplayList ) {
            if ( entry.second->pClip != nullptr && StringCmpI( entry.second->Name.c_str(), element.c_str() ) == 0 ) {
                pNext = entry.second->pClip.get();
                break;
            }
        }
        pCurrent = pNext;
    }
    return pCurrent;
}

bool FlashPlayer::resolveFrame( Clip &clip, const Value &frame, uint32_t &outFrame ) {
    if ( frame.Type == Value::eType::String ) {
        std::string label = frame.String;
        const size_t colon = label.rfind( ':' );
        if ( colon != std::string::npos ) {
            label = label.substr( colon + 1 );
        }
        char *pEnd = nullptr;
        const long number = strtol( label.c_str(), &pEnd, 10 );
        if ( pEnd != label.c_str() && *pEnd == '\0' ) {
            outFrame = static_cast<uint32_t>( Max( number - 1, 0L ) );
            return true;
        }
        auto it = clip.pTimeline->Labels.find( ToLower( label ) );
        if ( it == clip.pTimeline->Labels.end() ) {
            return false;
        }
        outFrame = it->second;
        return true;
    }
    outFrame = static_cast<uint32_t>( Max( frame.toNumber() - 1.0, 0.0 ) );
    return true;
}

// --- Game interface ----------------------------------------------------------------

void FlashPlayer::setVariable( const std::string &path, const std::string &value ) {
    assignVariable( &root, path, Value::MakeString( value ), nullptr );
}

void FlashPlayer::setVariableBool( const std::string &path, bool bValue ) {
    assignVariable( &root, path, Value::MakeBool( bValue ), nullptr );
}

void FlashPlayer::setVariableNumber( const std::string &path, double value ) {
    assignVariable( &root, path, Value::MakeNumber( value ), nullptr );
}

std::string FlashPlayer::getVariable( const std::string &path ) {
    return lookupVariable( &root, path, nullptr ).toString();
}

bool FlashPlayer::gotoLabel( const std::string &clipPath, const std::string &label, bool bPlay ) {
    Clip *pClip = resolveTarget( &root, clipPath );
    if ( pClip == nullptr || pClip->pTimeline == nullptr ) {
        return false;
    }
    uint32_t frame = 0;
    if ( !resolveFrame( *pClip, Value::MakeString( label ), frame ) ) {
        return false;
    }
    gotoFrame( *pClip, frame, true );
    pClip->bPlaying = bPlay;
    runPendingActions();
    return true;
}

// --- ActionScript ------------------------------------------------------------------

FlashPlayer::Value FlashPlayer::lookupVariable( Clip *pClip, const std::string &name,
                                                std::map<std::string, Value> *pLocals ) {
    // "path:variable" (slash syntax)
    const size_t colon = name.rfind( ':' );
    if ( colon != std::string::npos ) {
        Clip *pTarget = resolveTarget( pClip, name.substr( 0, colon ) );
        if ( pTarget == nullptr ) {
            return Value();
        }
        return lookupVariable( pTarget, name.substr( colon + 1 ), nullptr );
    }
    // Dot syntax path
    const size_t dot = name.rfind( '.' );
    if ( dot != std::string::npos ) {
        Value object = lookupVariable( pClip, name.substr( 0, dot ), pLocals );
        return getMember( object, name.substr( dot + 1 ) );
    }
    if ( name == "this" ) {
        Value v;
        v.Type = Value::eType::Clip;
        v.pClip = pClip;
        return v;
    }
    if ( name == "_root" || name == "_level0" ) {
        Value v;
        v.Type = Value::eType::Clip;
        v.pClip = &root;
        return v;
    }
    if ( name == "_parent" ) {
        Value v;
        v.Type = Value::eType::Clip;
        v.pClip = pClip->pParent;
        if ( v.pClip == nullptr ) {
            v.Type = Value::eType::Undefined;
        }
        return v;
    }
    if ( name == "_global" ) {
        return Value();
    }
    if ( pLocals != nullptr ) {
        auto it = pLocals->find( name );
        if ( it != pLocals->end() ) {
            return it->second;
        }
    }
    const int32_t property = PropertyIndex( name );
    if ( property >= 0 ) {
        return getProperty( pClip, property );
    }
    Value self;
    self.Type = Value::eType::Clip;
    self.pClip = pClip;
    Value member = getMember( self, name );
    if ( member.Type != Value::eType::Undefined ) {
        return member;
    }
    auto global = globals.find( name );
    return global != globals.end() ? global->second : Value();
}

void FlashPlayer::assignVariable( Clip *pClip, const std::string &name, const Value &value,
                                  std::map<std::string, Value> *pLocals ) {
    const size_t colon = name.rfind( ':' );
    if ( colon != std::string::npos ) {
        Clip *pTarget = resolveTarget( pClip, name.substr( 0, colon ) );
        if ( pTarget != nullptr ) {
            assignVariable( pTarget, name.substr( colon + 1 ), value, nullptr );
        }
        return;
    }
    const size_t dot = name.rfind( '.' );
    if ( dot != std::string::npos ) {
        Value object = lookupVariable( pClip, name.substr( 0, dot ), pLocals );
        setMember( object, name.substr( dot + 1 ), value );
        return;
    }
    if ( pLocals != nullptr && pLocals->count( name ) != 0 ) {
        ( *pLocals )[name] = value;
        return;
    }
    const int32_t property = PropertyIndex( name );
    if ( property >= 0 ) {
        setProperty( pClip, property, value );
        return;
    }
    pClip->Variables[name] = value;
}

FlashPlayer::Value FlashPlayer::getProperty( Clip *pClip, int32_t index ) {
    if ( pClip == nullptr ) {
        return Value();
    }
    Instance *pInstance = pClip->pInstance;
    switch ( index ) {
    case 0:
        return Value::MakeNumber( pInstance != nullptr ? pInstance->Matrix.TX / 20.0 : 0.0 );
    case 1:
        return Value::MakeNumber( pInstance != nullptr ? pInstance->Matrix.TY / 20.0 : 0.0 );
    case 2:
        return Value::MakeNumber( pInstance != nullptr ? pInstance->Matrix.A * 100.0 : 100.0 );
    case 3:
        return Value::MakeNumber( pInstance != nullptr ? pInstance->Matrix.D * 100.0 : 100.0 );
    case 4:
        return Value::MakeNumber( pClip->CurrentFrame + 1.0 );
    case 5:
    case 12:
        return Value::MakeNumber( pClip->pTimeline != nullptr ? pClip->pTimeline->FrameCount : 1 );
    case 6:
        return Value::MakeNumber( pInstance != nullptr ? pInstance->ColorTransform.Mul[3] * 100.0 : 100.0 );
    case 7:
        return Value::MakeBool( pInstance == nullptr || pInstance->bVisible );
    case 13:
        return Value::MakeString( pInstance != nullptr ? pInstance->Name : "" );
    default:
        return Value();
    }
}

void FlashPlayer::setProperty( Clip *pClip, int32_t index, const Value &value ) {
    if ( pClip == nullptr || pClip->pInstance == nullptr ) {
        return;
    }
    Instance &instance = *pClip->pInstance;
    const double v = value.toNumber();
    switch ( index ) {
    case 0:
        instance.Matrix.TX = static_cast<float>( v * 20.0 );
        break;
    case 1:
        instance.Matrix.TY = static_cast<float>( v * 20.0 );
        break;
    case 2:
        instance.Matrix.A = static_cast<float>( v / 100.0 );
        break;
    case 3:
        instance.Matrix.D = static_cast<float>( v / 100.0 );
        break;
    case 6:
        instance.ColorTransform.Mul[3] = static_cast<float>( v / 100.0 );
        break;
    case 7:
        instance.bVisible = value.toBool();
        break;
    case 13:
        instance.Name = value.toString();
        break;
    default:
        break;
    }
}

FlashPlayer::Value FlashPlayer::getMember( const Value &object, const std::string &name ) {
    if ( object.Type == Value::eType::Clip && object.pClip != nullptr ) {
        Clip *pClip = object.pClip;
        const int32_t property = PropertyIndex( name );
        if ( property >= 0 ) {
            return getProperty( pClip, property );
        }
        if ( name == "_parent" ) {
            Value v;
            v.Type = pClip->pParent != nullptr ? Value::eType::Clip : Value::eType::Undefined;
            v.pClip = pClip->pParent;
            return v;
        }
        auto var = pClip->Variables.find( name );
        if ( var != pClip->Variables.end() ) {
            return var->second;
        }
        for ( auto &entry : pClip->DisplayList ) {
            if ( entry.second->pClip != nullptr && entry.second->Name == name ) {
                Value v;
                v.Type = Value::eType::Clip;
                v.pClip = entry.second->pClip.get();
                return v;
            }
        }
        return Value();
    }
    if ( object.Type == Value::eType::Object && object.pObject != nullptr ) {
        auto it = object.pObject->Members.find( name );
        return it != object.pObject->Members.end() ? it->second : Value();
    }
    if ( object.Type == Value::eType::String && name == "length" ) {
        return Value::MakeNumber( static_cast<double>( object.String.size() ) );
    }
    return Value();
}

void FlashPlayer::setMember( Value &object, const std::string &name, const Value &value ) {
    if ( object.Type == Value::eType::Clip && object.pClip != nullptr ) {
        const int32_t property = PropertyIndex( name );
        if ( property >= 0 ) {
            setProperty( object.pClip, property, value );
        } else {
            object.pClip->Variables[name] = value;
        }
    } else if ( object.Type == Value::eType::Object && object.pObject != nullptr ) {
        object.pObject->Members[name] = value;
    }
}

bool FlashPlayer::invoke( const std::string &clipPath, const std::string &function,
                          const std::vector<std::string> &args ) {
    Clip *pClip = findClip( clipPath );
    if ( pClip == nullptr ) {
        return false;
    }
    Value object;
    object.Type = Value::eType::Clip;
    object.pClip = pClip;
    Value member = getMember( object, function );
    if ( member.Type != Value::eType::Function ) {
        return false;
    }
    std::vector<Value> values;
    for ( const std::string &arg : args ) {
        values.push_back( Value::MakeString( arg ) );
    }
    callFunction( member.FunctionIndex, pClip, values, &object );
    runPendingActions();
    return true;
}

bool FlashPlayer::setClipVisible( const std::string &clipPath, bool bVisible ) {
    Clip *pClip = findClip( clipPath );
    if ( pClip == nullptr || pClip->pInstance == nullptr ) {
        return false;
    }
    pClip->pInstance->bVisible = bVisible;
    return true;
}

FlashPlayer::Value FlashPlayer::callMethod( Value &object, const std::string &name, const std::vector<Value> &args,
                                            Clip *pClip ) {
    if ( object.Type == Value::eType::Clip && object.pClip != nullptr ) {
        Clip &target = *object.pClip;
        const std::string method = ToLower( name );
        if ( method == "play" ) {
            target.bPlaying = true;
        } else if ( method == "stop" ) {
            target.bPlaying = false;
        } else if ( ( method == "gotoandplay" || method == "gotoandstop" ) && !args.empty() ) {
            uint32_t frame = 0;
            if ( target.pTimeline != nullptr && resolveFrame( target, args[0], frame ) ) {
                gotoFrame( target, frame, true );
                target.bPlaying = method == "gotoandplay";
            }
        } else if ( method == "nextframe" && target.pTimeline != nullptr ) {
            gotoFrame( target, target.CurrentFrame + 1, true );
            target.bPlaying = false;
        } else if ( method == "prevframe" && target.CurrentFrame > 0 ) {
            gotoFrame( target, target.CurrentFrame - 1, true );
            target.bPlaying = false;
        } else {
            Value member = getMember( object, name );
            if ( member.Type == Value::eType::Function ) {
                return callFunction( member.FunctionIndex, &target, args, &object );
            }
        }
        return Value();
    }
    if ( object.Type == Value::eType::Object ) {
        Value member = getMember( object, name );
        if ( member.Type == Value::eType::Function ) {
            return callFunction( member.FunctionIndex, pClip, args, &object );
        }
    }
    if ( object.Type == Value::eType::String ) {
        const std::string method = ToLower( name );
        if ( method == "touppercase" ) {
            return Value::MakeString( ToUpper( object.String ) );
        }
        if ( method == "tolowercase" ) {
            return Value::MakeString( ToLower( object.String ) );
        }
    }
    return Value();
}

FlashPlayer::Value FlashPlayer::callFunction( int32_t index, Clip *pClip, const std::vector<Value> &args,
                                              Value *pThis ) {
    if ( index < 0 || index >= static_cast<int32_t>( functions.size() ) || actionDepth > 32 ) {
        return Value();
    }
    const Function function = functions[index];
    std::map<std::string, Value> locals;
    std::vector<Value> registers( Max<uint32_t>( function.NumRegisters, 4 ) );
    for ( size_t i = 0; i < function.Parameters.size(); i++ ) {
        locals[function.Parameters[i]] = i < args.size() ? args[i] : Value();
    }
    for ( size_t i = 0; i < function.RegisterParameters.size(); i++ ) {
        const Value arg = i < args.size() ? args[i] : Value();
        if ( function.RegisterParameters[i].first != 0 ) {
            registers[function.RegisterParameters[i].first] = arg;
        } else {
            locals[function.RegisterParameters[i].second] = arg;
        }
    }
    if ( function.Flags != 0 ) {
        // DefineFunction2 preloads (this, arguments, super, _root, _parent, _global)
        uint8_t reg = 1;
        if ( function.Flags & 0x01 ) {
            registers[reg++] = pThis != nullptr ? *pThis : Value();
        }
        if ( function.Flags & 0x04 ) {
            registers[reg++] = Value();
        }
        if ( function.Flags & 0x10 ) {
            registers[reg++] = Value();
        }
        if ( function.Flags & 0x40 ) {
            Value rootValue;
            rootValue.Type = Value::eType::Clip;
            rootValue.pClip = &root;
            registers[reg++] = rootValue;
        }
        if ( function.Flags & 0x80 ) {
            Value parent;
            parent.Type = pClip != nullptr && pClip->pParent != nullptr ? Value::eType::Clip : Value::eType::Undefined;
            parent.pClip = pClip != nullptr ? pClip->pParent : nullptr;
            registers[reg++] = parent;
        }
    }
    return execute( pClip, function.Offset, function.Length, &locals, &registers, pThis, &function.Constants );
}

FlashPlayer::Value FlashPlayer::execute( Clip *pClip, uint32_t offset, uint32_t length,
                                         std::map<std::string, Value> *pLocals, std::vector<Value> *pRegisters,
                                         Value *pThis, const std::vector<std::string> *pConstants ) {
    if ( pMovie == nullptr || pClip == nullptr ) {
        return Value();
    }
    const std::vector<uint8_t> &data = pMovie->getVMF().getData();
    const uint32_t end = Min<uint32_t>( offset + length, static_cast<uint32_t>( data.size() ) );
    std::vector<Value> stack;
    std::vector<std::string> constants;
    if ( pConstants != nullptr ) {
        constants = *pConstants;
    }
    std::vector<Value> globalRegisters( 4 );
    std::vector<Value> &registers = pRegisters != nullptr ? *pRegisters : globalRegisters;
    Clip *pTarget = pClip;
    actionDepth++;

    auto pop = [&]() {
        if ( stack.empty() ) {
            return Value();
        }
        Value v = std::move( stack.back() );
        stack.pop_back();
        return v;
    };
    auto push = [&]( Value v ) { stack.push_back( std::move( v ) ); };

    uint32_t pc = offset;
    int32_t guard = 0;
    Value result;
    while ( pc < end && guard++ < 100000 ) {
        const uint8_t code = data[pc];
        uint16_t actionLength = 0;
        uint32_t payload = pc + 1;
        if ( code >= 0x80 ) {
            actionLength = Read<uint16_t>( data, pc + 1 );
            payload = pc + 3;
        }
        uint32_t next = payload + actionLength;
        if ( code == A_End ) {
            break;
        }

        switch ( code ) {
        case A_NextFrame:
            if ( pTarget != nullptr ) {
                gotoFrame( *pTarget, pTarget->CurrentFrame + 1, true );
                pTarget->bPlaying = false;
            }
            break;
        case A_PrevFrame:
            if ( pTarget != nullptr && pTarget->CurrentFrame > 0 ) {
                gotoFrame( *pTarget, pTarget->CurrentFrame - 1, true );
                pTarget->bPlaying = false;
            }
            break;
        case A_Play:
            if ( pTarget != nullptr ) {
                pTarget->bPlaying = true;
            }
            break;
        case A_Stop:
            if ( pTarget != nullptr ) {
                pTarget->bPlaying = false;
            }
            break;
        case A_GotoFrame:
            if ( pTarget != nullptr ) {
                gotoFrame( *pTarget, Read<uint16_t>( data, payload ), true );
                pTarget->bPlaying = false;
            }
            break;
        case A_GoToLabel: {
            uint32_t p = payload;
            const std::string label = ReadString( data, p, next );
            uint32_t frame = 0;
            if ( pTarget != nullptr && resolveFrame( *pTarget, Value::MakeString( label ), frame ) ) {
                gotoFrame( *pTarget, frame, true );
                pTarget->bPlaying = false;
            }
            break;
        }
        case A_GotoFrame2: {
            const uint8_t flags = data[payload];
            Value frameValue = pop();
            Clip *pFrameTarget = pTarget;
            if ( frameValue.Type == Value::eType::String ) {
                const size_t colon = frameValue.String.rfind( ':' );
                if ( colon != std::string::npos ) {
                    pFrameTarget = resolveTarget( pTarget, frameValue.String.substr( 0, colon ) );
                }
            }
            uint32_t frame = 0;
            if ( pFrameTarget != nullptr && resolveFrame( *pFrameTarget, frameValue, frame ) ) {
                if ( flags & 0x02 ) {
                    frame += Read<uint16_t>( data, payload + 1 );
                }
                gotoFrame( *pFrameTarget, frame, true );
                pFrameTarget->bPlaying = ( flags & 0x01 ) != 0;
            }
            break;
        }
        case A_SetTarget: {
            uint32_t p = payload;
            const std::string target = ReadString( data, p, next );
            pTarget = target.empty() ? pClip : resolveTarget( pClip, target );
            break;
        }
        case A_SetTarget2: {
            const Value target = pop();
            if ( target.Type == Value::eType::Clip ) {
                pTarget = target.pClip;
            } else {
                const std::string path = target.toString();
                pTarget = path.empty() ? pClip : resolveTarget( pClip, path );
            }
            break;
        }
        case A_GetURL: {
            uint32_t p = payload;
            const std::string url = ReadString( data, p, next );
            const std::string window = ReadString( data, p, next );
            if ( StringNCmpI( url.c_str(), "FSCommand:", 10 ) == 0 && fsCommandCallback ) {
                fsCommandCallback( url.substr( 10 ), window );
            }
            break;
        }
        case A_GetURL2: {
            const Value window = pop();
            const Value url = pop();
            const std::string urlString = url.toString();
            if ( StringNCmpI( urlString.c_str(), "FSCommand:", 10 ) == 0 && fsCommandCallback ) {
                fsCommandCallback( urlString.substr( 10 ), window.toString() );
            }
            break;
        }
        case A_ConstantPool: {
            constants.clear();
            const uint16_t count = Read<uint16_t>( data, payload );
            uint32_t p = payload + 2;
            for ( uint16_t i = 0; i < count && p < next; i++ ) {
                constants.push_back( ReadString( data, p, next ) );
            }
            break;
        }
        case A_Push: {
            uint32_t p = payload;
            while ( p < next ) {
                const uint8_t type = data[p++];
                Value v;
                switch ( type ) {
                case 0:
                    v = Value::MakeString( ReadString( data, p, next ) );
                    break;
                case 1:
                    v = Value::MakeNumber( Read<float>( data, p ) );
                    p += 4;
                    break;
                case 2:
                    v.Type = Value::eType::Null;
                    break;
                case 3:
                    break;
                case 4: {
                    const uint8_t reg = data[p++];
                    v = reg < registers.size() ? registers[reg] : Value();
                    break;
                }
                case 5:
                    v = Value::MakeBool( data[p++] != 0 );
                    break;
                case 6: {
                    // Double stored as two swapped 32-bit halves.
                    uint32_t hi = Read<uint32_t>( data, p );
                    uint32_t lo = Read<uint32_t>( data, p + 4 );
                    uint64_t bits = ( static_cast<uint64_t>( hi ) << 32 ) | lo;
                    double d;
                    memcpy( &d, &bits, sizeof( d ) );
                    v = Value::MakeNumber( d );
                    p += 8;
                    break;
                }
                case 7:
                    v = Value::MakeNumber( Read<int32_t>( data, p ) );
                    p += 4;
                    break;
                case 8: {
                    const uint8_t index = data[p++];
                    v = Value::MakeString( index < constants.size() ? constants[index] : "" );
                    break;
                }
                case 9: {
                    const uint16_t index = Read<uint16_t>( data, p );
                    p += 2;
                    v = Value::MakeString( index < constants.size() ? constants[index] : "" );
                    break;
                }
                default:
                    p = next;
                    break;
                }
                push( v );
            }
            break;
        }
        case A_Pop:
            pop();
            break;
        case A_PushDuplicate:
            if ( !stack.empty() ) {
                push( stack.back() );
            }
            break;
        case A_StackSwap:
            if ( stack.size() >= 2 ) {
                std::swap( stack[stack.size() - 1], stack[stack.size() - 2] );
            }
            break;
        case A_StoreRegister: {
            const uint8_t reg = data[payload];
            if ( reg >= registers.size() ) {
                registers.resize( reg + 1 );
            }
            registers[reg] = stack.empty() ? Value() : stack.back();
            break;
        }
        case A_GetVariable: {
            const std::string name = pop().toString();
            push( lookupVariable( pTarget != nullptr ? pTarget : pClip, name, pLocals ) );
            break;
        }
        case A_SetVariable: {
            Value value = pop();
            const std::string name = pop().toString();
            assignVariable( pTarget != nullptr ? pTarget : pClip, name, value, pLocals );
            break;
        }
        case A_DefineLocal: {
            Value value = pop();
            const std::string name = pop().toString();
            if ( pLocals != nullptr ) {
                ( *pLocals )[name] = value;
            } else {
                pClip->Variables[name] = value;
            }
            break;
        }
        case A_DefineLocal2: {
            const std::string name = pop().toString();
            if ( pLocals != nullptr ) {
                ( *pLocals )[name] = Value();
            }
            break;
        }
        case A_GetProperty: {
            const int32_t index = static_cast<int32_t>( pop().toNumber() );
            const Value target = pop();
            Clip *pPropertyClip = target.Type == Value::eType::Clip ? target.pClip
                                                                   : resolveTarget( pTarget, target.toString() );
            push( getProperty( pPropertyClip, index ) );
            break;
        }
        case A_SetProperty: {
            Value value = pop();
            const int32_t index = static_cast<int32_t>( pop().toNumber() );
            const Value target = pop();
            Clip *pPropertyClip = target.Type == Value::eType::Clip ? target.pClip
                                                                   : resolveTarget( pTarget, target.toString() );
            setProperty( pPropertyClip, index, value );
            break;
        }
        case A_GetMember: {
            const std::string name = pop().toString();
            const Value object = pop();
            push( getMember( object, name ) );
            break;
        }
        case A_SetMember: {
            Value value = pop();
            const std::string name = pop().toString();
            Value object = pop();
            setMember( object, name, value );
            break;
        }
        case A_Add:
        case A_Subtract:
        case A_Multiply:
        case A_Divide:
        case A_Modulo:
        case A_Less:
        case A_Less2:
        case A_Greater:
        case A_BitAnd:
        case A_BitOr:
        case A_BitXor:
        case A_BitLShift:
        case A_BitRShift:
        case A_BitURShift: {
            const Value b = pop();
            const Value a = pop();
            const double x = a.toNumber();
            const double y = b.toNumber();
            switch ( code ) {
            case A_Add:
                push( Value::MakeNumber( x + y ) );
                break;
            case A_Subtract:
                push( Value::MakeNumber( x - y ) );
                break;
            case A_Multiply:
                push( Value::MakeNumber( x * y ) );
                break;
            case A_Divide:
                push( Value::MakeNumber( y != 0.0 ? x / y : 0.0 ) );
                break;
            case A_Modulo:
                push( Value::MakeNumber( y != 0.0 ? std::fmod( x, y ) : 0.0 ) );
                break;
            case A_Less:
            case A_Less2:
                if ( code == A_Less2 && a.Type == Value::eType::String && b.Type == Value::eType::String ) {
                    push( Value::MakeBool( a.String < b.String ) );
                } else {
                    push( Value::MakeBool( x < y ) );
                }
                break;
            case A_Greater:
                push( Value::MakeBool( x > y ) );
                break;
            case A_BitAnd:
                push( Value::MakeNumber( static_cast<int32_t>( x ) & static_cast<int32_t>( y ) ) );
                break;
            case A_BitOr:
                push( Value::MakeNumber( static_cast<int32_t>( x ) | static_cast<int32_t>( y ) ) );
                break;
            case A_BitXor:
                push( Value::MakeNumber( static_cast<int32_t>( x ) ^ static_cast<int32_t>( y ) ) );
                break;
            case A_BitLShift:
                push( Value::MakeNumber( static_cast<int32_t>( x ) << ( static_cast<int32_t>( y ) & 31 ) ) );
                break;
            case A_BitRShift:
                push( Value::MakeNumber( static_cast<int32_t>( x ) >> ( static_cast<int32_t>( y ) & 31 ) ) );
                break;
            case A_BitURShift:
                push( Value::MakeNumber( static_cast<uint32_t>( x ) >> ( static_cast<int32_t>( y ) & 31 ) ) );
                break;
            default:
                break;
            }
            break;
        }
        case A_Add2: {
            const Value b = pop();
            const Value a = pop();
            if ( a.Type == Value::eType::String || b.Type == Value::eType::String ) {
                push( Value::MakeString( a.toString() + b.toString() ) );
            } else {
                push( Value::MakeNumber( a.toNumber() + b.toNumber() ) );
            }
            break;
        }
        case A_Equals:
        case A_Equals2:
        case A_StrictEquals: {
            const Value b = pop();
            const Value a = pop();
            bool bEqual;
            if ( a.Type == Value::eType::Clip || b.Type == Value::eType::Clip ) {
                bEqual = a.Type == b.Type && a.pClip == b.pClip;
            } else if ( ( a.Type == Value::eType::Undefined || a.Type == Value::eType::Null ) ||
                        ( b.Type == Value::eType::Undefined || b.Type == Value::eType::Null ) ) {
                const bool aNull = a.Type == Value::eType::Undefined || a.Type == Value::eType::Null;
                const bool bNull = b.Type == Value::eType::Undefined || b.Type == Value::eType::Null;
                bEqual = aNull && bNull;
            } else if ( code != A_Equals && a.Type == Value::eType::String && b.Type == Value::eType::String ) {
                bEqual = a.String == b.String;
            } else {
                bEqual = a.toNumber() == b.toNumber();
            }
            push( Value::MakeBool( bEqual ) );
            break;
        }
        case A_And: {
            const bool b = pop().toBool();
            const bool a = pop().toBool();
            push( Value::MakeBool( a && b ) );
            break;
        }
        case A_Or: {
            const bool b = pop().toBool();
            const bool a = pop().toBool();
            push( Value::MakeBool( a || b ) );
            break;
        }
        case A_Not:
            push( Value::MakeBool( !pop().toBool() ) );
            break;
        case A_StringEquals: {
            const std::string b = pop().toString();
            const std::string a = pop().toString();
            push( Value::MakeBool( a == b ) );
            break;
        }
        case A_StringLess: {
            const std::string b = pop().toString();
            const std::string a = pop().toString();
            push( Value::MakeBool( a < b ) );
            break;
        }
        case A_StringGreater: {
            const std::string b = pop().toString();
            const std::string a = pop().toString();
            push( Value::MakeBool( a > b ) );
            break;
        }
        case A_StringAdd: {
            const std::string b = pop().toString();
            const std::string a = pop().toString();
            push( Value::MakeString( a + b ) );
            break;
        }
        case A_StringLength:
        case A_MBStringLength:
            push( Value::MakeNumber( static_cast<double>( pop().toString().size() ) ) );
            break;
        case A_StringExtract:
        case A_MBStringExtract: {
            const int32_t count = static_cast<int32_t>( pop().toNumber() );
            const int32_t index = static_cast<int32_t>( pop().toNumber() ) - 1;
            const std::string s = pop().toString();
            if ( index >= 0 && index < static_cast<int32_t>( s.size() ) ) {
                push( Value::MakeString( s.substr( index, Max( count, 0 ) ) ) );
            } else {
                push( Value::MakeString( "" ) );
            }
            break;
        }
        case A_ToInteger:
            push( Value::MakeNumber( std::trunc( pop().toNumber() ) ) );
            break;
        case A_ToNumber:
            push( Value::MakeNumber( pop().toNumber() ) );
            break;
        case A_ToString:
            push( Value::MakeString( pop().toString() ) );
            break;
        case A_CharToAscii: {
            const std::string s = pop().toString();
            push( Value::MakeNumber( s.empty() ? 0 : static_cast<unsigned char>( s[0] ) ) );
            break;
        }
        case A_AsciiToChar:
            push( Value::MakeString( std::string( 1, static_cast<char>( pop().toNumber() ) ) ) );
            break;
        case A_Increment:
            push( Value::MakeNumber( pop().toNumber() + 1.0 ) );
            break;
        case A_Decrement:
            push( Value::MakeNumber( pop().toNumber() - 1.0 ) );
            break;
        case A_TypeOf: {
            const Value v = pop();
            static const char *kTypes[] = { "undefined", "null", "boolean", "number",
                                            "string",    "movieclip", "object", "function" };
            push( Value::MakeString( kTypes[static_cast<int>( v.Type )] ) );
            break;
        }
        case A_RandomNumber: {
            const int32_t maximum = Max( static_cast<int32_t>( pop().toNumber() ), 1 );
            push( Value::MakeNumber( rand() % maximum ) );
            break;
        }
        case A_GetTime:
            push( Value::MakeNumber( static_cast<double>( SDL_GetTicks() ) ) );
            break;
        case A_Trace:
            OTDU_LOG_DEBUG( "[flash] trace: %s\n", pop().toString().c_str() );
            break;
        case A_TargetPath: {
            const Value v = pop();
            push( Value::MakeString( v.toString() ) );
            break;
        }
        case A_Delete:
            pop();
            pop();
            push( Value::MakeBool( true ) );
            break;
        case A_Delete2:
            pop();
            push( Value::MakeBool( true ) );
            break;
        case A_CloneSprite:
            pop();
            pop();
            pop();
            break;
        case A_RemoveSprite:
            pop();
            break;
        case A_StartDrag: {
            pop();
            const bool bConstraint = pop().toBool();
            pop();
            if ( bConstraint ) {
                pop();
                pop();
                pop();
                pop();
            }
            break;
        }
        case A_NewObject: {
            pop(); // constructor name
            const int32_t numArgs = static_cast<int32_t>( pop().toNumber() );
            for ( int32_t i = 0; i < numArgs; i++ ) {
                pop();
            }
            Value object;
            object.Type = Value::eType::Object;
            object.pObject = std::make_shared<Object>();
            push( object );
            break;
        }
        case A_NewMethod: {
            pop();
            pop();
            const int32_t numArgs = static_cast<int32_t>( pop().toNumber() );
            for ( int32_t i = 0; i < numArgs; i++ ) {
                pop();
            }
            Value object;
            object.Type = Value::eType::Object;
            object.pObject = std::make_shared<Object>();
            push( object );
            break;
        }
        case A_InitArray: {
            const int32_t count = static_cast<int32_t>( pop().toNumber() );
            Value object;
            object.Type = Value::eType::Object;
            object.pObject = std::make_shared<Object>();
            for ( int32_t i = 0; i < count; i++ ) {
                object.pObject->Members[std::to_string( i )] = pop();
            }
            object.pObject->Members["length"] = Value::MakeNumber( count );
            push( object );
            break;
        }
        case A_InitObject: {
            const int32_t count = static_cast<int32_t>( pop().toNumber() );
            Value object;
            object.Type = Value::eType::Object;
            object.pObject = std::make_shared<Object>();
            for ( int32_t i = 0; i < count; i++ ) {
                Value value = pop();
                object.pObject->Members[pop().toString()] = value;
            }
            push( object );
            break;
        }
        case A_Enumerate: {
            pop();
            push( Value() ); // empty enumeration (null terminator)
            break;
        }
        case A_DefineFunction:
        case A_DefineFunction2: {
            uint32_t p = payload;
            Function function;
            const std::string name = ReadString( data, p, next );
            const uint16_t numParams = Read<uint16_t>( data, p );
            p += 2;
            if ( code == A_DefineFunction2 ) {
                function.NumRegisters = data[p];
                function.Flags = Read<uint16_t>( data, p + 1 );
                p += 3;
                for ( uint16_t i = 0; i < numParams; i++ ) {
                    const uint8_t reg = data[p++];
                    function.RegisterParameters.emplace_back( reg, ReadString( data, p, next ) );
                }
            } else {
                for ( uint16_t i = 0; i < numParams; i++ ) {
                    function.Parameters.push_back( ReadString( data, p, next ) );
                }
            }
            const uint16_t codeSize = Read<uint16_t>( data, p );
            function.Offset = next;
            function.Length = codeSize;
            // The body uses the constant pool of the action block that
            // defines it.
            function.Constants = constants;
            functions.push_back( function );
            Value v;
            v.Type = Value::eType::Function;
            v.FunctionIndex = static_cast<int32_t>( functions.size() - 1 );
            if ( name.empty() ) {
                push( v );
            } else {
                pClip->Variables[name] = v;
            }
            next += codeSize;
            break;
        }
        case A_CallFunction: {
            const std::string name = pop().toString();
            const int32_t numArgs = static_cast<int32_t>( pop().toNumber() );
            std::vector<Value> args;
            for ( int32_t i = 0; i < numArgs; i++ ) {
                args.push_back( pop() );
            }
            const Value function = lookupVariable( pClip, name, pLocals );
            if ( function.Type == Value::eType::Function ) {
                push( callFunction( function.FunctionIndex, pClip, args, nullptr ) );
            } else {
                push( Value() );
            }
            break;
        }
        case A_CallMethod: {
            const std::string name = pop().toString();
            Value object = pop();
            const int32_t numArgs = static_cast<int32_t>( pop().toNumber() );
            std::vector<Value> args;
            for ( int32_t i = 0; i < numArgs; i++ ) {
                args.push_back( pop() );
            }
            if ( name.empty() && object.Type == Value::eType::Function ) {
                push( callFunction( object.FunctionIndex, pClip, args, nullptr ) );
            } else {
                push( callMethod( object, name, args, pClip ) );
            }
            break;
        }
        case A_Return:
            result = pop();
            next = end;
            break;
        case A_Jump:
            next = static_cast<uint32_t>( static_cast<int32_t>( next ) + Read<int16_t>( data, payload ) );
            break;
        case A_If:
            if ( pop().toBool() ) {
                next = static_cast<uint32_t>( static_cast<int32_t>( next ) + Read<int16_t>( data, payload ) );
            }
            break;
        case A_With: {
            pop();
            break; // executes the block in the current scope
        }
        case A_Call:
            pop();
            break;
        case A_WaitForFrame:
        case A_ToggleQuality:
        case A_StopSounds:
        case A_EndDrag:
            break;
        default:
            OTDU_LOG_DEBUG( "[flash] unsupported action 0x%02x\n", code );
            break;
        }
        pc = next;
    }
    actionDepth--;
    return result;
}

// --- Rendering ---------------------------------------------------------------------

bool FlashPlayer::sendKeyToClip( Clip &clip, uint8_t swfKey ) {
    bool bHandled = false;
    for ( auto &entry : clip.DisplayList ) {
        Instance &instance = *entry.second;
        if ( !instance.bVisible ) {
            continue;
        }
        const eFlashCharacterType type = definitions.getType( instance.CharacterId );
        if ( type == eFlashCharacterType::CT_Button ) {
            const FlashButton *pButton = definitions.getButton( instance.CharacterId );
            for ( const FlashButtonCondition &condition : pButton->Conditions ) {
                if ( ( condition.Conditions >> 9 ) == swfKey && condition.ActionLength > 0 ) {
                    pendingActions.push_back( { &clip, condition.ActionOffset, condition.ActionLength } );
                    bHandled = true;
                }
            }
        } else if ( type == eFlashCharacterType::CT_Sprite && instance.pClip != nullptr ) {
            bHandled |= sendKeyToClip( *instance.pClip, swfKey );
        }
    }
    return bHandled;
}

bool FlashPlayer::sendKey( uint8_t swfKey ) {
    if ( pMovie == nullptr ) {
        return false;
    }
    const bool bHandled = sendKeyToClip( root, swfKey );
    runPendingActions();
    return bHandled;
}

bool FlashPlayer::sendMouseToClip( Clip &clip, const FlashMatrix &matrix, float stageX, float stageY ) {
    // Front to back (highest depth first).
    for ( auto it = clip.DisplayList.rbegin(); it != clip.DisplayList.rend(); ++it ) {
        Instance &instance = *it->second;
        if ( !instance.bVisible ) {
            continue;
        }
        const FlashMatrix world = matrix * instance.Matrix;
        const eFlashCharacterType type = definitions.getType( instance.CharacterId );
        if ( type == eFlashCharacterType::CT_Sprite && instance.pClip != nullptr ) {
            if ( sendMouseToClip( *instance.pClip, world, stageX, stageY ) ) {
                return true;
            }
        } else if ( type == eFlashCharacterType::CT_Button ) {
            const FlashButton *pButton = definitions.getButton( instance.CharacterId );
            bool bHit = false;
            // Hit area: the Hit records (FUN_00841f10 keeps their bounds).
            for ( const FlashButtonRecord &record : pButton->Records ) {
                if ( ( record.States & 0x08 ) == 0 ) {
                    continue;
                }
                const FlashShape *pShape = definitions.getShape( record.CharacterId );
                if ( pShape == nullptr ) {
                    continue;
                }
                float x = 0.0f, y = 0.0f;
                ( world * record.Matrix ).inverse().transform( stageX * 20.0f, stageY * 20.0f, x, y );
                if ( x >= pShape->XMin && x <= pShape->XMax && y >= pShape->YMin && y <= pShape->YMax ) {
                    bHit = true;
                    break;
                }
            }
            if ( !bHit ) {
                continue;
            }
            bool bHandled = false;
            for ( const FlashButtonCondition &condition : pButton->Conditions ) {
                // OverDownToOverUp (release over the button).
                if ( ( condition.Conditions & 0x08 ) != 0 && condition.ActionLength > 0 ) {
                    pendingActions.push_back( { &clip, condition.ActionOffset, condition.ActionLength } );
                    bHandled = true;
                }
            }
            if ( bHandled ) {
                return true;
            }
        }
    }
    return false;
}

bool FlashPlayer::sendMouseRelease( float screenX, float screenY ) {
    if ( pMovie == nullptr || gpRender == nullptr ) {
        return false;
    }
    float x0, y0, width, height;
    stageRect( x0, y0, width, height );
    const float backbufferWidth = static_cast<float>( gpRender->getDevice().getBackbufferWidth() );
    const float backbufferHeight = static_cast<float>( gpRender->getDevice().getBackbufferHeight() );
    const float stageX = ( screenX / backbufferWidth - x0 ) / width * pMovie->getVMF().getStageWidth();
    const float stageY = ( screenY / backbufferHeight - y0 ) / height * pMovie->getVMF().getStageHeight();
    const bool bHandled = sendMouseToClip( root, FlashMatrix(), stageX, stageY );
    runPendingActions();
    return bHandled;
}

void FlashPlayer::stageRect( float &x0, float &y0, float &width, float &height ) const {
    static constexpr float kRects[5][4] = {
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, -1.0f / 6.0f, 1.0f, 4.0f / 3.0f },
        { 0.0f, -0.125f, 1.0f, 1.25f },
        { 0.0f, -1.0f / 6.0f, 1.0f, 4.0f / 3.0f },
        { 1.0f / 3.0f, -1.0f / 6.0f, 1.0f / 3.0f, 4.0f / 3.0f },
    };
    const int32_t mode = gpConfig != nullptr ? gpConfig->getVideoSettings().ScreenRatio : 0;
    const float *pRect = kRects[mode >= 0 && mode < 5 ? mode : 0];
    x0 = pRect[0];
    y0 = pRect[1];
    width = pRect[2];
    height = pRect[3];
}

void FlashPlayer::beginStage( Renderer2D &renderer ) const {
    // The whole stage maps onto the player destination rect chosen from the
    // screen ratio mode when the movie is loaded (FUN_00d7faa0).
    static constexpr float kRects[5][4] = {
        { 0.0f, 0.0f, 1.0f, 1.0f },                                  // 0: 4:3
        { 0.0f, -1.0f / 6.0f, 1.0f, 4.0f / 3.0f },                   // 1: 16:9
        { 0.0f, -0.125f, 1.0f, 1.25f },                              // 2
        { 0.0f, -1.0f / 6.0f, 1.0f, 4.0f / 3.0f },                   // 3
        { 1.0f / 3.0f, -1.0f / 6.0f, 1.0f / 3.0f, 4.0f / 3.0f },     // 4: triple screen
    };
    const int32_t mode = gpConfig != nullptr ? gpConfig->getVideoSettings().ScreenRatio : 0;
    const float *pRect = kRects[mode >= 0 && mode < 5 ? mode : 0];
    renderer.beginRect( pMovie->getVMF().getStageWidth(), pMovie->getVMF().getStageHeight(), pRect[0], pRect[1],
                        pRect[2], pRect[3] );
}

void FlashPlayer::draw( Renderer2D &renderer ) {
    if ( pMovie == nullptr ) {
        return;
    }
    beginStage( renderer );
    maskLevel = 0;
    FlashMatrix stageToCanvas;
    stageToCanvas.A = 1.0f / 20.0f;
    stageToCanvas.D = 1.0f / 20.0f;
    drawClip( renderer, root, FlashMatrix(), FlashColorTransform(), stageToCanvas );
    renderer.end();
}

void FlashPlayer::drawClipOnly( Renderer2D &renderer, const std::string &path ) {
    Clip *pClip = pMovie != nullptr ? findClip( path ) : nullptr;
    if ( pClip == nullptr || pClip->pInstance == nullptr ) {
        return;
    }
    FlashMatrix matrix;
    FlashColorTransform colorTransform;
    std::vector<const Instance *> chain;
    for ( const Clip *pCurrent = pClip; pCurrent != nullptr && pCurrent->pInstance != nullptr;
          pCurrent = pCurrent->pParent ) {
        chain.push_back( pCurrent->pInstance );
    }
    for ( auto it = chain.rbegin(); it != chain.rend(); ++it ) {
        matrix = matrix * ( *it )->Matrix;
        colorTransform = colorTransform * ( *it )->ColorTransform;
    }
    beginStage( renderer );
    maskLevel = 0;
    FlashMatrix stageToCanvas;
    stageToCanvas.A = 1.0f / 20.0f;
    stageToCanvas.D = 1.0f / 20.0f;
    drawClip( renderer, *pClip, matrix, colorTransform, stageToCanvas );
    renderer.end();
}

void FlashPlayer::drawClip( Renderer2D &renderer, const Clip &clip, const FlashMatrix &matrix,
                            const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas ) {
    // Masks (PlaceObject ClipDepth): the mask shape marks the stencil, the
    // layers up to ClipDepth are drawn where it is set (FUN_0085a720 keeps
    // the clip depth of the instance).
    struct ActiveMask {
        const Instance *pInstance;
        FlashMatrix Matrix;
    };
    std::vector<ActiveMask> masks;
    auto popMask = [&]() {
        const ActiveMask &mask = masks.back();
        renderer.setStencil( eStencilMode::SM_Decrement, 0 );
        maskDrawing++;
        drawInstance( renderer, *mask.pInstance, mask.Matrix, FlashColorTransform(), stageToCanvas );
        maskDrawing--;
        maskLevel--;
        masks.pop_back();
        renderer.setStencil( maskLevel > 0 ? eStencilMode::SM_TestEqual : eStencilMode::SM_None, maskLevel );
    };
    for ( const auto &entry : clip.DisplayList ) {
        const Instance &instance = *entry.second;
        while ( !masks.empty() && instance.Depth > masks.back().pInstance->ClipDepth ) {
            popMask();
        }
        if ( !instance.bVisible ) {
            continue;
        }
        if ( instance.ClipDepth != 0 ) {
            const FlashMatrix maskMatrix = matrix * instance.Matrix;
            renderer.setStencil( eStencilMode::SM_Increment, 0 );
            maskDrawing++;
            drawInstance( renderer, instance, maskMatrix, FlashColorTransform(), stageToCanvas );
            maskDrawing--;
            maskLevel++;
            masks.push_back( { &instance, maskMatrix } );
            renderer.setStencil( eStencilMode::SM_TestEqual, maskLevel );
            continue;
        }
        drawInstance( renderer, instance, matrix * instance.Matrix, colorTransform * instance.ColorTransform,
                      stageToCanvas );
    }
    while ( !masks.empty() ) {
        popMask();
    }
}

void FlashPlayer::drawInstance( Renderer2D &renderer, const Instance &instance, const FlashMatrix &matrix,
                                const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas ) {
    if ( colorTransform.Mul[3] <= 0.0f && colorTransform.Add[3] <= 0.0f ) {
        return;
    }
    switch ( definitions.getType( instance.CharacterId ) ) {
    case eFlashCharacterType::CT_Shape:
        drawShape( renderer, *definitions.getShape( instance.CharacterId ), matrix, colorTransform, stageToCanvas );
        break;
    case eFlashCharacterType::CT_Sprite:
        if ( instance.pClip != nullptr ) {
            drawClip( renderer, *instance.pClip, matrix, colorTransform, stageToCanvas );
        }
        break;
    case eFlashCharacterType::CT_EditText:
        break; // drawn by the owner clip below
    case eFlashCharacterType::CT_Button:
        // Eden buttons are hit areas only: the button object keeps the
        // bounds of its Hit record (FUN_00841f10) and its draw (vtable
        // 0xf3ce14 slot 5, FUN_00840c80) is empty.
        break;
    default:
        break;
    }
    if ( definitions.getType( instance.CharacterId ) == eFlashCharacterType::CT_EditText ) {
        // Variables of edit texts are resolved from the root (absolute paths)
        // or the clip owning the text field.
        drawEditText( renderer, *definitions.getEditText( instance.CharacterId ), root, matrix, colorTransform,
                      stageToCanvas );
    }
}

void FlashPlayer::drawShape( Renderer2D &renderer, const FlashShape &shape, const FlashMatrix &matrix,
                             const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas ) {
    const FlashMatrix toCanvas = stageToCanvas * matrix;
    std::vector<Vertex2D> vertices;
    for ( const FlashShapeMesh &mesh : shape.Meshes ) {
        const FlashFill &fill = shape.Fills[mesh.FillIndex];
        GPUTexture *pTexture = fill.pTexture;
        uint32_t color = colorTransform.apply( 0xffffffff );
        if ( fill.Type == eFlashFillType::FF_Solid ) {
            pTexture = nullptr;
            color = colorTransform.apply( fill.Color );
        }
        if ( maskDrawing > 0 ) {
            color |= 0xff000000u; // mask shapes mark the stencil whatever their alpha
        } else if ( ( color >> 24 ) == 0 ) {
            continue;
        }
        vertices.clear();
        vertices.reserve( mesh.Indices.size() );
        for ( uint32_t index : mesh.Indices ) {
            const float x = mesh.Positions[index * 2];
            const float y = mesh.Positions[index * 2 + 1];
            Vertex2D vertex;
            toCanvas.transform( x, y, vertex.X, vertex.Y );
            vertex.Color = color;
            vertex.U = vertex.V = 0.0f;
            float fx, fy;
            fill.InverseMatrix.transform( x, y, fx, fy );
            switch ( fill.Type ) {
            case eFlashFillType::FF_Bitmap:
                vertex.U = fill.U0 + fx / fill.BitmapWidth * ( fill.U1 - fill.U0 );
                vertex.V = fill.V0 + fy / fill.BitmapHeight * ( fill.V1 - fill.V0 );
                break;
            case eFlashFillType::FF_LinearGradient:
                vertex.U = ( fx + 16384.0f ) / 32768.0f;
                vertex.V = 0.5f;
                break;
            case eFlashFillType::FF_RadialGradient:
                vertex.U = ( fx + 16384.0f ) / 32768.0f;
                vertex.V = ( fy + 16384.0f ) / 32768.0f;
                break;
            default:
                break;
            }
            vertices.push_back( vertex );
        }
        renderer.drawTriangles( pTexture, vertices.data(), static_cast<uint32_t>( vertices.size() ) );
    }
}

const Font *FlashPlayer::findFont( uint16_t fontId ) const {
    const FlashFontDesc *pDesc = pMovie->getVMF().findFont( fontId );
    if ( pDesc == nullptr ) {
        return gpFlash != nullptr ? gpFlash->getTextFont() : nullptr;
    }
    const Font *pFont = pMovie->getFont( pDesc->Name.c_str() );
    if ( pFont == nullptr && gpFlash != nullptr ) {
        pFont = gpFlash->findFont( pDesc->Name.c_str() );
    }
    return pFont;
}

void FlashPlayer::drawEditText( Renderer2D &renderer, const FlashEditText &text, const Clip &owner,
                                const FlashMatrix &matrix, const FlashColorTransform &colorTransform,
                                const FlashMatrix &stageToCanvas ) {
    (void)owner;
    const Font *pFont = findFont( text.FontId );
    if ( pFont == nullptr ) {
        return;
    }
    std::string content;
    bool bHasValue = false;
    if ( !text.VariableName.empty() ) {
        Value value = const_cast<FlashPlayer *>( this )->lookupVariable( &root, text.VariableName, nullptr );
        if ( value.Type != Value::eType::Undefined ) {
            content = value.toString();
            bHasValue = true;
        }
    }
    if ( !bHasValue ) {
        content = text.Text;
        if ( !content.empty() && content[0] == '_' ) {
            content = content.substr( 1 );
        }
    }
    if ( content.empty() ) {
        return;
    }
    if ( CmdLine::HasArg( "-logtext" ) ) {
        const FlashFontDesc *pLogDesc = pMovie->getVMF().findFont( text.FontId );
        OTDU_LOG_INFO( "Text '%s' var '%s' font %u '%s' color %08x tex %p\n", content.c_str(), text.VariableName.c_str(),
                       text.FontId, pLogDesc != nullptr ? pLogDesc->Name.c_str() : "?", colorTransform.apply( text.Color ),
                       static_cast<const void *>( pFont->getTexture() ) );
    }
    for ( char &c : content ) {
        if ( c == '\r' ) {
            c = '\n';
        }
    }

    // Eden EditText layout (FUN_0083dfe0 / FUN_00849a70 / FUN_00849730 /
    // FUN_00848ef0, glyph quads FUN_0083de00), in integer font units
    // (1024 per em, s = FontHeight / 1024 twips per unit).
    const uint32_t color = colorTransform.apply( text.Color );
    const float s = text.FontHeight / 1024.0f;
    if ( s <= 0.0f ) {
        return;
    }
    // Metrics of the text field's own font (glyphs may come from the global
    // fallback font).
    const FlashFontDesc *pDesc = pMovie->getVMF().findFont( text.FontId );
    const int32_t ascent =
        pDesc != nullptr ? pDesc->Ascent : static_cast<int32_t>( std::lround( pFont->getAscent() * 1024.0f ) );
    const int32_t fontLeading =
        pDesc != nullptr ? pDesc->Leading : static_cast<int32_t>( std::lround( pFont->getEmLeading() * 1024.0f ) );
    const int32_t fontHeight = Max( static_cast<int32_t>( text.FontHeight ), 1 );
    const int32_t pitch = ( static_cast<int32_t>( text.Leading ) * 1024 ) / fontHeight + 1024 + fontLeading;
    // Rect: every value + 40 twips (FUN_0083f6c0), size unchanged.
    const int32_t left = static_cast<int32_t>( ( text.XMin + 40.0f ) / s );
    const int32_t right = static_cast<int32_t>( ( text.XMax + 40.0f ) / s );
    const int32_t top = static_cast<int32_t>( ( text.YMin + 40.0f ) / s );
    const int32_t bottom = static_cast<int32_t>( ( text.YMax + 40.0f ) / s );
    auto units = []( float em ) { return static_cast<int32_t>( std::lround( em * 1024.0f ) ); };
    const int32_t spaceAdvance = units( pFont->getSpaceAdvance() );
    const Font::Glyph *pDot = pFont->findGlyph( '.' );
    const int32_t dotAdvance = pDot != nullptr ? units( pDot->Advance ) : spaceAdvance / 3;

    // Glyph records ('#color:..#' markup only on read only fields).
    struct Record {
        uint32_t Code = 0;
        const Font::Glyph *pGlyph = nullptr;
        int32_t Advance = 0;
        int32_t X = 0;
        int32_t Y = 0;
        uint32_t Color = 0;
    };
    std::vector<Record> records;
    {
        uint32_t currentColor = color;
        size_t i = 0;
        while ( i < content.size() ) {
            if ( text.bReadOnly && content[i] == '#' ) {
                if ( content.compare( i, 7, "#color:" ) == 0 ) {
                    const size_t close = content.find( '#', i + 7 );
                    if ( close != std::string::npos && ( close - i - 7 == 6 || close - i - 7 == 8 ) ) {
                        const std::string hex = content.substr( i + 7, close - i - 7 );
                        const uint32_t value = static_cast<uint32_t>( std::strtoul( hex.c_str(), nullptr, 16 ) );
                        const uint32_t rgb = hex.size() == 8 ? value >> 8 : value;
                        const uint32_t alpha = hex.size() == 8 ? ( value & 0xff ) : ( text.Color >> 24 );
                        const uint32_t abgr = ( alpha << 24 ) | ( ( rgb & 0xff ) << 16 ) | ( rgb & 0xff00 ) |
                                              ( ( rgb >> 16 ) & 0xff );
                        currentColor = colorTransform.apply( abgr );
                        i = close + 1;
                        continue;
                    }
                }
                if ( content.compare( i, 9, "#nocolor#" ) == 0 || content.compare( i, 10, "#endcolor#" ) == 0 ) {
                    currentColor = color;
                    i += content[i + 1] == 'n' ? 9 : 10;
                    continue;
                }
                if ( content.compare( i, 11, "#underline#" ) == 0 ) {
                    i += 11;
                    continue;
                }
                if ( content.compare( i, 13, "#nounderline#" ) == 0 ) {
                    i += 13;
                    continue;
                }
            }
            const char *p = content.c_str() + i;
            uint32_t code = Font::DecodeUTF8( p );
            i = static_cast<size_t>( p - content.c_str() );
            if ( text.bPassword && code != '\n' && code != '\r' ) {
                code = '*';
            }
            Record record;
            record.Code = code;
            record.Color = currentColor;
            if ( code != ' ' && code != '\t' && code != '\n' && code != '\r' ) {
                record.pGlyph = pFont->findGlyph( code );
                if ( record.pGlyph == nullptr ) {
                    continue; // in no font: not emitted
                }
                record.Advance = units( record.pGlyph->Advance );
            }
            records.push_back( record );
        }
    }

    // Line breaking (FUN_00849730).
    struct Line {
        int32_t First, Last;
        bool bHardBreak;
    };
    std::vector<Line> lines;
    const int32_t numRecords = static_cast<int32_t>( records.size() );
    int32_t pen = left;
    int32_t y = top + static_cast<int32_t>( text.Indent ) + ascent; // the indent moves the baseline (Eden)
    int32_t lineStart = 0;
    int32_t lastSpace = -1;
    bool bStop = false;
    int32_t ellipsisPen = -1;
    auto newLine = [&]( int32_t last, bool bHard ) {
        lines.push_back( { lineStart, last, bHard } );
        pen = left;
        y += pitch;
        lineStart = last + 1;
        lastSpace = -1;
        if ( !( y < bottom && text.bMultiline ) ) {
            bStop = true;
        }
    };
    int32_t i = 0;
    while ( i < numRecords && !bStop ) {
        Record &record = records[i];
        if ( record.Code == '\n' || record.Code == '\r' ) {
            record.X = pen;
            record.Y = y;
            newLine( i, true );
            i++;
            continue;
        }
        const bool bSpace = record.Code == ' ';
        const bool bTab = record.Code == '\t';
        const int32_t tabStop = 4 * Max( spaceAdvance, 1 );
        const int32_t after = bSpace ? pen + spaceAdvance
                              : bTab ? ( ( pen - left ) / tabStop + 1 ) * tabStop
                                     : pen + record.Advance;
        if ( after > right ) {
            if ( !text.bMultiline ) {
                // Single line: cut here; read only fields end with "...".
                if ( text.bReadOnly ) {
                    int32_t keep = i;
                    int32_t position = pen;
                    while ( keep > lineStart && position + 3 * dotAdvance > right ) {
                        keep--;
                        position = records[keep].X;
                    }
                    records.resize( keep );
                    lines.push_back( { lineStart, keep - 1, false } );
                    ellipsisPen = position;
                } else {
                    lines.push_back( { lineStart, i - 1, false } );
                }
                bStop = true;
                break;
            }
            if ( bSpace || bTab ) {
                record.X = after;
                record.Y = y;
                newLine( i, false );
                i++;
            } else if ( i == lineStart ) {
                record.X = pen;
                record.Y = y;
                newLine( i, false );
                i++;
            } else if ( lastSpace >= lineStart ) {
                const int32_t space = lastSpace;
                newLine( space, false );
                i = space + 1;
            } else {
                newLine( i - 1, false );
            }
            continue;
        }
        record.X = pen;
        record.Y = y;
        pen = after;
        if ( bSpace ) {
            lastSpace = i;
        }
        i++;
    }
    if ( !bStop && lineStart < numRecords ) {
        lines.push_back( { lineStart, numRecords - 1, true } );
    }

    // Alignment (FUN_00848ef0).
    for ( size_t l = 0; l < lines.size(); l++ ) {
        const Line &line = lines[l];
        if ( line.Last < line.First || text.Align == 0 ) {
            continue;
        }
        const Record &last = records[line.Last];
        const int32_t shift = right - last.X - last.Advance;
        if ( text.Align == 1 || text.Align == 2 ) {
            const int32_t offset = text.Align == 1 ? shift : shift / 2;
            for ( int32_t r = line.First; r <= line.Last; r++ ) {
                records[r].X += offset;
            }
        } else if ( text.Align == 3 && !line.bHardBreak && l + 1 < lines.size() ) {
            int32_t numSpaces = 0;
            for ( int32_t r = line.First; r < line.Last; r++ ) {
                numSpaces += records[r].Code == ' ' ? 1 : 0;
            }
            if ( numSpaces > 0 ) {
                int32_t seen = 0;
                for ( int32_t r = line.First; r <= line.Last; r++ ) {
                    records[r].X += shift * seen / numSpaces;
                    if ( records[r].Code == ' ' && r < line.Last ) {
                        seen++;
                    }
                }
            }
        }
    }

    // Quads: x0 = pen - OffsetX (Glyph::X0), y0 = baseline - OffsetY.
    const FlashMatrix toCanvas = stageToCanvas * matrix;
    std::vector<Vertex2D> vertices;
    auto emit = [&]( const Font::Glyph &glyph, int32_t x, int32_t baseline, uint32_t glyphColor ) {
        if ( glyph.X1 <= glyph.X0 || glyph.Y1 <= glyph.Y0 ) {
            return;
        }
        const float x0 = ( x + glyph.X0 * 1024.0f ) * s;
        const float x1 = ( x + glyph.X1 * 1024.0f ) * s;
        const float y0 = ( baseline + glyph.Y0 * 1024.0f ) * s;
        const float y1 = ( baseline + glyph.Y1 * 1024.0f ) * s;
        const float corners[4][4] = { { x0, y0, glyph.U0, glyph.V0 },
                                      { x1, y0, glyph.U1, glyph.V0 },
                                      { x1, y1, glyph.U1, glyph.V1 },
                                      { x0, y1, glyph.U0, glyph.V1 } };
        static const int kOrder[6] = { 0, 1, 2, 0, 2, 3 };
        for ( int k : kOrder ) {
            Vertex2D vertex;
            toCanvas.transform( corners[k][0], corners[k][1], vertex.X, vertex.Y );
            vertex.U = corners[k][2];
            vertex.V = corners[k][3];
            vertex.Color = glyphColor;
            vertices.push_back( vertex );
        }
    };
    for ( const Line &line : lines ) {
        for ( int32_t r = line.First; r <= line.Last && r < static_cast<int32_t>( records.size() ); r++ ) {
            if ( records[r].pGlyph != nullptr ) {
                emit( *records[r].pGlyph, records[r].X, records[r].Y, records[r].Color );
            }
        }
    }
    if ( ellipsisPen >= 0 && pDot != nullptr ) {
        const int32_t baseline = lines.empty() ? top + ascent : records.empty() ? top + ascent : y;
        for ( int32_t d = 0; d < 3; d++ ) {
            emit( *pDot, ellipsisPen + d * dotAdvance, baseline, color );
        }
    }
    renderer.drawTriangles( pFont->getTexture(), vertices.data(), static_cast<uint32_t>( vertices.size() ), true );
}
