#pragma once

#include "flash/flash_player.h"
#include "game/bootmenu/title_scene.h"
#include "game/gm_base.h"

// Game mode LOGIN (0x14, class GMLogin, docs/re/bootmenu_spec.md): the press
// start screen. 3D press start cinematic (TitleScene) under LOG_IN2.vmf
// (bands, logo, "Press START to play", NAT message); on press start the
// movie plays its "next" sequence, then the savegame check shows the GENERAL
// "avert" popup (no progression data: OK / Retry); OK starts a new game
// (SCENARIOTUTORIAL = ON: AVATAR_CHOICE).
class GMLogin : public GameMode {
public:
    GMLogin();
    ~GMLogin();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawDebug() override;

private:
    // States of the original machine used on the offline / no save path.
    enum eState : int32_t {
        LS_PressStart = 1, // sub 0: loading screen, 1: 3000 ms, 2: press start, 3: wait "stok"
        LS_SaveCheck = 6,  // starts the save check (6 -> 7)
        LS_SaveResult = 7, // popup "no progression data" (OK / Retry)
        LS_NextMode = 21,  // next game mode decision
        LS_Leave = 23,     // mode switch request
    };

    eState state = LS_PressStart;
    int32_t subState = 0;
    int32_t timerMs = 0; // +0x1adc9c: ms since the mode object was created
    bool bStok = false;  // +0x1adcad: "stok" received from LOG_IN2
    bool bPopupShown = false; // GENERAL /avert drawn (open, rest, close)
    eGameMode nextMode = eGameMode::GM_None;
    FlashPlayer login2;  // LOG_IN2.vmf
    FlashPlayer general; // GENERAL.vmf (avert popup)
    TitleScene titleScene;
    std::string island = "ibiza";

private:
    void setState( eState newState );
    void loadTitleScene( int32_t cinematicIndex );
    void showNoDataPopup();
    bool isPopupClosing();
};
