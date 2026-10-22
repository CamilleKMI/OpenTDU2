#pragma once

class TestDriveGameInstance;

// Base class of a game service ("gs_*"). Services are global singletons
// created by the game instance and ticked every frame (in registration
// order).
class GameSystem {
public:
    GameSystem() : pGameInstance( nullptr ), bPaused( false ) {}
    virtual ~GameSystem() {}

    virtual const char *getName() const = 0;
    virtual bool initialize( TestDriveGameInstance *pInstance ) {
        pGameInstance = pInstance;
        return true;
    }
    virtual void tick( float deltaTime ) {}
    virtual void terminate() = 0;
    virtual void drawDebug() {}

    inline bool isPaused() const { return bPaused; }
    inline void setPaused( bool bState ) { bPaused = bState; }

protected:
    TestDriveGameInstance *pGameInstance;
    bool bPaused;
};
