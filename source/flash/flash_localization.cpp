#include "shared.h"
#include "flash_localization.h"

#include "database/gs_database.h"
#include "flash/flash_player.h"
#include "core/xmb.h"

namespace {
constexpr const char *kCategories[] = { "casino", "challenge", "club", "common", "drive_in", "general", "gme",
                                         "home",   "hud",       "ice",  "main",   "map",      "shops", "tdmedia" };

std::string ReplaceAll( std::string text, const std::string &from, const std::string &to ) {
    size_t position = 0;
    while ( ( position = text.find( from, position ) ) != std::string::npos ) {
        text.replace( position, from.size(), to );
        position += to.size();
    }
    return text;
}
} // namespace

void ApplyFlashLocalization( FlashPlayer &player ) {
    if ( gpDatabase == nullptr || !player.isLoaded() ) {
        return;
    }
    // "common" strings are present in every movie: intro.vmf (SetBDD:
    // db_home only) shows m_Garage_ / m_EnterCar_ (category common) in the
    // walk mode [DATA: the original's "Garage" low bar].
    std::vector<std::string> enabled = { "_common" };
    for ( const char *pCategory : kCategories ) {
        const std::string value = player.getVariable( std::string( "/:db_" ) + pCategory );
        if ( ( value == "1" || value == "true" ) && strcmp( pCategory, "common" ) != 0 ) {
            enabled.push_back( std::string( "_" ) + pCategory );
        }
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    uint32_t count = 0;
    for ( uint32_t i = 0; i < root.getArraySize( "DBLoc" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBLoc", i );
        bool bUsed = false;
        for ( const std::string &field : enabled ) {
            bUsed |= row[field.c_str()].asInt() != 0;
        }
        const std::string variable = row["_flash"].asString();
        if ( !bUsed || variable.empty() ) {
            continue;
        }
        const char *pText = gpDatabase->getString( static_cast<int32_t>( row["_hashcode"].asUInt64() ) );
        if ( pText == nullptr ) {
            continue;
        }
        // §GAME_NAME§ -> "TDU2" (DAT_00f18f8c).
        player.setVariable( "/:" + variable, ReplaceAll( pText, "\xc2\xa7GAME_NAME\xc2\xa7", "TDU2" ) );
        count++;
    }
    OTDU_LOG_DEBUG( "Flash localization: %u strings\n", count );
}
