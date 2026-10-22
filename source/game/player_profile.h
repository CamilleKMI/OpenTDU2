#pragma once

#include <string>

// Player profile data shared between the game modes (profile+0x30 avatar
// configuration, profile+0xc1 name; the save game itself is not
// implemented).
struct PlayerProfile {
    std::string AvatarConfig; // DBPNJ _configid (AVT_<Male|Female>[_Asia|_Afrik]_Intro)
    std::string Name;
    int32_t StoryStep = 0;    // profile+0x58 (FUN_00b10da0)
};

PlayerProfile &GetPlayerProfile();
