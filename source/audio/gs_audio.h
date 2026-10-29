#pragma once

#include <SDL3/SDL_audio.h>

#include "audio/audio_setup.h"
#include "core/math.h"

// Audio output. The original uses Eden's own software mixer (edAudio:
// edAudioMixer / edAudioSpatializer / edAudioChannelRouter, output over
// DirectSound or XAudio2 2.3; no X3DAudio / DS3D / HRTF), see
// spec_sound3d.md. The game banks store RIFF WAV files (MS-ADPCM 22050 Hz for
// the cinematics), decoded by SDL3.
//
// OpenTDU2 mixer (spec_sound3d.md 7 / 8):
//   - one SDL device stream fed by a callback (mix()), float output at the
//     device rate, 2 / 6 / 8 channels;
//   - each voice is resampled with a fractional step (pitch * doppler, linear
//     interpolation) and accumulated into the bus of its client's pipeline
//     (1 = mono, 2 = stereo, 5 = spatial FL FR BL BR C), with per channel
//     gains ramped linearly over the callback buffer;
//   - each bus is routed to the output by the pipeline routing matrix of
//     audiosetup.xmb (stereo / 5.1 / 7.1), edAudio channel order remapped to
//     SDL's;
//   - per client gains: mix preset of the game mode (setGameMode) * ducking
//     envelopes (audiosetup.xmb DuckingPresets).
// Voice parameters are computed in tick() (main thread) and read by the
// callback (audio thread) under a mutex.
struct AudioClip {
    std::string Name;
    SDL_AudioSpec Spec {};     // Source format of the WAV
    int32_t Channels = 0;      // 1 or 2 (F32 frames below)
    int32_t Rate = 0;
    std::vector<float> Frames; // Interleaved F32, Channels per frame, at Rate
    uint32_t NumFrames = 0;
    float Duration = 0.0f;
};

class GSAudio {
public:
    using VoiceHandle = uint32_t;
    static constexpr VoiceHandle kInvalidVoice = 0;

    // Voice parameters (edFxSound / edAudioSourceSpatial setters, spec 3 / 4.5).
    struct VoiceParams {
        // .bfx client index (+0xdc, AudioSetup::GetBfxClientName). -1: plain
        // stereo output, no pipeline / preset / ducking (legacy play()).
        int32_t Client = -1;
        float Volume = 1.0f; // linear (0x578430)
        float Pitch = 1.0f;  // ratio (0x578440)
        bool bLoop = false;  // loop count -1 (src+0xcc)
        float StartTime = 0.0f; // offset in the clip (s)

        // 3D. Used only when the client pipeline has 5 inputs (Sfx...).
        bool bSpatial = false;
        bool bRelative = false; // Position in the listener frame (0x5787b0 flag 1)
        Vector3f Position;      // world (0x5787b0)
        Vector3f Velocity;      // src+0x300
        Vector3f Direction = Vector3f( 0.0f, 0.0f, 1.0f ); // cone axis [INF]
        float ClipNear = 4.0f;  // .bfx ClpNr / 0x578860
        float ClipMid = 40.0f;  // .bfx ClpMd
        float ClipFar = 40.0f;  // .bfx ClpFr
        float ClipMidVolume = 0.0f; // .bfx ClpMdVol
        float ClipAttenuation = 0.0f; // .bfx ClpAttn / refraction (unused: [ENC])
        float ConeInner = 180.0f;     // .bfx CnIn (deg)
        float ConeOuter = 180.0f;     // .bfx CnOut (deg)
        float ConeOuterVolume = 1.0f; // .bfx CnOutVol
        float Doppler = 0.0f;         // DopplerFactor (0x578820), 0 = off
    };

public:
    GSAudio();
    ~GSAudio();

    // Loads audiosetup.xmb and opens the default playback device. Returns
    // false (and isReady() stays false) without a device.
    bool initialize();
    // No device: the mixer runs only through mixOffline() (tests).
    bool initializeOffline( int32_t sampleRate, int32_t numChannels );
    void shutdown();
    inline bool isReady() const { return device != 0 || bOffline; }

    // Decodes a RIFF WAV (nullptr on failure).
    std::shared_ptr<AudioClip> loadClip( const uint8_t *pData, size_t size, const char *pName );

    // startTime: offset in the clip (s). volume: linear gain. Plain stereo
    // (Client -1).
    VoiceHandle play( const std::shared_ptr<AudioClip> &clip, float volume = 1.0f, bool bLoop = false,
                      float startTime = 0.0f );
    VoiceHandle play( const std::shared_ptr<AudioClip> &clip, const VoiceParams &params );
    void stop( VoiceHandle voice );
    void stopAll();
    bool isPlaying( VoiceHandle voice ) const;
    void setVolume( VoiceHandle voice, float volume );
    void setPitch( VoiceHandle voice, float pitch );
    void setVoicePosition( VoiceHandle voice, const Vector3f &position,
                           const Vector3f &velocity = Vector3f( 0.0f, 0.0f, 0.0f ) );

    // GSAudio::SetListener (FUN_00d944b0): forward flattened on the
    // horizontal plane (y = 0, normalized, (0, 0, 1) if degenerate), up =
    // (0, 1, 0). World space of EdenSceneView (camera forward =
    // R(conj q) (0, 0, 1)).
    void setListener( const Vector3f &position, const Vector3f &forward, const Vector3f &velocity );

    // Game mode name of audiosetup.xmb ("AVATAR_CHOICE", "FIRST_WM" -> mix
    // preset "Intro", "CINE" -> "Cinematic", ...), or a mix preset name.
    // nullptr / unknown: no preset (all client gains 1).
    void setGameMode( const char *pGameMode );

    // Removes the finished voices, updates the ducking envelopes and the
    // voice gains (real time step).
    void tick();
    // Same with an explicit time step (tests).
    void update( float deltaTime );

    // Offline WAV dump (-audiodump): 32-bit float WAV of the mix, advanced
    // by tickOffline( game time step ) (offline mixer only).
    bool startDump( const char *pPath );
    inline bool isDumping() const { return pDumpFile != nullptr; }
    void tickOffline( float deltaTime );
    void finishDump();

    // Mixes numFrames frames into pOut (interleaved, getOutputChannels()
    // channels, SDL order). Called by the device callback; usable directly
    // with initializeOffline().
    void mixOffline( float *pOut, int32_t numFrames );

    inline int32_t getOutputRate() const { return outputRate; }
    inline int32_t getOutputChannels() const { return outputChannels; }
    inline const AudioSetup &getSetup() const { return setup; }

private:
    static constexpr int32_t kMaxBusInputs = 5;
    static constexpr int32_t kMaxClipChannels = 2;

    struct Voice {
        VoiceHandle Handle = kInvalidVoice;
        std::shared_ptr<AudioClip> Clip;
        VoiceParams Params;
        int32_t ClientIndex = -1; // in setup.getClients()
        int32_t Bus = -1;         // pipeline index, -1 = direct stereo bus
        double Cursor = 0.0;      // frames
        float Step = 1.0f;        // target step (clip frames per output frame)
        float Gains[kMaxClipChannels][kMaxBusInputs] {};
        float CurrentGains[kMaxClipChannels][kMaxBusInputs] {};
        bool bGainsSet = false;   // CurrentGains initialized
        bool bFinished = false;
        // Debug
        float Distance = 0.0f;
        float Attenuation = 1.0f;
    };

    struct Ducking {
        float Level = 1.0f;
        float Hold = 0.0f;
    };

    SDL_AudioDeviceID device = 0;
    SDL_AudioStream *pDeviceStream = nullptr;
    bool bOffline = false;
    int32_t outputRate = 48000;
    int32_t outputChannels = 2;
    AudioSetup::eLayout layout = AudioSetup::L_Stereo;
    int32_t channelMap[8] {}; // edAudio output channel -> SDL channel

    AudioSetup setup;
    std::array<int32_t, AudioSetup::kNumBfxClients> bfxClients {}; // bfx index -> setup client
    std::vector<float> presetGains;  // per setup client
    std::vector<Ducking> duckings;   // per ducking preset
    std::vector<float> duckGains;    // per setup client

    struct Listener {
        Vector3f Position;
        Vector3f Forward = Vector3f( 0.0f, 0.0f, 1.0f );
        Vector3f Up = Vector3f( 0.0f, 1.0f, 0.0f );
        Vector3f Right = Vector3f( -1.0f, 0.0f, 0.0f );
        Vector3f Velocity;
    } listener;

    bool bSpatialEnabled = true;
    bool bLogVoices = false;
    float logTimer = 0.0f;
    uint64_t lastTickNs = 0;

    FILE *pDumpFile = nullptr;
    uint64_t dumpFrames = 0;
    double dumpRemainder = 0.0;
    std::vector<float> dumpBuffer;

    mutable std::mutex mutex;
    std::vector<Voice> voices;
    VoiceHandle nextHandle = 1;

    // Callback scratch (audio thread)
    std::vector<std::vector<float>> busBuffers; // per pipeline, + 1 direct
    std::vector<uint8_t> busUsed;
    std::vector<float> mixBuffer;

private:
    void setupOutput( int32_t numChannels );
    void loadSetup();
    void computeVoice( Voice &voice ) const;
    void mixLocked( float *pOut, int32_t numFrames );
    Voice *findVoice( VoiceHandle handle );
    const Voice *findVoice( VoiceHandle handle ) const;

    static void SDLCALL DeviceCallback( void *pUserData, SDL_AudioStream *pStream, int additionalAmount,
                                        int totalAmount );
};

extern GSAudio *gpAudio;
