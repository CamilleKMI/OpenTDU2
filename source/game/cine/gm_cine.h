#pragma once

#include "game/avatarchoice/intro_cinematic.h"
#include "game/gm_base.h"

// "CINE" (mode 0x1f): a story cinematic at a spot of the island
// (docs/re/story_spec.md 3). FUN_00b9a1f0 posts the request (kind, story
// callback), FUN_009b6df0 (init) loads the cinematic bank and finds the spot
// (FUN_009b57b0: kind -> bank / spot name), FUN_009b54f0 (tick) starts the
// cinematic when the world is ready (FUN_009b5210: name from FUN_009b46c0,
// skippable for kind 1..3, 5..0x34 by FUN_009b41b0), the listener
// FUN_009b48c0 calls the story callback FUN_00b104d0 at the end (events 6 /
// 2): story step 3 -> 4, 5 -> 6, 6 -> 7, 13 -> 11.
//
// Implemented:
//   - kind 1, story step 3 "Meeting Tess": meeting_tess.bnk at the hotel spot
//     E_HOTELA_1266I, C_MEETING_TESS (male avatar) or C_MEETING_TESS_W
//     (female), 66.7 s: the avatar wakes up in the California parked at the
//     hotel valet place (PosCam D_CAR), Tess knocks, phone call, the Ferrari
//     key; end -> story step 4 "Driving Tess" (mode 4 MISSION, GMMission);
//   - kind 2, story step 5 "Solar Crown exterior": solar_crown_ext.bnk at
//     ECH_IA_1243I, C_SOLAR_CROWN_E (12.5 s): the California arrives at the
//     venue (PLAYER_CAR), the avatar gets out (avm4 -> avm1) and joins Tess;
//     end -> story step 6;
//   - kind 3, story step 6 "Solar Crown interior": solar_crown_int.bnk +
//     Interior/ich_ib.bnk, C_SOLAR_CROWN_I (107 s, the TV set: Tess, Todd
//     Bishop, the crew); end -> story step 7 "Used Car Dealer" (mode 3 SHOP,
//     GMShop hand-off stub).
// Command line: -gm CINE or -storystep 3 / 5 / 6 (-cinekind n, -cinetime s).
class GMCine : public GameMode {
public:
    // FUN_009b57b0 / FUN_009b46c0 table (kinds read from the code).
    struct Kind {
        int32_t Id = 0;
        const char *pBank = "";        // cinematic bank (Euro/Bnk/...)
        const char *pSpot = "";        // spots.bin spot (the scene frame)
        const char *pExterior = "";    // the spot exterior bank (world spot streaming)
        const char *pCinematic = "";   // male / default name
        const char *pCinematicW = "";  // female avatar name ("" = same)
        const char *pInterior = "";    // interior bank (FUN_009b57b0 3rd name)
    };
    static const Kind *FindKind( int32_t id );

    GMCine();
    ~GMCine();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;

private:
    enum class eState { Playing, Ended };

    // Car door channel (door object vtable 0x00f20214, docs/re/garage_car_spec.md
    // 2.2): elapsed / duration / opening.
    struct DoorChannel {
        float Elapsed = 1.0f;
        float Duration = 1.0f;
        bool bOpening = false;
    };

    IntroCinematic cine;
    const Kind *pKind = nullptr;
    std::string cinematicName;
    eState state = eState::Playing;
    bool bLoaded = false;
    bool bStoryEnded = false;
    DoorChannel doors[2];
    float doorMaxAngle = 1.0f;

    void onCinematicEnd();
    void onActorMessage( const IntroCinematic::ActorMessage &message );
    void startDoor( int32_t side, float duration, bool bOpen );
    void updateDoors( float deltaTime );
};
