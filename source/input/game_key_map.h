#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Game key map of the keyboard (Default_KB.xmb, PlayerKeyMapFile.KeyMap[]):
// ActionName -> its key bindings {ChannelName, ChannelLocName} in file order,
// plus the OpenTDU2 user bindings (keyboards without a numpad, e.g. a Mac;
// NOT game data, docs/re/text_icons_spec.md 7 and docs/re/accessories_spec.md 2).
//
// Action ids of the game (table at 0x00f7f108, 16-byte entries {name, id,
// option loc id, 0}) [DATA]: CAR_WINDOWS 0x16, CAR_RIGHT_BLINKER 0x17,
// CAR_LEFT_BLINKER 0x18, ACCESSORIES_LEFT 0x34, ACCESSORIES_RIGHT 0x35,
// ACCESSORIES_UP 0x36, ACCESSORIES_DOWN 0x37 (queried with FUN_00d5e9f0 =
// pressed this frame) [CODE].
struct GameKeyBinding {
    std::string Channel;    // ChannelName (a ChannelNameAND combo is kept: not a single key)
    std::string ChannelAnd; // ChannelNameAND ("" = single key)
    std::string LocName;    // ChannelLocName (the orange [key] text of #command:)
    bool bUser = false;     // OpenTDU2 binding (not in Default_KB.xmb)
    // OpenTDU2 binding only active while the accessories menu (HUD/ig_menu)
    // is open, so the main-row digit keeps its Default_KB action otherwise.
    bool bAccessoryMenuOnly = false;
};

using GameKeyMap = std::unordered_map<std::string, std::vector<GameKeyBinding>>;

// Loaded once (Default_KB.xmb of the game root).
const GameKeyMap &GetGameKeyMap();

// '#command:<ACTION>#' (FUN_00d87cc0): keyboard form
// "#color:ff6600#[<ChannelLocName>]#endcolor#" of the first key map entry of
// the action (docs/re/text_icons_spec.md 1 [CODE]); "" if none.
std::string ResolveGameCommand( const std::string &action );

// Default_KB channel name -> SDL scancode (key position). The channel names
// are the game's DirectInput key names: table at 0x00f73b08 (32-byte names
// indexed by the DIK code, the French AZERTY labels of the key positions:
// DIK 0x10 "A", 0x11 "Z", 0x1e "Q", 0x27 "M", 0x2c "W", ...) [DATA]. DIK codes
// are key positions like SDL scancodes, so e.g. "W" (DIK 0x2c) is
// SDL_SCANCODE_Z: the key labelled W on an AZERTY keyboard, Z on a QWERTY one.
// Returns SDL_SCANCODE_UNKNOWN (0) for an unknown channel.
int32_t GameChannelToScancode( const std::string &channel );

// True if a single-key binding of the action was pressed this frame
// (gpInput->wasKeyPressed). bAccessoryMenuOpen enables the menu-only OpenTDU2
// bindings.
bool WasGameActionPressed( const std::string &action, bool bAccessoryMenuOpen = false );
