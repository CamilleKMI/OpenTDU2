#include "shared.h"
#include "gs_input.h"

#include "core/xmb.h"
#include "tdu_instance.h"

#include "imgui.h"

#include <SDL3/SDL.h>

GSInput *gpInput = nullptr;

namespace {
struct ActionName {
    eInputAction Action;
    const char *pName;
};

constexpr ActionName kActionNames[] = {
    { eInputAction::IA_CarAccel, "CAR_ACCEL" },
    { eInputAction::IA_CarBrake, "CAR_BRAKE" },
    { eInputAction::IA_CarLeft, "CAR_LEFT" },
    { eInputAction::IA_CarRight, "CAR_RIGHT" },
    { eInputAction::IA_CarHandBrake, "CAR_HBRAKE" },
    { eInputAction::IA_CarClutch, "CAR_CLUTCH" },
    { eInputAction::IA_CarGearUp, "CAR_GUP" },
    { eInputAction::IA_CarGearDown, "CAR_GDOWN" },
    { eInputAction::IA_CarCamera, "CAR_CAMERA" },
    { eInputAction::IA_CarBackView, "CAR_BACKVIEW" },
    { eInputAction::IA_CarLight, "CAR_LIGHT" },
    { eInputAction::IA_CarHorn, "CAR_HORN" },
    { eInputAction::IA_BackOnTrack, "BACK_ON_TRACK" },
    { eInputAction::IA_AvatarLeft, "AVT_MOVE_LEFT" },
    { eInputAction::IA_AvatarRight, "AVT_MOVE_RIGHT" },
    { eInputAction::IA_AvatarForward, "AVT_MOVE_FORWARD" },
    { eInputAction::IA_AvatarBackward, "AVT_MOVE_BACKWARD" },
    { eInputAction::IA_Map, "MAP" },
    { eInputAction::IA_MenuLeft, "MENU_LEFT" },
    { eInputAction::IA_MenuRight, "MENU_RIGHT" },
    { eInputAction::IA_MenuUp, "MENU_UP" },
    { eInputAction::IA_MenuDown, "MENU_DOWN" },
    { eInputAction::IA_MenuValidate, "MENU_VALIDATE" },
    { eInputAction::IA_MenuBack, "MENU_BACK" },
    { eInputAction::IA_MenuPause, "MENU_PAUSE" },
    { eInputAction::IA_MenuOption1, "MENU_OPTION_1" },
    { eInputAction::IA_MenuOption2, "MENU_OPTION_2" },
};

// Eden key channel names (DeviceXXX.xmb / Default_KB.xmb) to SDL scancodes.
SDL_Scancode ChannelToScancode( const std::string &channel ) {
    static const std::unordered_map<std::string, SDL_Scancode> kChannels = {
        { "UP", SDL_SCANCODE_UP },         { "DOWN", SDL_SCANCODE_DOWN },
        { "LEFT", SDL_SCANCODE_LEFT },     { "RIGHT", SDL_SCANCODE_RIGHT },
        { "LSHIFT", SDL_SCANCODE_LSHIFT }, { "RSHIFT", SDL_SCANCODE_RSHIFT },
        { "LCTRL", SDL_SCANCODE_LCTRL },   { "RCTRL", SDL_SCANCODE_RCTRL },
        { "LALT", SDL_SCANCODE_LALT },     { "RALT", SDL_SCANCODE_RALT },
        { "SPACE", SDL_SCANCODE_SPACE },   { "ENTER", SDL_SCANCODE_RETURN },
        { "BACKSPACE", SDL_SCANCODE_BACKSPACE }, { "ESC", SDL_SCANCODE_ESCAPE },
        { "TAB", SDL_SCANCODE_TAB },       { "INS", SDL_SCANCODE_INSERT },
        { "SUP", SDL_SCANCODE_DELETE },    { "HOME", SDL_SCANCODE_HOME },
        { "END", SDL_SCANCODE_END },       { "PAGE_UP", SDL_SCANCODE_PAGEUP },
        { "PAGE_DOWN", SDL_SCANCODE_PAGEDOWN },
        { "PAD_0", SDL_SCANCODE_KP_0 },    { "PAD_1", SDL_SCANCODE_KP_1 },
        { "PAD_2", SDL_SCANCODE_KP_2 },    { "PAD_3", SDL_SCANCODE_KP_3 },
        { "PAD_4", SDL_SCANCODE_KP_4 },    { "PAD_5", SDL_SCANCODE_KP_5 },
        { "PAD_6", SDL_SCANCODE_KP_6 },    { "PAD_7", SDL_SCANCODE_KP_7 },
        { "PAD_8", SDL_SCANCODE_KP_8 },    { "PAD_9", SDL_SCANCODE_KP_9 },
        { "PAD_PLUS", SDL_SCANCODE_KP_PLUS }, { "PAD_MINUS", SDL_SCANCODE_KP_MINUS },
    };
    auto it = kChannels.find( channel );
    if ( it != kChannels.end() ) {
        return it->second;
    }
    if ( channel.size() == 1 ) {
        char c = static_cast<char>( toupper( static_cast<unsigned char>( channel[0] ) ) );
        if ( c >= 'A' && c <= 'Z' ) {
            return static_cast<SDL_Scancode>( SDL_SCANCODE_A + ( c - 'A' ) );
        }
        if ( c >= '1' && c <= '9' ) {
            return static_cast<SDL_Scancode>( SDL_SCANCODE_1 + ( c - '1' ) );
        }
        if ( c == '0' ) {
            return SDL_SCANCODE_0;
        }
    }
    if ( channel.size() >= 2 && channel[0] == 'F' ) {
        int32_t index = atoi( channel.c_str() + 1 );
        if ( index >= 1 && index <= 12 ) {
            return static_cast<SDL_Scancode>( SDL_SCANCODE_F1 + index - 1 );
        }
    }
    return SDL_SCANCODE_UNKNOWN;
}
} // namespace

GSInput::GSInput()
    : pGamepad( nullptr ),
      bTextInputActive( false ),
      bBackspacePressed( false ),
      bUIHasKeyboard( false ),
      mouseX( 0.0f ),
      mouseY( 0.0f ),
      bMouseClicked( false ),
      steeringAxis( 0.0f ) {
    gpInput = this;
    currentState.fill( false );
    previousState.fill( false );
    analogState.fill( 0.0f );
    injectedHolds.fill( false );
}

GSInput::~GSInput() { gpInput = nullptr; }

void GSInput::loadDefaultBindings() {
    auto bind = [&]( eInputAction action, std::initializer_list<SDL_Scancode> scancodes ) {
        for ( SDL_Scancode scancode : scancodes ) {
            bindings[static_cast<size_t>( action )].Scancodes.push_back( scancode );
        }
    };
    bind( eInputAction::IA_CarAccel, { SDL_SCANCODE_UP } );
    bind( eInputAction::IA_CarBrake, { SDL_SCANCODE_DOWN } );
    bind( eInputAction::IA_CarLeft, { SDL_SCANCODE_LEFT } );
    bind( eInputAction::IA_CarRight, { SDL_SCANCODE_RIGHT } );
    bind( eInputAction::IA_CarHandBrake, { SDL_SCANCODE_LSHIFT } );
    bind( eInputAction::IA_CarGearUp, { SDL_SCANCODE_SPACE } );
    bind( eInputAction::IA_CarGearDown, { SDL_SCANCODE_LALT } );
    bind( eInputAction::IA_CarCamera, { SDL_SCANCODE_C } );
    bind( eInputAction::IA_CarBackView, { SDL_SCANCODE_V } );
    bind( eInputAction::IA_BackOnTrack, { SDL_SCANCODE_B } );
    bind( eInputAction::IA_MenuLeft, { SDL_SCANCODE_LEFT } );
    bind( eInputAction::IA_MenuRight, { SDL_SCANCODE_RIGHT } );
    bind( eInputAction::IA_MenuUp, { SDL_SCANCODE_UP } );
    bind( eInputAction::IA_MenuDown, { SDL_SCANCODE_DOWN } );
    bind( eInputAction::IA_MenuValidate, { SDL_SCANCODE_RETURN } );
    bind( eInputAction::IA_MenuBack, { SDL_SCANCODE_BACKSPACE } );
    bind( eInputAction::IA_MenuPause, { SDL_SCANCODE_ESCAPE } );
}

bool GSInput::loadKeyMap( const char *pPath ) {
    XmbDocument document;
    if ( !LoadXmbFromFile( pPath, document ) ) {
        return false;
    }
    XmbValue root = document.getRoot();
    const uint32_t numKeys = root.getArraySize( "KeyMap" );
    if ( numKeys == 0 ) {
        return false;
    }

    for ( ActionBinding &binding : bindings ) {
        binding.Scancodes.clear();
    }
    uint32_t numBound = 0;
    for ( uint32_t i = 0; i < numKeys; i++ ) {
        XmbValue key = root.getArrayElement( "KeyMap", i );
        const std::string actionName = key["ActionName"].asString();
        const std::string channel = key["ChannelName"].asString();
        const std::string modifier = key["ChannelNameAND"].asString();
        if ( !modifier.empty() || channel.empty() ) {
            continue; // Key combinations are not supported yet
        }
        for ( const ActionName &entry : kActionNames ) {
            if ( actionName == entry.pName ) {
                SDL_Scancode scancode = ChannelToScancode( channel );
                if ( scancode != SDL_SCANCODE_UNKNOWN ) {
                    bindings[static_cast<size_t>( entry.Action )].Scancodes.push_back( scancode );
                    numBound++;
                }
            }
        }
    }
    OTDU_LOG_INFO( "Key mapping '%s': %u bindings\n", pPath, numBound );
    return numBound > 0;
}

bool GSInput::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );
    if ( !loadKeyMap( "Default_KB.xmb" ) ) {
        loadDefaultBindings();
    }
    // Keep a couple of convenient extra bindings (WASD).
    bindings[static_cast<size_t>( eInputAction::IA_CarAccel )].Scancodes.push_back( SDL_SCANCODE_W );
    bindings[static_cast<size_t>( eInputAction::IA_CarBrake )].Scancodes.push_back( SDL_SCANCODE_S );
    bindings[static_cast<size_t>( eInputAction::IA_CarLeft )].Scancodes.push_back( SDL_SCANCODE_A );
    bindings[static_cast<size_t>( eInputAction::IA_CarRight )].Scancodes.push_back( SDL_SCANCODE_D );
    bindings[static_cast<size_t>( eInputAction::IA_MenuValidate )].Scancodes.push_back( SDL_SCANCODE_KP_ENTER );
    bindings[static_cast<size_t>( eInputAction::IA_MenuBack )].Scancodes.push_back( SDL_SCANCODE_ESCAPE );
    return true;
}

void GSInput::terminate() {
    if ( pGamepad != nullptr ) {
        SDL_CloseGamepad( pGamepad );
        pGamepad = nullptr;
    }
}

bool GSInput::wasKeyPressed( int32_t scancode ) const {
    return std::find( frameKeyPresses.begin(), frameKeyPresses.end(), scancode ) != frameKeyPresses.end();
}

void GSInput::beginFrame() {
    previousState = currentState;
    framePresses.clear();
    frameKeyPresses.clear();
    textInput.clear();
    bBackspacePressed = false;
    bMouseClicked = false;
    bStartReleased = false;
    mouseDeltaX = 0.0f;
    mouseDeltaY = 0.0f;
}

void GSInput::processEvent( const SDL_Event &event, bool bUICapturesKeyboard, bool bUICapturesMouse ) {
    bUIHasKeyboard = bUICapturesKeyboard;
    if ( event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ) {
        lastDevice = eDevice::D_Keyboard;
    } else if ( event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                ( event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs( event.gaxis.value ) > 16384 ) ) {
        lastDevice = eDevice::D_Pad;
    }
    switch ( event.type ) {
    case SDL_EVENT_KEY_DOWN:
        if ( !bUICapturesKeyboard && !event.key.repeat ) {
            frameKeyPresses.push_back( static_cast<int32_t>( event.key.scancode ) );
            for ( size_t i = 0; i < bindings.size(); i++ ) {
                for ( int32_t scancode : bindings[i].Scancodes ) {
                    if ( scancode == event.key.scancode ) {
                        framePresses.push_back( static_cast<eInputAction>( i ) );
                    }
                }
            }
        }
        if ( event.key.scancode == SDL_SCANCODE_BACKSPACE ) {
            bBackspacePressed = true;
        }
        break;
    case SDL_EVENT_KEY_UP:
        if ( !bUICapturesKeyboard &&
             ( event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_SPACE ||
               event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER ) ) {
            bStartReleased = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if ( !bUICapturesMouse && event.button.button == SDL_BUTTON_LEFT ) {
            bStartReleased = true;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        bStartReleased = true;
        break;
    case SDL_EVENT_TEXT_INPUT:
        if ( bTextInputActive ) {
            textInput += event.text.text;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        {
            // Window points -> backbuffer pixels (Retina displays)
            SDL_Window *pEventWindow = SDL_GetWindowFromID( event.motion.windowID );
            const float density = pEventWindow != nullptr ? SDL_GetWindowPixelDensity( pEventWindow ) : 1.0f;
            mouseX = event.motion.x * ( density > 0.0f ? density : 1.0f );
            mouseY = event.motion.y * ( density > 0.0f ? density : 1.0f );
            if ( !bUICapturesMouse ) {
                mouseDeltaX += event.motion.xrel * ( density > 0.0f ? density : 1.0f );
                mouseDeltaY += event.motion.yrel * ( density > 0.0f ? density : 1.0f );
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if ( !bUICapturesMouse && event.button.button == SDL_BUTTON_LEFT ) {
            bMouseClicked = true;
            SDL_Window *pEventWindow = SDL_GetWindowFromID( event.button.windowID );
            const float density = pEventWindow != nullptr ? SDL_GetWindowPixelDensity( pEventWindow ) : 1.0f;
            mouseX = event.button.x * ( density > 0.0f ? density : 1.0f );
            mouseY = event.button.y * ( density > 0.0f ? density : 1.0f );
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if ( pGamepad == nullptr ) {
            pGamepad = SDL_OpenGamepad( event.gdevice.which );
            OTDU_LOG_INFO( "Gamepad connected: %s\n", SDL_GetGamepadName( pGamepad ) );
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if ( pGamepad != nullptr && SDL_GetGamepadID( pGamepad ) == event.gdevice.which ) {
            SDL_CloseGamepad( pGamepad );
            pGamepad = nullptr;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        switch ( event.gbutton.button ) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            framePresses.push_back( eInputAction::IA_MenuValidate );
            break;
        case SDL_GAMEPAD_BUTTON_EAST:
            framePresses.push_back( eInputAction::IA_MenuBack );
            break;
        case SDL_GAMEPAD_BUTTON_START:
            framePresses.push_back( eInputAction::IA_MenuPause );
            framePresses.push_back( eInputAction::IA_MenuValidate );
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            framePresses.push_back( eInputAction::IA_MenuUp );
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            framePresses.push_back( eInputAction::IA_MenuDown );
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            framePresses.push_back( eInputAction::IA_MenuLeft );
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            framePresses.push_back( eInputAction::IA_MenuRight );
            break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            framePresses.push_back( eInputAction::IA_CarGearUp );
            break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            framePresses.push_back( eInputAction::IA_CarGearDown );
            break;
        case SDL_GAMEPAD_BUTTON_NORTH:
            framePresses.push_back( eInputAction::IA_CarCamera );
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

bool GSInput::isMouseButtonDown( int32_t button ) const {
    return ( SDL_GetMouseState( nullptr, nullptr ) & SDL_BUTTON_MASK( button ) ) != 0;
}

void GSInput::updateGamepad() {
    steeringAxis = 0.0f;
    leftStick[0] = leftStick[1] = rightStick[0] = rightStick[1] = 0.0f;
    if ( pGamepad == nullptr ) {
        return;
    }
    auto axis = [&]( SDL_GamepadAxis a ) {
        return SDL_GetGamepadAxis( pGamepad, a ) / 32767.0f;
    };
    auto stick = [&]( SDL_GamepadAxis a ) {
        const float value = Clamp( axis( a ), -1.0f, 1.0f );
        return std::fabs( value ) < 0.15f ? 0.0f : value;
    };
    leftStick[0] = stick( SDL_GAMEPAD_AXIS_LEFTX );
    leftStick[1] = stick( SDL_GAMEPAD_AXIS_LEFTY );
    rightStick[0] = stick( SDL_GAMEPAD_AXIS_RIGHTX );
    rightStick[1] = stick( SDL_GAMEPAD_AXIS_RIGHTY );
    float steer = axis( SDL_GAMEPAD_AXIS_LEFTX );
    if ( std::fabs( steer ) < 0.12f ) {
        steer = 0.0f;
    }
    steeringAxis = Clamp( steer, -1.0f, 1.0f );
    analogState[static_cast<size_t>( eInputAction::IA_CarAccel )] =
        Max( analogState[static_cast<size_t>( eInputAction::IA_CarAccel )],
             axis( SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ) );
    analogState[static_cast<size_t>( eInputAction::IA_CarBrake )] =
        Max( analogState[static_cast<size_t>( eInputAction::IA_CarBrake )],
             axis( SDL_GAMEPAD_AXIS_LEFT_TRIGGER ) );
    if ( SDL_GetGamepadButton( pGamepad, SDL_GAMEPAD_BUTTON_EAST ) ) {
        analogState[static_cast<size_t>( eInputAction::IA_CarHandBrake )] = 1.0f;
    }
}

void GSInput::tick( float deltaTime ) {
    const bool *pKeys = SDL_GetKeyboardState( nullptr );
    for ( size_t i = 0; i < bindings.size(); i++ ) {
        bool bDown = injectedHolds[i];
        if ( !bUIHasKeyboard && pKeys != nullptr ) {
            for ( int32_t scancode : bindings[i].Scancodes ) {
                bDown |= pKeys[scancode];
            }
        }
        currentState[i] = bDown;
        analogState[i] = bDown ? 1.0f : 0.0f;
    }
    for ( eInputAction action : injectedPresses ) {
        framePresses.push_back( action );
        // Scripted "validate" (autoplay): also a press start release.
        bStartReleased |= action == eInputAction::IA_MenuValidate;
    }
    injectedPresses.clear();
    mouseDeltaX += injectedMouseX;
    mouseDeltaY += injectedMouseY;
    injectedMouseX = injectedMouseY = 0.0f;
    updateGamepad();
}

bool GSInput::isDown( eInputAction action ) const {
    return currentState[static_cast<size_t>( action )] ||
           analogState[static_cast<size_t>( action )] > 0.5f;
}

bool GSInput::wasPressed( eInputAction action ) const {
    return std::find( framePresses.begin(), framePresses.end(), action ) != framePresses.end();
}

float GSInput::getAnalog( eInputAction action ) const {
    return analogState[static_cast<size_t>( action )];
}

float GSInput::getSteering() const {
    float digital = ( isDown( eInputAction::IA_CarRight ) ? 1.0f : 0.0f ) -
                    ( isDown( eInputAction::IA_CarLeft ) ? 1.0f : 0.0f );
    return digital != 0.0f ? digital : steeringAxis;
}

void GSInput::startTextInput() {
    if ( !bTextInputActive && gpTestDriveInstance != nullptr ) {
        SDL_StartTextInput( gpTestDriveInstance->getWindow() );
        bTextInputActive = true;
    }
}

void GSInput::stopTextInput() {
    if ( bTextInputActive && gpTestDriveInstance != nullptr ) {
        SDL_StopTextInput( gpTestDriveInstance->getWindow() );
        bTextInputActive = false;
    }
}

void GSInput::drawDebug() {
    if ( !ImGui::Begin( "Input" ) ) {
        ImGui::End();
        return;
    }
    ImGui::Text( "Gamepad: %s", pGamepad != nullptr ? SDL_GetGamepadName( pGamepad ) : "none" );
    ImGui::Text( "Steering: %.2f", getSteering() );
    for ( const ActionName &entry : kActionNames ) {
        ImGui::Text( "%-20s %s %.2f", entry.pName, isDown( entry.Action ) ? "DOWN" : "-",
                     getAnalog( entry.Action ) );
    }
    ImGui::End();
}
