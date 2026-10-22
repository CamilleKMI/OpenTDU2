#include "shared.h"
#include "gm_base.h"

namespace {
constexpr const char *kGameModeNames[] = {
    "NONE", "LOGIN", "VIDEOBUMPER", "AVATAR_CHOICE", "FREERIDE", "CARSHOWCASE", "CINVIEWER", "TESTCAR", "FIRST_WM",
};
static_assert( sizeof( kGameModeNames ) / sizeof( kGameModeNames[0] ) ==
                   static_cast<size_t>( eGameMode::GM_Count ),
               "Game mode names mismatch" );
} // namespace

const char *GetGameModeName( eGameMode gameMode ) {
    const int32_t index = static_cast<int32_t>( gameMode );
    if ( index < 0 || index >= static_cast<int32_t>( eGameMode::GM_Count ) ) {
        return "UNKNOWN";
    }
    return kGameModeNames[index];
}

eGameMode GetGameModeFromName( const char *pName ) {
    for ( int32_t i = 0; i < static_cast<int32_t>( eGameMode::GM_Count ); i++ ) {
        if ( StringCmpI( kGameModeNames[i], pName ) == 0 ) {
            return static_cast<eGameMode>( i );
        }
    }
    if ( StringCmpI( pName, "intro" ) == 0 ) {
        return eGameMode::GM_AvatarChoice;
    }
    return eGameMode::GM_None;
}
