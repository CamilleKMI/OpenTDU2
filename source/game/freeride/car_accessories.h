#pragma once

#include <string>

class CinematicCar;
class FreeRideHud;

// Car accessories while driving (docs/re/accessories_spec.md): the HUD
// "Accessoires" menu (HUD/ig_menu), the retractable roof, the side windows
// and the blinkers of the player's car.
//
// Readable code [CODE]:
//   - FUN_00929be0 (in-game menus, per frame): this +0x18 menu (0 none,
//     1 accessories "HUD/ig_menu", 2 radio "HUD/radio"), +0x1c auto close
//     timer; inputs ACCESSORIES_UP / DOWN / LEFT / RIGHT (0x36 / 0x37 / 0x34
//     / 0x35). No menu: LEFT opens the accessories (FUN_00928c10, 4.0 s
//     DAT_00f75f18), RIGHT the radio (FUN_009284e0, 3.0 s; not implemented
//     here). Accessories open: FUN_009293f0 (UP roof, DOWN windows, LEFT /
//     RIGHT blinker). No accessory key: FUN_00929b60 (CAR_WINDOWS,
//     CAR_LEFT_BLINKER, CAR_RIGHT_BLINKER direct keys; radio, MAP).
//   - Player state (DAT_010257e8 +0x79bc): +0x3b5 roof command, +0x3a8
//     window command (-1 / 0 / 1), +0x3b2 / +0x3b3 left / right blinker;
//     copied each frame into the car state (FUN_00bbc650: +0x1e4 / +0x1e0 /
//     +0x1db / +0x1dc) and run by the car (FUN_00c67c60 roof / windows,
//     FUN_00c57ef0 blink).
class CarAccessories {
public:
    // DBCarPhysicsData of the car file (_windows_type, _hasturnsignals); the
    // car model gives the roof (Hardtop.anm / HARDTOP node). Resets the
    // commands (FUN_00c3fcc0: +0x3a8 = +0x3b5 = 0) and the blinkers.
    void load( const std::string &carFileName, const CinematicCar &car );
    // bInputAllowed: the driving state (FUN_00928c70 conditions: HUD loaded,
    // no pause / cinematic / popup... [INF: FREERIDE driving]). Applies the
    // roof / windows / blinkers to the car every frame.
    void tick( float deltaTime, bool bInputAllowed, CinematicCar &car, FreeRideHud &hud );
    void closeMenu( FreeRideHud &hud );

    inline bool isMenuOpen() const { return menu == eMenu::Accessories; }
    inline bool isLeftBlinkerOn() const { return bLeftBlinker; }
    inline bool isRightBlinkerOn() const { return bRightBlinker; }

private:
    enum class eMenu : int32_t { None = 0, Accessories = 1, Radio = 2 };
    eMenu menu = eMenu::None;
    float menuTimer = 0.0f; // this +0x1c

    // Car capabilities (FUN_00928840 HUD flags).
    bool bHasHood = false;        // car vfunc +0x34 (FUN_00c66250)
    bool bHasWindows = false;     // FUN_009287b0: _windows_type != 3
    bool bHasTurnSignals = false; // FUN_00928720: _hasturnsignals

    // Player commands (-1 close / raise, 0 none, 1 open / lower... see the spec).
    int8_t roofCommand = 0;   // +0x3b5
    int8_t windowCommand = 0; // +0x3a8
    bool bLeftBlinker = false;  // +0x3b2
    bool bRightBlinker = false; // +0x3b3
    // Car state end flags (+0x1e8 roof, +0x1ea windows: 1 / -1 when the
    // motion ended, 0 none).
    int16_t roofEnd = 0;
    int16_t windowEnd = 0;
    // Hardtop animation player mode (+0x8d0): 0 forward, 1 backward, 2
    // stopped (FUN_00c68c80: 2).
    int32_t roofMode = 2;

    void openMenu( FreeRideHud &hud );
    void menuSelect( bool bUp, bool bDown, bool bLeft, bool bRight, CinematicCar &car, FreeRideHud &hud );
    void directKeys( CinematicCar &car );
    void wrong();
    bool toggleRoof( const CinematicCar &car );
    bool toggleWindows( const CinematicCar &car );
    bool toggleBlinker( bool bLeft );
    void updateCar( float deltaTime, CinematicCar &car );
};
