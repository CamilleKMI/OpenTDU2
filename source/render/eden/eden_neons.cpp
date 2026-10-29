#include "shared.h"
#include "eden_neons.h"

#include "render/eden/eden_renderer.h"
#include "render/eden/eden_weather.h"

namespace {
constexpr uint64_t kTD_NeonDecal = 0x63078165ca37416dull;
constexpr uint64_t kTD_PubNeon = 0x18315cacac6cff57ull;
constexpr uint64_t kSpriteShader = 0xfcdaa96bdeadbeefull;
constexpr uint64_t kNeonParameter = 0x97909ca3a58487f1ull; // name not found
constexpr uint32_t kSpecU4 = 0x23;                          // MAT. +0x333

// Readable LCG (0xc8f8af, 0xca0426, 0xdf54a9...) [CODE]; FUN_005abe00 is
// [ENC], assumed identical [INF].
float NextRandom( uint32_t &seed ) {
    seed = seed * 0x19660du + 0x3c6ef35fu;
    const uint32_t bits = ( seed & 0x7fffffu ) | 0x3f800000u;
    float value;
    memcpy( &value, &bits, sizeof( value ) );
    return value - 1.0f;
}

Vector4f OneHot( float phase ) {
    // floor (CRT import) then _ftol (FUN_006c8f00) [CODE 0x982205]
    const int32_t index = Clamp( static_cast<int32_t>( std::floor( static_cast<double>( phase ) ) ), 0, 3 );
    Vector4f value( 0.0f, 0.0f, 0.0f, 0.0f );
    ( &value.x )[index] = 1.0f; // DAT_00edf668
    return value;
}
} // namespace

namespace EdenNeons {
const uint64_t kAnimValue = GetIdentifier64bit( "AnimValue" );
const uint64_t kWeatherEmissive = GetIdentifier64bit( "WeatherEmissive" ); // 0x45979b91a7948ea4

bool IsNeonSetMaterial( const EdenMaterial &material ) {
    return material.ShaderSetGUID == kTD_NeonDecal || material.ShaderSetGUID == kTD_PubNeon ||
           ( material.ShaderSetGUID == kSpriteShader && material.Spec[kSpecU4] != 0 );
}

bool IsBlinkMaterial( const EdenMaterial &material ) {
    // FUN_00985e40 0x985ea2..0x985ef0 [CODE]
    return ( ( material.ShaderSetGUID == kTD_NeonDecal || material.ShaderSetGUID == kTD_PubNeon ) &&
             material.Spec[kSpecU4] == 0 ) ||
           ( material.ShaderSetGUID == kSpriteShader && material.Spec[kSpecU4] != 0 );
}

void ApplyStrength( EdenMaterial &material, float strength ) {
    auto emissive = material.Parameters.find( kWeatherEmissive );
    if ( emissive != material.Parameters.end() ) {
        emissive->second = Vector4f( strength, strength, strength, 0.0f );
    }
    auto neon = material.Parameters.find( kNeonParameter );
    if ( neon != material.Parameters.end() ) {
        neon->second.x = strength;
        neon->second.y = 0.0f;
    }
}

bool GetStrength( const WeatherPreset *pWeather, float &outStrength ) {
    // FUN_009834d0: weather value 0x9ba1f0e6e2dce8f3 (building_neon_strength) +0x18
    if ( pWeather == nullptr ) {
        return false;
    }
    auto it = pWeather->Values.find( "building_neon_strength" );
    if ( it == pWeather->Values.end() ) {
        return false;
    }
    outStrength = it->second.x;
    return true;
}

void Blink::initialize( uint32_t key ) {
    // FUN_00985e40 0x985f81..0x986083 [CODE]: doubles 0.7 (0xf49eb8), 0.3
    // (0xf4a178), 3.0 (0xf49a50), 4.0 (0xf49a68).
    uint32_t seed = key * 0x19660du + 0x3c6ef35fu;
    const float r1 = NextRandom( seed );
    const float r2 = NextRandom( seed );
    Speed = static_cast<float>( ( static_cast<double>( r1 ) * 0.7 + 0.3 ) * 3.0 );
    Phase = static_cast<float>( static_cast<double>( r2 ) * 4.0 );
    Value = OneHot( Phase );
}

void Blink::update( float deltaTime ) {
    // FUN_009821a0 [CODE]: fmod( speed * dt + phase, 4.0 ) (_CIfmod), stored
    // as float at +8.
    const float t = static_cast<float>( static_cast<double>( Speed ) * deltaTime + Phase );
    Phase = static_cast<float>( std::fmod( static_cast<double>( t ), 4.0 ) );
    Value = OneHot( Phase );
}
} // namespace EdenNeons
