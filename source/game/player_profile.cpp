#include "shared.h"
#include "player_profile.h"

#include <algorithm>
#include <cmath>

PlayerProfile &GetPlayerProfile() {
    static PlayerProfile profile;
    return profile;
}

CineRequest &GetCineRequest() {
    static CineRequest request;
    return request;
}

TestDriveRequest &GetTestDriveRequest() {
    static TestDriveRequest request;
    return request;
}

void ProfileAddMoney( int32_t amount, bool bPending, int32_t reason ) {
    // FUN_00abe0e0 [CODE]: statistics +0xf05c[reason] += amount (reason <
    // 0x1c); bPending 0: money = max(money + amount, 0) (FUN_0041b580 /
    // FUN_004671e0 [ENC accessors]), profile saved (FUN_00acee30); bPending
    // 1: pending += amount (FUN_004aaf70 / FUN_004aafd0 [ENC accessors]).
    // The millionaire checks (money + amount > 1 000 000, +0x151) are not
    // implemented.
    PlayerProfile &profile = GetPlayerProfile();
    if ( reason >= 0 && reason < 28 ) {
        profile.MoneyEarned[reason] += amount;
    }
    if ( bPending ) {
        profile.PendingMoney += amount;
    } else {
        profile.Money = std::max( profile.Money + amount, 0 );
    }
}

void ProfileStartMoneyTransfer() {
    // FUN_00abd0d0: HUD "money" element shown (FUN_00d84110 / FUN_00d84a70),
    // money object +0x50 = 1.0 (DAT_00edf668) [CODE].
    GetPlayerProfile().MoneyTransferTimer = 1.0f;
}

void ProfileTickMoney( float deltaTime ) {
    // FUN_00abcf60(money object, dt) [CODE].
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.MoneyTransferTimer < 0.0f ) {
        return;
    }
    int32_t rate = 0;
    if ( profile.MoneyTransferTimer != 0.0f ) {
        rate = static_cast<int32_t>( std::lround( static_cast<float>( profile.PendingMoney ) / profile.MoneyTransferTimer ) );
    }
    int32_t step = static_cast<int32_t>( std::lround( static_cast<float>( rate ) * deltaTime ) );
    if ( static_cast<float>( profile.PendingMoney ) >= 0.0f ) {
        step = std::max( std::min( step, profile.PendingMoney ), 0 );
    } else {
        step = std::max( profile.PendingMoney, std::min( step, 0 ) );
    }
    profile.Money = std::max( profile.Money + step, 0 );
    profile.PendingMoney -= step;
    profile.MoneyTransferTimer -= deltaTime;
    if ( profile.MoneyTransferTimer < 0.0f ) {
        profile.Money = std::max( profile.Money + profile.PendingMoney, 0 );
        profile.PendingMoney = 0;
        profile.MoneyTransferTimer = -1.0f; // DAT_00ede404, HUD "money" hidden
    }
}

std::string GetStoryAvatarConfig( const std::string &introConfig ) {
    // The avatar choice configurations are AVT_<sex>[_<ethnic>]_Intro: the
    // same sex and ethnic group without "_Intro" (FUN_00b105d0 builds the
    // name from the profile sex and avatar index) [CODE + DATA].
    const std::string suffix = "_Intro";
    if ( introConfig.size() > suffix.size() &&
         introConfig.compare( introConfig.size() - suffix.size(), suffix.size(), suffix ) == 0 ) {
        return introConfig.substr( 0, introConfig.size() - suffix.size() );
    }
    return introConfig.empty() ? std::string( "AVT_Male" ) : introConfig;
}
