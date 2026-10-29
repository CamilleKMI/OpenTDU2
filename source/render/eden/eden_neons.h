#pragma once

#include "core/math.h"

struct EdenMaterial;
struct WeatherPreset;

// World neon materials (TestDrive2.exe, see docs/re/neon_spec.md).
//
// Registration FUN_00985e40(mgr, model instance) [CODE], for every material
// of a model instance (spot exteriors, streamed world objects, .pmi
// instances, actors):
// - neon set (mgr+0x1bc4, FUN_0041e1e0): TD_NEONDECAL 63078165ca37416d and
//   TD_pubneon 18315cacac6cff57 (any switches), sprites fcdaa96bdeadbeef
//   with spec[0x23] (U4) != 0. Every frame (FUN_009834d0 -> FUN_009829d0)
//   WeatherEmissive = (x, x, x, 0) and 97909ca3a58487f1.xy = (x, 0), x =
//   building_neon_strength.x of the weather.
// - blink entry (mgr+0x1bb0, 0x20 bytes): TD_NEONDECAL / TD_pubneon with U4
//   == 0, sprites with U4 != 0. A parameter block holding one 'AnimValue'
//   (name list 0xf2c7c8) is created for the shader and attached to (material,
//   model instance) (FUN_008ad580, FUN_00448310 [ENC]): one state per model
//   instance and material.
//     seed  = block pointer * 0x19660d + 0x3c6ef35f
//     speed = (r1 * 0.7 + 0.3) * 3.0     (entry +4, 0.9 .. 3.0 per second)
//     phase = r2 * 4.0                   (entry +8, 0 .. 4)
//     AnimValue = one hot vector, component floor(phase) = 1.0 (0xedf668)
//   r1, r2: FUN_005abe00(&seed) [ENC], taken as the LCG of the readable code
//   (seed = seed * 0x19660d + 0x3c6ef35f, float = (seed & 0x7fffff |
//   0x3f800000) - 1) [INF]. The original seeds from a heap pointer (not
//   reproducible); OpenTDU2 seeds from a stable key.
// - update FUN_009821a0(dt) for every entry, after the neon set:
//     phase = fmod( speed * dt + phase, 4.0 ); AnimValue = onehot( floor( phase ) )
//   No weather dependency (the weather only scales WeatherEmissive).
// Shaders [SHADER]: TD_NEONDECAL ps 3/4/10/11 and TD_pubneon ps (U4 = 0
// permutations): emissive = dp4( glow texture rgba, AnimValue ) *
// rgbExposure_EmissiveChannel * WeatherEmissive * __Material_Emissive: the
// four glow channels are four neon segments lit one after the other.
// TD_pubneon materials have no AnimValue in their 2DM [DATA]: unregistered,
// they draw no glow at all.
namespace EdenNeons {
extern const uint64_t kAnimValue;   // GetIdentifier64bit( "AnimValue" ) = 0x554c41564d494e86
extern const uint64_t kWeatherEmissive;

bool IsNeonSetMaterial( const EdenMaterial &material );
bool IsBlinkMaterial( const EdenMaterial &material );
// FUN_009829d0: only the parameters the material has.
void ApplyStrength( EdenMaterial &material, float strength );
// building_neon_strength.x; false when the weather has no value (materials
// are then left as they are).
bool GetStrength( const WeatherPreset *pWeather, float &outStrength );

struct Blink {
    float Speed = 0.0f;
    float Phase = 0.0f;
    Vector4f Value;
    // 'key': stable seed source (the original uses the parameter block
    // pointer).
    void initialize( uint32_t key );
    void update( float deltaTime );
};
} // namespace EdenNeons
