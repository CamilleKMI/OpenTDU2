#pragma once

class XmbDocument;

// edAudio setup (Euro/Bnk/sound/audiosetup.xmb, loaded by GSAudio::Init
// 0xd95770 -> FUN_00d95640; docs: spec_sound3d.md 2).
//   AudioSetupExport { PipelineExport[] Pipelines; ClientExport[] Clients;
//     MixerExport[] Mixers; DuckingPresetExport[] DuckingPresets;
//     MixPresetExport[] MixPresets; GameModeExport[] GameModes }
//   PipelineExport { RoutingMatrix RoutingStereo, Routing51, Routing71;
//     String Name; Int32 HashCode }
//   RoutingMatrix { Int32 InputChannelCount; Int32 OutputChannelCount;
//     Single[] Mapping } (row major [input][output], dB, -80 = mute)
//   ClientExport { Int32 Pipeline; Boolean IsMusic; String Name; Int32 HashCode }
//   MixerExport { Int32[] ClientsIds; Int32[] MixersIds; String Name; Int32 HashCode }
//   DuckingPresetExport { Int32 Trigger; Int32[] TargetsIds; Single AttackTime,
//     SustainTime, SustainGain, ReleaseTime; String Name; Int32 HashCode }
//   MixPresetExport { {Single Gain; Int32 TargetId}[] ClientPairs, MixerPairs;
//     String Name; Int32 HashCode }
//   GameModeExport { Int32 MixPreset; String Name; Int32 HashCode }
//
// edAudio channel orders: spatial pipelines (5 inputs) FL FR BL BR C; output
// 5.1 = FL FR BL BR C LFE, 7.1 = FL FR BL BR SL SR C LFE.
class AudioSetup {
public:
    enum eLayout { L_Stereo = 0, L_51, L_71, L_Count };

    struct Routing {
        int32_t Inputs = 0;
        int32_t Outputs = 0;
        std::vector<float> Gains; // linear, [input * Outputs + output]
    };

    struct Pipeline {
        std::string Name;
        int32_t Hash = 0;
        Routing Routings[L_Count];
        int32_t getInputs() const { return Routings[L_Stereo].Inputs; }
    };

    struct Client {
        std::string Name;
        int32_t Hash = 0;
        int32_t Pipeline = -1; // index in pipelines
        bool bIsMusic = false;
    };

    struct Mixer {
        std::string Name;
        int32_t Hash = 0;
        std::vector<int32_t> Clients; // indices
        std::vector<int32_t> Mixers;  // indices
    };

    struct DuckingPreset {
        std::string Name;
        int32_t Trigger = -1;         // client index
        std::vector<int32_t> Targets; // client indices
        float AttackTime = 0.0f;
        float SustainTime = 0.0f;
        float SustainGain = 1.0f;
        float ReleaseTime = 0.0f;
    };

    struct MixPreset {
        std::string Name;
        int32_t Hash = 0;
        std::vector<std::pair<int32_t, float>> ClientGains; // client index, linear gain
        std::vector<std::pair<int32_t, float>> MixerGains;  // mixer index, linear gain
    };

    struct GameMode {
        std::string Name;
        int32_t MixPreset = -1; // index
    };

public:
    // Client names by .bfx client index (+0xdc), exe table 0x00f90ac0.
    static constexpr int32_t kNumBfxClients = 33;
    static const char *GetBfxClientName( int32_t index );

    bool load( const XmbDocument &document );
    // Euro/Bnk/sound/audiosetup.xmb; builds the fallback on failure.
    bool loadDefault();
    // Without the file: "Cinematic" (stereo), "VoiceSubtitled" (mono) and
    // "Sfx" (5 ch) pipelines copied from audiosetup.xmb, the bfx clients
    // mapped on them, no mixer / preset / ducking (all gains 1).
    void buildFallback();

    int32_t findClient( const char *pName ) const;
    int32_t findPipeline( const char *pName ) const;
    int32_t findMixPreset( const char *pName ) const;
    int32_t findGameMode( const char *pName ) const;

    inline const std::vector<Pipeline> &getPipelines() const { return pipelines; }
    inline const std::vector<Client> &getClients() const { return clients; }
    inline const std::vector<Mixer> &getMixers() const { return mixers; }
    inline const std::vector<DuckingPreset> &getDuckingPresets() const { return duckingPresets; }
    inline const std::vector<MixPreset> &getMixPresets() const { return mixPresets; }
    inline const std::vector<GameMode> &getGameModes() const { return gameModes; }

    // Per client gain of a mix preset: client gain * gains of the mixers
    // containing it (recursively up to the root mixers). [INF: the edAudio
    // mixer evaluation is in encrypted code; a client/mixer missing from the
    // preset keeps 1.]
    void computeClientGains( int32_t mixPreset, std::vector<float> &outGains ) const;

private:
    std::vector<Pipeline> pipelines;
    std::vector<Client> clients;
    std::vector<Mixer> mixers;
    std::vector<DuckingPreset> duckingPresets;
    std::vector<MixPreset> mixPresets;
    std::vector<GameMode> gameModes;
};
