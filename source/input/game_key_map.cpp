#include "shared.h"
#include "game_key_map.h"

#include "core/xmb.h"
#include "input/gs_input.h"

#include <SDL3/SDL_scancode.h>

namespace {
// OpenTDU2 user-requested bindings (NOT game data), put before the
// Default_KB.xmb entries of the same action so the #command: tip shows them
// (FUN_00d87cc0 takes the first entry); the Default_KB keys keep working.
// Main-row digits for the numpad-only accessory keys (a Mac keyboard has no
// numpad). The channel / loc names follow the game's own main-row digit
// entries (channel table 0x00f73b08: DIK 2..0xb = "1".."0"; Default_KB
// RADIO_CHANNEL / RADIO_VOL_LESS / RADIO_VOL_MORE = "1" / "2" / "3") [DATA].
// - ACCESSORIES_LEFT (PAD_4): "4", always (opens the door in FIRST_WM, the
//   accessories menu when driving). No Default_KB action uses the main-row 4.
// - ACCESSORIES_UP (PAD_8) "8", ACCESSORIES_RIGHT (PAD_6) "6",
//   ACCESSORIES_DOWN (PAD_2) "2": only while the accessories menu is open
//   (main-row 2 is RADIO_VOL_LESS in Default_KB; 6 / 8 are unused there).
struct UserBinding {
    const char *pAction;
    const char *pChannel;
    const char *pLocName;
    bool bAccessoryMenuOnly;
};
constexpr UserBinding kUserBindings[] = {
    { "ACCESSORIES_LEFT", "4", "4", false },
    { "ACCESSORIES_UP", "8", "8", true },
    { "ACCESSORIES_RIGHT", "6", "6", true },
    { "ACCESSORIES_DOWN", "2", "2", true },
};

// Channel names of the game (0x00f73b08, index = DIK code) -> SDL scancode
// (same key position) [DATA]. Only the named entries ("unamed" skipped).
struct ChannelKey {
    const char *pName;
    uint8_t Dik;
    SDL_Scancode Scancode;
};
constexpr ChannelKey kChannelKeys[] = {
    { "ESC", 0x01, SDL_SCANCODE_ESCAPE },
    { "1", 0x02, SDL_SCANCODE_1 },
    { "2", 0x03, SDL_SCANCODE_2 },
    { "3", 0x04, SDL_SCANCODE_3 },
    { "4", 0x05, SDL_SCANCODE_4 },
    { "5", 0x06, SDL_SCANCODE_5 },
    { "6", 0x07, SDL_SCANCODE_6 },
    { "7", 0x08, SDL_SCANCODE_7 },
    { "8", 0x09, SDL_SCANCODE_8 },
    { "9", 0x0a, SDL_SCANCODE_9 },
    { "0", 0x0b, SDL_SCANCODE_0 },
    { "CLOSE_BRACKET", 0x0c, SDL_SCANCODE_MINUS },
    { "EQUAL", 0x0d, SDL_SCANCODE_EQUALS },
    { "BACKSPACE", 0x0e, SDL_SCANCODE_BACKSPACE },
    { "TAB", 0x0f, SDL_SCANCODE_TAB },
    { "A", 0x10, SDL_SCANCODE_Q },
    { "Z", 0x11, SDL_SCANCODE_W },
    { "E", 0x12, SDL_SCANCODE_E },
    { "R", 0x13, SDL_SCANCODE_R },
    { "T", 0x14, SDL_SCANCODE_T },
    { "Y", 0x15, SDL_SCANCODE_Y },
    { "U", 0x16, SDL_SCANCODE_U },
    { "I", 0x17, SDL_SCANCODE_I },
    { "O", 0x18, SDL_SCANCODE_O },
    { "P", 0x19, SDL_SCANCODE_P },
    { "TREMA", 0x1a, SDL_SCANCODE_LEFTBRACKET },
    { "DOLLAR", 0x1b, SDL_SCANCODE_RIGHTBRACKET },
    { "ENTER", 0x1c, SDL_SCANCODE_RETURN },
    { "LCTRL", 0x1d, SDL_SCANCODE_LCTRL },
    { "Q", 0x1e, SDL_SCANCODE_A },
    { "S", 0x1f, SDL_SCANCODE_S },
    { "D", 0x20, SDL_SCANCODE_D },
    { "F", 0x21, SDL_SCANCODE_F },
    { "G", 0x22, SDL_SCANCODE_G },
    { "H", 0x23, SDL_SCANCODE_H },
    { "J", 0x24, SDL_SCANCODE_J },
    { "K", 0x25, SDL_SCANCODE_K },
    { "L", 0x26, SDL_SCANCODE_L },
    { "M", 0x27, SDL_SCANCODE_SEMICOLON },
    { "PERCENT", 0x28, SDL_SCANCODE_APOSTROPHE },
    { "SQUARE", 0x29, SDL_SCANCODE_GRAVE },
    { "LSHIFT", 0x2a, SDL_SCANCODE_LSHIFT },
    { "STAR", 0x2b, SDL_SCANCODE_BACKSLASH },
    { "W", 0x2c, SDL_SCANCODE_Z },
    { "X", 0x2d, SDL_SCANCODE_X },
    { "C", 0x2e, SDL_SCANCODE_C },
    { "V", 0x2f, SDL_SCANCODE_V },
    { "B", 0x30, SDL_SCANCODE_B },
    { "N", 0x31, SDL_SCANCODE_N },
    { "COMMA", 0x32, SDL_SCANCODE_M },
    { "SEMI_COLON", 0x33, SDL_SCANCODE_COMMA },
    { "COLON", 0x34, SDL_SCANCODE_PERIOD },
    { "EXCLAMATION", 0x35, SDL_SCANCODE_SLASH },
    { "RSHIFT", 0x36, SDL_SCANCODE_RSHIFT },
    { "PAD_MUL", 0x37, SDL_SCANCODE_KP_MULTIPLY },
    { "LALT", 0x38, SDL_SCANCODE_LALT },
    { "SPACE", 0x39, SDL_SCANCODE_SPACE },
    { "CAPS_LOCK", 0x3a, SDL_SCANCODE_CAPSLOCK },
    { "F1", 0x3b, SDL_SCANCODE_F1 },
    { "F2", 0x3c, SDL_SCANCODE_F2 },
    { "F3", 0x3d, SDL_SCANCODE_F3 },
    { "F4", 0x3e, SDL_SCANCODE_F4 },
    { "F5", 0x3f, SDL_SCANCODE_F5 },
    { "F6", 0x40, SDL_SCANCODE_F6 },
    { "F7", 0x41, SDL_SCANCODE_F7 },
    { "F8", 0x42, SDL_SCANCODE_F8 },
    { "F9", 0x43, SDL_SCANCODE_F9 },
    { "F10", 0x44, SDL_SCANCODE_F10 },
    { "LOCK_NUM", 0x45, SDL_SCANCODE_NUMLOCKCLEAR },
    { "SCROLL_LOCK", 0x46, SDL_SCANCODE_SCROLLLOCK },
    { "PAD_7", 0x47, SDL_SCANCODE_KP_7 },
    { "PAD_8", 0x48, SDL_SCANCODE_KP_8 },
    { "PAD_9", 0x49, SDL_SCANCODE_KP_9 },
    { "PAD_MINUS", 0x4a, SDL_SCANCODE_KP_MINUS },
    { "PAD_4", 0x4b, SDL_SCANCODE_KP_4 },
    { "PAD_5", 0x4c, SDL_SCANCODE_KP_5 },
    { "PAD_6", 0x4d, SDL_SCANCODE_KP_6 },
    { "PAD_PLUS", 0x4e, SDL_SCANCODE_KP_PLUS },
    { "PAD_1", 0x4f, SDL_SCANCODE_KP_1 },
    { "PAD_2", 0x50, SDL_SCANCODE_KP_2 },
    { "PAD_3", 0x51, SDL_SCANCODE_KP_3 },
    { "PAD_0", 0x52, SDL_SCANCODE_KP_0 },
    { "PAD_DOT", 0x53, SDL_SCANCODE_KP_PERIOD },
    { "LOWER", 0x56, SDL_SCANCODE_NONUSBACKSLASH },
    { "F11", 0x57, SDL_SCANCODE_F11 },
    { "F12", 0x58, SDL_SCANCODE_F12 },
    { "PAD_ENTER", 0x9c, SDL_SCANCODE_KP_ENTER },
    { "RCTRL", 0x9d, SDL_SCANCODE_RCTRL },
    { "PAD_DIV", 0xb5, SDL_SCANCODE_KP_DIVIDE },
    { "PRINT_SCRN", 0xb7, SDL_SCANCODE_PRINTSCREEN },
    { "RALT", 0xb8, SDL_SCANCODE_RALT },
    { "PAUSE", 0xc5, SDL_SCANCODE_PAUSE },
    { "HOME", 0xc7, SDL_SCANCODE_HOME },
    { "UP", 0xc8, SDL_SCANCODE_UP },
    { "PAGE_UP", 0xc9, SDL_SCANCODE_PAGEUP },
    { "LEFT", 0xcb, SDL_SCANCODE_LEFT },
    { "RIGHT", 0xcd, SDL_SCANCODE_RIGHT },
    { "END", 0xcf, SDL_SCANCODE_END },
    { "DOWN", 0xd0, SDL_SCANCODE_DOWN },
    { "PAGE_DOWN", 0xd1, SDL_SCANCODE_PAGEDOWN },
    { "INS", 0xd2, SDL_SCANCODE_INSERT },
    { "SUP", 0xd3, SDL_SCANCODE_DELETE },
    { "APPLICATION", 0xdd, SDL_SCANCODE_APPLICATION },
};
} // namespace

const GameKeyMap &GetGameKeyMap() {
    static GameKeyMap map;
    static bool bLoaded = false;
    if ( !bLoaded ) {
        bLoaded = true;
        for ( const UserBinding &user : kUserBindings ) {
            GameKeyBinding binding;
            binding.Channel = user.pChannel;
            binding.LocName = user.pLocName;
            binding.bUser = true;
            binding.bAccessoryMenuOnly = user.bAccessoryMenuOnly;
            map[user.pAction].push_back( binding );
        }
        XmbDocument document;
        if ( LoadXmbFromFile( "Default_KB.xmb", document ) ) {
            XmbValue root = document.getRoot();
            for ( uint32_t i = 0; i < root.getArraySize( "KeyMap" ); i++ ) {
                XmbValue key = root.getArrayElement( "KeyMap", i );
                const std::string action = key["ActionName"].asString();
                const std::string channel = key["ChannelName"].asString();
                if ( channel.empty() ) {
                    continue;
                }
                GameKeyBinding binding;
                binding.Channel = channel;
                binding.ChannelAnd = key["ChannelNameAND"].asString();
                binding.LocName = key["ChannelLocName"].asString();
                map[action].push_back( binding );
            }
        }
    }
    return map;
}

std::string ResolveGameCommand( const std::string &action ) {
    const GameKeyMap &map = GetGameKeyMap();
    auto it = map.find( action );
    if ( it == map.end() || it->second.empty() ) {
        return "";
    }
    const GameKeyBinding &binding = it->second.front();
    const std::string &name = binding.LocName.empty() ? binding.Channel : binding.LocName;
    return "#color:ff6600#[" + name + "]#endcolor#";
}

int32_t GameChannelToScancode( const std::string &channel ) {
    for ( const ChannelKey &key : kChannelKeys ) {
        if ( channel == key.pName ) {
            return static_cast<int32_t>( key.Scancode );
        }
    }
    return static_cast<int32_t>( SDL_SCANCODE_UNKNOWN );
}

bool WasGameActionPressed( const std::string &action, bool bAccessoryMenuOpen ) {
    if ( gpInput == nullptr ) {
        return false;
    }
    const GameKeyMap &map = GetGameKeyMap();
    auto it = map.find( action );
    if ( it == map.end() ) {
        return false;
    }
    for ( const GameKeyBinding &binding : it->second ) {
        if ( !binding.ChannelAnd.empty() || ( binding.bAccessoryMenuOnly && !bAccessoryMenuOpen ) ) {
            continue;
        }
        const int32_t scancode = GameChannelToScancode( binding.Channel );
        if ( scancode != SDL_SCANCODE_UNKNOWN && gpInput->wasKeyPressed( scancode ) ) {
            return true;
        }
    }
    return false;
}
