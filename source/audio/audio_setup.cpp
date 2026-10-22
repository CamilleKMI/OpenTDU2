#include "shared.h"
#include "audio_setup.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"

namespace {
// exe 0x00f90ac0: u32 pointers to the client names (bfx +0xdc index).
constexpr const char *kBfxClientNames[AudioSetup::kNumBfxClients] = {
    "VoiceStoryline", "VoiceCops",         "VoiceGSM",        "VoicePassenger",    "VoiceAvatar",
    "VoiceGPS",       "VoicePlayers",      "FrontEnd",        "FrontEndMusic",     "Radio",
    "AmbienceInternal", "AmbienceInternalSfx", "AmbienceZone", "AmbienceRain",     "AmbienceExternalSfx",
    "Aircraft",       "Boat",              "AmbienceLightning", "Woosh",           "CinematicSfx",
    "AnimationSfx",   "PhysicSfx",         "Cinematic",       "CinematicMusic",    "Engine",
    "EngineFx",       "ExhaustPipe",       "Transmission",    "Aerodynamic",       "Skid",
    "Rolling",        "Misc",              "OtherVehicles" };

inline float DecibelToGain( float db ) { return db <= -80.0f ? 0.0f : std::pow( 10.0f, db / 20.0f ); }

AudioSetup::Routing MakeRouting( int32_t inputs, int32_t outputs, std::initializer_list<float> db ) {
    AudioSetup::Routing routing;
    routing.Inputs = inputs;
    routing.Outputs = outputs;
    for ( float value : db ) {
        routing.Gains.push_back( DecibelToGain( value ) );
    }
    routing.Gains.resize( static_cast<size_t>( inputs * outputs ), 0.0f );
    return routing;
}
} // namespace

const char *AudioSetup::GetBfxClientName( int32_t index ) {
    return index >= 0 && index < kNumBfxClients ? kBfxClientNames[index] : nullptr;
}

bool AudioSetup::load( const XmbDocument &document ) {
    pipelines.clear();
    clients.clear();
    mixers.clear();
    duckingPresets.clear();
    mixPresets.clear();
    gameModes.clear();
    XmbValue root = document.getRoot();
    if ( !root.isValid() ) {
        return false;
    }
    static constexpr const char *kRoutingNames[L_Count] = { "RoutingStereo", "Routing51", "Routing71" };
    for ( uint32_t i = 0; i < root.getArraySize( "Pipelines" ); i++ ) {
        XmbValue entry = root.getArrayElement( "Pipelines", i );
        Pipeline pipeline;
        pipeline.Name = entry["Name"].asString();
        pipeline.Hash = static_cast<int32_t>( entry["HashCode"].asInt() );
        for ( int l = 0; l < L_Count; l++ ) {
            XmbValue matrix = entry[kRoutingNames[l]];
            Routing &routing = pipeline.Routings[l];
            routing.Inputs = static_cast<int32_t>( matrix["InputChannelCount"].asInt() );
            routing.Outputs = static_cast<int32_t>( matrix["OutputChannelCount"].asInt() );
            const uint32_t count = matrix.getArraySize( "Mapping" );
            routing.Gains.assign( static_cast<size_t>( Max( routing.Inputs * routing.Outputs, 0 ) ), 0.0f );
            for ( uint32_t k = 0; k < count && k < routing.Gains.size(); k++ ) {
                routing.Gains[k] = DecibelToGain( matrix.getArrayElement( "Mapping", k ).asFloat( -80.0f ) );
            }
        }
        pipelines.push_back( std::move( pipeline ) );
    }
    auto findPipelineByHash = [this]( int32_t hash ) -> int32_t {
        for ( size_t i = 0; i < pipelines.size(); i++ ) {
            if ( pipelines[i].Hash == hash ) {
                return static_cast<int32_t>( i );
            }
        }
        return -1;
    };
    for ( uint32_t i = 0; i < root.getArraySize( "Clients" ); i++ ) {
        XmbValue entry = root.getArrayElement( "Clients", i );
        Client client;
        client.Name = entry["Name"].asString();
        client.Hash = static_cast<int32_t>( entry["HashCode"].asInt() );
        client.Pipeline = findPipelineByHash( static_cast<int32_t>( entry["Pipeline"].asInt() ) );
        client.bIsMusic = entry["IsMusic"].asBool();
        clients.push_back( std::move( client ) );
    }
    auto findClientByHash = [this]( int32_t hash ) -> int32_t {
        for ( size_t i = 0; i < clients.size(); i++ ) {
            if ( clients[i].Hash == hash ) {
                return static_cast<int32_t>( i );
            }
        }
        return -1;
    };
    // Mixers reference each other: names / hashes first.
    const uint32_t numMixers = root.getArraySize( "Mixers" );
    for ( uint32_t i = 0; i < numMixers; i++ ) {
        XmbValue entry = root.getArrayElement( "Mixers", i );
        Mixer mixer;
        mixer.Name = entry["Name"].asString();
        mixer.Hash = static_cast<int32_t>( entry["HashCode"].asInt() );
        mixers.push_back( std::move( mixer ) );
    }
    auto findMixerByHash = [this]( int32_t hash ) -> int32_t {
        for ( size_t i = 0; i < mixers.size(); i++ ) {
            if ( mixers[i].Hash == hash ) {
                return static_cast<int32_t>( i );
            }
        }
        return -1;
    };
    for ( uint32_t i = 0; i < numMixers; i++ ) {
        XmbValue entry = root.getArrayElement( "Mixers", i );
        for ( uint32_t k = 0; k < entry.getArraySize( "ClientsIds" ); k++ ) {
            const int32_t index =
                findClientByHash( static_cast<int32_t>( entry.getArrayElement( "ClientsIds", k ).asInt() ) );
            if ( index >= 0 ) {
                mixers[i].Clients.push_back( index );
            }
        }
        for ( uint32_t k = 0; k < entry.getArraySize( "MixersIds" ); k++ ) {
            const int32_t index =
                findMixerByHash( static_cast<int32_t>( entry.getArrayElement( "MixersIds", k ).asInt() ) );
            if ( index >= 0 && index != static_cast<int32_t>( i ) ) {
                mixers[i].Mixers.push_back( index );
            }
        }
    }
    for ( uint32_t i = 0; i < root.getArraySize( "DuckingPresets" ); i++ ) {
        XmbValue entry = root.getArrayElement( "DuckingPresets", i );
        DuckingPreset preset;
        preset.Name = entry["Name"].asString();
        preset.Trigger = findClientByHash( static_cast<int32_t>( entry["Trigger"].asInt() ) );
        for ( uint32_t k = 0; k < entry.getArraySize( "TargetsIds" ); k++ ) {
            const int32_t index =
                findClientByHash( static_cast<int32_t>( entry.getArrayElement( "TargetsIds", k ).asInt() ) );
            if ( index >= 0 ) {
                preset.Targets.push_back( index );
            }
        }
        preset.AttackTime = entry["AttackTime"].asFloat();
        preset.SustainTime = entry["SustainTime"].asFloat();
        preset.SustainGain = entry["SustainGain"].asFloat( 1.0f );
        preset.ReleaseTime = entry["ReleaseTime"].asFloat();
        if ( preset.Trigger >= 0 ) {
            duckingPresets.push_back( std::move( preset ) );
        }
    }
    for ( uint32_t i = 0; i < root.getArraySize( "MixPresets" ); i++ ) {
        XmbValue entry = root.getArrayElement( "MixPresets", i );
        MixPreset preset;
        preset.Name = entry["Name"].asString();
        preset.Hash = static_cast<int32_t>( entry["HashCode"].asInt() );
        for ( uint32_t k = 0; k < entry.getArraySize( "ClientPairs" ); k++ ) {
            XmbValue pair = entry.getArrayElement( "ClientPairs", k );
            const int32_t index = findClientByHash( static_cast<int32_t>( pair["TargetId"].asInt() ) );
            if ( index >= 0 ) {
                preset.ClientGains.emplace_back( index, pair["Gain"].asFloat( 1.0f ) );
            }
        }
        for ( uint32_t k = 0; k < entry.getArraySize( "MixerPairs" ); k++ ) {
            XmbValue pair = entry.getArrayElement( "MixerPairs", k );
            const int32_t index = findMixerByHash( static_cast<int32_t>( pair["TargetId"].asInt() ) );
            if ( index >= 0 ) {
                preset.MixerGains.emplace_back( index, pair["Gain"].asFloat( 1.0f ) );
            }
        }
        mixPresets.push_back( std::move( preset ) );
    }
    for ( uint32_t i = 0; i < root.getArraySize( "GameModes" ); i++ ) {
        XmbValue entry = root.getArrayElement( "GameModes", i );
        GameMode mode;
        mode.Name = entry["Name"].asString();
        const int32_t presetHash = static_cast<int32_t>( entry["MixPreset"].asInt() );
        for ( size_t k = 0; k < mixPresets.size(); k++ ) {
            if ( mixPresets[k].Hash == presetHash ) {
                mode.MixPreset = static_cast<int32_t>( k );
            }
        }
        gameModes.push_back( std::move( mode ) );
    }
    return !pipelines.empty() && !clients.empty();
}

bool AudioSetup::loadDefault() {
    XmbDocument document;
    const std::string path = ( gpFile != nullptr ? gpFile->getBankFolder() : std::string( "Euro/Bnk/" ) ) +
                             "sound/audiosetup.xmb";
    if ( LoadXmbFromFile( path.c_str(), document ) && load( document ) ) {
        OTDU_LOG_INFO( "Audio: %zu pipelines, %zu clients, %zu mixers, %zu ducking presets, %zu mix presets, %zu "
                       "game modes\n",
                       pipelines.size(), clients.size(), mixers.size(), duckingPresets.size(), mixPresets.size(),
                       gameModes.size() );
        return true;
    }
    OTDU_LOG_WARN( "Audio: '%s' not loaded, using the fallback routing\n", path.c_str() );
    buildFallback();
    return false;
}

void AudioSetup::buildFallback() {
    pipelines.clear();
    clients.clear();
    mixers.clear();
    duckingPresets.clear();
    mixPresets.clear();
    gameModes.clear();
    // Matrices copied from audiosetup.xmb (dB).
    Pipeline voice;
    voice.Name = "VoiceSubtitled";
    voice.Routings[L_Stereo] = MakeRouting( 1, 2, { -6, -6 } );
    voice.Routings[L_51] = MakeRouting( 1, 6, { -80, -80, -80, -80, 0, -80 } );
    voice.Routings[L_71] = MakeRouting( 1, 8, { -80, -80, -80, -80, -80, -80, 0, -80 } );
    Pipeline cinematic;
    cinematic.Name = "Cinematic";
    cinematic.Routings[L_Stereo] = MakeRouting( 2, 2, { 0, -80, -80, 0 } );
    cinematic.Routings[L_51] = MakeRouting( 2, 6, { 0, -80, -42, -80, -80, -80, -80, 0, -80, -42, -80, -80 } );
    cinematic.Routings[L_71] =
        MakeRouting( 2, 8, { 0, -80, -42, -80, -80, -80, -80, -80, -80, 0, -80, -42, -80, -80, -80, -80 } );
    Pipeline sfx;
    sfx.Name = "Sfx";
    sfx.Routings[L_Stereo] = MakeRouting( 5, 2, { -6, -80, -80, -6, -6, -80, -80, -6, -6, -6 } );
    sfx.Routings[L_51] = MakeRouting( 5, 6, { 0,   -80, -80, -80, -80, -12, -80, 0,   -80, -80, -80, -12, -80, -80, 0,
                                             -80, -80, -12, -80, -80, -80, 0,   -80, -12, -80, -80, -80, -80, 0,   -12 } );
    sfx.Routings[L_71] =
        MakeRouting( 5, 8, { 0,   -80, -80, -80, -6,  -80, -80, -12, -80, 0,   -80, -80, -80, -6,
                             -80, -12, -80, -80, 0,   -80, -6,  -80, -80, -12, -80, -80, -80, 0,
                             -80, -6,  -80, -12, -80, -80, -80, -80, -80, -80, 0,   -12 } );
    pipelines = { voice, cinematic, sfx };
    // Client -> pipeline family (spec_sound3d.md 2.2): voices on the mono
    // pipeline, the 2 input pipelines (FrontEnd*, Radio, AmbienceInternal,
    // AmbienceRain, Cinematic*) on the stereo one, the rest on Sfx [INF for
    // the vehicle pipelines].
    for ( int32_t i = 0; i < kNumBfxClients; i++ ) {
        Client client;
        client.Name = kBfxClientNames[i];
        if ( i <= 6 ) {
            client.Pipeline = 0;
        } else if ( i == 7 || i == 8 || i == 9 || i == 10 || i == 13 || i == 22 || i == 23 ) {
            client.Pipeline = 1;
        } else {
            client.Pipeline = 2;
        }
        client.bIsMusic = i == 8 || i == 9 || i == 23;
        clients.push_back( client );
    }
}

int32_t AudioSetup::findClient( const char *pName ) const {
    for ( size_t i = 0; i < clients.size(); i++ ) {
        if ( clients[i].Name == pName ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

int32_t AudioSetup::findPipeline( const char *pName ) const {
    for ( size_t i = 0; i < pipelines.size(); i++ ) {
        if ( pipelines[i].Name == pName ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

int32_t AudioSetup::findMixPreset( const char *pName ) const {
    for ( size_t i = 0; i < mixPresets.size(); i++ ) {
        if ( StringCmpI( mixPresets[i].Name.c_str(), pName ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

int32_t AudioSetup::findGameMode( const char *pName ) const {
    for ( size_t i = 0; i < gameModes.size(); i++ ) {
        if ( StringCmpI( gameModes[i].Name.c_str(), pName ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

void AudioSetup::computeClientGains( int32_t mixPreset, std::vector<float> &outGains ) const {
    outGains.assign( clients.size(), 1.0f );
    if ( mixPreset < 0 || mixPreset >= static_cast<int32_t>( mixPresets.size() ) ) {
        return;
    }
    const MixPreset &preset = mixPresets[mixPreset];
    std::vector<float> mixerGains( mixers.size(), 1.0f );
    for ( const auto &pair : preset.MixerGains ) {
        mixerGains[pair.first] = pair.second;
    }
    for ( const auto &pair : preset.ClientGains ) {
        outGains[pair.first] = pair.second;
    }
    // Parent of each mixer (first one found), then walk up from the mixer of
    // each client.
    std::vector<int32_t> mixerParent( mixers.size(), -1 );
    std::vector<int32_t> clientMixer( clients.size(), -1 );
    for ( size_t m = 0; m < mixers.size(); m++ ) {
        for ( int32_t child : mixers[m].Mixers ) {
            if ( mixerParent[child] < 0 ) {
                mixerParent[child] = static_cast<int32_t>( m );
            }
        }
        for ( int32_t client : mixers[m].Clients ) {
            if ( clientMixer[client] < 0 ) {
                clientMixer[client] = static_cast<int32_t>( m );
            }
        }
    }
    for ( size_t c = 0; c < clients.size(); c++ ) {
        int32_t mixer = clientMixer[c];
        for ( int depth = 0; mixer >= 0 && depth < 16; depth++ ) {
            outGains[c] *= mixerGains[mixer];
            mixer = mixerParent[mixer];
        }
    }
}
