#include "shared.h"
#include "car_accessories.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "game/cinematic/cinematic_car.h"
#include "game/freeride/freeride_hud.h"
#include "input/game_key_map.h"

namespace {
constexpr float kMenuTimeout = 4.0f;       // DAT_00f75f18 (accessories menu auto close) [CODE]
constexpr float kWindowSpeed = 0.333f;     // DAT_00f4b170 (double): lowering per second [CODE]
constexpr float kMotionEndLow = 0.01f;     // DAT_00ede410 [CODE]
constexpr float kMotionEndHigh = 0.99f;    // DAT_00f49ff8 (double) [CODE]
constexpr float kToggleHalf = 0.5f;        // DAT_00edd4a0 [CODE]
constexpr float kRoofOpenTime = 0.99999f;  // FUN_00c66920 clamp (DAT_00f4a390 / DAT_00f4a388) [CODE]
constexpr float kAnimEndEpsilon = 0.001f;  // FUN_00c4a770 (DAT_00f49848, double) [CODE]

// FUN_00c4a770 (= FUN_00c662b0 with the Hardtop.anm player): 2 while
// 0 < t < end - 0.001 (moving), 1 when t < end - 0.001 (closed: t = 0),
// 0 at the end (open) [CODE].
int32_t RoofStatus( const CinematicCar &car ) {
    if ( !car.hasHardtopAnimation() ) {
        return 1;
    }
    const float t = car.getHardtopTime();
    const float end = car.getHardtopDuration() - kAnimEndEpsilon;
    if ( 0.0f < t && t < end ) {
        return 2;
    }
    return t < end ? 1 : 0;
}

// FUN_00c66a80 -> FUN_00c4ab50: t / duration clamped to [0, 1] (0 without
// the clip) [CODE].
float RoofFraction( const CinematicCar &car ) {
    const float duration = car.getHardtopDuration();
    if ( duration <= 0.0f ) {
        return 0.0f;
    }
    return Clamp( car.getHardtopTime() / duration, 0.0f, 1.0f );
}
} // namespace

void CarAccessories::load( const std::string &carFileName, const CinematicCar &car ) {
    menu = eMenu::None;
    menuTimer = 0.0f;
    roofCommand = windowCommand = 0;
    roofEnd = windowEnd = 0;
    roofMode = 2;
    bLeftBlinker = bRightBlinker = false;
    // FUN_00c66250 (car vfunc +0x34, "/:HasHood"): the Hardtop animation
    // player has its clip (Hardtop.anm) or the car has the HARDTOP node
    // (slot 39) [CODE].
    bHasHood = car.hasHardtopAnimation() || car.hasHardtopNode();
    bHasWindows = false;
    bHasTurnSignals = false;
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue record = root.getArrayElement( "DBCarPhysicsData", i );
            if ( StringCmpI( record["_car_file_name"].asString().c_str(), carFileName.c_str() ) != 0 ) {
                continue;
            }
            // FUN_009287b0: record +0x45c (_windows_type) != 3; FUN_00928720:
            // record +0x1f7 (_hasturnsignals) != 0 (0x470-byte DBCarPhysicsData
            // rows, fields in name order: _hasdoors +0x1f4 ... _hasturnsignals
            // +0x1f7) [CODE + DATA].
            bHasWindows = record["_windows_type"].asInt( 3 ) != 3;
            bHasTurnSignals = record["_hasturnsignals"].asInt( 0 ) != 0;
            break;
        }
    }
    OTDU_LOG_INFO( "Accessories: '%s' hood %d (Hardtop.anm %.2f s), windows %d, turn signals %d\n", carFileName.c_str(),
                   bHasHood ? 1 : 0, car.getHardtopDuration(), bHasWindows ? 1 : 0, bHasTurnSignals ? 1 : 0 );
}

void CarAccessories::wrong() {
    // FUN_009291c0: FUN_00d84a70("wrong", DAT_00f80168 + 0x1f88, 0, 0): the
    // "wrong" cue of the frontend element list [CODE]; its sound / display
    // is not in readable code [INF: not played].
    OTDU_LOG_INFO( "Accessories: wrong (accessory not available)\n" );
}

void CarAccessories::openMenu( FreeRideHud &hud ) {
    // FUN_00928c10: FUN_00928840 (Has* flags), "open" on HUD/ig_menu,
    // menu = 1, timer = 4.0 s [CODE].
    hud.openAccessoryMenu( bHasHood, bHasWindows, bHasTurnSignals );
    menu = eMenu::Accessories;
    menuTimer = kMenuTimeout;
    OTDU_LOG_INFO( "Accessories: menu open\n" );
}

void CarAccessories::closeMenu( FreeRideHud &hud ) {
    // FUN_00929100: "close" on HUD/ig_menu (or HUD/radio), menu = 0 [CODE].
    if ( menu == eMenu::Accessories ) {
        hud.closeAccessoryMenu();
        OTDU_LOG_INFO( "Accessories: menu closed\n" );
    }
    menu = eMenu::None;
}

bool CarAccessories::toggleRoof( const CinematicCar &car ) {
    // FUN_00928da0: car (vfunc +0x2c == 0) with a hood (vfunc +0x34), roof
    // not moving (FUN_00c662b0 != 2); command +0x3b5: -1 -> 1; 0 -> 1 when
    // the roof fraction <= 0.5 else -1; 1 -> -1 [CODE]. No speed condition.
    if ( !bHasHood || RoofStatus( car ) == 2 ) {
        return false;
    }
    if ( roofCommand == -1 ) {
        roofCommand = 1;
    } else if ( roofCommand == 0 ) {
        roofCommand = RoofFraction( car ) <= kToggleHalf ? 1 : -1;
    } else {
        roofCommand = -1;
    }
    OTDU_LOG_INFO( "Accessories: roof %s\n", roofCommand > 0 ? "opening" : "closing" );
    return true;
}

bool CarAccessories::toggleWindows( const CinematicCar &car ) {
    // FUN_00928b70: FUN_009287b0 (windows) and a car; command +0x3a8: -1 ->
    // 1; 0 -> 1 when the left window lowering (car +0xa780) > 0.5 else -1;
    // 1 -> -1 [CODE]. 1 raises the windows (lowering -> 0), -1 lowers them.
    if ( !bHasWindows ) {
        return false;
    }
    if ( windowCommand == -1 ) {
        windowCommand = 1;
    } else if ( windowCommand == 0 ) {
        windowCommand = car.getWindowLowering( 0 ) > kToggleHalf ? 1 : -1;
    } else {
        windowCommand = -1;
    }
    OTDU_LOG_INFO( "Accessories: windows %s\n", windowCommand > 0 ? "up" : "down" );
    return true;
}

bool CarAccessories::toggleBlinker( bool bLeft ) {
    // FUN_00928a20 (left) / FUN_00928950 (right): FUN_00928720 (turn
    // signals) and a car or a bike; switching one on clears the other, a
    // second press switches it off [CODE].
    if ( !bHasTurnSignals ) {
        return false;
    }
    bool &bThis = bLeft ? bLeftBlinker : bRightBlinker;
    bool &bOther = bLeft ? bRightBlinker : bLeftBlinker;
    if ( !bThis ) {
        bThis = true;
        bOther = false;
    } else {
        bThis = false;
    }
    OTDU_LOG_INFO( "Accessories: blinkers left %d right %d\n", bLeftBlinker ? 1 : 0, bRightBlinker ? 1 : 0 );
    return true;
}

void CarAccessories::menuSelect( bool bUp, bool bDown, bool bLeft, bool bRight, CinematicCar &car, FreeRideHud &hud ) {
    // FUN_009293f0 [CODE]: UP, else DOWN, else LEFT, else RIGHT. A done
    // selection plays ValidIGM(<label>) (FUN_00928e70: car labels "hood",
    // "clil", "clir", "wind"; bikes "taun", "clbl", "clbr", "hell") and
    // leaves the menu state (the movie closes itself); a refused one restarts
    // the 4 s timer and plays "wrong".
    const char *pLabel = nullptr;
    bool bDone = false;
    if ( bUp ) {
        bDone = toggleRoof( car );
        pLabel = "hood";
    } else if ( bDown ) {
        bDone = toggleWindows( car );
        pLabel = "wind";
    } else if ( bLeft ) {
        bDone = toggleBlinker( true );
        pLabel = "clil";
    } else if ( bRight ) {
        bDone = toggleBlinker( false );
        pLabel = "clir";
    } else {
        return;
    }
    if ( bDone ) {
        hud.validAccessory( pLabel );
        menu = eMenu::None;
    } else {
        menuTimer = kMenuTimeout;
        wrong();
    }
}

void CarAccessories::directKeys( CinematicCar &car ) {
    // FUN_00929b60 [CODE]: CAR_WINDOWS (0x16) -> FUN_00928b70 ("wrong" when
    // refused); CAR_LEFT_BLINKER (0x18) -> FUN_00928a20; CAR_RIGHT_BLINKER
    // (0x17) -> FUN_00928950. (Radio keys and MAP: not part of the
    // accessories.)
    if ( WasGameActionPressed( "CAR_WINDOWS" ) && !toggleWindows( car ) ) {
        wrong();
    }
    if ( WasGameActionPressed( "CAR_LEFT_BLINKER" ) ) {
        toggleBlinker( true );
    }
    if ( WasGameActionPressed( "CAR_RIGHT_BLINKER" ) ) {
        toggleBlinker( false );
    }
}

void CarAccessories::tick( float deltaTime, bool bInputAllowed, CinematicCar &car, FreeRideHud &hud ) {
    // FUN_00929be0 (in-game menus) [CODE].
    if ( !bInputAllowed ) {
        closeMenu( hud );
    } else {
        const bool bMenuOpen = menu == eMenu::Accessories;
        const bool bUp = WasGameActionPressed( "ACCESSORIES_UP", bMenuOpen );
        const bool bDown = WasGameActionPressed( "ACCESSORIES_DOWN", bMenuOpen );
        const bool bLeft = WasGameActionPressed( "ACCESSORIES_LEFT", bMenuOpen );
        const bool bRight = WasGameActionPressed( "ACCESSORIES_RIGHT", bMenuOpen );
        if ( menu == eMenu::None ) {
            if ( bRight ) {
                // FUN_009284e0: the radio menu (HUD/radio, 3.0 s) [CODE]: no
                // radio in OpenTDU2.
                OTDU_LOG_INFO( "Accessories: ACCESSORIES_RIGHT (radio menu) not implemented\n" );
            } else if ( bLeft ) {
                openMenu( hud );
            }
        } else if ( menu == eMenu::Accessories ) {
            menuSelect( bUp, bDown, bLeft, bRight, car, hud );
        }
        if ( !bUp && !bDown && !bLeft && !bRight ) {
            directKeys( car );
        }
        // Auto close: timer +0x1c counted down while > 0 [CODE].
        if ( menu != eMenu::None && menuTimer > 0.0f ) {
            menuTimer -= deltaTime;
            if ( menuTimer <= 0.0f ) {
                closeMenu( hud );
            }
        }
    }
    updateCar( deltaTime, car );
}

void CarAccessories::updateCar( float deltaTime, CinematicCar &car ) {
    // Blinkers: FUN_00bbc650 copies +0x3b2 / +0x3b3 to the car state, the
    // blink (FUN_00c57ef0) is run by the car lights (CinematicCar).
    car.setIndicator( bLeftBlinker    ? CinematicCar::eIndicator::Left
                      : bRightBlinker ? CinematicCar::eIndicator::Right
                                      : CinematicCar::eIndicator::Off );

    // Windows, FUN_00c67c60 with the command copied every frame (state
    // +0x1e0 = (int)(char)+0x3a8, FUN_00bbc650) [CODE]: > 0 both windows
    // -= dt * 0.333 (FUN_004514f0 / FUN_004514a0 [ENC], the same delta for
    // both [INF]) until the left one < 0.01 (+0x1ea = 1); < 0 += dt * 0.333
    // until > 0.99 (+0x1ea = -1); 0: +0x1ea > 0 -> 0.0, < 0 -> 1.0.
    if ( windowCommand > 0 ) {
        for ( int32_t side = 0; side < 2; side++ ) {
            car.setWindowLowering( side, car.getWindowLowering( side ) - deltaTime * kWindowSpeed );
        }
        if ( car.getWindowLowering( 0 ) < kMotionEndLow ) {
            windowEnd = 1;
        }
    } else if ( windowCommand < 0 ) {
        for ( int32_t side = 0; side < 2; side++ ) {
            car.setWindowLowering( side, car.getWindowLowering( side ) + deltaTime * kWindowSpeed );
        }
        if ( car.getWindowLowering( 0 ) > kMotionEndHigh ) {
            windowEnd = -1;
        }
    } else if ( windowEnd != 0 ) {
        const float value = windowEnd > 0 ? 0.0f : 1.0f;
        car.setWindowLowering( 0, value );
        car.setWindowLowering( 1, value );
    }

    // Roof, FUN_00c67c60 (state +0x1e4 = (int)(char)+0x3b5) [CODE]: > 0
    // FUN_00c668f0 (play forward when not moving) until the fraction > 0.99
    // (+0x1e8 = 1); < 0 FUN_00c668c0 (backward) until < 0.01 (+0x1e8 = -1);
    // 0: +0x1e8 > 0 -> FUN_00c66920(1.0) (t = 0.99999 x duration), < 0 ->
    // FUN_00c66920(0).
    if ( !car.hasHardtopAnimation() ) {
        return;
    }
    const float duration = car.getHardtopDuration();
    if ( roofCommand > 0 ) {
        if ( RoofStatus( car ) != 2 ) {
            roofMode = 0;
        }
        if ( RoofFraction( car ) > kMotionEndHigh ) {
            roofEnd = 1;
        }
    } else if ( roofCommand < 0 ) {
        if ( RoofStatus( car ) != 2 ) {
            roofMode = 1;
        }
        if ( RoofFraction( car ) < kMotionEndLow ) {
            roofEnd = -1;
        }
    } else if ( roofEnd != 0 ) {
        car.setHardtopTime( roofEnd > 0 ? kRoofOpenTime * duration : 0.0f );
        roofMode = 2;
    }
    // Animation player tick FUN_00c4ac00 (speed +0x8d4 = 1.0 set by
    // FUN_00c68c80): t += dt (mode 0) / -= dt (mode 1), FUN_00c4a5f0 [INF:
    // t kept in [0, duration]], mode 2 (stopped) once not moving [CODE].
    if ( roofMode == 0 || roofMode == 1 ) {
        car.setHardtopTime( car.getHardtopTime() + ( roofMode == 0 ? deltaTime : -deltaTime ) );
        if ( RoofStatus( car ) != 2 ) {
            roofMode = 2;
        }
    }
}
