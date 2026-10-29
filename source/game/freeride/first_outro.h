#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "game/cinematic/cinematic_scene.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_scene_view.h"

class XmbDocument;

// The house spot of story step 2 (docs: /tmp/claude-0/spec_freeride.md 1.4):
// islands/ibiza/exterior/eph_l3di.bnk at the spot EPH_L3DI_5067I
// (spots.bin), its exterior models (Scenery EPH_L3DI, D_REFLECT, the
// interior LODs, the Door_garage roll-up door) and the spot cinematic
// C_FIRST_OUTRO (eph_l3di_sc_M_FIRST_OUTRO.cin / M_FIRST_OUTRO_first_outro.sce,
// 14.31 s): the California (.sce actor PLAYER_CAR, type 7) leaves the
// garage and drives down the road, music FxM_FIRST_OUTRO, shadow configs
// CINE_NEAR / CINE_FAR / FREERIDE. FUN_00b10da0 step 2 requests it with
// FUN_00b99c20("C_FIRST_OUTRO", spot), FREERIDE plays it (FUN_00a7cc70).
class FirstOutro {
public:
    // House spot (spots.bin): sets the scene origin.
    bool locate();
    bool load( EdenSceneView &view );
    inline const Bank &getBank() const { return bank; }
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    void start( float startTime = 0.0f );
    void tick( float deltaTime );
    inline bool isPlaying() const { return bPlaying; }
    inline float getTime() const { return time; }
    inline float getDuration() const { return cinematic.getDuration(); }

    // Cinematic camera (island space).
    bool getCamera( EdenSceneView::Camera &outCamera ) const;
    // PLAYER_CAR pose (island space): car origin and body -> world rotation,
    // velocity from the track (finite difference).
    bool getCarPose( float atTime, Vector3f &outPosition, Quaternionf &outOrientation, Vector3f &outVelocity ) const;
    bool getCarPose( Vector3f &outPosition, Quaternionf &outOrientation, Vector3f &outVelocity ) const {
        return getCarPose( time, outPosition, outOrientation, outVelocity );
    }
    // Last "shad:" message at the current time (CINE_NEAR / CINE_FAR / FREERIDE).
    std::string getShadowConfig() const;

    void draw( EdenRenderer &renderer, float carDistanceCull = 0.0f ) const;

    inline const Matrix4x4f &getSceneOrigin() const { return sceneOrigin; }
    inline const Vector3f &getHousePosition() const { return housePosition; }

private:
    struct Prop {
        std::string Name;
        uint64_t EntityHashcode = 0;
        Matrix4x4f Local;
        Matrix4x4f RootInverse;
        std::unique_ptr<EdenModel> Model;
    };

    Bank bank;
    BankHashcodes hashcodes;
    Cinematic cinematic;
    std::vector<CinematicScene> scenes;
    CinematicSounds sounds;
    std::vector<GSAudio::VoiceHandle> voices;
    std::vector<std::unique_ptr<EdenModel>> sets;
    std::vector<Prop> props;
    EdenSceneView *pView = nullptr; // GSReflection registrations of the models
    Matrix4x4f sceneOrigin;
    Quaternionf sceneRotation;
    Vector3f housePosition;
    float time = 0.0f;
    bool bPlaying = false;
    bool bLoaded = false;

    bool findHouseSpot( const char *pSpot, Vector3f &outPosition, float &outHeading ) const;
    bool sceneTime( float atTime, const CinematicScene *&outScene, float &outSceneTime ) const;
    void loadModels( EdenSceneView &view, const XmbDocument &entities );
};
