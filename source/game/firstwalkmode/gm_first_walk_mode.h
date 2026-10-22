#pragma once

#include "game/gm_base.h"
#include "game/avatarchoice/intro_cinematic.h"
#include "game/walkmode/walk_mode.h"

// "FIRST_WM" (mode 0x23, story step 1 "First Garage", garage.bnk;
// docs/re/tess_garage_spec.md 3): C_INTRO (the avatar and Tess walk into
// the garage), walk mode with Tess (C_LIFE_TESS) next to the California,
// her voice lines C_VOICE_03..06, C_ENTER_CAR, then C_OUT / C_OUTRO when
// the player revs the engine (-> story step 2 "First Freeride").
class GMFirstWalkMode : public GameMode {
public:
    GMFirstWalkMode();
    ~GMFirstWalkMode();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;

private:
    enum class eState { Intro, Walk, EnterCar, Seated, Out, Outro, Done };

    IntroCinematic garage;
    WalkMode walk;
    bool bLoaded = false;
    eState state = eState::Intro;
    bool voiceDone[4] = {};      // mode+0x1b8e59
    bool bVoicePlaying = false;  // mode+0x1b8e58
    bool bEnteredCar = false;    // mode+0x1b8e45
    float revTimer = 0.0f;       // mode+0x1b8e48
    std::vector<std::string> commands;

    void onCinematicEnd( const std::string &name );
    void updateVoices();
};
