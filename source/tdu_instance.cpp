#include "shared.h"
#include "tdu_instance.h"

#include "audio/gs_audio.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "input/gs_input.h"
#include "physics/gs_physics.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#include "game/bootmenu/gm_login.h"
#include "game/avatarchoice/gm_avatar_choice.h"
#include "game/firstwalkmode/gm_first_walk_mode.h"
#include "game/freeride/gm_freeride.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

TestDriveGameInstance *gpTestDriveInstance = nullptr;

static constexpr const char *kOpenTDU2Version = "0.1.0";

const char *TestDrive::GetVersion() { return kOpenTDU2Version; }

int32_t TestDrive::InitAndRun( const char **pCmdLineArgs, const int32_t argCount ) {
    CmdLine::Initialize( argCount, pCmdLineArgs );

    const char *pLogPath = CmdLine::GetArgValue( "-log" );
    Logger::Initialize( CmdLine::HasArg( "-nolog" ) ? nullptr
                                                    : ( pLogPath != nullptr ? pLogPath : "opentdu2.log" ),
                        CmdLine::HasArg( "-verbose" ), CmdLine::HasArg( "-silent" ) );
    Assert::gbBreakOnFailure = CmdLine::HasArg( "-break_on_assert_failure" );
    Assert::gbDisabled = CmdLine::HasArg( "-disable_assert" );

    OTDU_LOG_ALWAYS( "OpenTDU2 %s - Test Drive Unlimited 2 source port\n", GetVersion() );

    TestDriveGameInstance instance;
    int32_t exitCode = 0;
    if ( instance.initialize() ) {
        instance.mainLoop();
    } else {
        exitCode = 1;
    }
    instance.terminate();
    Logger::Shutdown();
    return exitCode;
}

TestDriveGameInstance::TestDriveGameInstance()
    : pWindow( nullptr ),
      nextGameMode( eGameMode::GM_None ),
      bRequestedExit( false ),
      bShowDebugUI( false ),
      totalTime( 0.0f ),
      frameIndex( 0 ),
      autoExitFrame( -1 ),
      screenshotFrame( -1 ) {
    gpTestDriveInstance = this;
}

TestDriveGameInstance::~TestDriveGameInstance() { gpTestDriveInstance = nullptr; }

// Loads the Vulkan loader. On macOS the LunarG SDK is often not installed
// system wide: look for it in $VULKAN_SDK and ~/VulkanSDK/<version>/macOS and
// point the loader to the MoltenVK ICD manifest of that SDK.
static bool LoadVulkanLibrary() {
    if ( SDL_Vulkan_LoadLibrary( nullptr ) ) {
        return true;
    }
#if defined( __APPLE__ )
    std::vector<std::filesystem::path> roots;
    if ( const char *pSDK = getenv( "VULKAN_SDK" ) ) {
        roots.emplace_back( pSDK );
    }
    if ( const char *pHome = getenv( "HOME" ) ) {
        std::error_code ec;
        std::vector<std::filesystem::path> versions;
        for ( const auto &entry : std::filesystem::directory_iterator( std::filesystem::path( pHome ) / "VulkanSDK", ec ) ) {
            if ( entry.is_directory() ) {
                versions.push_back( entry.path() / "macOS" );
            }
        }
        std::sort( versions.rbegin(), versions.rend() ); // newest first
        roots.insert( roots.end(), versions.begin(), versions.end() );
    }
    for ( const std::filesystem::path &root : roots ) {
        const std::filesystem::path loader = root / "lib" / "libvulkan.1.dylib";
        std::error_code ec;
        if ( !std::filesystem::exists( loader, ec ) ) {
            continue;
        }
        const std::filesystem::path icd = root / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json";
        if ( getenv( "VK_DRIVER_FILES" ) == nullptr && getenv( "VK_ICD_FILENAMES" ) == nullptr &&
             std::filesystem::exists( icd, ec ) ) {
            setenv( "VK_DRIVER_FILES", icd.string().c_str(), 0 );
        }
        const std::filesystem::path layers = root / "share" / "vulkan" / "explicit_layer.d";
        if ( getenv( "VK_LAYER_PATH" ) == nullptr && std::filesystem::exists( layers, ec ) ) {
            setenv( "VK_LAYER_PATH", layers.string().c_str(), 0 );
        }
        OTDU_LOG_INFO( "Loading Vulkan from '%s'\n", loader.string().c_str() );
        if ( SDL_Vulkan_LoadLibrary( loader.string().c_str() ) ) {
            return true;
        }
    }
#endif
    return false;
}

bool TestDriveGameInstance::createWindow() {
    if ( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS ) ) {
        OTDU_LOG_ERROR( "SDL_Init failed: %s\n", SDL_GetError() );
        return false;
    }
    if ( !LoadVulkanLibrary() ) {
        OTDU_LOG_ERROR( "Failed to load Vulkan: %s\n", SDL_GetError() );
        OTDU_LOG_ERROR( "On macOS install the LunarG Vulkan SDK (it provides MoltenVK) in ~/VulkanSDK "
                        "or set VULKAN_SDK / SDL_VULKAN_LIBRARY\n" );
        return false;
    }

    const VideoSettings &video = pConfig->getVideoSettings();
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if ( !video.bWindowed ) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    pWindow = SDL_CreateWindow( "OpenTDU2 - Test Drive Unlimited 2", video.Width, video.Height,
                                flags );
    if ( pWindow == nullptr ) {
        OTDU_LOG_ERROR( "SDL_CreateWindow failed: %s\n", SDL_GetError() );
        return false;
    }
    return true;
}

bool TestDriveGameInstance::initialize() {
    pFile = std::make_unique<GSFile>();
    pConfig = std::make_unique<GSConfig>();
    pDatabase = std::make_unique<GSDatabase>();
    pRender = std::make_unique<GSRender>();
    pInput = std::make_unique<GSInput>();
    pPhysics = std::make_unique<GSPhysics>();
    pFlash = std::make_unique<GSFlash>();
    pImGui = std::make_unique<GSImGui>();

    // Initialization order matters (FUN_ order of the original game services
    // list: file system, config, database...).
    services = { pFile.get(),     pConfig.get(),     pDatabase.get(),
                 pRender.get(),   pInput.get(),      pPhysics.get(),
                 pFlash.get(),    pImGui.get() };

    for ( GameSystem *pService : services ) {
        OTDU_LOG_INFO( "Initializing service '%s'...\n", pService->getName() );
        if ( !pService->initialize( this ) ) {
            OTDU_LOG_ERROR( "...service '%s' initialization FAILED!\n", pService->getName() );
            return false;
        }
        OTDU_LOG_INFO( "...service '%s' successfully initialized.\n", pService->getName() );

        // The window and the render device are created once the config is
        // known (window size, vsync...).
        if ( pService == pConfig.get() ) {
            if ( !createWindow() ) {
                return false;
            }
        } else if ( pService == pRender.get() ) {
            const VideoSettings &video = pConfig->getVideoSettings();
            if ( !pRender->createDevice( pWindow, video.bVSync,
                                         CmdLine::HasArg( "-vk_enable_validation_layer" ) ) ) {
                OTDU_LOG_ERROR( "Failed to create the Vulkan render device\n" );
                return false;
            }
        }
    }

    pAudio = std::make_unique<GSAudio>();
    if ( !CmdLine::HasArg( "-noaudio" ) ) {
        pAudio->initialize();
    }

    // Automated test support
    autoExitFrame = CmdLine::GetArgInt( "-exitframe", -1 );
    if ( const char *pAutoplay = CmdLine::GetArgValue( "-autoplay" ) ) {
        parseAutoplay( pAutoplay );
    }
    const char *pScreenshot = CmdLine::GetArgValue( "-screenshot" );
    if ( pScreenshot != nullptr ) {
        screenshotPath = pScreenshot;
        screenshotFrame = CmdLine::GetArgInt( "-screenshotframe", 60 );
    }

    eGameMode firstGameMode = eGameMode::GM_Login;
    const char *pGameModeArg = CmdLine::GetArgValue( "-gm" );
    if ( pGameModeArg != nullptr ) {
        eGameMode requested = GetGameModeFromName( pGameModeArg );
        if ( requested == eGameMode::GM_None ) {
            requested = static_cast<eGameMode>( atoi( pGameModeArg ) );
        }
        if ( requested > eGameMode::GM_None && requested < eGameMode::GM_Count ) {
            firstGameMode = requested;
        }
    }
    if ( CmdLine::HasArg( "-intro" ) ) {
        firstGameMode = eGameMode::GM_AvatarChoice;
    }
    setNextGameMode( firstGameMode );
    return true;
}

void TestDriveGameInstance::terminate() {
    if ( pRender != nullptr ) {
        pRender->getDevice().waitIdle();
    }
    if ( pActiveGameMode != nullptr ) {
        pActiveGameMode->exit();
        pActiveGameMode.reset();
    }
    pAudio.reset();
    for ( auto it = services.rbegin(); it != services.rend(); ++it ) {
        ( *it )->terminate();
    }
    services.clear();

    pImGui.reset();
    pFlash.reset();
    pPhysics.reset();
    pInput.reset();
    pRender.reset();
    pDatabase.reset();
    pConfig.reset();
    pFile.reset();

    if ( pWindow != nullptr ) {
        SDL_DestroyWindow( pWindow );
        pWindow = nullptr;
    }
    SDL_Quit();
}

void TestDriveGameInstance::setNextGameMode( eGameMode mode ) { nextGameMode = mode; }

std::unique_ptr<GameMode> TestDriveGameInstance::createGameMode( eGameMode mode ) {
    switch ( mode ) {
    case eGameMode::GM_Login:
        return std::make_unique<GMLogin>();
    case eGameMode::GM_AvatarChoice:
        return std::make_unique<GMAvatarChoice>();
    case eGameMode::GM_FirstWalkMode:
        return std::make_unique<GMFirstWalkMode>();
    case eGameMode::GM_FreeRide:
        return std::make_unique<GMFreeRide>();
    default:
        OTDU_LOG_WARN( "Game mode '%s' is not implemented yet\n", GetGameModeName( mode ) );
        return nullptr;
    }
}

void TestDriveGameInstance::switchToNextGameMode() {
    if ( nextGameMode == eGameMode::GM_None ) {
        return;
    }
    const eGameMode mode = nextGameMode;
    nextGameMode = eGameMode::GM_None;

    pRender->getDevice().waitIdle();
    if ( pActiveGameMode != nullptr ) {
        pActiveGameMode->exit();
        pActiveGameMode.reset();
    }

    OTDU_LOG_ALWAYS( "--------------------------------------------------\n" );
    OTDU_LOG_ALWAYS( "|              GAME MODE : %s\n", GetGameModeName( mode ) );
    OTDU_LOG_ALWAYS( "--------------------------------------------------\n" );

    pActiveGameMode = createGameMode( mode );
    if ( pActiveGameMode != nullptr && !pActiveGameMode->enter( this ) ) {
        OTDU_LOG_ERROR( "Failed to enter game mode '%s'\n", GetGameModeName( mode ) );
        pActiveGameMode.reset();
        if ( mode != eGameMode::GM_Login ) {
            nextGameMode = eGameMode::GM_Login;
        }
    }
}

void TestDriveGameInstance::pumpEvents() {
    pInput->beginFrame();
    SDL_Event event;
    while ( SDL_PollEvent( &event ) ) {
        pImGui->processEvent( event );
        pInput->processEvent( event, pImGui->wantsKeyboard(), pImGui->wantsMouse() );
        switch ( event.type ) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            bRequestedExit = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            pRender->onWindowResized();
            break;
        case SDL_EVENT_KEY_DOWN:
            if ( event.key.scancode == SDL_SCANCODE_F1 && !event.key.repeat ) {
                bShowDebugUI = !bShowDebugUI;
            }
            break;
        default:
            break;
        }
    }
}

void TestDriveGameInstance::mainLoop() {
    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>( SDL_GetPerformanceFrequency() );
    const bool bFixedTimestep = CmdLine::HasArg( "-fixedtimestep" );

    while ( !bRequestedExit ) {
        switchToNextGameMode();

        const uint64_t counter = SDL_GetPerformanceCounter();
        float deltaTime = static_cast<float>( ( counter - lastCounter ) / frequency );
        lastCounter = counter;
        if ( bFixedTimestep ) {
            deltaTime = 1.0f / 60.0f;
        }
        deltaTime = Clamp( deltaTime, 0.0001f, 0.1f );
        totalTime += deltaTime;

        pumpEvents();
        runAutoplay();

        for ( GameSystem *pService : services ) {
            if ( !pService->isPaused() ) {
                pService->tick( deltaTime );
            }
        }

        if ( pActiveGameMode != nullptr ) {
            pActiveGameMode->tick( deltaTime );
        }
        pAudio->tick();

        if ( screenshotFrame >= 0 && static_cast<int64_t>( frameIndex ) == screenshotFrame ) {
            pRender->getDevice().requestScreenshot( screenshotPath.c_str() );
        }

        if ( pRender->beginFrame() ) {
            if ( pActiveGameMode != nullptr ) {
                pActiveGameMode->drawOffscreen();
            }
            pRender->getDevice().beginMainPass();
            if ( pActiveGameMode != nullptr ) {
                pActiveGameMode->draw();
            }

            pImGui->beginFrame( deltaTime );
            if ( bShowDebugUI ) {
                for ( GameSystem *pService : services ) {
                    pService->drawDebug();
                }
                if ( pActiveGameMode != nullptr ) {
                    pActiveGameMode->drawDebug();
                }
            }
            pImGui->endFrame();
            pRender->endFrame();
        } else {
            SDL_Delay( 16 );
        }

        frameIndex++;
        if ( autoExitFrame >= 0 && static_cast<int64_t>( frameIndex ) >= autoExitFrame ) {
            bRequestedExit = true;
        }
    }
}

// Scripted input for automated tests, e.g.
//   -autoplay "60:press=validate;90:text=Camille;120:shot=/tmp/a.ppm"
// Commands: press=<action>, hold=<action>, release=<action>, text=<utf8>, shot=<path>,
//           mouse=<dx>,<dy> (relative mouse motion, pixels)
void TestDriveGameInstance::parseAutoplay( const char *pScript ) {
    std::string script( pScript );
    size_t start = 0;
    while ( start < script.size() ) {
        size_t end = script.find( ';', start );
        if ( end == std::string::npos ) {
            end = script.size();
        }
        const std::string entry = script.substr( start, end - start );
        start = end + 1;
        const size_t colon = entry.find( ':' );
        if ( colon == std::string::npos ) {
            continue;
        }
        AutoplayCommand command;
        command.Frame = strtoull( entry.substr( 0, colon ).c_str(), nullptr, 10 );
        const std::string body = entry.substr( colon + 1 );
        const size_t equal = body.find( '=' );
        command.Command = body.substr( 0, equal );
        command.Argument = equal != std::string::npos ? body.substr( equal + 1 ) : std::string();
        autoplay.push_back( command );
    }
}

void TestDriveGameInstance::runAutoplay() {
    static const std::pair<const char *, eInputAction> kActions[] = {
        { "validate", eInputAction::IA_MenuValidate }, { "back", eInputAction::IA_MenuBack },
        { "pause", eInputAction::IA_MenuPause },       { "up", eInputAction::IA_MenuUp },
        { "down", eInputAction::IA_MenuDown },         { "left", eInputAction::IA_MenuLeft },
        { "right", eInputAction::IA_MenuRight },       { "opt1", eInputAction::IA_MenuOption1 },
        { "accel", eInputAction::IA_CarAccel },        { "brake", eInputAction::IA_CarBrake },
        { "steerleft", eInputAction::IA_CarLeft },     { "steerright", eInputAction::IA_CarRight },
        { "handbrake", eInputAction::IA_CarHandBrake }, { "camera", eInputAction::IA_CarCamera },
        { "walk", eInputAction::IA_AvatarForward },    { "walkleft", eInputAction::IA_AvatarLeft },
        { "walkright", eInputAction::IA_AvatarRight },
    };
    for ( const AutoplayCommand &command : autoplay ) {
        if ( command.Frame != frameIndex ) {
            continue;
        }
        OTDU_LOG_INFO( "Autoplay frame %llu: %s=%s\n", static_cast<unsigned long long>( frameIndex ),
                       command.Command.c_str(), command.Argument.c_str() );
        if ( command.Command == "text" ) {
            pInput->injectText( command.Argument );
            continue;
        }
        if ( command.Command == "mouse" ) {
            float dx = 0.0f, dy = 0.0f;
            sscanf( command.Argument.c_str(), "%f,%f", &dx, &dy );
            pInput->injectMouseDelta( dx, dy );
            continue;
        }
        if ( command.Command == "shot" ) {
            pRender->getDevice().requestScreenshot( command.Argument.c_str() );
            continue;
        }
        for ( const auto &action : kActions ) {
            if ( command.Argument != action.first ) {
                continue;
            }
            if ( command.Command == "press" ) {
                pInput->injectPress( action.second );
            } else if ( command.Command == "hold" ) {
                pInput->setInjectedHold( action.second, true );
            } else if ( command.Command == "release" ) {
                pInput->setInjectedHold( action.second, false );
            }
        }
    }
}
