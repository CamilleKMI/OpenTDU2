#pragma once

#include "audio/gs_audio.h"
#include "game/avatar/facefx.h"
#include "render/eden/eden_animation.h"

class Bank;
class BankHashcodes;
class CinematicSounds;
class EdenAvatar;
class XmbDocument;

// Event passenger reactions (docs/re/story_spec.md 4): an
// Entity__AnimationHandler of the event pack ("E%02d_%s", e.g. E13_TESS of
// pack_events13.bnk) holds AnmRef { File[0] .bas, File_trk[0] .trk }; the
// story / event code plays them on the passenger PNJ with FUN_00a15430(first,
// count) -> FUN_00a151c0 [CODE]:
//   - index = random(count) + first (FUN_0095b3e0: seed (DAT_01025608 +
//     0x9bb0) = seed * 0x7ff8a3ed + 0x2aa01d31, range mapping FUN_006c8f00
//     [ENC -> INF: (seed >> 16) % count]); if that one was already played
//     (flags DAT_00fd944c[index]) the first unplayed one of the family; all
//     played: flags cleared, random again;
//   - played only while the passenger's first anim layer is idle (+0xd0 bit
//     1, FUN_00d96e60) [CODE] (here: no reaction playing [INF]); the clip
//     start passes (-1, -1, 0.3) (DAT_0144b610..618 = DAT_00ede404 /
//     DAT_00ede42c) [CODE] -> 0.3 s blend from / back to the seat pose [INF].
// .bas: "BAS!" with one REFS entry, the .xan of the clip (a .anm of the bank).
// .trk ("EVT!" u32 8, u32 0x71, u32 0x666; "DATA" { u32 size; u32 numItems;
// f32 duration; f32 duration; 16 bytes 0; items { u32 Size; char Tag[4];
// body } }; "REFS" { u32 size; u32 count; { u16 type; u16 size; u32 1; u32
// item offset; body } }) [DATA]:
//   "MORF" { u32 n; f32 duration; f32 times[n]; { u32 ref; f32 length }[n] }:
//          FaceFX animations (REFS 0x0302 "...\female.fxa::<group>:<anim>");
//   "FXS!" { u32 1; f32 start; f32 start; u32 1; u32 ref; u32 flags; u32 0;
//          f32 duration; f32 matrix[16] }: sounds (REFS 0x0004 u64 Fx__Root).
// The track player of AnmRef File_trk is not identified [INF: started with
// the clip, voices 2D at the start times, FaceFX like the .sce Morph track].
class EventReactions {
public:
    bool load( const Bank &bank, const BankHashcodes &hashcodes, const XmbDocument &entities, const char *pHandler );
    void unload();
    inline size_t getCount() const { return reactions.size(); }
    inline bool isLoaded() const { return !reactions.empty(); }

    // FUN_00a15430(first, count): -1 if nothing was started.
    int32_t play( int32_t first, int32_t count, CinematicSounds &sounds );
    inline bool isPlaying() const { return current >= 0; }
    inline int32_t getCurrent() const { return current; }
    const std::string &getName( int32_t index ) const;
    void stop( CinematicSounds &sounds );

    void tick( float deltaTime, CinematicSounds &sounds );
    // Clip of the playing reaction and its time; blend weight (0.3 s in /
    // out) against the seat pose.
    const EdenAnimation *getClip() const;
    inline float getTime() const { return time; }
    float getWeight() const;
    // FaceFX of the playing reaction on the local pose.
    void applyFaceFx( const FaceFxLibrary &faceFx, EdenAvatar &avatar, std::vector<BoneTransform> &locals ) const;

private:
    struct Morph {
        float Time = 0.0f;
        float Length = 0.0f;
        std::string Anim; // "...\female.fxa::EVTE_:EVTE_to_player_01"
    };
    struct Sound {
        float Start = 0.0f;
        float Duration = 0.0f;
        uint32_t Flags = 0;
        uint64_t Root = 0;
    };
    struct Reaction {
        std::string Name;
        EdenAnimation Clip;
        float Duration = 0.0f;
        std::vector<Morph> Morphs;
        std::vector<Sound> Sounds;
    };
    std::vector<Reaction> reactions;
    std::vector<uint8_t> played; // DAT_00fd944c
    uint32_t seed = 0;           // DAT_01025608 + 0x9bb0
    int32_t current = -1;
    float time = 0.0f;
    std::vector<GSAudio::VoiceHandle> voices;

    static bool ParseTrack( const uint8_t *pData, size_t size, Reaction &out );
};
