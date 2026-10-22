#pragma once

#include "config/ini_file.h"
#include "game/gs_base.h"

struct VideoSettings {
    int32_t Width = 1280;
    int32_t Height = 720;
    bool bWindowed = true;
    bool bVSync = true;
    // SETSCREENRATIO (GameConfig+0x40, FUN_00d66858): RATIO_16_9 -> 1,
    // RATIO_4_3 -> 2, anything else -> 0 (literal reading of the code).
    int32_t ScreenRatio = 0;
    // QUALITY_SETTINGS_LOD environment level (savegame/SystemDefault; the
    // game uses level 1 without saved settings, FUN_00d6f950). OpenTDU2
    // default: 3 (max), -envquality n.
    int32_t EnvironmentQuality = 3;
    // [ENVIRONMENT_QUALITY_LEVEL_n] of Quality_Settings.cpr.
    float GlobalLodFactor = 1.0f;     // SET_GLOBAL_LOD_FACTOR a (DAT_00f7f5f8)
    int32_t DefNbSectors = 5;         // SetDefNbSectors
};

// Configuration service. Reads the original configuration files (encrypted
// .cpr in the game root): SystemPC.cpr (video), GamePC.cpr (game rules),
// Physics.cpr (vehicle dynamics). Command line switches override them.
class GSConfig : public GameSystem {
public:
    inline const VideoSettings &getVideoSettings() const { return video; }
    inline const IniFile &getSystemConfig() const { return systemConfig; }
    inline const IniFile &getGameConfig() const { return gameConfig; }
    inline const IniFile &getPhysicsConfig() const { return physicsConfig; }
    inline const IniFile &getQualityConfig() const { return qualityConfig; }
    inline const std::string &getLanguage() const { return language; }

public:
    GSConfig();
    ~GSConfig();

    const char *getName() const override { return "Service : Config"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void terminate() override {}
    void drawDebug() override;

private:
    IniFile systemConfig;
    IniFile gameConfig;
    IniFile physicsConfig;
    IniFile qualityConfig;
    VideoSettings video;
    std::string language;
};

extern GSConfig *gpConfig;
