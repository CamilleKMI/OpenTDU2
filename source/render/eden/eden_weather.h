#pragma once

#include "core/math.h"

// Weather presets (islands/<island>/fx/game_weathers.xmb and
// cinematic_weathers.xmb):
//   WEATHER_EXPORT { EXPORT_WEATHER weather[] }
//   EXPORT_WEATHER { String name; Int32 hour, minutes, numberOfIntemperies;
//                    EXPORT_INTEMPERY intemperys[] }
//   EXPORT_INTEMPERY { EXPORT_PARAM weather_params[] }
//   EXPORT_PARAM { String name; Object param } (float1..float4, STRING_PARAM)
// Parameters: sundirection, sun_diffuse/specular/ambient, fog_color,
// fog_start/end/max, Skydome_*, clouds_*, ocean_*, car_*, photoshop_* (post
// processing)...
struct WeatherPreset {
    std::string Name;
    int32_t Hour = 12;
    int32_t Minutes = 0;
    // Merged view (global block + first intempery), what every consumer reads.
    std::unordered_map<std::string, Vector4f> Values;
    std::unordered_map<std::string, std::string> Strings;
    // Raw blocks of the file (WeatherSystem input): file intempery 0 = global
    // parameters (registry 0x00f84dc8: sundirection, tunnel_*), file
    // intemperies 1..n = the weather types (registry 0x00f84dd8) [CODE
    // FUN_00afc4d0].
    struct Block {
        std::unordered_map<std::string, Vector4f> Values;
        std::unordered_map<std::string, std::string> Strings;
    };
    Block Global;
    std::vector<Block> Intemperies;

    Vector4f get( const char *pName, const Vector4f &defaultValue = Vector4f() ) const;
    float getFloat( const char *pName, float defaultValue = 0.0f ) const;
    Vector3f getVector3( const char *pName, const Vector3f &defaultValue = Vector3f() ) const;
    std::string getString( const char *pName, const char *pDefaultValue = "" ) const {
        auto it = Strings.find( pName );
        return it != Strings.end() ? it->second : std::string( pDefaultValue );
    }
    Vector4f getVector4( const char *pName, const Vector4f &defaultValue ) const;
};

// Weather modifiers (islands/<island>/fx/cinematic_modifiers.xmb, the
// "wmod:<name>" cinematic messages):
//   MODIFIER_EXPORT { EXPORT_MODIFIER modifier[] }
//   EXPORT_MODIFIER { String name; Int32 type (1); String weather_name;
//                     Int32 intempery_index (-1: global, 0...);
//                     EXPORT_PARAM param[] }
// A modifier overrides parameters of the weather it is defined for.
class WeatherLibrary {
public:
    bool load( const char *pPath );
    bool loadModifiers( const char *pPath );
    const WeatherPreset *find( const char *pName ) const;
    // Weather with a modifier applied (cached); the weather itself when the
    // modifier does not exist for it.
    const WeatherPreset *findModified( const char *pWeather, const char *pModifier );
    inline const std::vector<WeatherPreset> &getPresets() const { return presets; }

private:
    struct Modifier {
        std::string Name;
        std::string WeatherName;
        int32_t IntemperyIndex = -1;
        std::unordered_map<std::string, Vector4f> Values;
        std::unordered_map<std::string, std::string> Strings;
    };

    std::vector<WeatherPreset> presets;
    std::vector<Modifier> modifiers;
    std::unordered_map<std::string, std::unique_ptr<WeatherPreset>> modifiedPresets;
};

// GSWeather time of day / weather type evaluation (TestDrive2.exe
// GSWeather.cpp, FUN_00af8f60 and helpers), see docs/re/weather_spec.md.
//
// game_weathers.xmb holds 24 time keys ("00" ... "23.20", file order =
// time order), each with a global block and 6 intemperies:
//   0 Sunny, 1 Cloudy, 2 Rainny, 3 Before_rain, 4 After_rain, 5 Tempest
// (exe names, table 0x00f7e08c, "Max_intempery" = 6).
//
// - Keys (FUN_00af7d40): A = the last key whose time <= now, B = the next
//   one (wrapping to the first).
// - Key blend (FUN_00af7aa0): t = (now - A) / (B - A), with B (and now when
//   its hour is below A's) + 86400 s when B's hour < A's; 0 if outside
//   [0, 1].
// - Parameters (FUN_00af1ac0): floats lerp(A, B, t), strings A if t <= 0.5
//   else B, photoshop_crappymode_enable (mode 10) lerp by floor(t + 0.5).
// - Weather type: the forecast gives one intempery per 2 h slice; source =
//   slice of the current hour, destination = next slice. Drawn weather =
//   W(source) blended to W(destination) by the same FUN_00af7aa0 factor
//   between the two slice start times (FUN_00af8f60 "Bilinear Generated
//   Weather"). Offline the slice table is all Sunny (FUN_00af82d0).
// - Clock (FUN_00af8ce0): time += 9.6 * dt (0x00f4ae88, set by
//   FUN_00afb1e0; 480 in game mode 0x14), i.e. 24 h of game time in 9000 s
//   of real time (default day length, FUN_00c1f390).
class WeatherSystem {
public:
    static constexpr int32_t kNumIntemperies = 6;
    static constexpr float kGameTimeSpeed = 9.6f; // 0x00f4ae88
    static constexpr float kDaySeconds = 86400.0f;
    static const char *GetIntemperyName( int32_t intempery );

    // Key presets of game_weathers.xmb (WeatherLibrary::load keeps the
    // blocks). The library must outlive the system.
    void setLibrary( const WeatherLibrary *pLibrary );
    inline bool isReady() const { return pLibrary != nullptr && !pLibrary->getPresets().empty(); }

    // Time of day in seconds [0, 86400).
    void setTimeOfDay( float seconds );
    inline float getTimeOfDay() const { return timeOfDay; }
    inline float getHours() const { return timeOfDay / 3600.0f; }
    // Game seconds per real second (0: frozen clock).
    inline void setTimeSpeed( float speed ) { timeSpeed = speed; }
    inline float getTimeSpeed() const { return timeSpeed; }
    // Source (current 2 h slice) and destination (next slice) intemperies.
    void setIntemperies( int32_t source, int32_t destination );
    inline int32_t getIntempery() const { return sourceIntempery; }
    inline int32_t getNextIntempery() const { return destinationIntempery; }
    // Fixed preset (cinematic / specific weathers) in place of the time of
    // day evaluation; nullptr: back to the time of day.
    inline void setOverride( const WeatherPreset *pPreset ) { pOverride = pPreset; }
    inline const WeatherPreset *getOverride() const { return pOverride; }

    // Advances the clock by realDelta * speed and evaluates the weather.
    // Returns the game time elapsed (s), used by the cloud rotation.
    float tick( float realDelta );
    void evaluate();

    // Weather drawn this frame (getCurrent()) and its inputs.
    inline const WeatherPreset &getCurrent() const { return pOverride != nullptr ? *pOverride : current; }
    inline const WeatherPreset *getKeyA() const { return pKeyA; }
    inline const WeatherPreset *getKeyB() const { return pKeyB; }
    inline float getKeyBlend() const { return keyBlend; }
    // Forecast blend source -> destination (GSWeather +0x28, DAT_0117a050).
    inline float getForecastBlend() const { return forecastBlend; }
    // clouds_cloud3dname of the source / destination weathers.
    inline const std::string &getSourceClouds() const { return sourceClouds; }
    inline const std::string &getDestinationClouds() const { return destinationClouds; }

    // FUN_00af7aa0 (times in seconds of the day, hours truncated).
    static float BlendFactor( float a, float b, float now );

private:
    const WeatherLibrary *pLibrary = nullptr;
    const WeatherPreset *pOverride = nullptr;
    const WeatherPreset *pKeyA = nullptr;
    const WeatherPreset *pKeyB = nullptr;
    float timeOfDay = 14.0f * 3600.0f;
    float timeSpeed = 0.0f;
    float keyBlend = 0.0f;
    float forecastBlend = 0.0f;
    int32_t sourceIntempery = 0;
    int32_t destinationIntempery = 0;
    std::string sourceClouds;
    std::string destinationClouds;
    WeatherPreset current;

    void evaluateIntempery( int32_t intempery, WeatherPreset::Block &outBlock ) const;
};
