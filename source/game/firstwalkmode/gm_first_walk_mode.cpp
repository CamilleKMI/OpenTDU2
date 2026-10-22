#include "shared.h"
#include "gm_first_walk_mode.h"

#include "database/gs_database.h"
#include "game/player_profile.h"
#include "audio/gs_audio.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

namespace {
constexpr uint32_t kLocGetIntoTheCar = 5032; // 0x13a8 TUTO_ENTER_IN_THE _CAR
// C_VOICE_03..06 (table 0x00f76a78).
const char *kVoices[4] = { "C_VOICE_03", "C_VOICE_04", "C_VOICE_05", "C_VOICE_06" };

std::string LocString( uint32_t id ) {
    const char *pText = gpDatabase != nullptr ? gpDatabase->getString( id ) : nullptr;
    return pText != nullptr ? pText : "";
}
} // namespace

GMFirstWalkMode::GMFirstWalkMode() : GameMode( eGameMode::GM_FirstWalkMode ) {}

GMFirstWalkMode::~GMFirstWalkMode() {}

bool GMFirstWalkMode::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    // audiosetup.xmb game mode -> mix preset.
    if ( gpAudio != nullptr ) {
        gpAudio->setGameMode( "FIRST_WM" );
    }
    // FUN_00a91360: Scenarics/Cinematics/intro/garage.bnk; the garage is a
    // closed room in its own frame (spec_pool_interior 4.2): no island.
    IntroCinematic::Setup setup;
    setup.Bank = "scenarics/cinematics/intro/garage.bnk";
    setup.HouseSpot.clear();
    setup.ExteriorBank.clear();
    setup.ExteriorPrefix.clear();
    setup.bWorld = false;
    setup.GameModeId = 0x23;
    bLoaded = garage.load( setup );
    if ( !bLoaded ) {
        OTDU_LOG_WARN( "FIRST_WM: garage.bnk could not be loaded\n" );
        return true;
    }
    // Story step 1 enter (FUN_00b10da0): car 387 (California) becomes the
    // current car; it rests at the garage PosCam D_CAR.
    garage.loadCar( CinematicCar::Desc(), "D_CAR", "CIN_CAR" );
    // The player avatar (profile+0x30 configuration).
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.AvatarConfig.empty() ) {
        const char *pAvatar = CmdLine::GetArgValue( "-avatar" );
        profile.AvatarConfig = pAvatar != nullptr ? pAvatar : "AVT_Male_Intro";
    }
    garage.setPlayerAvatar( profile.AvatarConfig );
    garage.setCommandCallback(
        [this]( const std::string &command, const std::string & ) { commands.push_back( command ); } );
    // FUN_009aa2c0: C_LIFE_SOUND ambient (CinProps |= 0x10), C_INTRO
    // (skippable).
    garage.playBackground( "C_LIFE_SOUND", true );
    garage.playMain( "C_INTRO" );
    state = eState::Intro;
    return true;
}

void GMFirstWalkMode::exit() {
    garage.unload();
    bLoaded = false;
}

void GMFirstWalkMode::onCinematicEnd( const std::string &name ) {
    // Listener FUN_009a9d10 (events 3 / 9).
    OTDU_LOG_DEBUG( "FIRST_WM: end of %s\n", name.c_str() );
    for ( uint32_t i = 0; i < 4; i++ ) {
        if ( name == kVoices[i] ) {
            voiceDone[i] = true;
            bVoicePlaying = false;
            return;
        }
    }
    if ( name == "C_INTRO" ) {
        // C_LIFE_TESS (CinProps |= 0x1010, loop), walk camera cam_entree
        // (Fov 0.8727), INTRO "wm_garage", goal "Get into the car".
        garage.stopMain();
        garage.playBackground( "C_LIFE_TESS", true );
        walk.start( garage, "cam_entree", 0.8727f );
        garage.getUi().gotoLabel( "/", "wm_garage", true );
        garage.showGoal( LocString( kLocGetIntoTheCar ) );
        state = eState::Walk;
    } else if ( name == "C_ENTER_CAR" ) {
        // FUN_008c1880 (enter car, seat 0): the in-car camera is encrypted
        // [ENC]; the garage Entity__Camera cam_EnterCar (Fov 30) is used
        // [INF from its name].
        garage.stopMain();
        EdenSceneView::Camera camera = garage.getCamera();
        Vector3f position;
        Quaternionf heading;
        if ( garage.getEntityCameraLocal( "cam_EnterCar", position, heading ) ) {
            garage.setLocalCamera( position, heading, 30.0f * kDegToRad );
        } else {
            garage.setCameraOverride( &camera );
        }
        bEnteredCar = true;
        state = eState::Seated;
    } else if ( name == "C_OUT" ) {
        garage.playMain( "C_OUTRO" );
        state = eState::Outro;
    } else if ( name == "C_OUTRO" ) {
        // Story step 2 "First Freeride" (fade_freeride.bnk, FREERIDE): not
        // implemented yet; the last frame stays.
        GetPlayerProfile().StoryStep = 2;
        OTDU_LOG_INFO( "FIRST_WM: end of the garage (story step 2 First Freeride not implemented)\n" );
        state = eState::Done;
    }
}

void GMFirstWalkMode::updateVoices() {
    // FUN_009a9990 -> FUN_009a9920: Tess comments while no cinematic runs.
    int32_t index = 0;
    if ( bEnteredCar ) {
        index = 3;
    } else if ( walk.getSelected() >= 0 ) {
        // playerVehicleState (+0xa794 meaning not identified): C_VOICE_04.
        index = 1;
    }
    if ( !voiceDone[index] && !bVoicePlaying ) {
        garage.playBackground( kVoices[index], false );
        bVoicePlaying = true;
    }
}

void GMFirstWalkMode::tick( float deltaTime ) {
    if ( !bLoaded ) {
        pGameInstance->setNextGameMode( eGameMode::GM_Login );
        return;
    }
    // C_INTRO is skippable (CinProps |= 4); the skip press is not reused
    // by the walk mode of the same frame.
    bool bSkipped = false;
    if ( state == eState::Intro &&
         ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) &&
         CmdLine::HasArg( "-allowskip" ) ) {
        garage.skipMain();
        bSkipped = true;
    }
    garage.tick( deltaTime );
    for ( const std::string &name : garage.takeEndEvents() ) {
        onCinematicEnd( name );
    }
    std::vector<std::string> pending;
    pending.swap( commands );
    for ( const std::string &command : pending ) {
        OTDU_LOG_DEBUG( "FIRST_WM: FSCommand %s\n", command.c_str() );
    }
    if ( state == eState::Walk ) {
        walk.update( garage, deltaTime, true );
        updateVoices();
        // FUN_009aa560: A while the "car" selectable is selected ->
        // C_ENTER_CAR.
        if ( !bEnteredCar && strcmp( walk.getSelectedType( garage ), "car" ) == 0 &&
             gpInput->wasPressed( eInputAction::IA_MenuValidate ) && !bSkipped ) {
            garage.getUi().setClipVisible( "/cursor", false );
            walk.clearSelection( garage );
            garage.closeGoal();
            garage.setCameraOverride( nullptr );
            garage.playMain( "C_ENTER_CAR" );
            state = eState::EnterCar;
        }
    } else if ( state == eState::Seated ) {
        updateVoices();
        // Throttle > 0.7 -> 3 s -> C_OUT.
        if ( revTimer == 0.0f && gpInput->getAnalog( eInputAction::IA_CarAccel ) > 0.7f ) {
            revTimer = 3.0f;
        }
        if ( revTimer > 0.0f ) {
            revTimer -= deltaTime;
            if ( revTimer <= 0.0f ) {
                revTimer = 0.0f;
                garage.setCameraOverride( nullptr );
                garage.playMain( "C_OUT" );
                state = eState::Out;
            }
        }
    }
}

void GMFirstWalkMode::drawOffscreen() {
    if ( bLoaded ) {
        garage.renderOffscreen();
    }
}

void GMFirstWalkMode::draw() {
    if ( bLoaded ) {
        garage.draw();
    }
}
