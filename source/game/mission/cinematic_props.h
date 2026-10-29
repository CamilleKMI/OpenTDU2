#pragma once

#include "game/cinematic/cinematic_scene.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_scene_view.h"

class Bank;
class BankHashcodes;
class XmbDocument;

// A no-camera ambient cinematic of an event bank played at a spot
// (docs/re/story_spec.md 4.1): story step 4 starts C_TRUCK of
// pack_events13.bnk with the loop byte (cine +0xb5b = 1), FUN_00b3f7a0,
// CinProps |= 0x10 (no camera) at spot ECH_IA_1243I (FUN_00b31490 /
// FUN_00b402a0) [CODE]. C_TRUCK [DATA]: CinProps 0, CinEnd 3, cin
// event_tess_M_SC_LIFE_Totem_LIFE.cin, scene M_SC_LIFE_life.sce (duration 0:
// one key per track): the actors Truck_01, Truck_02 and Carpet (Entity__Actor
// of the bank) stand in front of the Solar Crown venue; NO_CAMERA.
// The actors are drawn at their .sce pose (Position / Heading / Scale at the
// scene time, Visibility) in the spot frame (spots.bin position + heading)
// [INF: FUN_00b402a0's frame is the spot transform; the first one is used].
class CinematicProps {
public:
    bool load( EdenSceneView &view, const Bank &bank, const BankHashcodes &hashcodes, const XmbDocument &entities,
               const char *pCinematic, const Matrix4x4f &frame );
    void unload();
    inline bool isLoaded() const { return bLoaded; }
    void setPlaying( bool bPlay ) { bPlaying = bPlay; }
    inline bool isPlaying() const { return bPlaying; }
    void tick( float deltaTime );
    void draw( EdenRenderer &renderer ) const;
    inline const Matrix4x4f &getFrame() const { return frame; }

private:
    struct Prop {
        std::string Name;
        const CinematicScene::Object *pObject = nullptr;
        Matrix4x4f RootInverse;
        std::unique_ptr<EdenModel> Model;
    };
    Cinematic cinematic;
    std::vector<CinematicScene> scenes;
    std::vector<Prop> props;
    Matrix4x4f frame;
    float time = 0.0f;
    bool bPlaying = false;
    bool bLoaded = false;
};
