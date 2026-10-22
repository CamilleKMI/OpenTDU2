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
    std::unordered_map<std::string, Vector4f> Values;
    std::unordered_map<std::string, std::string> Strings;

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
