#include "shared.h"
#include "player_profile.h"

PlayerProfile &GetPlayerProfile() {
    static PlayerProfile profile;
    return profile;
}
