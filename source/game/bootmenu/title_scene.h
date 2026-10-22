#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cinematic_car.h"
#include "game/cinematic/cinematic_scene.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_scene_view.h"

// 3D background of the press start screen (GMLogin, FUN_009c7ea0 /
// FUN_009c6b20): one of the "press start" cinematics of the island
// (islands/<island>/scenarics/press_start.bnk: entities.xmb lists the
// Entity__Cinematic C_xx and the Entity__CinCar CIN_CAR_xx), played on the
// streamed island with the original shaders, lit by the cinematic weathers
// (fx/cinematic_weathers.xmb).
class TitleScene {
public:
    TitleScene();
    ~TitleScene();

    // cinematicIndex: FORCELOGINCINEINDEX (index among the press start
    // cinematics), -1: random choice like the original.
    bool load( const char *pIsland, int32_t cinematicIndex );
    void unload();
    inline bool isLoaded() const { return bLoaded; }
    inline int32_t getCinematicIndex() const { return cinematicIndex; }
    inline int32_t getNumCinematics() const { return numCinematics; }

    void tick( float deltaTime );
    // Renders the scene into its render targets (before the main pass).
    void renderOffscreen();
    // Composites the scene (main pass).
    void draw();

private:
    using CinematicCarDesc = CinematicCar::Desc;

    struct CinematicEntry {
        std::string SceneName;
        std::string CinematicName;
    };

    Bank pressStartBank;
    EdenSceneView view;
    Cinematic cinematic;
    std::vector<CinematicScene> scenes;
    std::string cinematicName;
    CinematicCar car;
    bool bEnvironmentAssigned = false;
    Vector3f carPosition;
    float time;
    int32_t cinematicIndex = -1;
    int32_t numCinematics = 0;
    bool bLoaded;

private:
    bool resolveCinematicCar( CinematicCarDesc &outDesc ) const;
    void updateView();
    bool computeCarWorld( Matrix4x4f &outWorld );
    const CinematicScene::Object *findCarObject() const;
    void drawCar( EdenRenderer &renderer );
};
