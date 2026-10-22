#pragma once

#include "filesystem/bank.h"
#include "flash/flash_player.h"
#include "game/avatar/eden_avatar.h"
#include "render/eden/eden_animation.h"
#include "game/cinematic/cinematic_audio.h"
#include "game/cinematic/cinematic_car.h"
#include "game/cinematic/cinematic_scene.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_scene_view.h"
#include "physics/havok_collision.h"

// Real time cinematics of the new game in the Ibiza party house
// (Scenarics/Cinematics/intro/new_intro.bnk, docs/re/intro_spec.md and
// docs/re/select_character_spec.md): every Entity__Cinematic of the bank
// (C_INTRO "The Dream", C_AVATAR, C_AV_xx, C_xTOy, C_FIESTA_INTRO_xx,
// C_WALKMODE, C_TESS...). Like the AVATAR_CHOICE game mode, two cinematics
// can run at once: the main one (camera, weather, flash and sound messages,
// listener event 7 at its natural end) and a background one (CinProps bit 4:
// no camera; its actors and its Fx only).
class IntroCinematic {
public:
    // Entity__Volume / Entity__Volume__FpSelectable (house-local).
    struct Volume {
        std::string Name;
        uint32_t OfType = 0;            // 0 plane, 1 sphere, 2 box, 3 cylinder, 4/5 cone (FUN_00b40e50)
        int32_t SelectableType = -1;    // FpSelectable Type (5 = garage door)
        std::string SelectableString;
        float Matrix[4][4] = {};        // volume_mat (D3D rows): house-local -> unit volume
        Vector3f Center;                // orient_pos_scale pos
        Vector3f toUnit( const Vector3f &p ) const;
        Vector3f toUnitDirection( const Vector3f &d ) const;
        bool contains( const Vector3f &p ) const;
        // Ray hit with the unit box (OfType 2) / sphere (1): distance.
        bool raycast( const Vector3f &origin, const Vector3f &direction, float maxDistance, float &outT ) const;
    };

    // Listener events (FUN_00da8040).
    struct EndEvent {
        std::string Name;
    };

    IntroCinematic();
    ~IntroCinematic();

    // Scene banks: the AVATAR_CHOICE party house (new_intro.bnk at the
    // EPH_L3DI_5067I spot, with the eph_l3di exterior and the island) or
    // the FIRST_WM garage (garage.bnk, a closed room: no island streaming).
    struct Setup {
        std::string Bank = "scenarics/cinematics/intro/new_intro.bnk";
        std::string HouseSpot = "EPH_L3DI_5067I";
        std::string ExteriorBank = "islands/ibiza/exterior/eph_l3di.bnk";
        std::string ExteriorPrefix = "eph_l3di";
        bool bWorld = true;
        uint32_t GameModeId = 0x20; // AVATAR_CHOICE (0x23 FIRST_WM)
    };

    bool load();
    bool load( const Setup &setup );
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    // FUN_00da7fd0: plays a cinematic as the main one (bStopPrevious: the
    // current main one is stopped, no event 7).
    bool playMain( const std::string &name, bool bStopPrevious = true );
    // Background cinematic (no camera: FUN_00da8040 / FUN_00da8d90, the
    // FIRST_WM ambient and voice cinematics); several can run.
    bool playBackground( const std::string &name, bool bLoop = true );
    void stopMain();
    void stopBackground();
    void stopBackground( const std::string &name );
    bool isBackgroundPlaying( const std::string &name ) const;
    const std::string &getMainName() const;
    float getMainTime() const { return main.Time; }
    // C_INTRO skip (CinEnd 3: end pose, event 7).
    void skipMain();

    // Natural ends since the last call (main: event 7; non looping
    // background: event 3).
    std::vector<std::string> takeEndEvents();

    // intro.vmf (credits, "Select your character", walk mode labels).
    inline FlashPlayer &getUi() { return ui; }
    // FSCommands of the INTRO movie (sbdd is handled here).
    void setCommandCallback( FlashPlayer::FSCommandCallback callback ) { commandCallback = std::move( callback ); }
    CinematicSounds &getSounds() { return sounds; }

    // Player avatar (the .sce actor "Avatar" of C_FIESTA_INTRO_xx): DBPNJ
    // configuration (AVT_<Male|Female>[_Afrik|_Asia]_Intro).
    bool setPlayerAvatar( const std::string &configId );
    // Entity camera pose (Entity__Camera, e.g. cam_entree_01), house frame
    // -> island.
    bool getEntityCamera( const std::string &name, EdenSceneView::Camera &outCamera ) const;
    // Walk mode camera in house-local coordinates (Heading convention).
    void setLocalCamera( const Vector3f &position, const Quaternionf &heading, float fov );
    bool getEntityCameraLocal( const std::string &name, Vector3f &outPosition, Quaternionf &outHeading ) const;
    const Volume *findVolume( const std::string &name ) const;
    inline const std::vector<Volume> &getSelectables() const { return selectables; }
    // House collision (intro_lph_l3di_*.shk, house-local frame).
    inline const HavokCollision &getCollision() const { return collision; }
    // Car of the scene (FIRST_WM: the current car, 387 California): rests at
    // an Entity__PosCam (D_CAR), driven by the .sce actor of an
    // Entity__CinCar (CIN_CAR) while a cinematic animates it.
    bool loadCar( const CinematicCar::Desc &desc, const std::string &restEntity, const std::string &cinCarEntity );
    // GENERAL pop_goal (FUN_00d61790 / FUN_00d61700).
    void showGoal( const std::string &text );
    void closeGoal();
    // Overrides the camera (walk mode); nullptr: cinematic camera.
    void setCameraOverride( const EdenSceneView::Camera *pCamera );
    inline const EdenSceneView::Camera &getCamera() const { return camera; }
    // Entity__Fx emitter (S_TimeStart...).
    bool playEmitter( const char *pName );
    // Stops the 4 S_WALKMODE_Music emitters (C_INTRO start, event 0).
    void stopMusic();

    void tick( float deltaTime );
    void renderOffscreen();
    void draw();

private:
    struct Definition {
        std::string Name;
        Cinematic Cin;
        std::vector<CinematicScene> Scenes;
        uint32_t CinProps = 0;
        std::string ShadowConfig;
        // additionalsStyleAnims: base anim rsc -> style anim rsc ("m", "w").
        std::unordered_map<uint64_t, uint64_t> StyleAnims[2];
    };
    struct Playing {
        const Definition *pDef = nullptr;
        float Time = 0.0f;
        float PreviousTime = -1.0f;
        bool bLoop = false;
        bool bNoCamera = false;
        bool bEnded = false; // natural end reached (end pose kept)
        std::vector<GSAudio::VoiceHandle> Voices;
    };
    struct Prop {
        std::string Name;
        uint64_t EntityHashcode = 0;
        Matrix4x4f Local;                // orient_pos_scale (house frame)
        Matrix4x4f RootInverse;          // inverse of the 3DD root node local
        std::unique_ptr<EdenModel> Model;
    };
    // Avatars: Entity__PNJ (DBPNJ), animated by the .sce actor Animation
    // tracks (.anm of the cinematic bank); the player avatar ("Avatar").
    struct Pnj {
        std::string Name;
        uint64_t EntityHashcode = 0;
        std::unique_ptr<EdenAvatar> Avatar;
        bool bVisible = false;
        Matrix4x4f World;
        std::vector<Matrix4x4f> WorldBones;
        std::vector<Matrix4x4f> Skin;
        EdenAvatarLighting Lighting;
    };
    // Entity__Light of the scene (FUN_00a4c7a0 light entry), world space.
    struct Light {
        Vector3f Position;
        Vector3f Direction;
        Vector3f Ambient, Diffuse, Specular; // x Intensity
        float CosOuter = 1.0f;               // cos(AngularCutOff)
        float ConeScale = 1.0f;              // 1 / (cos inner - cos outer)
        float ConeBias = 0.0f;
        float DistanceA = 1.0f;              // cut / (cut - drop) [INF]
        float DistanceB = 0.0f;              // 1 / (cut - drop) [INF]
        bool bInterior = true;
    };

    Bank introBank;
    BankHashcodes introHashcodes;
    std::vector<std::unique_ptr<Definition>> definitions;
    Playing main;
    std::vector<std::unique_ptr<Playing>> backgrounds;
    Setup setup;
    std::vector<std::string> endEvents;
    CinematicSounds sounds;
    std::vector<GSAudio::VoiceHandle> musicVoices; // S_WALKMODE_Music emitters
    EdenSceneView view;
    FlashPlayer ui;
    FlashPlayer::FSCommandCallback commandCallback;
    FlashPlayer general;            // GENERAL.vmf (black_stripes letterbox)
    Matrix4x4f sceneOrigin;         // cinematic scene space -> island
    Quaternionf sceneRotation;      // rotation of sceneOrigin (Eden convention)
    Bank exteriorBank;              // islands/ibiza/exterior/eph_l3di.bnk
    std::vector<std::unique_ptr<EdenModel>> sets; // house (D_SCENE) and exterior
    std::vector<EdenModel *> reflectSets; // Entity__Actor__Mesh D_REFLECT
    std::vector<Prop> props;
    EdenAvatarLibrary avatarLibrary;
    std::vector<Pnj> pnjs;
    Pnj player;
    std::unordered_map<std::string, std::pair<Vector3f, Quaternionf>> entityCameras; // Entity__Camera orient_pos_scale
    std::unordered_map<uint64_t, std::unique_ptr<EdenAnimation>> animations;
    HavokCollision collision;
    std::unordered_map<std::string, std::pair<Vector3f, Quaternionf>> entityPlacements; // PosCam, CinCar
    CinematicCar car;
    std::string carRestEntity;
    uint64_t carEntityHashcode = 0;
    std::unordered_map<std::string, uint64_t> cinCarHashcodes;
    bool bCarEnvironment = false;
    std::vector<Light> lights;
    Vector3f cameraForward = Vector3f( 0.0f, 0.0f, 1.0f );
    std::vector<Volume> volumes;
    std::vector<Volume> selectables;
    float goalTimer = -1.0f;
    std::string weatherName;
    std::string modifierName;
    EdenSceneView::Camera camera;   // last camera
    Vector3f previousListener;
    float lastDeltaTime = 0.0f;
    float seekTime = -1.0f;
    bool bPendingUiLocalization = false;
    EdenSceneView::Camera cameraOverride;
    bool bCameraOverride = false;
    bool bLoaded = false;

private:
    const Definition *findDefinition( const std::string &name ) const;
    void loadDefinitions( const XmbDocument &entities );
    void start( Playing &playing, const Definition *pDef, bool bLoop, bool bNoCamera, float startTime );
    void stop( Playing &playing );
    void advance( Playing &playing, float deltaTime, bool bMain );
    void processMessages( const Playing &playing, float from, float to, bool bMain );
    void updateFx( Playing &playing, float from, float to );
    void updateView();
    void drawSets( EdenRenderer &renderer, EdenSceneView::ePass pass );
    bool findHouseSpot( Vector3f &outPosition, float &outHeading ) const;
    void loadSets( const XmbDocument &entities );
    void loadBankModels( const Bank &bank, const BankHashcodes &hashcodes, const XmbDocument &entities,
                         bool bSkipInteriorLods );
    void applyShadowConfig( const std::string &name );
    void loadPnjs( const XmbDocument &entities );
    void loadVolumes( const XmbDocument &entities );
    const EdenAnimation *getAnimation( uint64_t hashcode );
    // The .sce object of an entity / a name in the current scene of a
    // playing cinematic (scene time out).
    const CinematicScene::Object *findActor( const Playing &playing, uint64_t entityHashcode, const char *pName,
                                             const CinematicScene *&outScene, float &outSceneTime ) const;
    void animatePnj( Pnj &pnj, const Playing &playing, const CinematicScene &scene,
                     const CinematicScene::Object &object, float sceneTime );
    void updatePnjs();
    void loadLights( const XmbDocument &entities );
    void computeAvatarLighting( const Vector3f &rootPosition, EdenAvatarLighting &outLighting ) const;
    bool computeCarWorld( Matrix4x4f &outWorld ) const;
    std::vector<const Playing *> playingOrder() const;
    void drawFade();
};
