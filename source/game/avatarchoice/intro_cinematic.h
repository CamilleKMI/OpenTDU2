#pragma once

#include "filesystem/bank.h"
#include "flash/flash_player.h"
#include "game/avatar/eden_avatar.h"
#include "game/avatar/facefx.h"
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
        // Ray hit with the unit box (OfType 2): entry distance (and exit).
        bool raycast( const Vector3f &origin, const Vector3f &direction, float maxDistance, float &outT,
                      float *pOutExit = nullptr ) const;
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
        // The scene bank has no collision: the spot exterior collision
        // (<spot>_sc_*.shk, same frame) is the floor of the scene car
        // (CINE, meeting_tess at e_hotela) [INF].
        bool bExteriorCollision = false;
        // Interior bank of the spot (CINE kind 3: "%sInterior/ich_ib.bnk",
        // FUN_00b9ff50 in FUN_009b6df0) drawn in the spot frame with its
        // interior meshes [CODE + INF frame].
        std::string InteriorBank;
        // Frontend movie of the walk mode / labels: "intro" (INTRO.vmf) or
        // the shop movie of the SHOP mode ("shopscar", SHOPSCAR.vmf: the used
        // car dealer cursor, mini menu, car data and price panels).
        std::string UiMovie = "intro";
    };

    bool load();
    bool load( const Setup &setup );
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    // FUN_00da7fd0: plays a cinematic as the main one (bStopPrevious: the
    // current main one is stopped, no event 7).
    // startTime >= 0: start (seek) time (-cinetime tests).
    bool playMain( const std::string &name, bool bStopPrevious = true, float startTime = -1.0f );
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
    // FSCommands of GENERAL.vmf other than sbdd (dialog box "dbxc", "wmof"...).
    // GENERAL /dialog_box drawn (between the "dbxo" and "dbxc" FSCommands).
    void setDialogBoxVisible( bool bVisible ) { bDialogBoxVisible = bVisible; }
    void setGeneralCommandCallback( FlashPlayer::FSCommandCallback callback ) {
        generalCommandCallback = std::move( callback );
    }
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
    // carActor: .sce actor name animating the car when it is not an
    // Entity__CinCar (meeting_tess: "PLAYER_CAR", the player car, the
    // object of the scene without entity).
    bool loadCar( const CinematicCar::Desc &desc, const std::string &restEntity, const std::string &cinCarEntity,
                  const std::string &carActor = std::string() );
    // Showroom cars (SHOP mode: the shop scene slots, FUN_00a4fea0 /
    // FUN_00a4fd50): a car resting at an Entity__PosCam of the scene bank
    // (Entity__ShopData PosCams[slot]), on the scene collision floor.
    int32_t addShowCar( const CinematicCar::Desc &desc, const std::string &restEntity );
    inline size_t getShowCarCount() const { return showCars.size(); }
    CinematicCar &getShowCar( size_t index ) { return *showCars[index].Car; }
    bool getShowCarWorld( size_t index, Matrix4x4f &outWorld ) const;
    void setShowCarVisible( size_t index, bool bVisible );
    // Extra walk-mode selectable (scene-local volume), e.g. the box of a
    // showroom car (SelItemType "vehicle"). Returns its selectable index.
    int32_t addSelectable( const Volume &volume );
    // The scene bank (Setup::Bank) and its hashcodes, e.g. for entities the
    // game mode reads itself (Entity__ShopData of a shop interior).
    inline const Bank &getSceneBank() const { return introBank; }
    inline const BankHashcodes &getSceneHashcodes() const { return introHashcodes; }
    // CinProps of a cinematic of the scene bank (0 if absent) and the bit 2
    // "skippable" set by the game (cinematic +0x610 |= 4).
    uint32_t getCinProps( const std::string &name ) const;
    // Scene frame (cinematic scene space -> island).
    inline const Matrix4x4f &getSceneOrigin() const { return sceneOrigin; }
    // GENERAL pop_goal (FUN_00d61790 / FUN_00d61700).
    void showGoal( const std::string &text );
    void closeGoal();
    // GENERAL pop_tutorial ("Tip" box, FUN_00d62ed0 / FUN_00d61600): /:TutText
    // then goto "open_tuto" (/:IsWalkmode false: the non-WM animation);
    // close_popup().
    void showTutorial( const std::string &text );
    void closeTutorial();
    // The scene car (FIRST_WM: the California): world / scene-local pose.
    inline CinematicCar &getCar() { return car; }
    inline const CinematicCar &getCar() const { return car; }
    bool getCarWorld( Matrix4x4f &outWorld ) const;
    // CINE scenes played relative to the player car (FUN_009b5210, no spot
    // slot: the cinematic transform cine+0x120 is built from the driver
    // matrix through encrypted helpers): the cinematic frame is the spot
    // frame lifted so that the rest entity (D_CAR) stands on the spot
    // exterior floor; the exterior sets and collision stay at the spot
    // [INF]. Returns the lift (m), 0 when no floor is found.
    float liftOntoFloor( const std::string &restEntity );
    // Alternative [INF]: the cinematic frame moved so that the rest entity
    // lands at a world pose (position, heading in the spots.bin convention:
    // world = T + R_y(heading) local); the exterior stays at the spot.
    void placeEntityAt( const std::string &restEntity, const Vector3f &worldPosition, float worldHeading );
    // spots.bin record of a spot (spot object ctor FUN_00b2eb80): +0x10
    // position, +0x1c / +0x20 / +0x24 Euler angles (rt +0x44..), +0x28 second
    // position (rt +0x50), +0x38 second heading (rt +0x60) [CODE + DATA].
    struct SpotRecord {
        Vector3f Position;
        float Heading = 0.0f;
        Vector3f Position2;
        float Heading2 = 0.0f;
        uint8_t Type = 0;    // rt+0x79 <- rec+0x100 (shop type: 1 / 2 car dealer, 4 after market, 8 car wash /
                             // rental, 0x11 used car dealer "scr_open"; FUN_009f3050 / FUN_009f23e0)
    };
    static bool ReadSpot( const char *pName, SpotRecord &out );
    // Island terrain height (heightfield of the loaded sectors / areas).
    bool getGroundHeight( float x, float z, float &outHeight ) const;
    bool getCarLocal( Matrix4x4f &outLocal ) const;
    // Player avatar seated at the car driver seat (DVRSEAT_LR) with the
    // avatarsdriver.bnk drive pose (anims\car\stance_turn.xan) and the
    // driveiklayer6 IK (hands / feet / back on the car nodes); replaces the
    // .sce "Avatar" actor while set (C_OUTRO message avm4:CIN_CAR) [INF].
    bool setPlayerSeated( bool bSeated );
    // SHOP "Enter" (FUN_00a519b0): the player avatar on the driver seat of a
    // showroom car (index of addShowCar) instead of the scene car; -1 = the
    // scene car (default).
    void setSeatedShowCar( int32_t showCarIndex );
    inline int32_t getSeatedShowCar() const { return seatedShowCar; }
    // Overrides the camera (walk mode); nullptr: cinematic camera.
    void setCameraOverride( const EdenSceneView::Camera *pCamera );
    inline const EdenSceneView::Camera &getCamera() const { return camera; }
    // Look-at request of a PNJ (docs/re/facefx_spec.md 6.3): FUN_00b858f0
    // (avatar +0x1d70 = target, +0x1d81 = 1, +0x1d68 = 0), the call of the
    // lkon message and of the walk-mode state machine (0x9520b9, the player
    // avatar in the self-view camera, 6.3.3); stopLookAt = lkof (+0x1d68 = 1).
    // The target stays until the next request / lkof; FUN_00b89730 applies it
    // while it is inside the 0.2 cone. False if no PNJ has that name.
    bool setLookAtTarget( const std::string &pnjName, const Vector3f &worldTarget );
    bool stopLookAt( const std::string &pnjName );
    // Entity__Fx emitter (S_TimeStart...).
    bool playEmitter( const char *pName );
    // Stops the 4 S_WALKMODE_Music emitters (C_INTRO start, event 0).
    void stopMusic();
    // GENERAL.vmf (pop_goal, black_stripes; /:PlayerName of the CINE mode
    // listener event 0).
    inline FlashPlayer &getGeneral() { return general; }

    // .sce actor messages (object "Message" tracks, e.g. the car door
    // messages opdl:0.64 of meeting_tess PLAYER_CAR) of the main cinematic
    // crossed since the last call, in time order.
    struct ActorMessage {
        std::string Actor;
        std::string Message;
    };
    std::vector<ActorMessage> takeActorMessages();

    void tick( float deltaTime );
    void renderOffscreen();
    void draw();
    // The frontend movies only (tests without the 3D view, -no3d).
    void drawUi();

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
        // Entity__Actor skeleton[0] (.XSB, subtype 7): skinned prop posed by
        // the Animation track of its .sce actor (meeting_tess CF_KCar key
        // ring, CF_Phone).
        std::unique_ptr<EdenSkeleton> Skeleton;
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
        bool bFaceFxUsed = false; // facialAnimCoef written once (FUN_00b77b20)
        // Look-at (docs/re/facefx_spec.md 6): lkon / lkof message state
        // (avatar +0x1d70 target, +0x1d81 on, +0x1d68 lkof) and the FaceFX
        // registers LookAt_T2_side / LookAt_T2_vertical of this frame.
        const void *pLookMessageKey = nullptr; // last lkon / lkof key seen
        bool bLookOn = false;
        Vector3f LookTarget;
        bool bLookAt = false;
        float LookAt[2] = {};
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
    FlashPlayer::FSCommandCallback generalCommandCallback;
    bool bDialogBoxVisible = false;
    FlashPlayer general;            // GENERAL.vmf (black_stripes letterbox)
    Matrix4x4f sceneOrigin;         // cinematic scene space -> island
    Matrix4x4f exteriorOrigin;      // spot frame (exterior sets / collision), = sceneOrigin unless lifted
    float collisionLift = 0.0f;     // cinematic-local y + lift = collision (spot) local y
    bool bSceneMoved = false;       // sceneOrigin != exteriorOrigin
    size_t introSetCount = 0;       // sets[0..n) from the scene bank, then the exterior ones
    size_t introPropCount = 0;      // same for props
    Quaternionf sceneRotation;      // rotation of sceneOrigin (Eden convention)
    Bank exteriorBank;              // islands/ibiza/exterior/eph_l3di.bnk
    Bank interiorBank;              // Setup::InteriorBank
    std::vector<std::unique_ptr<EdenModel>> sets; // house (D_SCENE) and exterior
    std::vector<EdenModel *> reflectSets; // Entity__Actor__Mesh D_REFLECT
    std::vector<Prop> props;
    EdenAvatarLibrary avatarLibrary;
    FaceFxLibrary faceFx; // avatar_face_fx.bnk actors + the .fxe of the bank
    std::vector<Pnj> pnjs;
    Pnj player;
    std::unordered_map<std::string, std::pair<Vector3f, Quaternionf>> entityCameras; // Entity__Camera orient_pos_scale
    std::unordered_map<uint64_t, std::unique_ptr<EdenAnimation>> animations;
    HavokCollision collision;
    std::unordered_map<std::string, std::pair<Vector3f, Quaternionf>> entityPlacements; // PosCam, CinCar
    CinematicCar car;
    struct ShowCar {
        std::unique_ptr<CinematicCar> Car;
        std::string RestEntity;
        bool bVisible = true;
        bool bEnvironment = false;
    };
    std::vector<ShowCar> showCars;
    int32_t seatedShowCar = -1; // setSeatedShowCar
    std::string carRestEntity;
    uint64_t carEntityHashcode = 0;
    std::unordered_map<std::string, uint64_t> cinCarHashcodes;
    std::string carActorName;
    std::vector<ActorMessage> actorMessages;
    void collectActorMessages( const Playing &playing, float from, float to );
    bool bCarEnvironment = false;
    // Static car cube of the scene bank (CUBECAR, garage.bnk): FUN_009aa2c0
    // -> FUN_00a4baf0; the dynamic cube is then not used.
    bool bCarStaticCube = false;
    std::vector<Light> lights;
    Vector3f cameraForward = Vector3f( 0.0f, 0.0f, 1.0f );
    std::vector<Volume> volumes;
    std::vector<Volume> selectables;
    float goalTimer = -1.0f;
    Bank driverBank;               // scenarics/avatars/avatarsdriver.bnk
    EdenAnimation driverPose;
    bool bDriverPose = false;
    // IKOffsetDB.bin record of the car for the player's sex
    // (docs/re/driver_seat_spec.md): seat group adjustment (y, z) and the
    // offsets of the 27 cockpit nodes (DvrSeat, Hand_R, Hand_L, Foot_R,
    // Foot_L, Radio, Back_L, ...) in their node frame.
    struct DriverIKOffsets {
        bool bValid = false;
        float SeatY = 0.0f, SeatZ = 0.0f;
        Vector3f Node[27];
    } driverIK;
    bool bDriverIKLoaded = false;
    bool loadDriverIKOffsets( int32_t idCar, bool bWoman );
    bool bPlayerSeated = false;
    void updateSeatedPlayer();
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
    // Animation track of a .sce actor on a skeleton (avatar_spec 4.1); the
    // style anims of bWoman replace the clip references.
    void sampleActorAnimation( const Playing &playing, const CinematicScene &scene, const CinematicScene::Object &object,
                               float sceneTime, const EdenSkeleton &skeleton, bool bWoman,
                               std::vector<BoneTransform> &locals );
    void updatePnjs();
    // .sce actor track "Morph": FaceFX animation of the facial bones.
    void applyFaceFx( Pnj &pnj, const CinematicScene &scene, const CinematicScene::Object &object, float sceneTime,
                      std::vector<BoneTransform> &locals );
    // FUN_00b871a0 / FUN_00b89730 cone test: LookAt_T2 registers of the
    // frame (pnj.bLookAt / pnj.LookAt) from the property LookAtTargetFaceFx
    // or the lkon message.
    void computeLookAt( Pnj &pnj, const CinematicScene &scene, const CinematicScene::Object &object, float sceneTime,
                        const std::vector<BoneTransform> &locals, const Matrix4x4f &world );
    void loadLights( const XmbDocument &entities );
    void computeAvatarLighting( const Vector3f &rootPosition, EdenAvatarLighting &outLighting ) const;
    bool computeCarWorld( Matrix4x4f &outWorld ) const;
    Matrix4x4f fitOnSceneFloor( const CinematicCar &car, const Vector3f &position, const Quaternionf &rotation ) const;
    std::vector<const Playing *> playingOrder() const;
    void drawFade();
};
