#pragma once

#include "audio/gs_audio.h"
#include "core/math.h"

class Bank;
struct BankEntry;
class Cinematic;
class CinematicScene;

// Resource hashcodes of a bank (hashcodes.xmb, bank type 8/10; the language
// banks have hashcodes__<lang>.xmb):
//   ___CmdSaveFileInfoArray[] { Type; SubType; FileIndices[]; FileReferences[] }
// FileReferences[i] (u64 "rsc" of the entities) -> bank entry FileIndices[i].
class BankHashcodes {
public:
    // pFilename nullptr: "hashcodes.xmb", else the first "hashcodes*.xmb".
    bool load( const Bank &bank, const char *pFilename = nullptr );
    const BankEntry *find( uint64_t rsc, int32_t type = -1, int32_t subType = -1 ) const;

private:
    struct Key {
        uint64_t Rsc;
        int32_t Type;
        int32_t SubType;
    };
    const Bank *pBank = nullptr;
    std::vector<std::pair<Key, uint32_t>> files;
};

// Sound FX of a cinematic bank (entities.xmb, docs/re/intro_spec.md 5.3,
// spec_sound3d.md 3 / 4):
//   Fx__Root { SourceData } -> Fx__SourceData { Fx[0] = .bfx (0/31);
//   SoundList[] { SoundNode } } -> Fx__Sound { SoundFile[0] = .wav (10/6) }
//   or Fx__Sound__<LANG> { LocalizedSoundFile[0] = .wav (0/36) in the
//   language bank "<bank>__<lang>.bnk" }.
// .bfx: SND nodes (edFxSound, class hash 0x8540076c at +0x58, " DNS" at
// +0xe0); a voice .bfx is a "LSGL" language switch node (+0xa0 u32 count,
// +0xb4 "LSGL", { u32 offset (node = offset - 0x10); u16 2; u16 language }[])
// over one SND node per language (names "US_", "FR_"...).
// SND node: +0x5c u32 loop count (-1 = loop), +0x78 name, +0xa0 u64 sound rsc,
// +0xac Vol, +0xb0 Ptch, +0xb4 VolRnd, +0xb8 PtchRnd, +0xbc CnIn, +0xc0 CnOut,
// +0xc4 CnOutVol, +0xc8 ClpNr, +0xcc ClpMd, +0xd0 ClpFr, +0xd4 ClpMdVol,
// +0xd8 ClpAttn, +0xdc client index ([INF] field mapping, spec 3).
// Entity__Fx { orient_pos_scale.pos; Fx = Fx__Root; AutoStart } are the
// emitters started by the "snd:" messages (positional, bank frame = the
// frame of the .sce actors).
//
// Positions: the bank / cinematic frame is mapped to the world by
// setSceneOrigin() before being given to GSAudio (listener in world space).
class CinematicSounds {
public:
    struct Sound {
        std::string Name;
        std::shared_ptr<AudioClip> Clip;
        float Volume = 1.0f;
        bool bLoop = false; // .bfx +0x5c == -1 (C_AVATAR, S_WATER, S_WALKMODE_Music)
        float Pitch = 1.0f;
        float VolumeRandom = 0.0f;
        float PitchRandom = 0.0f;
        float ConeInner = 180.0f;
        float ConeOuter = 180.0f;
        float ConeOuterVolume = 0.25f;
        float ClipNear = 4.0f;
        float ClipMid = 40.0f;
        float ClipFar = 40.0f;
        float ClipMidVolume = 0.0f;
        float ClipAttenuation = 0.0f;
        int32_t Client = 22; // [INF] default "Cinematic" when the .bfx node is missing
        bool bHasPosition = false; // Entity__Fx emitter
        Vector3f Position;         // bank frame
    };

    // .cin / .sce Fx record flags: bit 0x10 (0x37 music / voices, 0x27
    // whooshes / logo) [INF: "not positional"].
    static constexpr uint32_t kFxFlag2D = 0x10;

public:
    CinematicSounds();
    ~CinematicSounds();

    // Also opens the language bank "<bank path without .bnk>__<lang>.bnk"
    // (gpConfig language, fallback "us") when it exists (voices).
    bool load( const Bank &bank );
    void unload();

    // Bank / cinematic frame -> world (e.g. IntroCinematic sceneOrigin).
    void setSceneOrigin( const Matrix4x4f &localToWorld ) { sceneOrigin = localToWorld; }
    inline const Matrix4x4f &getSceneOrigin() const { return sceneOrigin; }

    // Sounds of a Fx__Root (by hashcode) / of an Entity__Fx (by name, with
    // its position).
    const std::vector<Sound> *findRoot( uint64_t rootHashcode );
    const std::vector<Sound> *findEmitter( const char *pName );

    // Plays the Fx events of the cinematic crossed in (previousTime, time]
    // (late starts are offset in the clip). Positions: record matrix
    // translation (cinematic frame) through the scene origin.
    // pOutVoices: the voices started (stopped with the cinematic).
    void update( const Cinematic &cinematic, float previousTime, float time,
                 std::vector<GSAudio::VoiceHandle> *pOutVoices = nullptr );
    // Fx tracks (0x7846) of the .sce objects (e.g. Tess voices in
    // M_TESS_Tess.sce): 0x54 records { u32 RefIndex (.sce REFS, type 0x010d
    // u64 Fx__Root); u32 Flags; f32; f32 Duration; f32; f32 Matrix[16] },
    // crossed in (previousSceneTime, sceneTime]. sceneToWorld maps the raw
    // object track positions to the world (e.g. sceneOrigin *
    // scene.getOrigin()). Position = object position + record translation
    // [INF: record matrix relative to the object, identity in the data].
    void updateSceneFx( const CinematicScene &scene, float previousSceneTime, float sceneTime,
                        const Matrix4x4f &sceneToWorld, std::vector<GSAudio::VoiceHandle> *pOutVoices = nullptr );
    // pWorldPosition: world position overriding the emitter one; fxFlags:
    // record flags (kFxFlag2D).
    void play( const std::vector<Sound> &sounds, float offset = 0.0f, bool bLoop = false,
               std::vector<GSAudio::VoiceHandle> *pOutVoices = nullptr, const Vector3f *pWorldPosition = nullptr,
               uint32_t fxFlags = 0 );
    void stop( std::vector<GSAudio::VoiceHandle> &handles );
    void stopAll();

private:
    struct SndNode {
        std::string Name;
        uint64_t Rsc = 0;
        uint32_t LoopCount = 0;
        Sound Params;
    };
    struct Bfx {
        bool bLanguageSwitch = false;
        std::vector<SndNode> Nodes;
    };

    const Bank *pBank = nullptr;
    std::unique_ptr<Bank> pLanguageBank;
    BankHashcodes hashcodes;
    BankHashcodes languageHashcodes;
    std::string languagePrefix; // "US_"
    Matrix4x4f sceneOrigin;
    uint32_t randomSeed = 12345; // SC emitter +0x114 seed

    std::unordered_map<uint64_t, uint64_t> rootSources;             // Fx__Root -> Fx__SourceData
    std::unordered_map<uint64_t, std::pair<uint64_t, std::vector<uint64_t>>> sources; // bfx rsc, sound nodes
    std::unordered_map<uint64_t, uint64_t> soundFiles;              // Fx__Sound(__<LANG>) -> wav rsc
    std::unordered_map<std::string, std::pair<uint64_t, Vector3f>> emitters; // Entity__Fx name -> Fx__Root, pos
    std::unordered_map<uint64_t, std::vector<Sound>> resolved;
    std::unordered_map<std::string, std::vector<Sound>> resolvedEmitters;
    std::vector<GSAudio::VoiceHandle> voices;

private:
    static bool ParseBfx( const uint8_t *pData, size_t size, Bfx &outBfx );
    const BankEntry *findWav( uint64_t rsc ) const;
    float randomUnit();
    float randomVolume( float randomness, float volume );
    float randomPitch( float randomness, float pitch );
};
