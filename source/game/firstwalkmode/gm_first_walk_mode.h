#pragma once

#include "game/gm_base.h"
#include "game/avatarchoice/intro_cinematic.h"
#include "game/walkmode/walk_mode.h"

// "FIRST_WM" (mode 0x23, story step 1 "First Garage", garage.bnk;
// docs/re/tess_garage_spec.md 3, /tmp/claude-0/spec_garage_car.md): C_INTRO
// (the avatar and Tess walk into the garage), walk mode with Tess
// (C_LIFE_TESS) next to the California, her voice lines C_VOICE_03..06, the
// "Tip" boxes (DBTutoMess "opdo" / "stca"), the driver door opened with
// ACCESSORIES_LEFT (FSCommand opdo), C_ENTER_CAR, the cockpit view, then
// C_OUT / C_OUTRO when the player revs the engine (-> story step 2 "First
// Freeride", FREERIDE).
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

    // DBTutoMess row + the tutorial object fields used by FUN_00d63240
    // (+0xc timer, +0x10 condition timer, +0x14 last on screen timer, +0x18
    // done, +0x19 on screen, +0x20 NbTimeAllowed).
    struct Tutorial {
        std::string Name;          // _tutomess
        uint32_t Text = 0;         // _mess_hc
        uint32_t TextPC = 0;       // _messpc_hc
        float ConditionTimer = 0.0f;
        float LastOnScreenTimer = 0.0f;
        int32_t NbTimeAllowed = 1;
        float Timer = 0.0f;
        bool bOnScreen = false;
        bool bDone = false;
        bool bValid = false;
    };

    // Door object (DAT_00fd9290 + 0x8510, vtable 0x00f20214): left door
    // channel +0x14 elapsed, +0x18 duration, +0x1c opening.
    struct DoorAnim {
        float Elapsed = 1.0f;
        float Duration = 1.0f;
        bool bOpening = false;
    };

    IntroCinematic garage;
    WalkMode walk;
    bool bLoaded = false;
    eState state = eState::Intro;
    bool voiceDone[4] = {};      // mode+0x1b8e59
    bool bVoicePlaying = false;  // mode+0x1b8e58
    bool bEnteredCar = false;    // mode+0x1b8e45
    float revTimer = 0.0f;       // mode+0x1b8e48
    std::vector<std::string> commands;
    int32_t carSelectable = -1;  // FST_EnterCar ("car")
    DoorAnim door;
    float doorMaxAngle = 1.0f;   // DBCarPhysicsData _doormaxangle
    bool bAimingAtCar = false;
    Tutorial tutorials[2];       // "opdo", "stca"
    bool bCockpitCamera = false;
    Vector3f cockpitPosition = Vector3f( 0.37f, 1.01f, -0.36f );
    Vector3f cockpitTarget = Vector3f( 0.37f, 0.9f, 1.353186f );
    float cockpitFov = 55.0f * kDegToRad;
    float cockpitNear = 0.05f;
    bool bFreeRideRequested = false;

    void onCinematicEnd( const std::string &name );
    void onCommand( const std::string &command );
    void updateVoices();
    void loadTutorials();
    void loadCockpitCamera();
    void updateTutorials( float deltaTime );
    bool tutorialActive( int32_t index ) const;
    bool tutorialCloseCondition( int32_t index ) const;
    void startDoor( float duration, bool bOpen );
    void updateDoor( float deltaTime );
    bool isDoorOpen() const;
    bool aimingAtCar() const;
    bool wasAccessoriesLeftPressed() const;
    void updateCockpitCamera();
};
