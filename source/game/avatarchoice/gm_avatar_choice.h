#pragma once

#include "game/gm_base.h"
#include "game/avatarchoice/intro_cinematic.h"
#include "game/walkmode/walk_mode.h"

// "AVATAR_CHOICE" (mode 0x20, story step 0 "Avatar choice / First
// WalkMode"): C_INTRO, "Select your character" (C_AV_xx / C_xTOy over the
// C_AVATAR party in slow motion), C_FIESTA_INTRO_xx, then the walk mode in
// the party house up to C_TESS and the garage door (FSCommand "gain" ->
// story step 1, FIRST_WM). docs/re/select_character_spec.md and
// docs/re/tess_garage_spec.md.
class GMAvatarChoice : public GameMode {
public:
    GMAvatarChoice();
    ~GMAvatarChoice();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;

private:
    enum class eState { Intro, Choice, Fiesta, Walk, Tess, Leaving };

    IntroCinematic intro;
    WalkMode walk;
    bool bIntroLoaded = false;
    eState state = eState::Intro;
    int32_t index = -1;          // gm[0x6e09c]: current candidate (0..5)
    int32_t selected = -1;       // gm[0x6e0a1]: validated candidate
    bool bTessStarted = false;   // gm[0x6e0a0]
    std::vector<std::string> commands; // FSCommands of the INTRO movie
    float skipTimer = 0.0f;      // gm+0x1b8294 (FUN_00da7e70)

private:
    void onCinematicEnd( const std::string &name );
    void onCommand( const std::string &command );
    void validate();
    void startWalkMode();
    void updateWalkMode( float deltaTime );
};
