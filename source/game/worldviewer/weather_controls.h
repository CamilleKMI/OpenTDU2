#pragma once

#include "render/eden/eden_weather.h"

#include <string>
#include <vector>

class EdenSceneView;

// Time of day / weather of the OpenTDU2 developer tools (WORLD_VIEWER,
// FREE_DRIVE): the GSWeather evaluation of the island game_weathers.xmb
// (WeatherSystem: time keys x intemperies, forecast slices), the fixed
// presets of specific_weathers.xmb / cinematic_weathers.xmb, the rain
// switch, and the ImGui panel that drives them.
//
// Command line (applied by the first applyArguments call only; later calls,
// e.g. after an island switch, keep the clock and drop the fixed preset):
// -time hh:mm[:ss] (default 14:00), -timespeed x (game seconds per second,
// default 0 = frozen), -intempery / -nextintempery <name or 0..5>, -weather
// <game_weathers key ("14", "21.30": sets the time) or fixed preset>, -rain.
class WeatherControls {
public:
    // Loads islands/<island>/fx/game_weathers.xmb and specific_weathers.xmb.
    void load( const std::string &island );
    // pLogTag: prefix of the warnings ("WORLD_VIEWER", "FREE_DRIVE").
    void applyArguments( EdenSceneView &view, const char *pLogTag );
    // Weather of the frame before the first tick: the evaluated weather, else
    // the first cinematic weather of the island (no game_weathers.xmb).
    const WeatherPreset *getInitialWeather( EdenSceneView &view );
    // Clock + weather (FUN_00af8ce0 / FUN_00af8f60): the evaluated weather,
    // the forecast cloud sets, the game time of the frame and the rain are
    // given to the view. Returns the weather of the frame.
    const WeatherPreset *tick( float deltaTime, EdenSceneView &view );
    void drawPanel( EdenSceneView &view );

    inline const WeatherPreset *getWeather() const { return pWeather; }
    inline WeatherSystem &getSystem() { return weatherSystem; }
    inline bool isRainEnabled() const { return bRain; }

private:
    WeatherLibrary gameWeathers;
    WeatherLibrary specificWeathers; // specific_weathers.xmb (fixed presets)
    WeatherSystem weatherSystem;
    const WeatherPreset *pWeather = nullptr;
    int32_t selectedPreset = -1; // -1: time of day, else index in getPresetList()
    float timeSpeed = 0.0f;
    bool bRain = false; // RAIN (rain engineer): panel checkbox, default off
    bool bArgumentsApplied = false;

    // Fixed presets of the panel list: specific_weathers.xmb then
    // cinematic_weathers.xmb.
    std::vector<const WeatherPreset *> getPresetList( EdenSceneView &view ) const;
};
