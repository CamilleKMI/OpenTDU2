#include "shared.h"
#include "eden_weather.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"

Vector4f WeatherPreset::get( const char *pName, const Vector4f &defaultValue ) const {
    auto it = Values.find( pName );
    return it != Values.end() ? it->second : defaultValue;
}

float WeatherPreset::getFloat( const char *pName, float defaultValue ) const {
    auto it = Values.find( pName );
    return it != Values.end() ? it->second.x : defaultValue;
}

Vector3f WeatherPreset::getVector3( const char *pName, const Vector3f &defaultValue ) const {
    auto it = Values.find( pName );
    return it != Values.end() ? it->second.xyz() : defaultValue;
}

Vector4f WeatherPreset::getVector4( const char *pName, const Vector4f &defaultValue ) const {
    auto it = Values.find( pName );
    return it != Values.end() ? it->second : defaultValue;
}

bool WeatherLibrary::load( const char *pPath ) {
    presets.clear();
    std::vector<uint8_t> buffer;
    if ( !gpFile->loadFile( pPath, buffer ) ) {
        OTDU_LOG_WARN( "Weather library '%s' not found\n", pPath );
        return false;
    }
    XmbDocument document;
    if ( !document.load( buffer ) ) {
        return false;
    }
    const int32_t typeFloat[5] = { -1, document.findType( "float1" ), document.findType( "float2" ),
                                   document.findType( "float3" ), document.findType( "float4" ) };
    const int32_t typeString = document.findType( "STRING_PARAM" );

    const uint32_t numWeathers = document.readU32( 0 );
    const uint32_t weathersOffset = document.readU32( 4 );
    for ( uint32_t i = 0; i < numWeathers; i++ ) {
        const uint32_t weather = weathersOffset + i * 24;
        WeatherPreset preset;
        preset.Name = document.readString( weather );
        preset.Hour = static_cast<int32_t>( document.readU32( weather + 4 ) );
        preset.Minutes = static_cast<int32_t>( document.readU32( weather + 8 ) );
        const uint32_t numIntemperies = document.readU32( weather + 16 );
        const uint32_t intemperiesOffset = document.readU32( weather + 20 );
        // Intempery 0: base (clear) values, the following ones override them.
        for ( uint32_t j = 0; j < numIntemperies && j < 2; j++ ) {
            const uint32_t numParams = document.readU32( intemperiesOffset + j * 8 );
            const uint32_t paramsOffset = document.readU32( intemperiesOffset + j * 8 + 4 );
            for ( uint32_t k = 0; k < numParams; k++ ) {
                const uint32_t param = paramsOffset + k * 12;
                const std::string name = document.readString( param );
                const int32_t typeIndex = static_cast<int32_t>( document.readU32( param + 4 ) / 2 );
                const uint32_t valueOffset = document.readU32( param + 8 );
                bool bFloat = false;
                Vector4f value;
                for ( int32_t c = 1; c <= 4; c++ ) {
                    if ( typeIndex == typeFloat[c] ) {
                        float components[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        for ( int32_t n = 0; n < c; n++ ) {
                            const uint32_t bits = document.readU32( valueOffset + n * 4 );
                            memcpy( &components[n], &bits, sizeof( float ) );
                        }
                        value = Vector4f( components[0], components[1], components[2], components[3] );
                        bFloat = true;
                    }
                }
                if ( bFloat ) {
                    preset.Values[name] = value;
                } else if ( typeIndex == typeString ) {
                    preset.Strings[name] = document.readString( valueOffset );
                }
            }
        }
        presets.push_back( std::move( preset ) );
    }
    OTDU_LOG_DEBUG( "Weather library '%s': %zu presets\n", pPath, presets.size() );
    return !presets.empty();
}

const WeatherPreset *WeatherLibrary::find( const char *pName ) const {
    for ( const WeatherPreset &preset : presets ) {
        if ( StringCmpI( preset.Name.c_str(), pName ) == 0 ) {
            return &preset;
        }
    }
    return nullptr;
}

bool WeatherLibrary::loadModifiers( const char *pPath ) {
    modifiers.clear();
    modifiedPresets.clear();
    std::vector<uint8_t> buffer;
    if ( !gpFile->loadFile( pPath, buffer ) ) {
        OTDU_LOG_WARN( "Weather modifiers '%s' not found\n", pPath );
        return false;
    }
    XmbDocument document;
    if ( !document.load( buffer ) ) {
        return false;
    }
    XmbValue root = document.getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "modifier" ); i++ ) {
        XmbValue entry = root.getArrayElement( "modifier", i );
        Modifier modifier;
        modifier.Name = entry["name"].asString();
        modifier.WeatherName = entry["weather_name"].asString();
        modifier.IntemperyIndex = static_cast<int32_t>( entry["intempery_index"].asInt( -1 ) );
        for ( uint32_t j = 0; j < entry.getArraySize( "param" ); j++ ) {
            XmbValue param = entry.getArrayElement( "param", j );
            const std::string name = param["name"].asString();
            XmbValue value = param["param"];
            const XmbType *pType = value.isValid() ? value.getType() : nullptr;
            if ( pType == nullptr ) {
                continue;
            }
            if ( pType->Name.size() == 6 && pType->Name.compare( 0, 5, "float" ) == 0 ) {
                const int32_t numComponents = pType->Name[5] - '0';
                const char *pComponents[4] = { "x", "y", "z", "w" };
                float components[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for ( int32_t c = 0; c < numComponents && c < 4; c++ ) {
                    components[c] = value[pComponents[c]].asFloat();
                }
                modifier.Values[name] = Vector4f( components[0], components[1], components[2], components[3] );
            } else if ( pType->Name == "STRING_PARAM" && !pType->Fields.empty() ) {
                modifier.Strings[name] = value[pType->Fields[0].Name.c_str()].asString();
            }
        }
        modifiers.push_back( std::move( modifier ) );
    }
    OTDU_LOG_DEBUG( "Weather modifiers '%s': %zu\n", pPath, modifiers.size() );
    return !modifiers.empty();
}

const WeatherPreset *WeatherLibrary::findModified( const char *pWeather, const char *pModifier ) {
    const WeatherPreset *pBase = find( pWeather );
    if ( pBase == nullptr || pModifier == nullptr || pModifier[0] == '\0' ) {
        return pBase;
    }
    const std::string key = std::string( pWeather ) + "|" + pModifier;
    auto it = modifiedPresets.find( key );
    if ( it != modifiedPresets.end() ) {
        return it->second.get();
    }
    auto pPreset = std::make_unique<WeatherPreset>( *pBase );
    bool bFound = false;
    // Global parameters (intempery -1), then the clear intempery (0).
    for ( int32_t intempery : { -1, 0 } ) {
        for ( const Modifier &modifier : modifiers ) {
            if ( modifier.IntemperyIndex != intempery || StringCmpI( modifier.Name.c_str(), pModifier ) != 0 ||
                 StringCmpI( modifier.WeatherName.c_str(), pWeather ) != 0 ) {
                continue;
            }
            for ( const auto &value : modifier.Values ) {
                pPreset->Values[value.first] = value.second;
            }
            for ( const auto &value : modifier.Strings ) {
                pPreset->Strings[value.first] = value.second;
            }
            bFound = true;
        }
    }
    if ( !bFound ) {
        OTDU_LOG_WARN( "Weather modifier '%s' not found for '%s'\n", pModifier, pWeather );
    }
    WeatherPreset *pResult = pPreset.get();
    modifiedPresets.emplace( key, std::move( pPreset ) );
    return pResult;
}
