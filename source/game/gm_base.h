#pragma once

class TestDriveGameInstance;

// TDU2 game modes. Names come from the original executable (command line
// switches and "GAME MODE : %s" banner), the ordering is OpenTDU2 specific.
enum class eGameMode : int32_t {
    GM_None = 0,
    GM_Login,         // "LOGIN" (0x14, GMLogin): press start screen (LOG_IN2.vmf)
    GM_VideoBumper,   // "VIDEOBUMPER" (0xe): Atari / Eden bumpers
    GM_AvatarChoice,  // "AVATAR_CHOICE" (0x20): C_INTRO cinematic, avatar choice
    GM_FreeRide,      // "FREERIDE"
    GM_CarShowcase,   // "CARSHOWCASE"
    GM_CinViewer,     // "CINVIEWER"
    GM_TestCar,       // "TESTCAR"
    GM_FirstWalkMode, // "FIRST_WM" (0x23): story step 1 "First Garage" (garage.bnk)

    GM_Count
};

const char *GetGameModeName( eGameMode gameMode );
eGameMode GetGameModeFromName( const char *pName );

// Base class of a game mode ("gm_*"). Only one game mode is active at a
// time; switching is requested through TestDriveGameInstance::setNextGameMode.
class GameMode {
public:
    GameMode( eGameMode mode ) : gameMode( mode ), pGameInstance( nullptr ) {}
    virtual ~GameMode() {}

    inline eGameMode getGameMode() const { return gameMode; }

    virtual bool enter( TestDriveGameInstance *pInstance ) {
        pGameInstance = pInstance;
        return true;
    }
    virtual void tick( float deltaTime ) = 0;
    // Offscreen passes (render targets), before the main render pass.
    virtual void drawOffscreen() {}
    virtual void draw() = 0;
    virtual void exit() {}
    virtual void drawDebug() {}

protected:
    eGameMode gameMode;
    TestDriveGameInstance *pGameInstance;
};
