#include "shared.h"
#include "gm_cine.h"

#include "audio/gs_audio.h"
#include "database/gs_database.h"
#include "flash/flash_player.h"
#include "game/player_profile.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

namespace {
// FUN_009b57b0 (bank / spot by kind) and FUN_009b46c0 (cinematic name)
// [CODE]; exterior: islands/ibiza/exterior/<spots.bin bank>.bnk of the spot
// [DATA]; interior "%sInterior/ich_ib.bnk" of kind 3 (FUN_00b9ff50) [CODE].
// Kinds 1..3 are reached by the story steps 3, 5, 6.
constexpr GMCine::Kind kKinds[] = {
    { 1, "scenarics/cinematics/intro/meeting_tess.bnk", "E_HOTELA_1266I", "islands/ibiza/exterior/e_hotela.bnk",
      "C_MEETING_TESS", "C_MEETING_TESS_W", "" },
    { 2, "scenarics/cinematics/intro/solar_crown_ext.bnk", "ECH_IA_1243I", "islands/ibiza/exterior/ech_ia.bnk",
      "C_SOLAR_CROWN_E", "", "" },
    { 3, "scenarics/cinematics/intro/solar_crown_int.bnk", "ECH_IA_1243I", "islands/ibiza/exterior/ech_ia.bnk",
      "C_SOLAR_CROWN_I", "", "interior/ich_ib.bnk" },
    { 0x2b, "scenarics/cinematics/island/isl_igloo.bnk", "EPH_L0A_1276", "", "C_IGLOO", "", "" },
};

// Door ease FUN_00a48310 (docs/re/garage_car_spec.md 2.2) [CODE].
float DoorEase( float f, bool bOpening ) {
    if ( bOpening ) {
        return f < 0.1f ? f * 0.5f : f * 1.0555556f - 0.0555556f;
    }
    return f < 0.1f ? f * 2.0f : f * 0.8888889f + 0.1111111f;
}

std::string PlayerName() {
    // profile+0xc1; no profile yet: -playername, else the system user name [INF].
    if ( const char *pName = CmdLine::GetArgValue( "-playername" ) ) {
        return pName;
    }
    if ( const char *pUser = getenv( "USER" ) ) {
        return pUser;
    }
    return "Player";
}
} // namespace

const GMCine::Kind *GMCine::FindKind( int32_t id ) {
    for ( const Kind &kind : kKinds ) {
        if ( kind.Id == id ) {
            return &kind;
        }
    }
    return nullptr;
}

GMCine::GMCine() : GameMode( eGameMode::GM_Cine ) {}

GMCine::~GMCine() { exit(); }

bool GMCine::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    if ( gpAudio != nullptr ) {
        gpAudio->setGameMode( "CINE" );
    }
    // FUN_009b6df0: the request (DAT_01025608 + 0x9730 -> mode +0x1adc80).
    CineRequest &request = GetCineRequest();
    if ( request.Kind == 0 ) {
        // -gm CINE without a story request: story step 3 (kind 1), or the
        // -cinekind one (test); the story callback chains the next steps.
        request.Kind = CmdLine::HasArg( "-cinekind" ) ? CmdLine::GetArgInt( "-cinekind", 1 ) : 1;
        request.bStoryCallback = true;
    }
    pKind = FindKind( request.Kind );
    if ( pKind == nullptr ) {
        OTDU_LOG_WARN( "CINE: cinematic kind %d not handled\n", request.Kind );
        return true;
    }
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.AvatarConfig.empty() ) {
        const char *pAvatar = CmdLine::GetArgValue( "-avatar" );
        profile.AvatarConfig = GetStoryAvatarConfig( pAvatar != nullptr ? pAvatar : "AVT_Male_Intro" );
    }
    if ( request.Kind == 2 ) {
        // Story step 5 enter (case 6): profile+0x58 = 5, car 0x183 (-1, -1,
        // -1) (FUN_00aba280), CINE kind 2 with the story callback [CODE].
        profile.StoryStep = 5;
    } else if ( request.Kind == 3 ) {
        // Story step 6 enter (case 7): no profile write (+0x58 stays 5), CINE
        // kind 3 without callback; the step tick sets the callback
        // (DAT_01025608 + 0x9734 = FUN_00b104d0) once the switch is done
        // (DAT_00fd50fc + 0x31 == 0) [CODE].
        profile.StoryStep = 6;
    }
    if ( request.Kind == 1 ) {
        // Story step 3 enter (FUN_00b10da0 case 4): profile+0x58 = 3, the
        // avatar out of the dream (FUN_00b105d0), car 0x183 (387 California)
        // current (FUN_00ab9be0), CINE request kind 1 with the story callback.
        profile.StoryStep = 3;
        if ( profile.AvatarConfig.empty() ) {
            const char *pAvatar = CmdLine::GetArgValue( "-avatar" );
            profile.AvatarConfig = pAvatar != nullptr ? pAvatar : "AVT_Male_Intro";
        }
        profile.AvatarConfig = GetStoryAvatarConfig( profile.AvatarConfig );
        if ( profile.Name.empty() ) {
            profile.Name = PlayerName();
        }
    }
    IntroCinematic::Setup setup;
    setup.Bank = pKind->pBank;
    setup.HouseSpot = pKind->pSpot;
    setup.ExteriorBank = pKind->pExterior;
    setup.ExteriorPrefix.clear();
    setup.bWorld = true;
    setup.GameModeId = 0x1f;
    setup.bExteriorCollision = true;
    setup.InteriorBank = pKind->pInterior;
    bLoaded = cine.load( setup );
    if ( !bLoaded ) {
        OTDU_LOG_WARN( "CINE: '%s' could not be loaded\n", pKind->pBank );
        return true;
    }
    cine.setPlayerAvatar( profile.AvatarConfig.empty() ? "AVT_Male" : profile.AvatarConfig );
    // The player car (.sce actor PLAYER_CAR, no entity): kind 1 at the hotel
    // valet place (Entity__PosCam D_CAR: the PLAYER_CAR key maps on it);
    // kind 2 driven by its track (134 keys); kind 3 has no car [DATA].
    if ( request.Kind != 3 ) {
        cine.loadCar( CinematicCar::Desc(), request.Kind == 1 ? "D_CAR" : "", "", "PLAYER_CAR" );
    }
    // FUN_009b5210 (no spot slot for kind 1, mode +0xb448 == 0): the
    // cinematic transform (cine +0x11e = 1, +0x120) is derived from the
    // player car driver matrix (driver +0x160), snapped on the road network
    // by FUN_00a6a5c0 (FUN_00cfb280 / FUN_00cfab80 on DAT_00fd5134 + 0x3610);
    // the matrix helpers FUN_0041d9e0 / FUN_0042a7d0 / FUN_0042b4f0 are
    // encrypted [CODE + ENC]. The car itself is at the spot's second
    // transform (spot object +0x50 position / +0x60 heading = spots.bin
    // +0x28 / +0x38, FUN_00b2eb80): the scene is placed so that its PLAYER_CAR
    // (= Entity__PosCam D_CAR) is there [INF: where the car is put before
    // the cinematic is not in readable code; with this pose the car stands
    // on the sea-front road of the hotel and the actors on the ground].
    // Kinds 2 / 3 (no D_CAR) stay in the spot frame, like the spot
    // cinematic C_FIRST_OUTRO whose PLAYER_CAR track lands on the road in
    // that frame (freeride_spec 1.4) [INF].
    IntroCinematic::SpotRecord spot;
    if ( request.Kind == 1 ) {
        if ( IntroCinematic::ReadSpot( pKind->pSpot, spot ) && !CmdLine::HasArg( "-cinelift" ) ) {
            cine.placeEntityAt( "D_CAR", spot.Position2, spot.Heading2 );
        } else {
            // Fallback / comparison: the spot frame lifted onto the hotel floor.
            cine.liftOntoFloor( "D_CAR" );
        }
    }
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
            if ( StringCmpI( car["_car_file_name"].asString().c_str(), "California" ) == 0 ) {
                doorMaxAngle = car["_doormaxangle"].asFloat( 1.0f );
                break;
            }
        }
    }
    // FUN_009b46c0 kind 1: FUN_00cb5fb0() (profile avatar +8 = DBPNJ _sex)
    // == 0 -> C_MEETING_TESS, else C_MEETING_TESS_W. The "_Female" configs
    // have _sex 1, "_Male" 2 [DATA]; the male avatar plays C_MEETING_TESS
    // (its scene has the -m style anims and intronew-e-m.fxe) [INF: the
    // value compared is the avatar sex field].
    const bool bWoman = profile.AvatarConfig.find( "Female" ) != std::string::npos;
    cinematicName = bWoman && pKind->pCinematicW[0] != '\0' ? pKind->pCinematicW : pKind->pCinematic;
    // Listener event 0 (FUN_009b48c0): GENERAL /:PlayerName.
    if ( cine.getGeneral().isLoaded() ) {
        cine.getGeneral().setVariable( "/:PlayerName", profile.Name );
    }
    if ( !cine.playMain( cinematicName, true, static_cast<float>( CmdLine::GetArgInt( "-cinetime", 0 ) ) ) ) {
        OTDU_LOG_WARN( "CINE: cinematic '%s' not found\n", cinematicName.c_str() );
    }
    state = eState::Playing;
    Matrix4x4f carWorld;
    if ( cine.getCarWorld( carWorld ) ) {
        const Vector3f p = carWorld.getTranslation();
        float terrain = 0.0f;
        const bool bTerrain = cine.getGroundHeight( p.x, p.z, terrain );
        OTDU_LOG_INFO( "CINE: car at (%.2f, %.2f, %.2f), terrain %s %.2f\n", p.x, p.y, p.z, bTerrain ? "" : "(none)",
                       terrain );
    }
    OTDU_LOG_INFO( "CINE: kind %d, %s (bank %s, spot %s), avatar %s, story step %d\n", request.Kind,
                   cinematicName.c_str(), pKind->pBank, pKind->pSpot, profile.AvatarConfig.c_str(), profile.StoryStep );
    return true;
}

void GMCine::exit() {
    if ( bLoaded ) {
        cine.unload();
    }
    bLoaded = false;
}

void GMCine::startDoor( int32_t side, float duration, bool bOpen ) {
    // FUN_00a48770(duration, channel, open, 1): 0 = end state at once,
    // otherwise start / reverse the channel [CODE].
    DoorChannel &door = doors[side & 1];
    if ( duration <= 0.0f ) {
        door.Elapsed = door.Duration = 1.0f;
        door.bOpening = bOpen;
    } else if ( door.Elapsed < door.Duration && door.bOpening != bOpen ) {
        // Reversed while moving [INF: FUN_00a483f0 inverse ease approximated
        // by the linear ratio, as in FIRST_WM].
        const float progress = door.Elapsed / door.Duration;
        door.Duration = duration;
        door.Elapsed = ( 1.0f - progress ) * duration;
        door.bOpening = bOpen;
    } else {
        door.Duration = duration;
        door.Elapsed = 0.0f;
        door.bOpening = bOpen;
    }
}

void GMCine::updateDoors( float deltaTime ) {
    // Door tick 0x00a49400 / FUN_00a484c0: angle = -max * ease(f) for the
    // left door, +max * ease(f) for the right one [CODE].
    for ( int32_t side = 0; side < 2; side++ ) {
        DoorChannel &door = doors[side];
        door.Elapsed = Min( door.Elapsed + deltaTime, door.Duration );
        float f = door.Duration > 0.0f ? door.Elapsed / door.Duration : 1.0f;
        if ( !door.bOpening ) {
            f = 1.0f - f;
        }
        const float angle = ( side == 0 ? -doorMaxAngle : doorMaxAngle ) * DoorEase( f, door.bOpening );
        if ( angle != cine.getCar().getDoorAngle( side ) ) {
            cine.getCar().setDoorAngle( side, angle );
        }
    }
}

void GMCine::onActorMessage( const IntroCinematic::ActorMessage &message ) {
    // Car messages (FUN_00dc5a40): opdl / opdr / opda, cldl / cldr / clda
    // (duration argument), opw* / clw* windows [CODE].
    const std::string &text = message.Message;
    if ( text.size() < 4 ) {
        return;
    }
    const std::string code = text.substr( 0, 4 );
    const float duration = text.size() > 5 && text[4] == ':' ? static_cast<float>( atof( text.c_str() + 5 ) ) : 0.0f;
    if ( code == "opdl" || code == "cldl" || code == "opda" || code == "clda" ) {
        startDoor( 0, duration, code[0] == 'o' );
    }
    if ( code == "opdr" || code == "cldr" || code == "opda" || code == "clda" ) {
        startDoor( 1, duration, code[0] == 'o' );
    }
    // Windows (clwl:0 / clwr:0 at t = 0: closed): the California windows are
    // not animated by CinematicCar [INF: no window node motion implemented].
    OTDU_LOG_DEBUG( "CINE: %s message %s\n", message.Actor.c_str(), text.c_str() );
}

void GMCine::onCinematicEnd() {
    // Listener FUN_009b48c0: event 6 (near end) / 2 (stop) -> the request
    // callback FUN_00b104d0(story): story step 3 -> vfunc8(4) "Driving Tess"
    // (FUN_00b10da0 case 5: profile+0x58 = 4, event "TESSEVEN" (DBEvent 21,
    // type 13), mode 4 MISSION). CinEnd 3: the end pose stays.
    state = eState::Ended;
    CineRequest &request = GetCineRequest();
    if ( request.bStoryCallback && !bStoryEnded ) {
        bStoryEnded = true;
        PlayerProfile &profile = GetPlayerProfile();
        if ( profile.StoryStep == 3 ) {
            // Story step 4 enter (case 5): mode 4 MISSION, event TESSEVEN.
            profile.StoryStep = 4;
            request = CineRequest();
            OTDU_LOG_INFO( "CINE: end of %s -> story step 4 'Driving Tess' (MISSION, TESSEVEN)\n",
                           cinematicName.c_str() );
            pGameInstance->setNextGameMode( eGameMode::GM_Mission );
        } else if ( profile.StoryStep == 5 ) {
            // Step 6 enter (case 7): mode 0x1f again (switch requested), CINE
            // request kind 3 (FUN_00b9a1f0 without callback, set by the tick).
            profile.StoryStep = 6;
            request.Kind = 3;
            request.bStoryCallback = true;
            OTDU_LOG_INFO( "CINE: end of %s -> story step 6 'Solar Crown interior' (CINE kind 3)\n",
                           cinematicName.c_str() );
            pGameInstance->setNextGameMode( eGameMode::GM_Cine );
        } else if ( profile.StoryStep == 6 ) {
            // Step 7 enter (case 8): profile+0x58 = 7, car 0x146 (326
            // Dino_246_GTS) current, spot ESTOL_B_1275I, mode 3 SHOP.
            profile.StoryStep = 7;
            request = CineRequest();
            OTDU_LOG_INFO( "CINE: end of %s -> story step 7 'Used Car Dealer' (SHOP)\n", cinematicName.c_str() );
            pGameInstance->setNextGameMode( eGameMode::GM_Shop );
        }
    }
}

void GMCine::tick( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    // Skippable (FUN_009b41b0(kind) -> CinProps |= 4) [CODE]; the skip input
    // itself is encrypted -> validate / pause as for C_INTRO [INF].
    if ( state == eState::Playing && cine.getMainTime() > 0.5f &&
         ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) ) {
        cine.skipMain();
    }
    cine.tick( deltaTime );
    for ( const IntroCinematic::ActorMessage &message : cine.takeActorMessages() ) {
        if ( message.Actor == "PLAYER_CAR" ) {
            onActorMessage( message );
        } else if ( message.Actor == "Avatar" && message.Message.compare( 0, 3, "avm" ) == 0 ) {
            // avm4 (in the car) / avm1 (on foot): C_SOLAR_CROWN_E Avatar at 0
            // and 4.32 s [DATA]; the handler is [ENC]: avm4 seats the avatar
            // at the driver seat of the scene car (as avm4:CIN_CAR of C_OUTRO),
            // another mode gives it back to its .sce tracks [INF].
            cine.setPlayerSeated( message.Message.compare( 0, 4, "avm4" ) == 0 );
        }
    }
    updateDoors( deltaTime );
    for ( const std::string &name : cine.takeEndEvents() ) {
        if ( name == cinematicName ) {
            onCinematicEnd();
        }
    }
}

void GMCine::drawOffscreen() {
    // -no3d: logic tests without the (software) 3D rendering.
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( bLoaded && !bNo3D ) {
        cine.renderOffscreen();
    }
}

void GMCine::draw() {
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( bLoaded && !bNo3D ) {
        cine.draw();
    }
}
