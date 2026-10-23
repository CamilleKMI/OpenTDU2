#include "shared.h"
#include "gm_first_walk_mode.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/flash_player.h"
#include "game/player_profile.h"
#include "audio/gs_audio.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include <SDL3/SDL_scancode.h>

namespace {
constexpr uint32_t kLocGetIntoTheCar = 5032; // 0x13a8 TUTO_ENTER_IN_THE _CAR
// C_VOICE_03..06 (table 0x00f76a78).
const char *kVoices[4] = { "C_VOICE_03", "C_VOICE_04", "C_VOICE_05", "C_VOICE_06" };
// DBTutoMess rows of the garage (FUN_00503c60 tutorial factory, fourcc
// 'opdo' / 'stca'; their classes are encrypted [ENC]).
const char *kTutorials[2] = { "opdo", "stca" };
constexpr float kDoorDuration = 2.0f;      // DAT_00eddb40 (FUN_009a9c4e opdo)
constexpr float kReticleRayLength = 50.0f; // FUN_00952410 /cursor depth ray (DAT_00f4a040)

std::string LocString( uint32_t id ) {
    const char *pText = gpDatabase != nullptr ? gpDatabase->getString( static_cast<int32_t>( id ) ) : nullptr;
    return pText != nullptr ? pText : "";
}

// Default_KB.xmb (PlayerKeyMapFile.KeyMap[]): ActionName -> its single key
// bindings {ChannelName, ChannelLocName} in file order (a ChannelNameAND
// combo keeps its ChannelLocName, e.g. "MAJ + F1"; it is not a key press).
struct KeyBinding {
    std::string Channel;
    std::string LocName;
};
// OpenTDU2 user-requested bindings (NOT game data): put before the
// Default_KB.xmb entries of the same action, so the #command: tip shows them
// (FUN_00d87cc0 takes the first entry of the action); the Default_KB keys
// keep working.
// - ACCESSORIES_LEFT (open the car door, Default_KB PAD_4 "4 (PAVE NUM.)")
//   also on the main-row 4 key, for keyboards without a numpad (Mac). The
//   channel / loc names follow the game's own main-row digit entries:
//   channel table at 0x00f73b08 (32-byte names indexed by DirectInput key
//   code, DIK_4 = 5 -> "4") [DATA], and Default_KB RADIO_CHANNEL /
//   RADIO_VOL_LESS / RADIO_VOL_MORE = "1" / "2" / "3" with ChannelLocName
//   "1" / "2" / "3" [DATA]. No Default_KB.xmb action uses the main-row "4".
struct UserBinding {
    const char *pAction;
    const char *pChannel;
    const char *pLocName;
};
constexpr UserBinding kUserBindings[] = { { "ACCESSORIES_LEFT", "4", "4" } };

const std::unordered_map<std::string, std::vector<KeyBinding>> &KeyMap() {
    static std::unordered_map<std::string, std::vector<KeyBinding>> map;
    static bool bLoaded = false;
    if ( !bLoaded ) {
        bLoaded = true;
        for ( const UserBinding &user : kUserBindings ) {
            map[user.pAction].push_back( KeyBinding{ user.pChannel, user.pLocName } );
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
                KeyBinding binding;
                binding.Channel = channel;
                binding.LocName = key["ChannelLocName"].asString();
                map[action].push_back( binding );
            }
        }
    }
    return map;
}

// '#command:<ACTION>#' (FUN_00d87cc0): keyboard form
// "#color:ff6600#[<ChannelLocName>]#endcolor#" of the first key map entry
// of the action (docs/re/text_icons_spec.md 1 [CODE]). The X360 pad form "#bitmap:x360_..#" (BUTTON_n / STICK_0_x / dpad
// table at 0x00d87930) is not drawn by the flash player: keyboard only.
std::string ResolveCommand( const std::string &action ) {
    const auto &map = KeyMap();
    auto it = map.find( action );
    if ( it == map.end() || it->second.empty() ) {
        return "";
    }
    const KeyBinding &binding = it->second.front();
    const std::string &name = binding.LocName.empty() ? binding.Channel : binding.LocName;
    return "#color:ff6600#[" + name + "]#endcolor#";
}

// Default_KB channel -> SDL scancode (the channels used by ACCESSORIES_* and
// the main-row digits; SDL scancodes are key positions, like DIK codes).
SDL_Scancode ChannelScancode( const std::string &channel ) {
    if ( channel.size() == 5 && channel.compare( 0, 4, "PAD_" ) == 0 && channel[4] >= '0' && channel[4] <= '9' ) {
        return channel[4] == '0' ? SDL_SCANCODE_KP_0
                                 : static_cast<SDL_Scancode>( SDL_SCANCODE_KP_1 + ( channel[4] - '1' ) );
    }
    if ( channel.size() == 1 && channel[0] >= 'A' && channel[0] <= 'Z' ) {
        return static_cast<SDL_Scancode>( SDL_SCANCODE_A + ( channel[0] - 'A' ) );
    }
    if ( channel.size() == 1 && channel[0] >= '0' && channel[0] <= '9' ) {
        return channel[0] == '0' ? SDL_SCANCODE_0 : static_cast<SDL_Scancode>( SDL_SCANCODE_1 + ( channel[0] - '1' ) );
    }
    return SDL_SCANCODE_UNKNOWN;
}

// FUN_00a48310: door easing, piecewise linear (constants 0x00f49af0 0.1,
// 0x00f49748 0.5, 0x00f49ad8 1.0555556 / 0x00f49ad0 0.0555556 when opening;
// x 2 then 0x00f49ae8 0.8888889 / 0x00f49ae0 0.1111111 when closing).
float DoorEase( float f, bool bOpening ) {
    if ( bOpening ) {
        return f < 0.1f ? f * 0.5f : f * 1.0555556f - 0.0555556f;
    }
    return f < 0.1f ? f * 2.0f : f * 0.8888889f + 0.1111111f;
}

// Ray / oriented box (car body bounds in the car frame).
bool RayHitsBox( const Matrix4x4f &boxToScene, const Vector3f &boxMin, const Vector3f &boxMax, const Vector3f &origin,
                 const Vector3f &direction, float maxDistance ) {
    const Matrix4x4f sceneToBox = boxToScene.inverse();
    const Vector3f o = sceneToBox.transformPoint( origin );
    const Vector3f d = sceneToBox.transformVector( direction );
    float tMin = 0.0f, tMax = maxDistance;
    const float origin3[3] = { o.x, o.y, o.z };
    const float direction3[3] = { d.x, d.y, d.z };
    const float min3[3] = { boxMin.x, boxMin.y, boxMin.z };
    const float max3[3] = { boxMax.x, boxMax.y, boxMax.z };
    for ( int32_t axis = 0; axis < 3; axis++ ) {
        if ( std::fabs( direction3[axis] ) < 1e-8f ) {
            if ( origin3[axis] < min3[axis] || origin3[axis] > max3[axis] ) {
                return false;
            }
            continue;
        }
        float t0 = ( min3[axis] - origin3[axis] ) / direction3[axis];
        float t1 = ( max3[axis] - origin3[axis] ) / direction3[axis];
        if ( t0 > t1 ) {
            std::swap( t0, t1 );
        }
        tMin = Max( tMin, t0 );
        tMax = Min( tMax, t1 );
        if ( tMin > tMax ) {
            return false;
        }
    }
    return true;
}
} // namespace

GMFirstWalkMode::GMFirstWalkMode() : GameMode( eGameMode::GM_FirstWalkMode ) {}

GMFirstWalkMode::~GMFirstWalkMode() {}

bool GMFirstWalkMode::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    // audiosetup.xmb game mode -> mix preset.
    if ( gpAudio != nullptr ) {
        gpAudio->setGameMode( "FIRST_WM" );
    }
    FlashPlayer::SetCommandResolver( ResolveCommand );
    // FUN_00a91360: Scenarics/Cinematics/intro/garage.bnk; the garage is a
    // closed room in its own frame (spec_pool_interior 4.2): no island.
    IntroCinematic::Setup setup;
    setup.Bank = "scenarics/cinematics/intro/garage.bnk";
    setup.HouseSpot.clear();
    setup.ExteriorBank.clear();
    setup.ExteriorPrefix.clear();
    setup.bWorld = false;
    setup.GameModeId = 0x23;
    bLoaded = garage.load( setup );
    if ( !bLoaded ) {
        OTDU_LOG_WARN( "FIRST_WM: garage.bnk could not be loaded\n" );
        return true;
    }
    // Story step 1 enter (FUN_00b10da0): car 387 (California) becomes the
    // current car; it rests at the garage PosCam D_CAR.
    garage.loadCar( CinematicCar::Desc(), "D_CAR", "CIN_CAR" );
    // The player avatar (profile+0x30 configuration).
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.AvatarConfig.empty() ) {
        const char *pAvatar = CmdLine::GetArgValue( "-avatar" );
        profile.AvatarConfig = pAvatar != nullptr ? pAvatar : "AVT_Male_Intro";
    }
    garage.setPlayerAvatar( profile.AvatarConfig );
    garage.setCommandCallback(
        [this]( const std::string &command, const std::string & ) { commands.push_back( command ); } );

    // FST_EnterCar (SelectableString "car"): picked only once the driver
    // door is fully open (FUN_009aa560 carReady: car +0xa794 == -1.0).
    const auto &selectables = garage.getSelectables();
    for ( size_t i = 0; i < selectables.size(); i++ ) {
        if ( selectables[i].SelectableString == "car" ) {
            carSelectable = static_cast<int32_t>( i );
        }
    }
    walk.setSelectableFilter( [this]( int32_t index ) { return index != carSelectable || isDoorOpen(); } );
    // DBCarPhysicsData _doormaxangle of the California (1) [DATA]; the door
    // object reads the car record +0x14c [INF: that field].
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
            if ( StringCmpI( car["_car_file_name"].asString().c_str(), "California" ) == 0 ) {
                doorMaxAngle = car["_doormaxangle"].asFloat( 1.0f );
                break;
            }
        }
    }
    door = DoorAnim();
    loadTutorials();
    loadCockpitCamera();

    // FUN_009aa2c0: C_LIFE_SOUND ambient (CinProps |= 0x10), C_INTRO
    // (skippable).
    garage.playBackground( "C_LIFE_SOUND", true );
    garage.playMain( "C_INTRO" );
    state = eState::Intro;
    return true;
}

void GMFirstWalkMode::exit() {
    garage.unload();
    FlashPlayer::SetCommandResolver( nullptr );
    bLoaded = false;
}

void GMFirstWalkMode::loadTutorials() {
    // DBTutoMess: opdo (_mess_hc 5036, condition 2 s, on screen until its
    // close condition, unlimited, sound Intro_FiestaGarage_04) and stca (5035,
    // 2 s, once, Intro_FiestaGarage_06) [DATA].
    for ( int32_t i = 0; i < 2; i++ ) {
        tutorials[i] = Tutorial();
        tutorials[i].Name = kTutorials[i];
    }
    if ( gpDatabase == nullptr ) {
        return;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t row = 0; row < root.getArraySize( "DBTutoMess" ); row++ ) {
        XmbValue entry = root.getArrayElement( "DBTutoMess", row );
        const std::string name = entry["_tutomess"].asString();
        for ( Tutorial &tutorial : tutorials ) {
            if ( tutorial.Name == name ) {
                tutorial.Text = static_cast<uint32_t>( entry["_mess_hc"].asUInt64() );
                tutorial.TextPC = static_cast<uint32_t>( entry["_messpc_hc"].asUInt64() );
                tutorial.ConditionTimer = entry["_conditiontimer"].asFloat();
                tutorial.LastOnScreenTimer = entry["_lastonscreentimer"].asFloat();
                tutorial.NbTimeAllowed = static_cast<int32_t>( entry["_nbtimeallowed"].asInt( 1 ) );
                tutorial.bValid = true;
            }
        }
    }
}

void GMFirstWalkMode::loadCockpitCamera() {
    // database/camerasingame.xmb: CameraInGame."Car n" { id_car, nb_cameras,
    // "Camera k" }. Car 387 (DBCarPhysicsData _camera = 387): Camera 1, mode
    // 23, fixed_pos (0.37, 1.01, -0.36) = above DVRSEAT_LR (0.365, 0.247,
    // -0.382), fixed_targ (0.37, 0.90, 1.353), fov 55, near 0.05 [DATA]; mode
    // 23 = the cockpit view [INF: its eye is the driver seat].
    XmbDocument document;
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( ( gpFile->getBankFolder() + "database/camerasingame.xmb" ).c_str(), data ) ||
         !document.load( data ) ) {
        OTDU_LOG_WARN( "FIRST_WM: camerasingame.xmb not found (cockpit camera defaults)\n" );
        return;
    }
    XmbValue root = document.getRoot()["CameraInGame"];
    for ( int32_t c = 0; c < 400; c++ ) {
        XmbValue car = root[( "Car " + std::to_string( c ) ).c_str()];
        if ( !car.isValid() ) {
            continue;
        }
        if ( car["id_car"].asInt() != 387 ) {
            continue;
        }
        const int32_t count = static_cast<int32_t>( car["nb_cameras"].asInt() );
        for ( int32_t k = 0; k < count; k++ ) {
            XmbValue camera = car[( "Camera " + std::to_string( k ) ).c_str()];
            if ( !camera.isValid() || camera["mode"].asInt() != 23 ) {
                continue;
            }
            cockpitPosition = Vector3f( camera["fixed_pos_x"].asFloat(), camera["fixed_pos_y"].asFloat(),
                                        camera["fixed_pos_z"].asFloat() );
            cockpitTarget = Vector3f( camera["fixed_targ_x"].asFloat(), camera["fixed_targ_y"].asFloat(),
                                      camera["fixed_targ_z"].asFloat() );
            cockpitFov = camera["fov"].asFloat( cockpitFov );
            cockpitNear = camera["nearProjPlane"].asFloat( cockpitNear );
            OTDU_LOG_INFO( "FIRST_WM: cockpit camera (car %d, camera %d): pos (%.3f %.3f %.3f) target (%.3f %.3f %.3f) "
                           "fov %.1f near %.2f\n",
                           c, k, cockpitPosition.x, cockpitPosition.y, cockpitPosition.z, cockpitTarget.x,
                           cockpitTarget.y, cockpitTarget.z, cockpitFov / kDegToRad, cockpitNear );
            return;
        }
    }
}

void GMFirstWalkMode::onCinematicEnd( const std::string &name ) {
    // Listener FUN_009a9d10 (events 3 / 9).
    OTDU_LOG_DEBUG( "FIRST_WM: end of %s\n", name.c_str() );
    for ( uint32_t i = 0; i < 4; i++ ) {
        if ( name == kVoices[i] ) {
            voiceDone[i] = true;
            bVoicePlaying = false;
            return;
        }
    }
    if ( name == "C_INTRO" ) {
        // C_LIFE_TESS (CinProps |= 0x1010, loop), walk camera cam_entree
        // (Fov 0.8727), INTRO "wm_garage", goal "Get into the car".
        garage.stopMain();
        garage.playBackground( "C_LIFE_TESS", true );
        walk.start( garage, "cam_entree", 0.8727f );
        garage.getUi().gotoLabel( "/", "wm_garage", true );
        garage.showGoal( LocString( kLocGetIntoTheCar ) );
        state = eState::Walk;
        if ( CmdLine::HasArg( "-fwmseated" ) ) {
            // Test shortcut: straight to the seated state.
            garage.getUi().setClipVisible( "/cursor", false );
            garage.closeGoal();
            onCinematicEnd( "C_ENTER_CAR" );
        }
    } else if ( name == "C_ENTER_CAR" ) {
        // FUN_008c1880 (enter car, seat 0): the player avatar is put on the
        // driver seat [ENC]; the view is the car's cockpit camera
        // (camerasingame.xmb mode 23) [INF]. The door is closed again
        // (FUN_00a49230 resets the door channel with a 0 duration) [INF: the
        // caller is not identified].
        garage.stopMain();
        garage.setPlayerSeated( true );
        startDoor( 0.0f, false );
        bCockpitCamera = true;
        updateCockpitCamera();
        bEnteredCar = true;
        state = eState::Seated;
    } else if ( name == "C_OUT" ) {
        // DAT_01179620 restored; C_OUTRO (the car drives out, M_OUTRO message
        // avm4:CIN_CAR: the avatar in CIN_CAR).
        garage.playMain( "C_OUTRO" );
        state = eState::Outro;
    } else if ( name == "C_OUTRO" ) {
        // End of C_OUTRO -> story step 2 "First Freeride" (FUN_00b10da0 case
        // 3: profile+0x58 = 2, fade_freeride.bnk, C_FIRST_OUTRO, mode 1
        // FREERIDE).
        GetPlayerProfile().StoryStep = 2;
        OTDU_LOG_INFO( "FIRST_WM: end of the garage, story step 2 (FREERIDE)\n" );
        state = eState::Done;
        if ( !bFreeRideRequested ) {
            bFreeRideRequested = true;
            pGameInstance->setNextGameMode( eGameMode::GM_FreeRide );
        }
    }
}

void GMFirstWalkMode::onCommand( const std::string &command ) {
    // FIRST_WM command handler FUN_009a9c4e: 'opdo' opens the driver door
    // (FUN_00a48770(2.0, left, open, 1)) unless the player is in the car;
    // 'wmof' is ignored while the movie is "INTRO".
    OTDU_LOG_DEBUG( "FIRST_WM: FSCommand %s\n", command.c_str() );
    if ( command == "opdo" && !bEnteredCar ) {
        startDoor( kDoorDuration, true );
    }
}

void GMFirstWalkMode::startDoor( float duration, bool bOpen ) {
    // FUN_00a48770(duration, channel 1 = left, open, 1): a 0 duration sets
    // the end state at once.
    if ( duration == 0.0f ) {
        door.Elapsed = door.Duration = 1.0f;
        door.bOpening = bOpen;
    } else if ( door.bOpening == bOpen ) {
        return; // already moving / arrived that way
    } else if ( door.Elapsed < door.Duration ) {
        // Reversed while moving: continue from the same opening [INF: the
        // FUN_00a483f0 inverse ease is approximated by the linear ratio].
        const float progress = door.Elapsed / door.Duration;
        door.Duration = duration;
        door.Elapsed = ( 1.0f - progress ) * duration;
        door.bOpening = bOpen;
    } else {
        door.Duration = duration;
        door.Elapsed = 0.0f;
        door.bOpening = bOpen;
    }
    updateDoor( 0.0f );
}

void GMFirstWalkMode::updateDoor( float deltaTime ) {
    // Door object tick (0x00a49400): elapsed = min(elapsed + dt, duration);
    // FUN_00a484c0: f = elapsed / duration (1 - f when closing), angle =
    // -record(+0x14c) * ease(f) for the left door; FUN_00c67230 stores it at
    // car +0xa794 and poses DOOR_L.
    door.Elapsed = Min( door.Elapsed + deltaTime, door.Duration );
    float f = door.Duration > 0.0f ? door.Elapsed / door.Duration : 1.0f;
    if ( !door.bOpening ) {
        f = 1.0f - f;
    }
    const float angle = -doorMaxAngle * DoorEase( f, door.bOpening );
    if ( angle != garage.getCar().getDoorAngle( 0 ) ) {
        garage.getCar().setDoorAngle( 0, angle );
    }
}

bool GMFirstWalkMode::isDoorOpen() const {
    // carReady (FUN_009aa560): car +0xa794 == -1.0f (DAT_00ede404), i.e.
    // the opening finished (_doormaxangle 1).
    return door.bOpening && door.Elapsed >= door.Duration;
}

bool GMFirstWalkMode::aimingAtCar() const {
    // The reticle ray (camera eye, forward, 50 m) on the car body. The test
    // that sends opdo in the walk mode is not in readable code [ENC]: the
    // body bounds (BODY_HR) stand for it [INF].
    Matrix4x4f carLocal;
    Vector3f boundsMin, boundsMax;
    if ( !garage.getCarLocal( carLocal ) || !garage.getCar().getBodyBounds( boundsMin, boundsMax ) ) {
        return false;
    }
    return RayHitsBox( carLocal, boundsMin, boundsMax, walk.getPosition(), walk.getForward(), kReticleRayLength );
}

bool GMFirstWalkMode::wasAccessoriesLeftPressed() const {
    // ACCESSORIES_LEFT (input 0x34 [INF from its uses next to 0x35..0x37]):
    // Default_KB.xmb key (PAD_4), the OpenTDU2 main-row 4, or the pad d-pad left (IA_MenuLeft, not the
    // arrow key that also strafes: AVT_MOVE_LEFT).
    // Every binding of the action: Default_KB PAD_4 and the OpenTDU2
    // main-row 4 (kUserBindings).
    const auto &map = KeyMap();
    auto it = map.find( "ACCESSORIES_LEFT" );
    if ( it != map.end() ) {
        for ( const KeyBinding &binding : it->second ) {
            const SDL_Scancode scancode = ChannelScancode( binding.Channel );
            if ( scancode != SDL_SCANCODE_UNKNOWN && gpInput->wasKeyPressed( scancode ) ) {
                return true;
            }
        }
    }
    return gpInput->wasPressed( eInputAction::IA_MenuLeft ) && !gpInput->isDown( eInputAction::IA_AvatarLeft );
}

bool GMFirstWalkMode::tutorialActive( int32_t index ) const {
    // Tutorial class conditions (vfunc +4) are encrypted [ENC] [INF]:
    // opdo while walking with the driver door closed; stca when seated and
    // the engine not yet started.
    if ( index == 0 ) {
        return state == eState::Walk && !bEnteredCar && garage.getCar().getDoorAngle( 0 ) == 0.0f &&
               !door.bOpening && garage.getMainName().empty();
    }
    return state == eState::Seated && revTimer == 0.0f;
}

bool GMFirstWalkMode::tutorialCloseCondition( int32_t index ) const {
    // vfunc +0x18 [ENC] [INF]: opdo when the door opens (or the walk mode
    // ends), stca when the engine is started.
    if ( index == 0 ) {
        return door.bOpening || state != eState::Walk;
    }
    return revTimer != 0.0f || state != eState::Seated;
}

void GMFirstWalkMode::updateTutorials( float deltaTime ) {
    // FUN_00d63240 (manager tick): cooldown +0x218; per tutorial: on screen
    // -> timer, close after _lastonscreentimer (when not 0) or on its close
    // condition (FUN_00d61600); else while its condition holds and
    // NbTimeAllowed (-1 = unlimited) remains: timer, shown after
    // _conditiontimer when no other tutorial is on screen (FUN_00d62ed0:
    // NbTimeAllowed--; the 10 s cooldown +0x218 only gates the tutorials
    // flagged +0x1c, not identified [ENC]: not used).
    bool bAnyOnScreen = false;
    for ( const Tutorial &tutorial : tutorials ) {
        bAnyOnScreen |= tutorial.bOnScreen;
    }
    for ( int32_t i = 0; i < 2; i++ ) {
        Tutorial &tutorial = tutorials[i];
        if ( !tutorial.bValid ) {
            continue;
        }
        if ( tutorial.bOnScreen ) {
            tutorial.Timer += deltaTime;
            if ( ( tutorial.LastOnScreenTimer != 0.0f && tutorial.Timer > tutorial.LastOnScreenTimer ) ||
                 tutorialCloseCondition( i ) ) {
                garage.closeTutorial();
                tutorial.bOnScreen = false;
                tutorial.bDone = true;
                tutorial.Timer = 0.0f;
                bAnyOnScreen = false;
            }
            continue;
        }
        if ( tutorial.bDone && tutorial.NbTimeAllowed == 0 ) {
            continue;
        }
        if ( ( tutorial.NbTimeAllowed == -1 || tutorial.NbTimeAllowed > 0 ) && tutorialActive( i ) ) {
            tutorial.Timer += deltaTime;
            if ( tutorial.Timer > tutorial.ConditionTimer && !bAnyOnScreen ) {
                // FUN_00d62a90: the PC text when given (and the device is not
                // the X360 pad), else _mess_hc.
                const uint32_t text = tutorial.TextPC != 0 ? tutorial.TextPC : tutorial.Text;
                garage.showTutorial( LocString( text ) );
                tutorial.bOnScreen = true;
                tutorial.Timer = 0.0f;
                if ( tutorial.NbTimeAllowed != -1 ) {
                    tutorial.NbTimeAllowed--;
                }
                bAnyOnScreen = true;
                OTDU_LOG_INFO( "FIRST_WM: tutorial %s (loc %u)\n", tutorial.Name.c_str(), text );
            }
        } else {
            tutorial.Timer = 0.0f;
        }
    }
}

void GMFirstWalkMode::updateVoices() {
    // FUN_009a9990 -> FUN_009a9920: Tess comments while no cinematic runs.
    // idx 3 in the car; 1 / 2 with a selectable targeted (2 when the driver
    // door angle +0xa794 != 0); 0 otherwise. The car aimed with the door
    // closed counts as targeted [INF: the "car" item is only picked here once
    // the door is open].
    int32_t index = 0;
    if ( bEnteredCar ) {
        index = 3;
    } else if ( walk.getSelected() >= 0 || bAimingAtCar ) {
        index = garage.getCar().getDoorAngle( 0 ) != 0.0f ? 2 : 1;
    }
    if ( !voiceDone[index] && !bVoicePlaying ) {
        garage.playBackground( kVoices[index], false );
        bVoicePlaying = true;
    }
}

void GMFirstWalkMode::updateCockpitCamera() {
    // Cockpit camera in the car frame (+X left, +Y up, +Z forward), moved
    // with the car (C_OUTRO drives CIN_CAR).
    Matrix4x4f carWorld;
    if ( !garage.getCarWorld( carWorld ) ) {
        return;
    }
    const Vector3f eye = carWorld.transformPoint( cockpitPosition );
    const Vector3f direction = ( carWorld.transformPoint( cockpitTarget ) - eye ).normalized();
    // Eden convention: forward = R(q*) (0, 0, 1), q* = Ry(yaw) Rx(-pitch).
    const float yaw = std::atan2( direction.x, direction.z );
    const float pitch = std::asin( Clamp( direction.y, -1.0f, 1.0f ) );
    const Quaternionf conjugate = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ) *
                                  Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), -pitch );
    EdenSceneView::Camera camera = garage.getCamera();
    camera.Position = eye;
    camera.Rotation = Quaternionf( -conjugate.x, -conjugate.y, -conjugate.z, conjugate.w );
    camera.Fov = cockpitFov;
    camera.Near = cockpitNear;
    garage.setCameraOverride( &camera );
}

void GMFirstWalkMode::tick( float deltaTime ) {
    if ( !bLoaded ) {
        pGameInstance->setNextGameMode( eGameMode::GM_Login );
        return;
    }
    // C_INTRO is skippable (CinProps |= 4); the skip press is not reused
    // by the walk mode of the same frame.
    bool bSkipped = false;
    if ( state == eState::Intro &&
         ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) || gpInput->wasPressed( eInputAction::IA_MenuPause ) ) &&
         CmdLine::HasArg( "-allowskip" ) ) {
        garage.skipMain();
        bSkipped = true;
    }
    garage.tick( deltaTime );
    for ( const std::string &name : garage.takeEndEvents() ) {
        onCinematicEnd( name );
    }
    std::vector<std::string> pending;
    pending.swap( commands );
    for ( const std::string &command : pending ) {
        onCommand( command );
    }
    updateDoor( deltaTime );
    if ( state == eState::Walk ) {
        walk.update( garage, deltaTime, true );
        bAimingAtCar = aimingAtCar();
        // ACCESSORIES_LEFT with the reticle on the car and the door closed:
        // FSCommand opdo (intro.vmf ItemType 'vehicle' over sends it; the
        // walk-mode side is not readable [ENC]) [INF].
        if ( bAimingAtCar && !door.bOpening && wasAccessoriesLeftPressed() ) {
            onCommand( "opdo" );
        }
        updateVoices();
        if ( CmdLine::HasArg( "-logwm" ) && walk.getSelected() >= 0 ) {
            OTDU_LOG_INFO( "FIRST_WM: selected %d '%s' low bar '%s' m_EnterCar_ '%s'\n", walk.getSelected(),
                           walk.getSelectedType( garage ), garage.getUi().getVariable( "/:t_wm_destination" ).c_str(),
                           garage.getUi().getVariable( "/:m_EnterCar_" ).c_str() );
        }
        // FUN_009aa560: A while the "car" selectable is selected (door open)
        // -> C_ENTER_CAR.
        if ( !bEnteredCar && strcmp( walk.getSelectedType( garage ), "car" ) == 0 && isDoorOpen() &&
             gpInput->wasPressed( eInputAction::IA_MenuValidate ) && !bSkipped ) {
            garage.getUi().setClipVisible( "/cursor", false );
            walk.clearSelection( garage );
            garage.closeGoal();
            garage.setCameraOverride( nullptr );
            garage.playMain( "C_ENTER_CAR" );
            state = eState::EnterCar;
        }
    } else if ( state == eState::Seated ) {
        updateCockpitCamera();
        updateVoices();
        // Throttle > 0.7 (0x00eddb08) -> timer 3 s (0x00edf3b8), DAT_01179620 =
        // 0x1e, FUN_009bddf0("C_OUTRO"): the Entity__Fx C_OUTRO (Fx
        // 0x40a58c5b68a6f9de) is started (FUN_00b17b70(fx, -1.0)).
        if ( revTimer == 0.0f && gpInput->getAnalog( eInputAction::IA_CarAccel ) > 0.7f ) {
            revTimer = 3.0f;
            garage.playEmitter( "C_OUTRO" );
        }
        if ( revTimer > 0.0f ) {
            revTimer -= deltaTime;
            if ( revTimer <= 0.0f ) {
                // FUN_008c11f0 (out of the car), FUN_00d80340, C_OUT.
                revTimer = 0.0f;
                bCockpitCamera = false;
                garage.setCameraOverride( nullptr );
                garage.playMain( "C_OUT" );
                state = eState::Out;
            }
        }
    }
    updateTutorials( deltaTime );
}

void GMFirstWalkMode::drawOffscreen() {
    if ( bLoaded ) {
        garage.renderOffscreen();
    }
}

void GMFirstWalkMode::draw() {
    if ( bLoaded ) {
        garage.draw();
    }
}
