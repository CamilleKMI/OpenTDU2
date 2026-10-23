#include "shared.h"
#include "gm_avatar_choice.h"

#include "database/gs_database.h"
#include "audio/gs_audio.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"
#include "game/player_profile.h"

namespace {
// Localized strings (DBLoc).
constexpr uint32_t kLocTalkToTess = 5033;   // TUTO_TALK_TO_TESS
constexpr uint32_t kLocGoToGarage = 5108;   // 0x13f4, FUN_00da8040 C_TESS end

std::string LocString( uint32_t id ) {
    const char *pText = gpDatabase != nullptr ? gpDatabase->getString( id ) : nullptr;
    return pText != nullptr ? pText : "";
}

std::string PlayerName() {
    // profile+0xc1 (the profile name). OpenTDU2 has no profile yet:
    // -playername, else the system user name [INF].
    if ( const char *pName = CmdLine::GetArgValue( "-playername" ) ) {
        return pName;
    }
    if ( const char *pUser = getenv( "USER" ) ) {
        return pUser;
    }
    if ( const char *pUser = getenv( "USERNAME" ) ) {
        return pUser;
    }
    return "Player";
}
} // namespace

GMAvatarChoice::GMAvatarChoice() : GameMode( eGameMode::GM_AvatarChoice ) {}

GMAvatarChoice::~GMAvatarChoice() {}

bool GMAvatarChoice::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    // audiosetup.xmb game mode -> mix preset.
    if ( gpAudio != nullptr ) {
        gpAudio->setGameMode( "AVATAR_CHOICE" );
    }
    // AVATAR_CHOICE start (FUN_00da92e0): the C_INTRO real time cinematic.
    bIntroLoaded = intro.load();
    if ( !bIntroLoaded ) {
        OTDU_LOG_WARN( "AVATAR_CHOICE: new_intro could not be loaded\n" );
        return true;
    }
    intro.setCommandCallback(
        [this]( const std::string &command, const std::string & ) { commands.push_back( command ); } );
    state = eState::Intro;
    index = -1;
    selected = -1;
    bTessStarted = false;
    skipTimer = 0.0f;
    // C_INTRO start (event 0): the party music emitters are stopped.
    intro.stopMusic();
    intro.playMain( "C_INTRO" );
    // Test shortcuts: -skipintro (straight to the choice), -walkmode n
    // (avatar n validated, straight to the walk mode).
    if ( CmdLine::HasArg( "-skipintro" ) ) {
        intro.skipMain();
    }
    if ( CmdLine::HasArg( "-walkmode" ) ) {
        selected = Clamp( CmdLine::GetArgInt( "-walkmode", 1 ) - 1, 0, 5 );
        static const char *kConfigs[6] = { "AVT_Male_Intro",      "AVT_Female_Intro",     "AVT_Male_Afrik_Intro",
                                           "AVT_Female_Afrik_Intro", "AVT_Male_Asia_Intro", "AVT_Female_Asia_Intro" };
        intro.stopMain();
        intro.playBackground( "C_WALKMODE" );
        intro.setPlayerAvatar( kConfigs[selected] );
        GetPlayerProfile().AvatarConfig = kConfigs[selected];
        GetPlayerProfile().Name = PlayerName();
        // State of the C_AV_01 / C_FIESTA weather messages.
        intro.playMain( "C_AV_01" );
        intro.stopMain();
        startWalkMode();
    }
    return true;
}

void GMAvatarChoice::exit() {
    intro.unload();
    bIntroLoaded = false;
    commands.clear();
}

void GMAvatarChoice::onCinematicEnd( const std::string &name ) {
    // FUN_00da8040, event 7.
    OTDU_LOG_DEBUG( "AVATAR_CHOICE: end of %s\n", name.c_str() );
    if ( name == "C_INTRO" ) {
        intro.playMain( "C_AV_01", false );
        index = 0;
        intro.playBackground( "C_AVATAR" );
        intro.getUi().gotoLabel( "/", "avatar", true );
        state = eState::Choice;
        return;
    }
    if ( name.find( "C_FIESTA_INTRO" ) != std::string::npos ) {
        startWalkMode();
        return;
    }
    if ( name == "C_TESS" ) {
        // FUN_0094fdb0, cursor visible, pop_goal 5108, TUTO_GO_TO_THE_GARAGE.
        intro.getUi().setClipVisible( "/cursor", true );
        intro.stopMain();
        walk.resume( intro );
        intro.showGoal( LocString( kLocGoToGarage ) );
        state = eState::Walk;
        return;
    }
    if ( index >= 0 ) {
        char prefix[16];
        snprintf( prefix, sizeof( prefix ), "C_%dTO", index + 1 );
        int from = 0;
        int to = 0;
        if ( name.find( prefix ) != std::string::npos && sscanf( name.c_str(), "C_%dTO%d", &from, &to ) == 2 ) {
            index = to - 1;
            char next[16];
            snprintf( next, sizeof( next ), "C_AV_%02d", to );
            intro.playMain( next, false );
        }
    }
}

void GMAvatarChoice::onCommand( const std::string &command ) {
    // FUN_00da91c0.
    OTDU_LOG_DEBUG( "AVATAR_CHOICE: FSCommand %s\n", command.c_str() );
    char name[16];
    if ( command == "nxav" ) {
        if ( index < 0 || index > 4 ) {
            return;
        }
        snprintf( name, sizeof( name ), "C_%dTO%d", index + 1, ( index + 1 ) % 6 + 1 );
        intro.playMain( name, true );
    } else if ( command == "prav" ) {
        if ( index < 1 ) {
            return;
        }
        snprintf( name, sizeof( name ), "C_%dTO%d", index + 1, ( index - 1 ) % 6 + 1 );
        intro.playMain( name, true );
    } else if ( command == "crav" ) {
        intro.playEmitter( "S_TimeStart" );
        selected = index;
        validate();
        index = -1;
    } else if ( command == "skip" ) {
        skipTimer = -1.0f;
    } else if ( command == "gain" ) {
        // Story step 1 "First Garage" (FIRST_WM, garage.bnk).
        OTDU_LOG_INFO( "AVATAR_CHOICE: garage selected (story step 1)\n" );
        state = eState::Leaving;
        GetPlayerProfile().StoryStep = 1;
        pGameInstance->setNextGameMode( eGameMode::GM_FirstWalkMode );
    }
}

void GMAvatarChoice::validate() {
    // FUN_00da8d90.
    intro.stopBackground();
    intro.playBackground( "C_WALKMODE" );
    FlashPlayer &ui = intro.getUi();
    const std::string sex = ui.getVariable( "/:WhatSex" );
    const std::string ethnicGroup = ui.getVariable( "/:WhatEthnicGrp" );
    const bool bFemale = !sex.empty() && sex[0] == 'f';
    const int32_t ethnic = ( ethnicGroup.size() > 1 && ethnicGroup[1] == 'a' )   ? 0
                           : ( ethnicGroup.size() > 1 && ethnicGroup[1] == 's' ) ? 1
                                                                                 : 2;
    static const char *kEthnic[3] = { "", "_Asia", "_Afrik" };
    const std::string config = std::string( "AVT_" ) + ( bFemale ? "Female" : "Male" ) + kEthnic[ethnic] + "_Intro";
    OTDU_LOG_INFO( "AVATAR_CHOICE: avatar %d validated (%s, %s) -> %s\n", selected + 1, sex.c_str(),
                   ethnicGroup.c_str(), config.c_str() );
    intro.setPlayerAvatar( config );
    PlayerProfile &profile = GetPlayerProfile();
    profile.AvatarConfig = config;
    profile.Name = PlayerName();
    ui.setVariable( "/:PlayerName", profile.Name );
    char name[32];
    snprintf( name, sizeof( name ), "C_FIESTA_INTRO_%02d", selected + 1 );
    intro.playMain( name, true );
    state = eState::Fiesta;
}

void GMAvatarChoice::startWalkMode() {
    // FUN_00da8040 end of C_FIESTA_INTRO_xx: walk camera from
    // cam_entree_%02d (selected + 1, Fov 1.0472), INTRO "wm_home".
    char name[32];
    snprintf( name, sizeof( name ), "cam_entree_%02d", Max( selected, 0 ) + 1 );
    intro.stopMain();
    walk.start( intro, name, 1.0472f );
    intro.getUi().gotoLabel( "/", "wm_home", true );
    if ( FlashPlayer::Clip *pAvert = intro.getUi().findClip( "/pop_avert" ) ) {
        OTDU_LOG_DEBUG( "AVATAR_CHOICE: pop_avert frame %u playing %d IsCasinoSaveData '%s'\n", pAvert->CurrentFrame,
                        pAvert->bPlaying ? 1 : 0, intro.getUi().getVariable( "/:IsCasinoSaveData" ).c_str() );
    }
    // "Talk to Tess" (loc 5033): the code that shows it is encrypted [ENC];
    // shown when the walk mode starts [INF].
    intro.showGoal( LocString( kLocTalkToTess ) );
    state = eState::Walk;
}

void GMAvatarChoice::updateWalkMode( float deltaTime ) {
    walk.update( intro, deltaTime, true );
    // FUN_00da87f0: C_TESS when the camera enters the Tess volume.
    if ( !bTessStarted ) {
        const IntroCinematic::Volume *pTess = intro.findVolume( "Tess" );
        // Test shortcut -tess: C_TESS at once (facial animation tests).
        if ( pTess != nullptr && ( pTess->contains( walk.getPosition() ) || CmdLine::HasArg( "-tess" ) ) ) {
            intro.closeGoal();
            walk.clearSelection( intro );
            intro.setCameraOverride( nullptr );
            intro.playMain( "C_TESS", true );
            bTessStarted = true;
            intro.getUi().setClipVisible( "/cursor", false );
            state = eState::Tess;
            return;
        }
    }
    // A (cursor A()): validate the selected item (garage -> camn, wmof,
    // gain).
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) && walk.getSelected() >= 0 ) {
        if ( !intro.getUi().invoke( "/cursor", "A", {} ) ) {
            intro.getUi().invoke( "/", "ItemType", { "valid" } );
        }
    }
}

void GMAvatarChoice::tick( float deltaTime ) {
    if ( !bIntroLoaded ) {
        pGameInstance->setNextGameMode( eGameMode::GM_Login );
        return;
    }
    if ( state == eState::Leaving ) {
        return;
    }
    // Input -> INTRO movie (button 21 key codes: 1 left, 2 right, 13 enter).
    FlashPlayer &ui = intro.getUi();
    if ( state == eState::Choice ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuLeft ) ) {
            ui.sendKey( 1 );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuRight ) ) {
            ui.sendKey( 2 );
        }
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            ui.sendKey( 13 );
        }
        if ( gpInput->wasMouseClicked() ) {
            ui.sendMouseRelease( gpInput->getMouseX(), gpInput->getMouseY() );
        }
    }
    // C_INTRO skip (FUN_00da7e70): prompt at 25 s, skippable from then.
    if ( intro.getMainName() == "C_INTRO" && skipTimer >= 0.0f ) {
        skipTimer = intro.getMainTime();
        if ( skipTimer >= 25.0f &&
             ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) ) {
            intro.skipMain();
            skipTimer = -1.0f;
        }
    }

    intro.tick( deltaTime );
    for ( const std::string &name : intro.takeEndEvents() ) {
        onCinematicEnd( name );
    }
    std::vector<std::string> pending;
    pending.swap( commands );
    for ( const std::string &command : pending ) {
        onCommand( command );
    }
    // FUN_00da7d30: /:IsTransition (0 while a C_AV_xx loop is current).
    if ( index >= 0 && !intro.getMainName().empty() ) {
        ui.setVariableNumber( "/:IsTransition", intro.getMainName().find( "C_AV_" ) != std::string::npos ? 0.0 : 1.0 );
    }
    if ( state == eState::Walk || state == eState::Tess ) {
        if ( state == eState::Walk ) {
            updateWalkMode( deltaTime );
        }
    }
}

void GMAvatarChoice::drawOffscreen() {
    if ( bIntroLoaded ) {
        intro.renderOffscreen();
    }
}

void GMAvatarChoice::draw() {
    if ( bIntroLoaded ) {
        intro.draw();
    }
}
