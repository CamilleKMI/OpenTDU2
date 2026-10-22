#pragma once

#include "game/gm_base.h"

class GameSystem;
class GSFile;
class GSConfig;
class GSDatabase;
class GSRender;
class GSInput;
class GSPhysics;
class GSFlash;
class GSImGui;
class GSAudio;
struct SDL_Window;

namespace TestDrive {
int32_t InitAndRun( const char **pCmdLineArgs, const int32_t argCount );
const char *GetVersion();
} // namespace TestDrive

// Root object: creates the window, the game services ("gs_*") and runs the
// active game mode ("gm_*").
class TestDriveGameInstance {
public:
    inline SDL_Window *getWindow() const { return pWindow; }
    inline eGameMode getActiveGameMode() const {
        return pActiveGameMode != nullptr ? pActiveGameMode->getGameMode()
                                          : eGameMode::GM_None;
    }
    inline GameMode *getActiveGameModeInstance() const { return pActiveGameMode.get(); }
    inline void requestExit() { bRequestedExit = true; }
    inline bool hasRequestedExit() const { return bRequestedExit; }
    inline float getTotalTime() const { return totalTime; }
    inline uint64_t getFrameIndex() const { return frameIndex; }

public:
    TestDriveGameInstance();
    ~TestDriveGameInstance();

    bool initialize();
    void mainLoop();
    void terminate();

    // The switch happens at the beginning of the next frame.
    void setNextGameMode( eGameMode nextGameMode );

private:
    SDL_Window *pWindow;
    std::vector<GameSystem *> services;
    std::unique_ptr<GameMode> pActiveGameMode;
    eGameMode nextGameMode;
    bool bRequestedExit;
    bool bShowDebugUI;
    float totalTime;
    uint64_t frameIndex;

    // Automated runs (-autotest): scripted inputs + screenshots.
    struct AutoplayCommand {
        uint64_t Frame;
        std::string Command;
        std::string Argument;
    };
    std::vector<AutoplayCommand> autoplay; // -autoplay "frame:cmd=arg;..." (testing)

    int32_t autoExitFrame;
    std::string screenshotPath;
    int32_t screenshotFrame;

    std::unique_ptr<GSFile> pFile;
    std::unique_ptr<GSConfig> pConfig;
    std::unique_ptr<GSDatabase> pDatabase;
    std::unique_ptr<GSRender> pRender;
    std::unique_ptr<GSInput> pInput;
    std::unique_ptr<GSPhysics> pPhysics;
    std::unique_ptr<GSFlash> pFlash;
    std::unique_ptr<GSImGui> pImGui;
    std::unique_ptr<GSAudio> pAudio;

private:
    bool createWindow();
    void switchToNextGameMode();
    void pumpEvents();
    void parseAutoplay( const char *pScript );
    void runAutoplay();
    std::unique_ptr<GameMode> createGameMode( eGameMode mode );
};

extern TestDriveGameInstance *gpTestDriveInstance;
