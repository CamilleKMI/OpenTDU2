#pragma once

#include "game/gs_base.h"

union SDL_Event;
struct SDL_Gamepad;

// Logical actions (names from Default_KB.xmb / Device*.xmb).
enum class eInputAction : uint32_t {
    IA_CarAccel = 0,
    IA_CarBrake,
    IA_CarLeft,
    IA_CarRight,
    IA_CarHandBrake,
    IA_CarClutch,
    IA_CarGearUp,
    IA_CarGearDown,
    IA_CarCamera,
    IA_CarBackView,
    IA_CarLight,
    IA_CarHorn,
    IA_BackOnTrack,
    IA_AvatarLeft,
    IA_AvatarRight,
    IA_AvatarForward,
    IA_AvatarBackward,
    IA_Map,
    IA_MenuLeft,
    IA_MenuRight,
    IA_MenuUp,
    IA_MenuDown,
    IA_MenuValidate,
    IA_MenuBack,
    IA_MenuPause,
    IA_MenuOption1,
    IA_MenuOption2,

    IA_Count
};

class GSInput : public GameSystem {
public:
    GSInput();
    ~GSInput();

    const char *getName() const override { return "Service : Input"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void terminate() override;
    void drawDebug() override;

    void beginFrame();
    void processEvent( const SDL_Event &event, bool bUICapturesKeyboard, bool bUICapturesMouse );

    // Digital state
    bool isDown( eInputAction action ) const;
    bool wasPressed( eInputAction action ) const;
    // Analog value in [0, 1] (triggers/pedals) or [-1, 1] (steering axis).
    float getAnalog( eInputAction action ) const;
    float getSteering() const;

    // Text input (profile name...)
    void startTextInput();
    void stopTextInput();
    const std::string &getTextInput() const { return textInput; }
    void clearTextInput() { textInput.clear(); }
    void injectText( const std::string &text ) { textInput += text; }
    bool wasBackspacePressed() const { return bBackspacePressed; }

    // Mouse (window pixels)
    float getMouseX() const { return mouseX; }
    float getMouseY() const { return mouseY; }
    bool wasMouseClicked() const { return bMouseClicked; }
    // Relative mouse motion this frame (backbuffer pixels).
    float getMouseDeltaX() const { return mouseDeltaX; }
    float getMouseDeltaY() const { return mouseDeltaY; }
    bool isMouseButtonDown( int32_t button ) const;
    // Gamepad sticks in [-1, 1] (dead zone applied; y up = -1 like SDL).
    float getLeftStickX() const { return leftStick[0]; }
    float getLeftStickY() const { return leftStick[1]; }
    float getRightStickX() const { return rightStick[0]; }
    float getRightStickY() const { return rightStick[1]; }
    // FUN_00d5eed0 ("press start"): release of ESC, SPACE, ENTER, the left
    // mouse button or any pad button this frame (edge on release).
    bool wasStartReleased() const { return bStartReleased; }
    // Raw key press this frame (SDL scancode, debug keys not in the key maps).
    bool wasKeyPressed( int32_t scancode ) const;

    // Device of the last user input (keyboard/mouse or pad). The game itself
    // uses the controller selected in the options instead: profile +0x45
    // IsX360PadIconsUsed = (selected device type == 3) (FUN_0093d620,
    // FUN_00acaaf0 -> flash "/:IsPC_X360"), and FUN_00d87cc0 picks the pad
    // bitmap form per key map entry (DevSubType == 3).
    enum class eDevice : uint8_t { D_Keyboard = 0, D_Pad };
    eDevice getLastDevice() const { return lastDevice; }

    // Scripted input injection (automated tests).
    void injectPress( eInputAction action ) { injectedPresses.push_back( action ); }
    void injectMouseDelta( float dx, float dy ) { injectedMouseX += dx; injectedMouseY += dy; }
    void setInjectedHold( eInputAction action, bool bHold ) { injectedHolds[static_cast<uint32_t>( action )] = bHold; }

private:
    struct ActionBinding {
        std::vector<int32_t> Scancodes;
    };

    std::array<ActionBinding, static_cast<size_t>( eInputAction::IA_Count )> bindings;
    std::array<bool, static_cast<size_t>( eInputAction::IA_Count )> currentState;
    std::array<bool, static_cast<size_t>( eInputAction::IA_Count )> previousState;
    std::array<float, static_cast<size_t>( eInputAction::IA_Count )> analogState;
    std::array<bool, static_cast<size_t>( eInputAction::IA_Count )> injectedHolds;
    std::vector<eInputAction> injectedPresses;
    std::vector<eInputAction> framePresses;
    std::vector<int32_t> frameKeyPresses;
    SDL_Gamepad *pGamepad;
    std::string textInput;
    bool bTextInputActive;
    bool bBackspacePressed;
    bool bUIHasKeyboard;
    float mouseX;
    float mouseY;
    bool bMouseClicked;
    float injectedMouseX = 0.0f;
    float injectedMouseY = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    float leftStick[2] = { 0.0f, 0.0f };
    float rightStick[2] = { 0.0f, 0.0f };
    bool bStartReleased = false;
    float steeringAxis;
    eDevice lastDevice = eDevice::D_Keyboard;

private:
    void loadDefaultBindings();
    bool loadKeyMap( const char *pPath );
    void updateGamepad();
};

extern GSInput *gpInput;
