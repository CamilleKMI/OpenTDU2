#include "shared.h"
#include "gm_login.h"

#include <SDL3/SDL_scancode.h>

#include "database/gs_database.h"
#include "audio/gs_audio.h"
#include "flash/gs_flash.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"

namespace {
// Localized strings (db_<lang>.cpr line ids, DBLoc _flash variables).
constexpr int32_t kLocPressStart = 938;          // m_PressStart
constexpr int32_t kLocNATMessage = 7963;         // m_NAT_msg_
constexpr int32_t kLocNoProgressionData = 7097;  // savegame popup text (FUN_00ac4c80, status 1, load)
constexpr int32_t kLocOk = 219;                  // m_Ok
constexpr int32_t kLocRetry = 1839;              // m_Retry_min
constexpr const char *kGameName = "TDU2";        // DAT_00f18f8c (§GAME_NAME§)

std::string ReplaceAll( std::string text, const std::string &from, const std::string &to ) {
    size_t position = 0;
    while ( ( position = text.find( from, position ) ) != std::string::npos ) {
        text.replace( position, from.size(), to );
        position += to.size();
    }
    return text;
}
} // namespace

GMLogin::GMLogin() : GameMode( eGameMode::GM_Login ) {}

GMLogin::~GMLogin() {}

void GMLogin::loadTitleScene( int32_t cinematicIndex ) {
    titleScene.unload();
    if ( !CmdLine::HasArg( "-notitlescene" ) ) {
        titleScene.load( island.c_str(), cinematicIndex );
    }
}

bool GMLogin::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    if ( gpAudio != nullptr ) {
        gpAudio->setGameMode( "LOGIN" );
    }

    // Press start cinematic (FUN_009c6b20): random, FORCELOGINCINEINDEX n
    // (-cine n: alias).
    int32_t cinematicIndex = -1;
    if ( CmdLine::HasArg( "-FORCELOGINCINEINDEX" ) ) {
        cinematicIndex = CmdLine::GetArgInt( "-FORCELOGINCINEINDEX", -1 );
    } else if ( CmdLine::HasArg( "-cine" ) ) {
        cinematicIndex = CmdLine::GetArgInt( "-cine", -1 );
    }
    loadTitleScene( cinematicIndex );

    // FUN_009c7ea0: LOG_IN2 (/:WhatGameMode = 0, play), GENERAL
    // (/:WhatGameMode = 0); NAT message iff the registry NetworkNatType is
    // not "Open" (no registry here: Open).
    if ( login2.load( gpFlash->getMovie( "log_in2" ) ) ) {
        login2.setFSCommandCallback( [this]( const std::string &command, const std::string & ) {
            if ( command == "stok" ) {
                bStok = true;
            }
        } );
        login2.setVariableNumber( "/:WhatGameMode", 0.0 );
        login2.setVariable( "/:m_PressStart", LocStr( kLocPressStart, "Press START to play" ) );
        login2.setVariable( "/:m_NAT_msg_", LocStr( kLocNATMessage, "" ) );
        login2.setVariableBool( "/:NAT_msg", false );
    }
    // FUN_00d8e200 -> FUN_00d8d930 (GENERAL, MAIN): /:WhatPlatform = 1 (PC;
    // the button glyph sprites pick their "pc" frame from it).
    if ( general.load( gpFlash->getMovie( "general" ) ) ) {
        general.setVariableNumber( "/:WhatGameMode", 0.0 );
        general.setVariableNumber( "/:WhatPlatform", 1.0 );
    }
    setState( LS_PressStart );
    return true;
}

void GMLogin::exit() {
    titleScene.unload();
    login2.unload();
    general.unload();
}

void GMLogin::setState( eState newState ) {
    state = newState;
    subState = 0;
}

void GMLogin::showNoDataPopup() {
    // FUN_00ac4c80 -> FUN_00d86c00: GENERAL "avert", buttons type 3 (A = OK,
    // B = Retry), label "no_data", text with §GAME_NAME§ = "TDU2".
    std::string text = ReplaceAll( LocStr( kLocNoProgressionData, "" ), "\xc2\xa7GAME_NAME\xc2\xa7", kGameName );
    text = ReplaceAll( text, "\xa7GAME_NAME\xa7", kGameName );
    general.setVariable( "/:m_AvertText", text );
    general.setVariable( "/:AvStateButtonA", LocStr( kLocOk, "OK" ) );
    general.setVariable( "/:AvStateButtonB", LocStr( kLocRetry, "Retry" ) );
    general.gotoLabel( "/avert", "no_data", true );
    bPopupShown = true;
}

bool GMLogin::isPopupClosing() {
    // avert "close_no_data" (f61..f71) ends with gotoLabel "default" (f0).
    FlashPlayer::Clip *pAvert = general.findClip( "/avert" );
    return pAvert != nullptr && pAvert->CurrentFrame >= 61;
}

void GMLogin::tick( float deltaTime ) {
    timerMs += static_cast<int32_t>( std::lround( deltaTime * 1000.0f ) );
    titleScene.tick( deltaTime );
    login2.tick( deltaTime );
    general.tick( deltaTime );

    switch ( state ) {
    case LS_PressStart:
        if ( subState == 0 ) {
            subState = 1; // no loading screen video
        }
        if ( subState == 1 && timerMs > 3000 ) {
            subState = 2;
        }
        if ( subState <= 2 && gpInput->wasKeyPressed( SDL_SCANCODE_R ) && titleScene.getNumCinematics() > 0 ) {
            // OpenTDU2 key: next press start cinematic.
            loadTitleScene( ( titleScene.getCinematicIndex() + 1 ) % titleScene.getNumCinematics() );
        }
        if ( subState == 2 && gpInput->wasStartReleased() ) {
            login2.gotoLabel( "/", "next", true );
            bStok = false;
            subState = 3;
        }
        if ( subState == 3 && bStok ) {
            // States 2 -> 4 -> 6 (offline, profile present).
            setState( LS_SaveCheck );
        }
        break;
    case LS_SaveCheck:
        // The save backend (edFileSaveGamePC) is encrypted in the
        // executable: no progression data is ever found here. A new popup
        // waits for the previous one to be closed.
        if ( !isPopupClosing() ) {
            showNoDataPopup();
            setState( LS_SaveResult );
        }
        break;
    case LS_SaveResult:
        // GENERAL avert buttons: key catcher ch481 (Enter: "avye" = OK,
        // Backspace: "avno" = Retry), then "close_no_data"; listener
        // FUN_009c7010: OK = new game, Retry = check again.
        if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            general.gotoLabel( "/avert", "close_no_data", true );
            setState( LS_NextMode );
        } else if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            general.gotoLabel( "/avert", "close_no_data", true );
            setState( LS_SaveCheck ); // next = prev: the check runs again
        }
        break;
    case LS_NextMode:
        // FUN_009c9180: SCENARIOTUTORIAL = ON and no progression loaded:
        // AVATAR_CHOICE.
        nextMode = eGameMode::GM_AvatarChoice;
        setState( LS_Leave );
        break;
    case LS_Leave:
        if ( subState == 0 ) {
            pGameInstance->setNextGameMode( nextMode );
            subState = 1;
        }
        break;
    }
}

void GMLogin::drawOffscreen() { titleScene.renderOffscreen(); }

void GMLogin::draw() {
    Renderer2D &renderer = gpRender->get2DRenderer();
    titleScene.draw();
    static const bool bNoTitleFlash = CmdLine::HasArg( "-notitleflash" );
    if ( !bNoTitleFlash && login2.isLoaded() ) {
        login2.draw( renderer );
    }
    if ( bPopupShown && general.isLoaded() ) {
        general.drawClipOnly( renderer, "/avert" );
    }
}

void GMLogin::drawDebug() {
    if ( ImGui::Begin( "LOGIN" ) ) {
        ImGui::Text( "state %d sub %d timer %d ms", static_cast<int32_t>( state ), subState, timerMs );
        ImGui::Text( "cinematic %d / %d", titleScene.getCinematicIndex(), titleScene.getNumCinematics() );
    }
    ImGui::End();
}
