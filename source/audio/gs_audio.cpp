#include "shared.h"
#include "gs_audio.h"

#include <SDL3/SDL.h>

GSAudio *gpAudio = nullptr;

namespace {
// edAudio 5 channel spatial bus: 0 FL, 1 FR, 2 BL, 3 BR, 4 C
// (spec_sound3d.md 2.1).
constexpr int32_t kFL = 0, kFR = 1, kBL = 2, kBR = 3, kC = 4;

// [INF] Speed of sound (m/s) of the Doppler shift (edAudio formula: [ENC]).
constexpr float kSpeedOfSound = 343.0f;

bool EnvFlag( const char *pName, bool bDefault ) {
    const char *pValue = SDL_getenv( pName );
    if ( pValue == nullptr || pValue[0] == '\0' ) {
        return bDefault;
    }
    return pValue[0] != '0';
}

// [INF] Piecewise linear curve: 1 up to Near, MidVol at Mid, 0 at Far (the
// edAudio curve is [ENC]; the exporter writes Far = Mid + epsilon and
// MidVol = 0, i.e. a single 1 -> 0 segment from Near to Mid).
float DistanceGain( float d, float clipNear, float clipMid, float clipFar, float midVolume ) {
    clipMid = Max( clipMid, clipNear );
    clipFar = Max( clipFar, clipMid );
    if ( d <= clipNear ) {
        return 1.0f;
    }
    if ( d <= clipMid ) {
        const float span = clipMid - clipNear;
        return span > 0.0f ? 1.0f + ( midVolume - 1.0f ) * ( d - clipNear ) / span : midVolume;
    }
    if ( d < clipFar ) {
        const float span = clipFar - clipMid;
        return span > 0.0f ? midVolume * ( 1.0f - ( d - clipMid ) / span ) : 0.0f;
    }
    return 0.0f;
}

// [INF] Pairwise constant power panning over FL -30, C 0, FR +30, BR +110,
// BL -110 degrees (positive azimuth = right). The edAudio pan law and speaker
// angles are [ENC].
void Pan5( float azimuthDeg, float outGains[5] ) {
    struct Speaker {
        float Angle;
        int32_t Channel;
    };
    static constexpr Speaker kRing[5] = { { -110.0f, kBL }, { -30.0f, kFL }, { 0.0f, kC }, { 30.0f, kFR },
                                          { 110.0f, kBR } };
    for ( int i = 0; i < 5; i++ ) {
        outGains[i] = 0.0f;
    }
    for ( int i = 0; i < 5; i++ ) {
        const float a0 = kRing[i].Angle;
        const float a1 = i < 4 ? kRing[i + 1].Angle : kRing[0].Angle + 360.0f;
        float a = azimuthDeg;
        if ( i == 4 && a < a0 ) {
            a += 360.0f;
        }
        if ( a >= a0 && a <= a1 ) {
            const float t = ( a - a0 ) / ( a1 - a0 );
            outGains[kRing[i].Channel] = std::cos( t * kPI * 0.5f );
            outGains[kRing[( i + 1 ) % 5].Channel] = std::sin( t * kPI * 0.5f );
            return;
        }
    }
    outGains[kC] = 1.0f;
}
} // namespace

GSAudio::GSAudio() {
    gpAudio = this;
    bfxClients.fill( -1 );
}

GSAudio::~GSAudio() {
    shutdown();
    if ( gpAudio == this ) {
        gpAudio = nullptr;
    }
}

void GSAudio::loadSetup() {
    if ( setup.getClients().empty() ) {
        setup.loadDefault();
    }
    for ( int32_t i = 0; i < AudioSetup::kNumBfxClients; i++ ) {
        bfxClients[i] = setup.findClient( AudioSetup::GetBfxClientName( i ) );
    }
    presetGains.assign( setup.getClients().size(), 1.0f );
    duckGains.assign( setup.getClients().size(), 1.0f );
    duckings.assign( setup.getDuckingPresets().size(), Ducking() );
    bSpatialEnabled = EnvFlag( "OTDU_AUDIO_3D", true );
    bLogVoices = EnvFlag( "OTDU_AUDIO_LOG", false );
    listener = Listener();
    setListener( Vector3f(), Vector3f( 0.0f, 0.0f, 1.0f ), Vector3f() );
}

void GSAudio::setupOutput( int32_t numChannels ) {
    // SetOutputNbSpeakers 2 / 6 / 8 (GSAudio Init 0xd957e2). edAudio orders:
    // 5.1 FL FR BL BR C LFE, 7.1 FL FR BL BR SL SR C LFE; SDL: 5.1 FL FR FC
    // LFE BL BR, 7.1 FL FR FC LFE BL BR SL SR.
    static constexpr int32_t kMap51[6] = { 0, 1, 4, 5, 2, 3 };
    static constexpr int32_t kMap71[8] = { 0, 1, 4, 5, 6, 7, 2, 3 };
    if ( numChannels >= 8 ) {
        outputChannels = 8;
        layout = AudioSetup::L_71;
        memcpy( channelMap, kMap71, sizeof( kMap71 ) );
    } else if ( numChannels >= 6 ) {
        outputChannels = 6;
        layout = AudioSetup::L_51;
        memcpy( channelMap, kMap51, sizeof( kMap51 ) );
    } else {
        outputChannels = 2;
        layout = AudioSetup::L_Stereo;
        channelMap[0] = 0;
        channelMap[1] = 1;
    }
}

bool GSAudio::initialize() {
    loadSetup();
    if ( !SDL_InitSubSystem( SDL_INIT_AUDIO ) ) {
        OTDU_LOG_WARN( "Audio: SDL_InitSubSystem failed: %s\n", SDL_GetError() );
        return false;
    }
    SDL_AudioSpec preferred {};
    int sampleFrames = 0;
    if ( !SDL_GetAudioDeviceFormat( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &preferred, &sampleFrames ) ) {
        preferred.channels = 2;
        preferred.freq = 48000;
    }
    int32_t channels = preferred.channels;
    if ( const char *pChannels = SDL_getenv( "OTDU_AUDIO_CHANNELS" ) ) {
        channels = atoi( pChannels );
    }
    setupOutput( channels );
    SDL_AudioSpec spec {};
    spec.format = SDL_AUDIO_F32;
    spec.channels = outputChannels;
    spec.freq = preferred.freq > 0 ? preferred.freq : 48000;
    pDeviceStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, DeviceCallback, this );
    if ( pDeviceStream == nullptr ) {
        OTDU_LOG_WARN( "Audio: no playback device: %s\n", SDL_GetError() );
        SDL_QuitSubSystem( SDL_INIT_AUDIO );
        return false;
    }
    outputRate = spec.freq;
    device = SDL_GetAudioStreamDevice( pDeviceStream );
    if ( device == 0 ) {
        device = 1; // stream opened: treat as ready
    }
    lastTickNs = SDL_GetTicksNS();
    SDL_ResumeAudioStreamDevice( pDeviceStream );
    OTDU_LOG_INFO( "Audio: '%s' %d Hz, %d channels (mixer)\n", SDL_GetCurrentAudioDriver(), outputRate,
                   outputChannels );
    return true;
}

bool GSAudio::initializeOffline( int32_t sampleRate, int32_t numChannels ) {
    loadSetup();
    setupOutput( numChannels );
    outputRate = sampleRate > 0 ? sampleRate : 48000;
    bOffline = true;
    return true;
}

void GSAudio::shutdown() {
    if ( pDeviceStream != nullptr ) {
        SDL_DestroyAudioStream( pDeviceStream ); // closes the device
        pDeviceStream = nullptr;
        device = 0;
        SDL_QuitSubSystem( SDL_INIT_AUDIO );
    }
    stopAll();
    bOffline = false;
}

std::shared_ptr<AudioClip> GSAudio::loadClip( const uint8_t *pData, size_t size, const char *pName ) {
    SDL_IOStream *pStream = SDL_IOFromConstMem( pData, size );
    if ( pStream == nullptr ) {
        return nullptr;
    }
    auto clip = std::make_shared<AudioClip>();
    clip->Name = pName != nullptr ? pName : "";
    Uint8 *pSamples = nullptr;
    Uint32 length = 0;
    if ( !SDL_LoadWAV_IO( pStream, true, &clip->Spec, &pSamples, &length ) ) {
        OTDU_LOG_WARN( "Audio: failed to decode '%s': %s\n", clip->Name.c_str(), SDL_GetError() );
        return nullptr;
    }
    // F32, at most 2 channels (SDL downmix), source rate (resampled by the
    // mixer with the pitch).
    SDL_AudioSpec dst {};
    dst.format = SDL_AUDIO_F32;
    dst.channels = Clamp( clip->Spec.channels, 1, kMaxClipChannels );
    dst.freq = clip->Spec.freq;
    Uint8 *pConverted = nullptr;
    int convertedLength = 0;
    const bool bConverted =
        SDL_ConvertAudioSamples( &clip->Spec, pSamples, static_cast<int>( length ), &dst, &pConverted, &convertedLength );
    SDL_free( pSamples );
    if ( !bConverted ) {
        OTDU_LOG_WARN( "Audio: failed to convert '%s': %s\n", clip->Name.c_str(), SDL_GetError() );
        return nullptr;
    }
    clip->Channels = dst.channels;
    clip->Rate = dst.freq;
    clip->NumFrames = static_cast<uint32_t>( convertedLength / ( sizeof( float ) * dst.channels ) );
    clip->Frames.resize( static_cast<size_t>( clip->NumFrames ) * dst.channels );
    memcpy( clip->Frames.data(), pConverted, clip->Frames.size() * sizeof( float ) );
    SDL_free( pConverted );
    clip->Duration = clip->Rate > 0 ? static_cast<float>( clip->NumFrames ) / clip->Rate : 0.0f;
    return clip;
}

GSAudio::VoiceHandle GSAudio::play( const std::shared_ptr<AudioClip> &clip, float volume, bool bLoop,
                                    float startTime ) {
    VoiceParams params;
    params.Volume = volume;
    params.bLoop = bLoop;
    params.StartTime = startTime;
    return play( clip, params );
}

GSAudio::VoiceHandle GSAudio::play( const std::shared_ptr<AudioClip> &clip, const VoiceParams &params ) {
    if ( !isReady() || clip == nullptr || clip->NumFrames == 0 || clip->Rate <= 0 ) {
        return kInvalidVoice;
    }
    Voice voice;
    voice.Clip = clip;
    voice.Params = params;
    if ( params.Client >= 0 && params.Client < AudioSetup::kNumBfxClients ) {
        voice.ClientIndex = bfxClients[params.Client];
    }
    if ( voice.ClientIndex >= 0 ) {
        voice.Bus = setup.getClients()[voice.ClientIndex].Pipeline;
        if ( voice.Bus >= 0 ) {
            const int32_t inputs = setup.getPipelines()[voice.Bus].getInputs();
            if ( inputs < 1 || inputs > kMaxBusInputs ) {
                voice.Bus = -1;
            }
        }
    }
    double cursor = static_cast<double>( Max( params.StartTime, 0.0f ) ) * clip->Rate;
    if ( params.bLoop ) {
        cursor = std::fmod( cursor, static_cast<double>( clip->NumFrames ) );
    } else if ( cursor >= clip->NumFrames ) {
        return kInvalidVoice;
    }
    voice.Cursor = cursor;
    std::lock_guard<std::mutex> lock( mutex );
    voice.Handle = nextHandle++;
    if ( nextHandle == kInvalidVoice ) {
        nextHandle = 1;
    }
    computeVoice( voice );
    voices.push_back( std::move( voice ) );
    return voices.back().Handle;
}

GSAudio::Voice *GSAudio::findVoice( VoiceHandle handle ) {
    for ( Voice &voice : voices ) {
        if ( voice.Handle == handle ) {
            return &voice;
        }
    }
    return nullptr;
}

const GSAudio::Voice *GSAudio::findVoice( VoiceHandle handle ) const {
    for ( const Voice &voice : voices ) {
        if ( voice.Handle == handle ) {
            return &voice;
        }
    }
    return nullptr;
}

void GSAudio::stop( VoiceHandle handle ) {
    std::lock_guard<std::mutex> lock( mutex );
    for ( size_t i = 0; i < voices.size(); i++ ) {
        if ( voices[i].Handle == handle ) {
            voices.erase( voices.begin() + i );
            return;
        }
    }
}

void GSAudio::stopAll() {
    std::lock_guard<std::mutex> lock( mutex );
    voices.clear();
}

bool GSAudio::isPlaying( VoiceHandle handle ) const {
    std::lock_guard<std::mutex> lock( mutex );
    const Voice *pVoice = findVoice( handle );
    return pVoice != nullptr && !pVoice->bFinished;
}

void GSAudio::setVolume( VoiceHandle handle, float volume ) {
    std::lock_guard<std::mutex> lock( mutex );
    if ( Voice *pVoice = findVoice( handle ) ) {
        pVoice->Params.Volume = volume;
        computeVoice( *pVoice );
    }
}

void GSAudio::setPitch( VoiceHandle handle, float pitch ) {
    std::lock_guard<std::mutex> lock( mutex );
    if ( Voice *pVoice = findVoice( handle ) ) {
        pVoice->Params.Pitch = pitch;
        computeVoice( *pVoice );
    }
}

void GSAudio::setVoicePosition( VoiceHandle handle, const Vector3f &position, const Vector3f &velocity ) {
    std::lock_guard<std::mutex> lock( mutex );
    if ( Voice *pVoice = findVoice( handle ) ) {
        pVoice->Params.Position = position;
        pVoice->Params.Velocity = velocity;
        computeVoice( *pVoice );
    }
}

void GSAudio::setListener( const Vector3f &position, const Vector3f &forward, const Vector3f &velocity ) {
    // FUN_00d944b0: f = (fwd.x, 0, fwd.z); (0, 0, 1) if x^2 + z^2 <= 1e-5,
    // else normalized; up = (0, 1, 0).
    Vector3f flat( forward.x, 0.0f, forward.z );
    if ( flat.x * flat.x + flat.z * flat.z <= 1e-5f ) {
        flat = Vector3f( 0.0f, 0.0f, 1.0f );
    } else {
        flat = flat.normalized();
    }
    std::lock_guard<std::mutex> lock( mutex );
    listener.Position = position;
    listener.Forward = flat;
    listener.Up = Vector3f( 0.0f, 1.0f, 0.0f );
    // The exe stores right = 0x40d9e0(ecx = up, arg = forward) (+0x40); the
    // operand order of that [ENC] helper is unknown. [INF] OpenTDU2 uses the
    // screen right of EdenSceneView (camera frame R(conj q) * diag(-1, 1, -1),
    // right handed: screen right = forward x up = -X for forward +Z), so the
    // panning follows the picture. OTDU_AUDIO_SWAPLR=1 flips it.
    listener.Right = flat.cross( listener.Up );
    static const bool bSwap = EnvFlag( "OTDU_AUDIO_SWAPLR", false );
    if ( bSwap ) {
        listener.Right = -listener.Right;
    }
    listener.Velocity = velocity;
}

void GSAudio::setGameMode( const char *pGameMode ) {
    int32_t preset = -1;
    if ( pGameMode != nullptr ) {
        const int32_t mode = setup.findGameMode( pGameMode );
        preset = mode >= 0 ? setup.getGameModes()[mode].MixPreset : setup.findMixPreset( pGameMode );
        if ( preset < 0 ) {
            OTDU_LOG_WARN( "Audio: unknown game mode / mix preset '%s'\n", pGameMode );
        } else {
            OTDU_LOG_DEBUG( "Audio: game mode '%s' -> mix preset '%s'\n", pGameMode,
                            setup.getMixPresets()[preset].Name.c_str() );
        }
    }
    std::lock_guard<std::mutex> lock( mutex );
    setup.computeClientGains( preset, presetGains );
    for ( Voice &voice : voices ) {
        computeVoice( voice );
    }
}

void GSAudio::computeVoice( Voice &voice ) const {
    const VoiceParams &params = voice.Params;
    const AudioClip &clip = *voice.Clip;
    float gain = params.Volume;
    if ( voice.ClientIndex >= 0 ) {
        gain *= presetGains[voice.ClientIndex] * duckGains[voice.ClientIndex];
    }
    for ( auto &row : voice.Gains ) {
        for ( float &value : row ) {
            value = 0.0f;
        }
    }
    const int32_t inputs = voice.Bus >= 0 ? setup.getPipelines()[voice.Bus].getInputs() : 2;
    const int32_t channels = clip.Channels;
    float doppler = 1.0f;
    voice.Distance = 0.0f;
    voice.Attenuation = 1.0f;
    if ( inputs == 5 && params.bSpatial && bSpatialEnabled ) {
        Vector3f position = params.Position;
        if ( params.bRelative ) {
            position = listener.Position + listener.Right * params.Position.x + listener.Up * params.Position.y +
                       listener.Forward * params.Position.z;
        }
        const Vector3f toSource = position - listener.Position;
        const float d = toSource.length();
        float attenuation = DistanceGain( d, params.ClipNear, params.ClipMid, params.ClipFar, params.ClipMidVolume );
        // [INF] Cone (CnIn / CnOut as half angles: 180 = omnidirectional, the
        // value of all the shipped data).
        if ( params.ConeOuter < 180.0f && d > 1e-4f ) {
            const Vector3f axis = params.Direction.normalized();
            const float cosine = Clamp( axis.dot( toSource * ( -1.0f / d ) ), -1.0f, 1.0f );
            const float angle = std::acos( cosine ) * kRadToDeg;
            const float inner = Min( params.ConeInner, params.ConeOuter );
            float cone = 1.0f;
            if ( angle >= params.ConeOuter ) {
                cone = params.ConeOuterVolume;
            } else if ( angle > inner ) {
                cone = 1.0f + ( params.ConeOuterVolume - 1.0f ) * ( angle - inner ) / ( params.ConeOuter - inner );
            }
            attenuation *= cone;
        }
        // Azimuth in the listener frame, elevation ignored (the listener
        // forward is horizontal and up fixed).
        float pan[5];
        const float x = toSource.dot( listener.Right );
        const float z = toSource.dot( listener.Forward );
        Pan5( ( x * x + z * z ) > 1e-8f ? std::atan2( x, z ) * kRadToDeg : 0.0f, pan );
        // [INF] Closer than Near / 2: blend (constant power) toward an equal
        // spread over the 5 channels.
        const float spreadRadius = params.ClipNear * 0.5f;
        if ( spreadRadius > 0.0f && d < spreadRadius ) {
            const float s = 1.0f - d / spreadRadius;
            for ( float &value : pan ) {
                value = std::sqrt( ( 1.0f - s ) * value * value + s * 0.2f );
            }
        }
        // [INF] A stereo clip is downmixed to mono before panning.
        const float downmix = channels == 2 ? 0.5f : 1.0f;
        for ( int32_t c = 0; c < channels; c++ ) {
            for ( int32_t i = 0; i < 5; i++ ) {
                voice.Gains[c][i] = gain * attenuation * downmix * pan[i];
            }
        }
        // [INF] Doppler (formula [ENC]): u = unit(source - listener),
        // f = (c + DF vl.u) / (c + DF vs.u), clamped to [0.5, 2].
        if ( params.Doppler > 0.0f && !params.bRelative && d > 1e-4f ) {
            const Vector3f u = toSource * ( 1.0f / d );
            const float vl = listener.Velocity.dot( u ) * params.Doppler;
            const float vs = params.Velocity.dot( u ) * params.Doppler;
            const float denominator = Max( kSpeedOfSound + vs, 1.0f );
            doppler = Clamp( ( kSpeedOfSound + vl ) / denominator, 0.5f, 2.0f );
        }
        voice.Distance = d;
        voice.Attenuation = attenuation;
    } else if ( inputs == 1 ) {
        // [INF] stereo clip -> mono pipeline: (L + R) / 2.
        for ( int32_t c = 0; c < channels; c++ ) {
            voice.Gains[c][0] = channels == 2 ? gain * 0.5f : gain;
        }
    } else if ( inputs == 2 ) {
        // 2D: L, R (a mono clip feeds both).
        if ( channels == 1 ) {
            voice.Gains[0][0] = gain;
            voice.Gains[0][1] = gain;
        } else {
            voice.Gains[0][0] = gain;
            voice.Gains[1][1] = gain;
        }
    } else {
        // [INF] Non spatial voice on a 5 input pipeline: mono -> C, stereo ->
        // FL / FR.
        if ( channels == 1 ) {
            voice.Gains[0][kC] = gain;
        } else {
            voice.Gains[0][kFL] = gain;
            voice.Gains[1][kFR] = gain;
        }
    }
    voice.Step = static_cast<float>( clip.Rate ) / static_cast<float>( outputRate ) * Max( params.Pitch, 0.01f ) *
                 doppler;
}

void GSAudio::tick() {
    const uint64_t now = SDL_GetTicksNS();
    const float deltaTime = lastTickNs != 0 ? static_cast<float>( ( now - lastTickNs ) * 1e-9 ) : 0.0f;
    lastTickNs = now;
    update( Clamp( deltaTime, 0.0f, 0.25f ) );
}

void GSAudio::update( float deltaTime ) {
    std::lock_guard<std::mutex> lock( mutex );
    voices.erase( std::remove_if( voices.begin(), voices.end(), []( const Voice &voice ) { return voice.bFinished; } ),
                  voices.end() );
    // [INF] Ducking envelope (edAudio implementation [ENC]): while a voice of
    // the trigger client plays, the targets go down to SustainGain in
    // AttackTime; they stay there SustainTime after it stops, then go back
    // to 1 in ReleaseTime. Several envelopes on a client: the lowest wins.
    const auto &presets = setup.getDuckingPresets();
    std::fill( duckGains.begin(), duckGains.end(), 1.0f );
    for ( size_t k = 0; k < presets.size(); k++ ) {
        const AudioSetup::DuckingPreset &preset = presets[k];
        Ducking &ducking = duckings[k];
        bool bActive = false;
        for ( const Voice &voice : voices ) {
            if ( voice.ClientIndex == preset.Trigger ) {
                bActive = true;
                break;
            }
        }
        const float range = 1.0f - preset.SustainGain;
        if ( bActive ) {
            ducking.Hold = preset.SustainTime;
            ducking.Level = preset.AttackTime > 0.0f ? ducking.Level - deltaTime * range / preset.AttackTime
                                                     : preset.SustainGain;
            ducking.Level = Max( ducking.Level, preset.SustainGain );
        } else if ( ducking.Hold > 0.0f ) {
            ducking.Hold -= deltaTime;
        } else {
            ducking.Level =
                preset.ReleaseTime > 0.0f ? ducking.Level + deltaTime * range / preset.ReleaseTime : 1.0f;
            ducking.Level = Min( ducking.Level, 1.0f );
        }
        for ( int32_t target : preset.Targets ) {
            duckGains[target] = Min( duckGains[target], ducking.Level );
        }
    }
    for ( Voice &voice : voices ) {
        computeVoice( voice );
    }
    if ( bLogVoices ) {
        logTimer += deltaTime;
        if ( logTimer >= 1.0f ) {
            logTimer = 0.0f;
            for ( const Voice &voice : voices ) {
                float sum[kMaxBusInputs] {};
                for ( int32_t c = 0; c < kMaxClipChannels; c++ ) {
                    for ( int32_t i = 0; i < kMaxBusInputs; i++ ) {
                        sum[i] += voice.Gains[c][i];
                    }
                }
                OTDU_LOG_INFO( "Audio: '%s' client %d bus %d d %.1f att %.2f gains %.2f %.2f %.2f %.2f %.2f step "
                               "%.3f\n",
                               voice.Clip->Name.c_str(), voice.Params.Client, voice.Bus, voice.Distance,
                               voice.Attenuation, sum[0], sum[1], sum[2], sum[3], sum[4], voice.Step );
            }
        }
    }
}

void GSAudio::mixOffline( float *pOut, int32_t numFrames ) {
    std::lock_guard<std::mutex> lock( mutex );
    mixLocked( pOut, numFrames );
}

void GSAudio::mixLocked( float *pOut, int32_t numFrames ) {
    memset( pOut, 0, sizeof( float ) * static_cast<size_t>( numFrames ) * outputChannels );
    if ( numFrames <= 0 ) {
        return;
    }
    const auto &pipelines = setup.getPipelines();
    const size_t numBuses = pipelines.size() + 1; // last = direct stereo
    busBuffers.resize( numBuses );
    busUsed.assign( numBuses, 0 );
    auto busInputs = [&]( size_t bus ) -> int32_t { return bus < pipelines.size() ? pipelines[bus].getInputs() : 2; };

    for ( Voice &voice : voices ) {
        if ( voice.bFinished ) {
            continue;
        }
        const AudioClip &clip = *voice.Clip;
        const size_t bus = voice.Bus >= 0 ? static_cast<size_t>( voice.Bus ) : numBuses - 1;
        const int32_t inputs = busInputs( bus );
        std::vector<float> &buffer = busBuffers[bus];
        if ( !busUsed[bus] ) {
            buffer.assign( static_cast<size_t>( numFrames ) * inputs, 0.0f );
            busUsed[bus] = 1;
        }
        if ( !voice.bGainsSet ) {
            memcpy( voice.CurrentGains, voice.Gains, sizeof( voice.Gains ) );
            voice.bGainsSet = true;
        }
        const int32_t channels = clip.Channels;
        float gains[kMaxClipChannels][kMaxBusInputs];
        float deltas[kMaxClipChannels][kMaxBusInputs];
        for ( int32_t c = 0; c < kMaxClipChannels; c++ ) {
            for ( int32_t i = 0; i < kMaxBusInputs; i++ ) {
                gains[c][i] = voice.CurrentGains[c][i];
                deltas[c][i] = ( voice.Gains[c][i] - voice.CurrentGains[c][i] ) / numFrames;
            }
        }
        const double length = static_cast<double>( clip.NumFrames );
        const double step = voice.Step;
        double cursor = voice.Cursor;
        const float *pFrames = clip.Frames.data();
        for ( int32_t f = 0; f < numFrames; f++ ) {
            const uint32_t i0 = static_cast<uint32_t>( cursor );
            const float frac = static_cast<float>( cursor - i0 );
            uint32_t i1 = i0 + 1;
            bool bHasNext = true;
            if ( i1 >= clip.NumFrames ) {
                if ( voice.Params.bLoop ) {
                    i1 = 0;
                } else {
                    bHasNext = false;
                }
            }
            float* pBus = &buffer[static_cast<size_t>( f ) * inputs];
            for ( int32_t c = 0; c < channels; c++ ) {
                const float a = pFrames[static_cast<size_t>( i0 ) * channels + c];
                const float b = bHasNext ? pFrames[static_cast<size_t>( i1 ) * channels + c] : 0.0f;
                const float sample = a + ( b - a ) * frac;
                for ( int32_t i = 0; i < inputs; i++ ) {
                    gains[c][i] += deltas[c][i];
                    pBus[i] += sample * gains[c][i];
                }
            }
            cursor += step;
            if ( cursor >= length ) {
                if ( voice.Params.bLoop ) {
                    cursor = std::fmod( cursor, length );
                } else {
                    voice.bFinished = true;
                    break;
                }
            }
        }
        voice.Cursor = cursor;
        memcpy( voice.CurrentGains, voice.Gains, sizeof( voice.Gains ) );
    }

    // Buses -> output (pipeline routing matrix, edAudio -> SDL channel order).
    for ( size_t bus = 0; bus < numBuses; bus++ ) {
        if ( !busUsed[bus] ) {
            continue;
        }
        const std::vector<float> &buffer = busBuffers[bus];
        const int32_t inputs = busInputs( bus );
        struct Tap {
            int32_t Input;
            int32_t Output;
            float Gain;
        };
        Tap taps[kMaxBusInputs * 8];
        int32_t numTaps = 0;
        if ( bus < pipelines.size() ) {
            const AudioSetup::Routing &routing = pipelines[bus].Routings[layout];
            if ( routing.Inputs != inputs ) {
                continue;
            }
            for ( int32_t i = 0; i < inputs; i++ ) {
                for ( int32_t o = 0; o < routing.Outputs && o < outputChannels; o++ ) {
                    const float g = routing.Gains[static_cast<size_t>( i ) * routing.Outputs + o];
                    if ( g > 0.0f ) {
                        taps[numTaps++] = { i, channelMap[o], g };
                    }
                }
            }
        } else {
            taps[numTaps++] = { 0, channelMap[0], 1.0f };
            taps[numTaps++] = { 1, channelMap[1], 1.0f };
        }
        for ( int32_t f = 0; f < numFrames; f++ ) {
            const float *pIn = &buffer[static_cast<size_t>( f ) * inputs];
            float *pFrame = pOut + static_cast<size_t>( f ) * outputChannels;
            for ( int32_t t = 0; t < numTaps; t++ ) {
                pFrame[taps[t].Output] += pIn[taps[t].Input] * taps[t].Gain;
            }
        }
    }
}

void SDLCALL GSAudio::DeviceCallback( void *pUserData, SDL_AudioStream *pStream, int additionalAmount,
                                      int totalAmount ) {
    GSAudio *pAudio = static_cast<GSAudio *>( pUserData );
    const int32_t frameSize = static_cast<int32_t>( sizeof( float ) ) * pAudio->outputChannels;
    const int32_t numFrames = additionalAmount / frameSize;
    if ( numFrames <= 0 ) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock( pAudio->mutex );
        pAudio->mixBuffer.resize( static_cast<size_t>( numFrames ) * pAudio->outputChannels );
        pAudio->mixLocked( pAudio->mixBuffer.data(), numFrames );
    }
    SDL_PutAudioStreamData( pStream, pAudio->mixBuffer.data(), numFrames * frameSize );
}
