#pragma once

#include "core/math.h"

// Real time cinematics (docs/re/intro_spec.md): a cinematic (.cin, "CIN!")
// is a timeline of shots over one or more scenes (.sce, "SCE!"), plus message
// tracks (weather, shadows, flash, sounds) and FX (sound) events.
//
// Common track layout: { u16 Kind; u16 Flags; u32 Size (whole track);
// u32 Id; u32 NumKeys; f32 Duration; f32 Times[NumKeys]; values[NumKeys] }.
// Track ids are 4 byte folded names (character i added into byte i & 3):
// "Position", "Heading" (quaternion xyzw), "Scale", "Fov" (degrees),
// "Animation", "Visibility", "Morph", "Message", "Fx".
//
// .sce DATA chunk:
//   +0x00 f32 Duration, u32 NumObjects, u32 NumCameras, u32 NumActors
//   +0x28 float[16] scene origin (D3D matrix, world space)
//   +0x68 objects: { u32 Size; u32 HeaderSize (0x3c); u32 ClassHash
//                    (0x2a043536 camera, 0xd039fa46 actor); u32 NameHash;
//                    u32 NumTracks; char Name[32]; u64 entity __Hc;
//                    tracks... }
class CinematicScene {
public:
    static constexpr uint32_t kTrackPosition = 0xd7e2d8c4;
    static constexpr uint32_t kTrackRotation = 0x64c8d3b1; // "Heading"
    static constexpr uint32_t kTrackScale = 0x6c6163b8;
    static constexpr uint32_t kTrackFov = 0x00766f46;      // "Fov"
    static constexpr uint32_t kTrackMessage = 0x73d8ccae;  // "Message"
    static constexpr uint32_t kTrackFx = 0x00007846;       // "Fx"
    static constexpr uint32_t kClassCamera = 0x2a043536;
    static constexpr uint32_t kClassActor = 0xd039fa46;

    struct Track {
        uint32_t Id = 0;
        uint32_t NumComponents = 0;
        std::vector<float> Times;
        std::vector<float> Values; // NumComponents per key (raw dwords)
    };

    struct Object {
        std::string Name;
        uint32_t ClassHash = 0;
        uint32_t NameHash = 0;
        uint64_t EntityHashcode = 0;
        std::vector<Track> Tracks;

        inline bool isCamera() const { return ClassHash == kClassCamera; }
        const Track *findTrack( uint32_t id ) const;
        Vector3f samplePosition( float time ) const;
        Quaternionf sampleRotation( float time ) const;
        float sampleFloat( uint32_t trackId, float time, float defaultValue ) const;
    };

    // REFS entry: { u16 Type (0x0302 ANIM, 0x0306 MORPH); u16 Size; u64
    // hashcode (bank resource); char name[] }.
    struct Reference {
        uint16_t Type = 0;
        uint64_t Hashcode = 0;
        std::string Name;
    };

public:
    bool parseScene( const uint8_t *pData, size_t size );
    inline const std::vector<Reference> &getReferences() const { return references; }

    inline float getDuration() const { return duration; }
    inline const Matrix4x4f &getOrigin() const { return origin; }
    inline const std::vector<Object> &getObjects() const { return objects; }
    const Object *findObject( const char *pName ) const;
    const Object *findObjectByHash( uint32_t nameHash ) const;

private:
    float duration = 0.0f;
    Matrix4x4f origin;
    std::vector<Object> objects;
    std::vector<Reference> references;
};

// .cin:
//   "CIN!" u32 8, u32 0xa0, u32 0x107
//   "REFS" { u32 Size; u32 NumRefs; { u16 Type; u16 EntrySize; body } }:
//     0x020b message "<4cc>:<arg>", 0x010d FX (u64 Fx__Root hashcode),
//     0x0303 scene (u64 hashcode + .sce source path)
//   "DATA" { u32 Size; f32 Duration; ...; +0x0c u32 NumScenes; +0x30 u32
//     NumRefs; +0x3c { u32 1; u32 RefIndex }[NumScenes]; tracks }
//   Shot track (Id 0): 0x3c records { f32 Duration; u32 Scene; u32
//     CameraNameHash; f32 CameraStart; f32 CameraEnd; ... }, times = global
//     shot start times.
//   Message tracks: { u32 RefIndex; f32 Duration }.
//   Fx tracks: 0x54 records { u32 RefIndex; u32 Flags; f32; f32 Duration;
//     f32; f32 World[16] (D3D) }.
class Cinematic {
public:
    struct Reference {
        uint16_t Type = 0;
        std::string Text;    // message "weat:SS_Death_Valley" or scene path
        uint64_t Hashcode = 0;
    };

    struct Shot {
        float Start = 0.0f;
        float Duration = 0.0f;
        uint32_t Scene = 0;
        uint32_t CameraHash = 0;
        float CameraStart = 0.0f;
        float CameraEnd = 0.0f;
        // +0x14 / +0x18 / +0x1c: start transition (1 = fade from a colour,
        // duration, RGB) [INF: C_INTRO 4 s from black, C_FIESTA_INTRO 1.5 s
        // from white].
        uint32_t FadeType = 0;
        float FadeDuration = 0.0f;
        Vector3f FadeColor;

        // Scene time of a cinematic time in this shot: the camera range is
        // mapped on the shot duration (rate 0.2 = the "time stop" slow
        // motion of C_INTRO's last shot and C_AVATAR).
        float getSceneTime( float time ) const {
            const float rate = Duration > 0.0f ? ( CameraEnd - CameraStart ) / Duration : 1.0f;
            return CameraStart + Max( time - Start, 0.0f ) * rate;
        }
    };

    struct Event {
        float Time = 0.0f;
        float Duration = 0.0f;
        int32_t Reference = -1;
        uint32_t Track = 0;     // message track index (several per cinematic)
        uint32_t Flags = 0;     // Fx
        Matrix4x4f World;       // Fx (OpenTDU2 convention)
    };

public:
    bool parse( const uint8_t *pData, size_t size );

    inline float getDuration() const { return duration; }
    inline const std::vector<Reference> &getReferences() const { return references; }
    inline const std::vector<int32_t> &getSceneReferences() const { return sceneReferences; }
    inline const std::vector<Shot> &getShots() const { return shots; }
    inline const std::vector<Event> &getMessages() const { return messages; }
    inline const std::vector<Event> &getFxEvents() const { return fxEvents; }
    const Shot *getShot( float time ) const;

    // Last message starting with pPrefix ("weat:") at a time (its argument),
    // nullptr if none. pOutTime: time of that message.
    const std::string *getLastMessage( const char *pPrefix, float time, float *pOutTime = nullptr ) const;

    // Active camera: world position (D3D space), orientation and field of
    // view (radians).
    bool getCamera( const std::vector<CinematicScene> &scenes, float time, Vector3f &outPosition,
                    Quaternionf &outRotation, float &outFov ) const;

private:
    float duration = 0.0f;
    std::vector<Reference> references;
    std::vector<int32_t> sceneReferences;
    std::vector<Shot> shots;
    std::vector<Event> messages;
    std::vector<Event> fxEvents;
};

// 4 byte folded name (Eden cinematic identifiers).
uint32_t CinematicNameHash( const char *pName );
