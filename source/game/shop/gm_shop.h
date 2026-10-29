#pragma once

#include "game/avatarchoice/intro_cinematic.h"
#include "game/gm_base.h"
#include "game/walkmode/walk_mode.h"

// "SHOP" (mode 3), story step 7 "Used Car Dealer" (FUN_00b10da0 case 8,
// docs/re/story_spec.md 5.2): Todd Bishop's used car dealer at the spot
// ESTOL_B_1275I (spots.bin type 0x11: the used car dealer object
// FUN_00dde970, vtable 0x00f26380, shop page "scr_open" of SHOPSCAR.vmf).
//   - enter: profile+0x58 = 7, loaned car 0x146 (326 Dino_246_GTS,
//     FUN_00aba280), interior bank Interior/ISTOL_B.bnk (FUN_00a91360) with
//     the exterior islands/ibiza/exterior/estol_b.bnk, the DBCarShop slots of
//     the spot (colour 0, default rims, interior 0: FUN_009f4080) on the
//     Entity__ShopData PosCams (D_CAR_01..), SHOPSCAR /:num_car, /:nb_cars,
//     /:Message, /:IsTutorial (FUN_009f4ab0);
//   - C_FIRST_INTRO (Todd's welcome, 30.2 s, skippable), then C_FIRST_LOOP
//     (Todd and Jude idle) and the walk mode from CAM_INTRO
//     (Entity__FirstPerson StartCamera) with the SHOPSCAR cursor; the step
//     tick pops the goal 4033 "Buy a car!" (TUTO_BUY_A_CAR);
//   - a car under the reticle: its data in SHOPSCAR (FUN_0099dbd0 /
//     FUN_009f25f0: name, km, price, specs), validate: mini menu (Buy, Enter,
//     Data, Compare), Buy: price panel, FSCommand eval (FUN_00ddec10(1):
//     /:EnoughMoney, /:BuyOK, /:PriceRed), confirmation, FSCommand buyc
//     (FUN_00ddec10(0)): the car is bought (FUN_00ada490 / FUN_00adc920:
//     garage record, money - price), C_EXPLAIN_FROG (Todd explains the
//     F.R.I.M.), at its end FREERIDE (mode 1) -> story step 8 "Tuto FROG"
//     (hand-off: GMFreeRide step-8 stub).
// Command line: -storystep 7 or -gm SHOP; -money n (test), -skipcine (no
// C_FIRST_INTRO).
class GMShop : public GameMode {
public:
    GMShop();
    ~GMShop();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;

private:
    enum class eState { Intro, Walk, SelectableCinematic, InCar, Bought, ExplainFrog, TestDrive, Done };

    // A car of the shop scene (mode +0x1b95c0 slot ring, 0x90 bytes: +8
    // colour, +0xc rims, +0x10 interior; dealer +0xd0 car ids).
    struct ShopCar {
        int32_t Slot = 0;          // DBCarShop _slot_<Slot + 1>
        int32_t IdCar = -1;
        int32_t PhysicsRow = -1;   // DBCarPhysicsData row
        int32_t Color = 0;         // DBCarsAndColors row index of the car
        int32_t ColorRow = -1;     // DBCarsAndColors table row
        int32_t Rims = 0;          // DBCarsAndRims row index of the car
        uint64_t RimsHashcode = 0; // DBRims _hashcode
        int32_t Interior = 0;
        int32_t Price = 0;         // dealer vfunc 0x38 (FUN_00dde780)
        std::string Name;
        std::string ColorName;
        std::string PosCam;
        int32_t ShowCar = -1;      // IntroCinematic show car
        int32_t Selectable = -1;   // walk-mode selectable ("vehicle")
    };

    IntroCinematic scene;
    WalkMode walk;
    bool bLoaded = false;
    eState state = eState::Intro;
    std::string shopTitle;
    uint32_t shopTitleId = 0;
    std::vector<ShopCar> cars;
    int32_t hoveredCar = -1;       // index in cars
    int32_t boughtCar = -1;
    bool bWalkInput = true;        // wmof / wmon (FUN_0094fb30 / FUN_0094fb90)
    bool bGoalShown = false;       // step 7 tick this[6]
    bool bWalkOffSinceBuy = false; // DAT_010268b6 (set by wmof)
    bool bDialogOpen = false;      // GENERAL dialog box (dbxo .. dbxc)
    std::string selectableCinematic; // walk manager +0xd64 (camn)
    std::vector<std::string> pendingCommands;
    float explainEnd = 0.0f;
    // "Enter" (FSCommands rqca / cain / caou / caca): the player seated in a
    // showroom car (FUN_008c1880 -> FUN_008c12f0 -> FUN_00a519b0), the
    // cockpit view (FUN_00a50550) [CODE]; look-around angles [INF].
    int32_t seatedCar = -1;
    float lookYaw = 0.0f;
    float lookPitch = 0.0f;
    struct CockpitCamera {
        Vector3f Position = Vector3f( 0.37f, 1.01f, -0.36f );
        Vector3f Target = Vector3f( 0.37f, 0.9f, 1.35f );
        float Fov = 55.0f * kDegToRad;
        float Near = 0.05f;
    } cockpit;
    std::string introCinematic = "C_FIRST_INTRO";

    bool loadCars();
    void startWalk();
    void onCommand( const std::string &command, const std::string &args );
    void onCinematicEnd( const std::string &name );
    void onSelection( int32_t selectable );
    void fillCarVariables( const ShopCar &car );
    void updateMoneyVariables();
    // FUN_00ddec10(bEvalOnly): returns true when bought.
    bool buyOrEvaluate( bool bEvalOnly );
    void startExplainFrog();
    void routeInput();
    void enterCar( int32_t carIndex );
    void exitCar();
    void updateCockpitCamera( float deltaTime );
    bool loadCockpitCamera( int32_t cameraId );
    void compareSelected();
    void writeCompareVariables();
    void startTestDrive();
    std::string formatMoney( int32_t value ) const;
};
