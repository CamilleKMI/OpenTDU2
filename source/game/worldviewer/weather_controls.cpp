#include "shared.h"
#include "weather_controls.h"

#include "filesystem/gs_file.h"
#include "render/eden/eden_scene_view.h"
#include "system/cmd_line.h"

#include "imgui.h"

namespace {
int32_t ParseIntempery( const char *pValue, int32_t defaultValue, const char *pLogTag ) {
    if ( pValue == nullptr ) {
        return defaultValue;
    }
    for ( int32_t i = 0; i < WeatherSystem::kNumIntemperies; i++ ) {
        if ( StringCmpI( pValue, WeatherSystem::GetIntemperyName( i ) ) == 0 ) {
            return i;
        }
    }
    if ( pValue[0] >= '0' && pValue[0] <= '9' ) {
        return Clamp( atoi( pValue ), 0, WeatherSystem::kNumIntemperies - 1 );
    }
    OTDU_LOG_WARN( "%s: unknown intempery '%s' (Sunny, Cloudy, Rainny, Before_rain, After_rain, Tempest)\n", pLogTag,
                   pValue );
    return defaultValue;
}

void FormatTime( float seconds, char *pBuffer, size_t size ) {
    const int32_t total = static_cast<int32_t>( seconds );
    snprintf( pBuffer, size, "%02d:%02d:%02d", total / 3600, ( total / 60 ) % 60, total % 60 );
}
} // namespace

void WeatherControls::load( const std::string &island ) {
    const std::string fxFolder = gpFile->getBankFolder() + "islands/" + island + "/fx/";
    weatherSystem.setOverride( nullptr );
    weatherSystem.setLibrary( nullptr );
    pWeather = nullptr;
    gameWeathers = WeatherLibrary();
    gameWeathers.load( ( fxFolder + "game_weathers.xmb" ).c_str() );
    specificWeathers = WeatherLibrary();
    specificWeathers.load( ( fxFolder + "specific_weathers.xmb" ).c_str() );
    weatherSystem.setLibrary( &gameWeathers );
}

std::vector<const WeatherPreset *> WeatherControls::getPresetList( EdenSceneView &view ) const {
    std::vector<const WeatherPreset *> list;
    for ( const WeatherPreset &preset : specificWeathers.getPresets() ) {
        list.push_back( &preset );
    }
    for ( const WeatherPreset &preset : view.getWeathers().getPresets() ) {
        list.push_back( &preset );
    }
    return list;
}

void WeatherControls::applyArguments( EdenSceneView &view, const char *pLogTag ) {
    if ( bArgumentsApplied ) {
        weatherSystem.setOverride( nullptr );
        selectedPreset = -1;
        weatherSystem.evaluate();
        return;
    }
    bArgumentsApplied = true;
    bRain = CmdLine::HasArg( "-rain" ); // RAIN (rain engineer): checkbox initial state (default off)
    float seconds = 14.0f * 3600.0f;
    if ( const char *pTime = CmdLine::GetArgValue( "-time" ) ) {
        int32_t h = 0, m = 0, sec = 0;
        if ( sscanf( pTime, "%d:%d:%d", &h, &m, &sec ) >= 1 ) {
            seconds = static_cast<float>( h * 3600 + m * 60 + sec );
        }
    }
    if ( const char *pSpeed = CmdLine::GetArgValue( "-timespeed" ) ) {
        timeSpeed = static_cast<float>( atof( pSpeed ) );
    }
    const int32_t intempery = ParseIntempery( CmdLine::GetArgValue( "-intempery" ), 0, pLogTag );
    const int32_t next = ParseIntempery( CmdLine::GetArgValue( "-nextintempery" ), intempery, pLogTag );
    weatherSystem.setIntemperies( intempery, next );
    if ( const char *pWeatherName = CmdLine::GetArgValue( "-weather" ) ) {
        if ( const WeatherPreset *pKey = gameWeathers.find( pWeatherName ) ) {
            seconds = static_cast<float>( pKey->Hour * 3600 + pKey->Minutes * 60 );
        } else {
            const std::vector<const WeatherPreset *> list = getPresetList( view );
            for ( size_t i = 0; i < list.size(); i++ ) {
                if ( StringCmpI( list[i]->Name.c_str(), pWeatherName ) == 0 ) {
                    selectedPreset = static_cast<int32_t>( i );
                    weatherSystem.setOverride( list[i] );
                    break;
                }
            }
            if ( selectedPreset < 0 ) {
                OTDU_LOG_WARN( "%s: weather '%s' not found\n", pLogTag, pWeatherName );
            }
        }
    }
    weatherSystem.setTimeOfDay( seconds );
    weatherSystem.evaluate();
}

const WeatherPreset *WeatherControls::getInitialWeather( EdenSceneView &view ) {
    pWeather = weatherSystem.isReady() || weatherSystem.getOverride() != nullptr ? &weatherSystem.getCurrent() : nullptr;
    if ( pWeather == nullptr ) {
        // No game weather: the first cinematic weather of the island.
        pWeather = !view.getWeathers().getPresets().empty() ? &view.getWeathers().getPresets().front() : nullptr;
    }
    return pWeather;
}

const WeatherPreset *WeatherControls::tick( float deltaTime, EdenSceneView &view ) {
    weatherSystem.setTimeSpeed( timeSpeed );
    const float gameDelta = weatherSystem.tick( deltaTime );
    if ( weatherSystem.isReady() || weatherSystem.getOverride() != nullptr ) {
        pWeather = &weatherSystem.getCurrent();
        if ( weatherSystem.getOverride() == nullptr ) {
            view.setCloudLayers( weatherSystem.getSourceClouds(), weatherSystem.getDestinationClouds(),
                                 weatherSystem.getForecastBlend() );
        } else {
            const std::string name = pWeather->getString( "clouds_cloud3dname" );
            view.setCloudLayers( name, name, 0.0f );
        }
    }
    view.setGameTimeDelta( gameDelta );
    view.setWeather( pWeather );
    view.setRainEnabled( bRain );
    return pWeather;
}

void WeatherControls::drawPanel( EdenSceneView &view ) {
    ImGui::Separator();
    ImGui::TextUnformatted( "Time of day / weather (GSWeather)" );
    if ( !weatherSystem.isReady() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.3f, 1.0f ), "game_weathers.xmb not loaded" );
        return;
    }
    const bool bFixed = weatherSystem.getOverride() != nullptr;
    char label[32];
    FormatTime( weatherSystem.getTimeOfDay(), label, sizeof( label ) );
    // hh:mm slider (minutes of the day).
    int32_t minutes = static_cast<int32_t>( weatherSystem.getTimeOfDay() / 60.0f );
    if ( ImGui::SliderInt( "time", &minutes, 0, 24 * 60 - 1, label ) ) {
        weatherSystem.setTimeOfDay( static_cast<float>( minutes ) * 60.0f );
        weatherSystem.evaluate();
    }
    const struct {
        const char *pLabel;
        float Hours;
    } kTimes[] = { { "00:00", 0.0f }, { "05:30", 5.5f }, { "08:00", 8.0f }, { "12:00", 12.0f },
                   { "14:00", 14.0f }, { "19:30", 19.5f }, { "21:30", 21.5f } };
    for ( size_t i = 0; i < sizeof( kTimes ) / sizeof( kTimes[0] ); i++ ) {
        if ( i != 0 ) {
            ImGui::SameLine();
        }
        if ( ImGui::SmallButton( kTimes[i].pLabel ) ) {
            weatherSystem.setTimeOfDay( kTimes[i].Hours * 3600.0f );
            weatherSystem.evaluate();
        }
    }
    // Clock speed: game seconds per real second (game: 9.6, FUN_00afb1e0).
    ImGui::SliderFloat( "time speed", &timeSpeed, 0.0f, 2000.0f, timeSpeed == 0.0f ? "frozen" : "x%.1f",
                        ImGuiSliderFlags_Logarithmic );
    const struct {
        const char *pLabel;
        float Speed;
    } kSpeeds[] = { { "stop", 0.0f }, { "x1", 1.0f }, { "game x9.6", WeatherSystem::kGameTimeSpeed },
                    { "x60", 60.0f }, { "x480", 480.0f } };
    for ( size_t i = 0; i < sizeof( kSpeeds ) / sizeof( kSpeeds[0] ); i++ ) {
        if ( i != 0 ) {
            ImGui::SameLine();
        }
        if ( ImGui::SmallButton( kSpeeds[i].pLabel ) ) {
            timeSpeed = kSpeeds[i].Speed;
        }
    }
    // Weather types (intemperies, exe names) of the current and next 2 h
    // forecast slices.
    int32_t source = weatherSystem.getIntempery();
    int32_t destination = weatherSystem.getNextIntempery();
    bool bChanged = false;
    if ( ImGui::BeginCombo( "weather", WeatherSystem::GetIntemperyName( source ) ) ) {
        for ( int32_t i = 0; i < WeatherSystem::kNumIntemperies; i++ ) {
            if ( ImGui::Selectable( WeatherSystem::GetIntemperyName( i ), i == source ) ) {
                // Picking a weather sets both slices (steady weather).
                bChanged |= source != i || destination != i;
                source = destination = i;
            }
        }
        ImGui::EndCombo();
    }
    if ( ImGui::BeginCombo( "next slice", WeatherSystem::GetIntemperyName( destination ) ) ) {
        for ( int32_t i = 0; i < WeatherSystem::kNumIntemperies; i++ ) {
            if ( ImGui::Selectable( WeatherSystem::GetIntemperyName( i ), i == destination ) ) {
                bChanged |= destination != i;
                destination = i;
            }
        }
        ImGui::EndCombo();
    }
    if ( bChanged ) {
        weatherSystem.setIntemperies( source, destination );
        weatherSystem.evaluate();
    }
    // Fixed presets (specific / cinematic weathers) in place of the clock.
    const std::vector<const WeatherPreset *> presets = getPresetList( view );
    const char *pPresetLabel =
        bFixed && selectedPreset >= 0 && selectedPreset < static_cast<int32_t>( presets.size() )
            ? presets[selectedPreset]->Name.c_str()
            : "(time of day)";
    if ( ImGui::BeginCombo( "preset", pPresetLabel ) ) {
        if ( ImGui::Selectable( "(time of day)", !bFixed ) ) {
            selectedPreset = -1;
            weatherSystem.setOverride( nullptr );
        }
        const size_t numSpecific = specificWeathers.getPresets().size();
        for ( size_t i = 0; i < presets.size(); i++ ) {
            char item[96];
            snprintf( item, sizeof( item ), "%s: %s##%zu", i < numSpecific ? "specific" : "cinematic",
                      presets[i]->Name.c_str(), i );
            if ( ImGui::Selectable( item, selectedPreset == static_cast<int32_t>( i ) ) ) {
                selectedPreset = static_cast<int32_t>( i );
                weatherSystem.setOverride( presets[i] );
            }
        }
        ImGui::EndCombo();
    }

    // RAIN (rain engineer): rain rendering on / off (default off). The
    // checkbox only calls EdenSceneView::setRainEnabled (in tick()).
    ImGui::Checkbox( "Rain", &bRain );

    if ( !bFixed && weatherSystem.getKeyA() != nullptr ) {
        ImGui::TextDisabled( "keys %s -> %s, t %.3f", weatherSystem.getKeyA()->Name.c_str(),
                             weatherSystem.getKeyB()->Name.c_str(), weatherSystem.getKeyBlend() );
        ImGui::TextDisabled( "forecast %s -> %s, f %.3f", WeatherSystem::GetIntemperyName( source ),
                             WeatherSystem::GetIntemperyName( destination ), weatherSystem.getForecastBlend() );
        ImGui::TextDisabled( "clouds %s -> %s", weatherSystem.getSourceClouds().c_str(),
                             weatherSystem.getDestinationClouds().c_str() );
    }
    if ( pWeather != nullptr ) {
        const Vector3f sun = pWeather->getVector3( "sundirection", Vector3f( 0.0f, -1.0f, 0.0f ) );
        ImGui::TextDisabled( "sundirection (%.2f %.2f %.2f), haze %.2f", sun.x, sun.y, sun.z,
                             pWeather->getFloat( "Skydome_haze" ) );
        ImGui::TextDisabled( "neon %.2f, rain_alpha %.0f, moon %.2f", pWeather->getFloat( "building_neon_strength" ),
                             pWeather->getFloat( "rain_alpha" ), pWeather->getFloat( "Skydome_moonPercent" ) );
    }
}
