#include "shared.h"
#include "gs_config.h"

#include <SDL3/SDL.h>

#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include "imgui.h"

GSConfig *gpConfig = nullptr;

GSConfig::GSConfig() : language( "us" ) { gpConfig = this; }

GSConfig::~GSConfig() { gpConfig = nullptr; }

bool GSConfig::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );

    if ( systemConfig.loadCPR( "SystemPC.cpr" ) ) {
        video.bWindowed = systemConfig.getBool( "Video", "SetWindowed", true );
        video.Width = systemConfig.getInt( "Video", video.bWindowed ? "SetWinWidth" : "SetWidth", 1280 );
        video.Height = systemConfig.getInt( "Video", video.bWindowed ? "SetWinHeight" : "SetHeight", 720 );
        video.bVSync = systemConfig.getBool( "Video", "SetSyncToVBL", true );
        const std::string ratio = systemConfig.getString( "Video", "SetScreenRatio", "" );
        video.ScreenRatio = StringCmpI( ratio.c_str(), "RATIO_16_9" ) == 0  ? 1
                            : StringCmpI( ratio.c_str(), "RATIO_4_3" ) == 0 ? 2
                                                                             : 0;
    } else {
        OTDU_LOG_WARN( "SystemPC.cpr not found; using default video settings\n" );
    }
    if ( !gameConfig.loadCPR( "GamePC.cpr" ) ) {
        OTDU_LOG_WARN( "GamePC.cpr not found\n" );
    }
    if ( !physicsConfig.loadCPR( "Physics.cpr" ) ) {
        OTDU_LOG_WARN( "Physics.cpr not found\n" );
    }
    // FUN_00d6f770: "%s_QUALITY_LEVEL_%d" sections of Quality_Settings.cpr.
    if ( CmdLine::HasArg( "-envquality" ) ) {
        video.EnvironmentQuality = Clamp( CmdLine::GetArgInt( "-envquality", 3 ), 0, 3 );
    }
    if ( qualityConfig.loadCPR( "Quality_Settings.cpr" ) ) {
        const std::string section = "ENVIRONMENT_QUALITY_LEVEL_" + std::to_string( video.EnvironmentQuality );
        video.GlobalLodFactor = qualityConfig.getFloat( section.c_str(), "SET_GLOBAL_LOD_FACTOR", 1.0f );
        video.DefNbSectors = qualityConfig.getInt( section.c_str(), "SetDefNbSectors", 5 );
        OTDU_LOG_INFO( "Quality: environment level %d, LOD factor %.2f, %d sectors\n", video.EnvironmentQuality,
                       video.GlobalLodFactor, video.DefNbSectors );
    } else {
        OTDU_LOG_WARN( "Quality_Settings.cpr not found\n" );
    }

    // Language (Language.cpr is not encrypted in every release; keep it simple)
    const char *pLanguage = CmdLine::GetArgValue( "-lang" );
    if ( pLanguage != nullptr ) {
        language = ToLower( pLanguage );
    } else {
        // System language -> TDU2 database suffix (db_<lang>.cpr)
        static constexpr const char *kLanguages[][2] = { { "en", "us" }, { "fr", "fr" }, { "de", "ge" },
                                                         { "es", "sp" }, { "it", "it" }, { "ru", "ru" },
                                                         { "nl", "du" }, { "pl", "po" }, { "ja", "jp" } };
        int numLocales = 0;
        SDL_Locale **ppLocales = SDL_GetPreferredLocales( &numLocales );
        if ( ppLocales != nullptr && numLocales > 0 && ppLocales[0]->language != nullptr ) {
            for ( const auto &mapping : kLanguages ) {
                if ( StringCmpI( ppLocales[0]->language, mapping[0] ) == 0 ) {
                    language = mapping[1];
                    break;
                }
            }
        }
        SDL_free( ppLocales );
    }
    OTDU_LOG_INFO( "Language: %s\n", language.c_str() );

    // Command line overrides (same switches as OpenTDU)
    if ( CmdLine::HasArg( "-w" ) ) {
        video.bWindowed = true;
    }
    if ( CmdLine::HasArg( "-fullscreen" ) ) {
        video.bWindowed = false;
    }
    video.Width = CmdLine::GetArgInt( "-width", video.Width );
    video.Height = CmdLine::GetArgInt( "-height", video.Height );
    if ( CmdLine::HasArg( "-novsync" ) ) {
        video.bVSync = false;
    }
    video.Width = Max( video.Width, 640 );
    video.Height = Max( video.Height, 360 );

    OTDU_LOG_INFO( "Video: %dx%d %s vsync=%d screen ratio mode %d\n", video.Width, video.Height,
                   video.bWindowed ? "windowed" : "fullscreen", video.bVSync, video.ScreenRatio );
    return true;
}

void GSConfig::drawDebug() {
    if ( !ImGui::Begin( "Config" ) ) {
        ImGui::End();
        return;
    }
    auto drawIni = [&]( const char *pLabel, const IniFile &ini ) {
        if ( !ImGui::TreeNode( pLabel ) ) {
            return;
        }
        for ( const auto &section : ini.getSections() ) {
            if ( ImGui::TreeNode( section.first.c_str() ) ) {
                for ( const IniFile::Entry &entry : section.second ) {
                    std::string values;
                    for ( const std::string &value : entry.Values ) {
                        values += value + " ";
                    }
                    ImGui::Text( "%s %s = %s", entry.Key.c_str(), entry.Qualifier.c_str(),
                                 values.c_str() );
                }
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    };
    drawIni( "SystemPC.cpr", systemConfig );
    drawIni( "GamePC.cpr", gameConfig );
    drawIni( "Physics.cpr", physicsConfig );
    ImGui::End();
}
