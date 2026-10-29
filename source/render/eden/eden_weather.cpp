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
        // Intempery 0: global block, 1...: weather types. The merged view
        // (Values / Strings) holds the global block and the first type
        // (Sunny for game_weathers.xmb).
        if ( numIntemperies > 1 ) {
            preset.Intemperies.resize( numIntemperies - 1 );
        }
        for ( uint32_t j = 0; j < numIntemperies; j++ ) {
            WeatherPreset::Block &block = j == 0 ? preset.Global : preset.Intemperies[j - 1];
            const bool bMerged = j < 2;
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
                    block.Values[name] = value;
                    if ( bMerged ) {
                        preset.Values[name] = value;
                    }
                } else if ( typeIndex == typeString ) {
                    block.Strings[name] = document.readString( valueOffset );
                    if ( bMerged ) {
                        preset.Strings[name] = block.Strings[name];
                    }
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

// ---------------------------------------------------------------------------
// WeatherSystem

namespace {
constexpr float kHourSeconds = 3600.0f; // 0x00f49b30
constexpr float kMinuteSeconds = 60.0f; // 0x00f49b28
// Exe table 0x00f7e08c (followed by "Max_intempery").
const char *const kIntemperyNames[WeatherSystem::kNumIntemperies] = { "Sunny",      "Cloudy",     "Rainny",
                                                                       "Before_rain", "After_rain", "Tempest" };

float KeySeconds( const WeatherPreset &preset ) {
    return static_cast<float>( preset.Hour ) * kHourSeconds + static_cast<float>( preset.Minutes ) * kMinuteSeconds;
}

// FUN_00af1ac0 per parameter type (+0x28 components, +0x2c mode): floats
// linear, mode 10 (photoshop_crappymode_enable, registry FUN_00af30d0)
// lerp by floor(t + 0.5), strings (type 0, default case) B when t > 0.5
// (0x00edd4a0). Parameters present on one side only keep that value.
void BlendBlocks( const WeatherPreset::Block &a, const WeatherPreset::Block &b, float t, WeatherPreset::Block &out ) {
    out.Values = a.Values;
    for ( const auto &entry : b.Values ) {
        auto it = out.Values.find( entry.first );
        if ( it == out.Values.end() ) {
            out.Values.emplace( entry.first, entry.second );
            continue;
        }
        // x87: t * B + (1 - t) * A in extended precision, one rounding to
        // float (FUN_00af1ac0 case 0).
        const long double factor =
            entry.first == "photoshop_crappymode_enable" ? std::floor( static_cast<double>( t ) + 0.5 ) : t;
        const long double inverse = 1.0L - factor;
        const Vector4f &va = it->second;
        const Vector4f &vb = entry.second;
        auto lerp = [factor, inverse]( float a, float b ) {
            return static_cast<float>( factor * b + inverse * a );
        };
        it->second = Vector4f( lerp( va.x, vb.x ), lerp( va.y, vb.y ), lerp( va.z, vb.z ), lerp( va.w, vb.w ) );
    }
    out.Strings = a.Strings;
    for ( const auto &entry : b.Strings ) {
        auto it = out.Strings.find( entry.first );
        if ( it == out.Strings.end() ) {
            out.Strings.emplace( entry.first, entry.second );
        } else if ( t > 0.5f ) {
            it->second = entry.second;
        }
    }
}
} // namespace

const char *WeatherSystem::GetIntemperyName( int32_t intempery ) {
    return intempery >= 0 && intempery < kNumIntemperies ? kIntemperyNames[intempery] : "?";
}

void WeatherSystem::setLibrary( const WeatherLibrary *pNewLibrary ) {
    pLibrary = pNewLibrary;
    pKeyA = pKeyB = nullptr;
    evaluate();
}

void WeatherSystem::setTimeOfDay( float seconds ) {
    seconds = std::fmod( seconds, kDaySeconds );
    timeOfDay = seconds < 0.0f ? seconds + kDaySeconds : seconds;
}

void WeatherSystem::setIntemperies( int32_t source, int32_t destination ) {
    sourceIntempery = Clamp( source, 0, kNumIntemperies - 1 );
    destinationIntempery = Clamp( destination, 0, kNumIntemperies - 1 );
}

float WeatherSystem::BlendFactor( float a, float b, float now ) {
    // FUN_00af7aa0: the hours are compared truncated (cvttss2si).
    const int32_t hourA = static_cast<int32_t>( a / kHourSeconds );
    const int32_t hourB = static_cast<int32_t>( b / kHourSeconds );
    const int32_t hourNow = static_cast<int32_t>( now / kHourSeconds );
    if ( hourB - hourA < 0 ) {
        b += kDaySeconds;
        if ( hourNow < hourA ) {
            now += kDaySeconds;
        }
    }
    if ( a == b ) {
        return 0.0f;
    }
    // x87 subtract / divide, stored as float (0xaf7b40 .. 0xaf7b4c).
    const float t = static_cast<float>( ( static_cast<long double>( now ) - a ) / ( static_cast<long double>( b ) - a ) );
    return t >= 0.0f && t <= 1.0f ? t : 0.0f;
}

float WeatherSystem::tick( float realDelta ) {
    // FUN_00af8ce0: time += speed * dt, wrapped at 24 h (FUN_00af7520).
    const float gameDelta = realDelta * timeSpeed;
    if ( gameDelta != 0.0f ) {
        setTimeOfDay( timeOfDay + gameDelta );
    }
    evaluate();
    return gameDelta;
}

void WeatherSystem::evaluateIntempery( int32_t intempery, WeatherPreset::Block &outBlock ) const {
    WeatherPreset::Block global;
    WeatherPreset::Block type;
    static const WeatherPreset::Block kEmpty;
    auto intemperyBlock = [intempery]( const WeatherPreset &preset ) -> const WeatherPreset::Block & {
        return intempery < static_cast<int32_t>( preset.Intemperies.size() ) ? preset.Intemperies[intempery] : kEmpty;
    };
    BlendBlocks( pKeyA->Global, pKeyB->Global, keyBlend, global );
    BlendBlocks( intemperyBlock( *pKeyA ), intemperyBlock( *pKeyB ), keyBlend, type );
    // One flat map like the consumers expect (the two registries have
    // distinct names).
    outBlock = std::move( type );
    for ( auto &entry : global.Values ) {
        outBlock.Values[entry.first] = entry.second;
    }
    for ( auto &entry : global.Strings ) {
        outBlock.Strings[entry.first] = entry.second;
    }
}

void WeatherSystem::evaluate() {
    if ( !isReady() ) {
        return;
    }
    const std::vector<WeatherPreset> &keys = pLibrary->getPresets();
    // FUN_00af7d40: the last key at or before the current time (exact match
    // first), the next one wrapping to the first.
    size_t keyA = 0;
    float best = kDaySeconds; // 0x00f15338
    for ( size_t i = 0; i < keys.size(); i++ ) {
        const float elapsed = timeOfDay - KeySeconds( keys[i] );
        if ( elapsed == 0.0f ) {
            keyA = i;
            break;
        }
        const float distance = Min( elapsed, kDaySeconds + elapsed );
        if ( distance >= 0.0f && distance <= best ) {
            keyA = i;
            best = distance;
        }
    }
    const size_t keyB = keyA + 1 < keys.size() ? keyA + 1 : 0;
    pKeyA = &keys[keyA];
    pKeyB = &keys[keyB];
    keyBlend = BlendFactor( KeySeconds( *pKeyA ), KeySeconds( *pKeyB ), timeOfDay );

    // FUN_00af8f60: source / destination forecast slices (FUN_00c1f1b0:
    // slice = hour / 2, slice start = 2 * slice hours, destination = next
    // slice).
    const int32_t slice = static_cast<int32_t>( timeOfDay / kHourSeconds ) / 2;
    const float sourceStart = static_cast<float>( slice * 2 ) * kHourSeconds;
    const float destinationStart = static_cast<float>( ( ( slice + 1 ) % 12 ) * 2 ) * kHourSeconds;
    forecastBlend = BlendFactor( sourceStart, destinationStart, timeOfDay );

    WeatherPreset::Block source;
    evaluateIntempery( sourceIntempery, source );
    sourceClouds = source.Strings["clouds_cloud3dname"];
    WeatherPreset::Block result;
    if ( destinationIntempery != sourceIntempery ) {
        WeatherPreset::Block destination;
        evaluateIntempery( destinationIntempery, destination );
        destinationClouds = destination.Strings["clouds_cloud3dname"];
        BlendBlocks( source, destination, forecastBlend, result );
    } else {
        destinationClouds = sourceClouds;
        result = std::move( source );
    }
    current.Values = std::move( result.Values );
    current.Strings = std::move( result.Strings );
    const int32_t minutes = static_cast<int32_t>( timeOfDay / kMinuteSeconds );
    current.Hour = minutes / 60;
    current.Minutes = minutes % 60;
    char name[64];
    snprintf( name, sizeof( name ), "%02d:%02d %s", current.Hour, current.Minutes, GetIntemperyName( sourceIntempery ) );
    current.Name = name;
}
