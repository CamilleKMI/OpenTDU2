#include "shared.h"
#include "gm_world_viewer.h"

#include "audio/gs_audio.h"
#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "game/freeride/chase_camera.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"

#include <SDL3/SDL.h>

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kMaxPitch = 89.0f * kPi / 180.0f;
// Spot start: camera above the spot (developer tool values) [INF].
constexpr float kSpotHeight = 25.0f;
constexpr float kGroundClearance = 1.5f;
constexpr float kNoSectorHeight = 400.0f; // no sector bank there: high enough to see the island level heightfield
// OpenTDU CameraMouse sensitivity steps per wheel notch (DAT_00f4174c / DAT_00f417e4).
constexpr float kWheelUp = 1.1f;
constexpr float kWheelDown = 0.9090909f;
// Spot exteriors streamed within this horizontal distance, dropped beyond
// the second one [INF: developer tool values].
constexpr float kExteriorLoadDistance = 1500.0f;
constexpr float kExteriorUnloadDistance = 2000.0f;

float HorizontalDistance( const Vector3f &a, const Vector3f &b ) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt( dx * dx + dz * dz );
}

const char *IslandLabel( const char *pIsland ) {
    if ( strcmp( pIsland, "ibiza" ) == 0 ) {
        return "Ibiza";
    }
    if ( strcmp( pIsland, "hawai" ) == 0 ) {
        return "Hawai";
    }
    return "Casino";
}

int32_t SectorKey( const Vector3f &p ) {
    const EdenWorld::SectorId id = EdenWorld::getSectorAt( p );
    const int32_t column = id.AreaX * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + id.X;
    const int32_t row = id.AreaY * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + id.Y;
    return row * 1024 + column;
}

bool IsKnownIsland( const std::string &name ) {
    for ( const char *pIsland : GMWorldViewer::kIslands ) {
        if ( name == pIsland ) {
            return true;
        }
    }
    return false;
}
} // namespace

GMWorldViewer::GMWorldViewer() : GameMode( eGameMode::GM_WorldViewer ) {}

GMWorldViewer::~GMWorldViewer() { exit(); }

bool GMWorldViewer::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    sectorRadius = Clamp( CmdLine::GetArgInt( "-sectorradius", 1 ), 0, 4 );
    bExteriors = !CmdLine::HasArg( "-noexteriors" );
    bShowPanel = !CmdLine::HasArg( "-nopanel" );
    if ( const char *pSpeed = CmdLine::GetArgValue( "-flyspeed" ) ) {
        baseSpeed = Clamp( static_cast<float>( atof( pSpeed ) ), 1.0f, 5000.0f );
    }
    const char *pIsland = CmdLine::GetArgValue( "-island" );
    std::string name = ToLower( std::string( pIsland != nullptr ? pIsland : "ibiza" ) );
    if ( name == "hawaii" || name == "oahu" ) {
        name = "hawai";
    }
    if ( !IsKnownIsland( name ) ) {
        OTDU_LOG_WARN( "WORLD_VIEWER: unknown island '%s' (ibiza, hawai, casino), using ibiza\n", name.c_str() );
        name = "ibiza";
    }
    // Audio mix: the World Viewer stands for the free roaming mode (game mode
    // FREERIDE -> mix preset of audiosetup.xmb) [INF].
    if ( gpAudio != nullptr && gpAudio->isReady() ) {
        gpAudio->setGameMode( "FREERIDE" );
    }
    // The command line camera applies to the first island only.
    loadIsland( name );
    if ( const char *pPosition = CmdLine::GetArgValue( "-campos" ) ) {
        Vector3f p;
        if ( sscanf( pPosition, "%f,%f,%f", &p.x, &p.y, &p.z ) == 3 ) {
            position = p;
        }
    }
    if ( const char *pYaw = CmdLine::GetArgValue( "-camyaw" ) ) {
        yaw = static_cast<float>( atof( pYaw ) ) * kPi / 180.0f;
    }
    if ( const char *pPitch = CmdLine::GetArgValue( "-campitch" ) ) {
        pitch = Clamp( static_cast<float>( atof( pPitch ) ) * kPi / 180.0f, -kMaxPitch, kMaxPitch );
    }
    streamWorld( true );
    // The mode stays active when the island fails to load: the panel lets
    // the user pick another one.
    return true;
}

void GMWorldViewer::loadSpots() {
    spots.clear();
    std::vector<uint8_t> data;
    const std::string path = gpFile->getBankFolder() + "islands/" + island + "/level/spots.bin";
    if ( !gpFile->loadFile( path.c_str(), data ) || data.size() < 0x20 || memcmp( data.data() + 0x10, "SPTA", 4 ) != 0 ) {
        OTDU_LOG_WARN( "WORLD_VIEWER: '%s' not found or not a spot list\n", path.c_str() );
        return;
    }
    uint32_t size = 0;
    memcpy( &size, data.data() + 0x14, 4 );
    for ( size_t offset = 0x20; offset + 0x110 <= data.size() && offset < 0x20 + static_cast<size_t>( size );
          offset += 0x110 ) {
        Spot spot;
        memcpy( &spot.Id, data.data() + offset, 8 );
        char bank[9] = {};
        memcpy( bank, data.data() + offset + 8, 8 );
        spot.Bank = bank;
        memcpy( &spot.Position, data.data() + offset + 0x10, 12 );
        memcpy( &spot.Heading, data.data() + offset + 0x20, 4 );
        // Sector banks present around the spot (3 x 3 sectors): the start
        // spot is the best covered one (partial game data).
        const EdenWorld::SectorId center = EdenWorld::getSectorAt( spot.Position );
        const int32_t centerColumn = center.AreaX * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + center.X;
        const int32_t centerRow = center.AreaY * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + center.Y;
        for ( int32_t row = centerRow - 1; row <= centerRow + 1; row++ ) {
            for ( int32_t column = centerColumn - 1; column <= centerColumn + 1; column++ ) {
                if ( row < 0 || column < 0 ) {
                    continue;
                }
                EdenWorld::SectorId id;
                id.AreaX = column / static_cast<int32_t>( EdenWorld::kSectorsPerArea );
                id.AreaY = row / static_cast<int32_t>( EdenWorld::kSectorsPerArea );
                id.X = column % static_cast<int32_t>( EdenWorld::kSectorsPerArea );
                id.Y = row % static_cast<int32_t>( EdenWorld::kSectorsPerArea );
                if ( gpFile->fileExists( EdenWorld::GetSectorBankPath( island.c_str(), id ).c_str() ) ) {
                    spot.NumSectors++;
                    spot.bHasSector |= id == center;
                }
            }
        }
        const std::string exterior = gpFile->getBankFolder() + "islands/" + island + "/exterior/" + ToLower( spot.Bank ) + ".bnk";
        if ( !spot.Bank.empty() && gpFile->fileExists( exterior.c_str() ) ) {
            spot.ExteriorPath = exterior;
        }
        spots.push_back( spot );
    }
    OTDU_LOG_INFO( "WORLD_VIEWER: %s: %zu spots\n", island.c_str(), spots.size() );
}

bool GMWorldViewer::loadIsland( const std::string &name ) {
    unloadIsland();
    island = name;
    statusText.clear();
    loadSpots();

    // Start spot: -spot <bank name>, else the spot with the most sector banks
    // present around it (partial game data), else the first spot.
    selectedSpot = -1;
    const char *pSpotArg = CmdLine::GetArgValue( "-spot" );
    for ( size_t i = 0; i < spots.size() && pSpotArg != nullptr; i++ ) {
        if ( StringCmpI( spots[i].Bank.c_str(), pSpotArg ) == 0 ) {
            selectedSpot = static_cast<int32_t>( i );
            break;
        }
    }
    if ( selectedSpot < 0 ) {
        uint32_t bestSectors = 0;
        for ( size_t i = 0; i < spots.size(); i++ ) {
            if ( spots[i].bHasSector && spots[i].NumSectors > bestSectors ) {
                bestSectors = spots[i].NumSectors;
                selectedSpot = static_cast<int32_t>( i );
            }
        }
    }
    if ( selectedSpot < 0 && !spots.empty() ) {
        selectedSpot = 0;
    }
    if ( selectedSpot >= 0 ) {
        const Spot &spot = spots[selectedSpot];
        teleport( spot.Position, spot.Heading, spot.bHasSector ? kSpotHeight : kNoSectorHeight );
        OTDU_LOG_INFO( "WORLD_VIEWER: start spot %s (%.1f, %.1f, %.1f)%s\n", spot.Bank.c_str(), spot.Position.x,
                       spot.Position.y, spot.Position.z, spot.bHasSector ? "" : " (no sector bank there)" );
    } else {
        teleport( Vector3f( 3072.0f, 0.0f, 3072.0f ), 0.0f, kNoSectorHeight );
    }

    if ( !view.initialize( island.c_str(), position, sectorRadius ) ) {
        statusText = "island '" + island + "' could not be loaded (Euro/Bnk/islands/" + island + "/level)";
        OTDU_LOG_ERROR( "WORLD_VIEWER: %s\n", statusText.c_str() );
        view.destroy();
        return false;
    }
    view.getWorld().setIslandTerrainEnabled( true );
    // Island ambient effects (-F.pmi + fx/vfx_env.bnk); -noworldfx: off.
    view.getWorld().setEffectsEnabled( !CmdLine::HasArg( "-noworldfx" ) );
    view.setDrawCallback( [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        if ( pass == EdenSceneView::ePass::ProjectorActors ) {
            return;
        }
        for ( const auto &pExterior : exteriors ) {
            pExterior->draw( renderer );
        }
    } );
    // Island weather: GSWeather evaluation of game_weathers.xmb (time keys x
    // intemperies, WeatherSystem), plus the fixed presets of
    // specific_weathers.xmb / cinematic_weathers.xmb (WeatherControls).
    weather.load( island );
    weather.applyArguments( view, "WORLD_VIEWER" );
    pWeather = weather.getInitialWeather( view );
    view.setWeather( pWeather );
    view.setShadowConfig( EdenShadows::FindConfig( "FREERIDE" ) );
    // GSReflection (pool refraction / planar mirrors of the spot exteriors):
    // off like FREERIDE (mode 1, table 0x00f817e0), the free roaming mode
    // this tool stands for; -gsreflection turns it on (the value of the
    // SPOT_VIEWER / LIVING / CASINO... modes) for comparisons.
    view.setPlanarReflectionsEnabled( CmdLine::HasArg( "-gsreflection" ) ||
                                      EdenPlanarReflections::IsEnabledForGameMode( 0x01 ) );
    lastSectorKey = SectorKey( position );
    bWorldReady = true;
    const EdenWorld &world = view.getWorld();
    OTDU_LOG_INFO( "WORLD_VIEWER: %s ready: %d x %d areas grid, %zu areas, %zu sectors, island heightfield %s, weather '%s'\n",
                   island.c_str(), world.getAreaColumns(), world.getAreaRows(), world.getNumAreas(), world.getNumSectors(),
                   world.hasIslandTerrain() ? "yes" : "no", pWeather != nullptr ? pWeather->Name.c_str() : "(none)" );
    return true;
}

void GMWorldViewer::unloadIsland() {
    if ( gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
    }
    exteriors.clear();
    view.destroy();
    pWeather = nullptr;
    bWorldReady = false;
    lastSectorKey = INT32_MIN;
}

void GMWorldViewer::teleport( const Vector3f &target, float heading, float height ) {
    position = target + Vector3f( 0.0f, height, 0.0f );
    yaw = heading;
    pitch = -15.0f * kPi / 180.0f;
}

Vector3f GMWorldViewer::getForward() const {
    return Vector3f( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
}

void GMWorldViewer::setRelativeMouse( bool bEnable ) {
    if ( bEnable == bRelativeMouse || pGameInstance == nullptr || pGameInstance->getWindow() == nullptr ) {
        return;
    }
    bRelativeMouse = bEnable;
    SDL_SetWindowRelativeMouseMode( pGameInstance->getWindow(), bEnable );
}

void GMWorldViewer::updateCamera( float deltaTime ) {
    // Look: a mouse button held outside the panel (OpenTDU CameraMouse
    // "only while a mouse button is held"), or the Tab captured mode.
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_TAB ) ) {
        bCaptured = !bCaptured;
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F2 ) ) {
        bShowPanel = !bShowPanel;
    }
    // F5 / F6 / F7: Ibiza / Hawai / Casino (same as the panel buttons).
    for ( uint32_t i = 0; i < 3; i++ ) {
        if ( gpInput->wasKeyPressed( SDL_SCANCODE_F5 + i ) && island != kIslands[i] ) {
            pendingIsland = kIslands[i];
        }
    }
    const bool bButton = gpInput->isMouseButtonDown( SDL_BUTTON_RIGHT ) || gpInput->isMouseButtonDown( SDL_BUTTON_LEFT );
    if ( !bLooking && bButton && ( gpImGui == nullptr || !gpImGui->wantsMouse() ) ) {
        bLooking = true;
    } else if ( bLooking && !bButton ) {
        bLooking = false;
    }
    setRelativeMouse( bLooking || bCaptured );
    if ( bLooking || bCaptured ) {
        yaw -= gpInput->getMouseDeltaX() * lookSensitivity;
        pitch = Clamp( pitch - gpInput->getMouseDeltaY() * lookSensitivity, -kMaxPitch, kMaxPitch );
        if ( yaw > kPi ) {
            yaw -= 2.0f * kPi;
        } else if ( yaw < -kPi ) {
            yaw += 2.0f * kPi;
        }
    }

    // Base speed: mouse wheel (x1.1 per notch, OpenTDU), Shift x2.
    const float wheel = gpInput->getMouseWheel();
    if ( wheel > 0.0f ) {
        baseSpeed = Min( baseSpeed * std::pow( kWheelUp, wheel ), 5000.0f );
    } else if ( wheel < 0.0f ) {
        baseSpeed = Max( baseSpeed * std::pow( kWheelDown, -wheel ), 1.0f );
    }
    auto key = []( SDL_Scancode a, SDL_Scancode b = SDL_SCANCODE_UNKNOWN, SDL_Scancode c = SDL_SCANCODE_UNKNOWN ) {
        return gpInput->isKeyDown( a ) || ( b != SDL_SCANCODE_UNKNOWN && gpInput->isKeyDown( b ) ) ||
               ( c != SDL_SCANCODE_UNKNOWN && gpInput->isKeyDown( c ) );
    };
    const bool bFast = key( SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT );
    currentSpeed = baseSpeed * ( bFast ? 2.0f : 1.0f );

    const Vector3f up( 0.0f, 1.0f, 0.0f );
    const Vector3f forward = getForward();
    // Eden camera basis: x = up ^ forward is the left of the screen
    // (ChaseCamera::LookRotation), so the right is forward ^ up.
    Vector3f right = forward.cross( up );
    right = right.length() > 1e-4f ? right.normalized() : Vector3f( -1.0f, 0.0f, 0.0f );
    Vector3f move;
    if ( key( SDL_SCANCODE_W, SDL_SCANCODE_UP ) ) {
        move = move + forward;
    }
    if ( key( SDL_SCANCODE_S, SDL_SCANCODE_DOWN ) ) {
        move = move - forward;
    }
    if ( key( SDL_SCANCODE_D, SDL_SCANCODE_RIGHT ) ) {
        move = move + right;
    }
    if ( key( SDL_SCANCODE_A, SDL_SCANCODE_LEFT ) ) {
        move = move - right;
    }
    if ( key( SDL_SCANCODE_E, SDL_SCANCODE_SPACE, SDL_SCANCODE_PAGEUP ) ) {
        move = move + up;
    }
    if ( key( SDL_SCANCODE_Q, SDL_SCANCODE_LCTRL, SDL_SCANCODE_PAGEDOWN ) ) {
        move = move - up;
    }
    if ( move.length() > 1e-4f ) {
        position = position + move.normalized() * ( currentSpeed * deltaTime );
    }
    // Ground clamp (loaded sector heightfields only): the camera stays above
    // the terrain unless turned off in the panel.
    float ground = 0.0f;
    if ( bGroundClamp && bWorldReady && view.getWorld().getHeight( position.x, position.z, ground ) &&
         position.y < ground + kGroundClearance ) {
        position.y = ground + kGroundClearance;
    }
}

void GMWorldViewer::streamWorld( bool bForce ) {
    if ( !bWorldReady ) {
        return;
    }
    const int32_t key = SectorKey( position );
    if ( key == lastSectorKey && !bForce ) {
        return;
    }
    lastSectorKey = key;
    EdenWorld &world = view.getWorld();
    // Hysteresis: sectors are dropped one ring beyond the load radius.
    if ( world.unloadFar( position, sectorRadius + 1 ) != 0 ) {
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
        }
    }
    world.loadAround( position, sectorRadius );
}

void GMWorldViewer::streamExteriors() {
    // Spots sharing a bank share one SpotExterior (one frame per spot).
    bool bUnloaded = false;
    bool bLoadedAny = false;
    for ( auto it = exteriors.begin(); it != exteriors.end(); ) {
        bool bKeep = false;
        for ( const Spot &spot : spots ) {
            bKeep |= bExteriors && spot.ExteriorPath == ( *it )->getPath() &&
                     HorizontalDistance( spot.Position, position ) < kExteriorUnloadDistance;
        }
        if ( !bKeep ) {
            // Slots resolved from the other exteriors' banks (one of them
            // may own the texture): looked up again below.
            for ( auto &pOther : exteriors ) {
                pOther->clearForeignTextures();
            }
            it = exteriors.erase( it );
            bUnloaded = true;
        } else {
            ++it;
        }
    }
    if ( bUnloaded ) {
        // Lightmap / shadow caster caches hold material pointers.
        view.clearLightmapMaterials();
        for ( auto &pExterior : exteriors ) {
            pExterior->registerLightmaps( view );
        }
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
        }
    }
    if ( !bExteriors ) {
        return;
    }
    for ( const Spot &spot : spots ) {
        if ( spot.ExteriorPath.empty() || HorizontalDistance( spot.Position, position ) >= kExteriorLoadDistance ) {
            continue;
        }
        bool bLoaded = false;
        for ( const auto &pExterior : exteriors ) {
            bLoaded |= pExterior->getPath() == spot.ExteriorPath;
        }
        if ( bLoaded ) {
            continue;
        }
        auto pExterior = std::make_unique<SpotExterior>();
        if ( !pExterior->load( view, spot.ExteriorPath ) ) {
            continue;
        }
        // Every spot of this bank: T(position) R_y(heading) (FirstOutro).
        for ( const Spot &other : spots ) {
            if ( other.ExteriorPath == spot.ExteriorPath ) {
                pExterior->getFrames().push_back(
                    Matrix4x4f::Translation( other.Position ) *
                    Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), other.Heading ) ) );
            }
        }
        pExterior->registerReflections( view );
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pExterior->startEffects( *pRenderer );
        }
        exteriors.push_back( std::move( pExterior ) );
        bLoadedAny = true;
    }
    if ( bLoadedAny || bUnloaded ) {
        // Textures of a spot packed in another exterior bank of the same
        // place (ECAS windows_05 REFLECTION "CUBEMAP" -> elandca.bnk).
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            for ( auto &pExterior : exteriors ) {
                if ( const uint32_t numResolved = pExterior->resolveForeignTextures( *pRenderer ) ) {
                    OTDU_LOG_INFO( "WORLD_VIEWER: %s: %u texture slot(s) resolved from the other exterior banks\n",
                                   pExterior->getPath().c_str(), numResolved );
                }
            }
        }
    }
}

void GMWorldViewer::tick( float deltaTime ) {
    if ( !pendingIsland.empty() ) {
        const std::string name = pendingIsland;
        pendingIsland.clear();
        loadIsland( name );
    }
    updateCamera( deltaTime );
    if ( !bWorldReady ) {
        return;
    }
    streamWorld( false );
    streamExteriors();
    for ( auto &pExterior : exteriors ) {
        pExterior->update( deltaTime );
    }

    EdenSceneView::Camera camera;
    camera.Position = position;
    camera.Rotation = ChaseCamera::LookRotation( position, position + getForward() );
    camera.Fov = fovDegrees * kPi / 180.0f;
    view.setCamera( camera );
    // Clock + weather (FUN_00af8ce0 / FUN_00af8f60): the evaluated weather,
    // the forecast cloud sets, the game time of the frame (clouds), the rain.
    pWeather = weather.tick( deltaTime, view );
    view.setEnvironmentCenter( position, false );
    view.tick( deltaTime );
    // Listener = the free-fly camera (GSAudio::SetListener FUN_00d944b0,
    // camera listener FUN_00c3e530).
    if ( gpAudio != nullptr ) {
        gpAudio->setListener( position, getForward(), Vector3f() );
    }
}

void GMWorldViewer::drawOffscreen() {
    if ( bWorldReady && gpRender->getEdenRenderer() != nullptr ) {
        view.renderOffscreen();
    }
}

void GMWorldViewer::draw() {
    if ( bWorldReady ) {
        view.draw();
    }
}

void GMWorldViewer::exit() {
    setRelativeMouse( false );
    if ( bWorldReady || view.isReady() ) {
        unloadIsland();
    }
}

void GMWorldViewer::drawUI() {
    if ( !bShowPanel ) {
        return;
    }
    ImGui::SetNextWindowPos( ImVec2( 10.0f, 10.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( ImVec2( 400.0f, 0.0f ), ImGuiCond_FirstUseEver );
    if ( !ImGui::Begin( "World Viewer" ) ) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted( "Island" );
    for ( const char *pIsland : kIslands ) {
        ImGui::SameLine();
        if ( ImGui::RadioButton( IslandLabel( pIsland ), island == pIsland ) && island != pIsland ) {
            pendingIsland = pIsland;
        }
    }
    if ( !pendingIsland.empty() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "loading %s...", IslandLabel( pendingIsland.c_str() ) );
    }
    if ( !statusText.empty() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.3f, 1.0f ), "%s", statusText.c_str() );
    }
    if ( bWorldReady ) {
        const EdenWorld &world = view.getWorld();
        const EdenWorld::SectorId id = EdenWorld::getSectorAt( position );
        ImGui::Text( "grid %d x %d areas, %zu areas, %zu sectors loaded", world.getAreaColumns(), world.getAreaRows(),
                     world.getNumAreas(), world.getNumSectors() );
        ImGui::Text( "missing sector banks skipped: %zu%s", world.getNumFailedSectors(),
                     world.hasIslandTerrain() ? ", island heightfield" : "" );
        ImGui::Text( "sector-%d-%d-%d-%d", id.AreaX, id.AreaY, id.X, id.Y );
        ImGui::Text( "ambient effects: %u placed, %u running", world.getNumEffects(), world.getNumRunningEffects() );
    }
    ImGui::Separator();
    ImGui::Text( "pos %.1f %.1f %.1f", position.x, position.y, position.z );
    ImGui::Text( "yaw %.1f pitch %.1f deg", yaw * 180.0f / kPi, pitch * 180.0f / kPi );
    ImGui::Text( "speed %.1f m/s (%.0f km/h)%s", currentSpeed, currentSpeed * 3.6f,
                 currentSpeed > baseSpeed ? "  [Shift x2]" : "" );
    ImGui::SliderFloat( "base speed", &baseSpeed, 1.0f, 2000.0f, "%.0f m/s", ImGuiSliderFlags_Logarithmic );
    ImGui::SliderFloat( "fov", &fovDegrees, 20.0f, 110.0f, "%.0f deg" );
    ImGui::SliderFloat( "look", &lookSensitivity, 0.0005f, 0.01f, "%.4f rad/px", ImGuiSliderFlags_Logarithmic );
    if ( ImGui::SliderInt( "sector radius", &sectorRadius, 0, 4 ) ) {
        streamWorld( true );
    }
    ImGui::Checkbox( "ground clamp", &bGroundClamp );
    ImGui::SameLine();
    ImGui::Checkbox( "spot exteriors", &bExteriors );
    ImGui::SameLine();
    ImGui::Text( "(%zu banks loaded)", exteriors.size() );
    weather.drawPanel( view );
    if ( !spots.empty() ) {
        ImGui::Separator();
        ImGui::Text( "spots.bin: %zu spots (n = sector banks present /9)", spots.size() );
        if ( ImGui::BeginListBox( "##spots", ImVec2( -1.0f, 160.0f ) ) ) {
            for ( size_t i = 0; i < spots.size(); i++ ) {
                const Spot &spot = spots[i];
                char label[96];
                snprintf( label, sizeof( label ), "%u %-8s (%.0f, %.0f, %.0f)##%zu", spot.NumSectors,
                          spot.Bank.c_str(), spot.Position.x, spot.Position.y, spot.Position.z, i );
                if ( ImGui::Selectable( label, selectedSpot == static_cast<int32_t>( i ) ) ) {
                    selectedSpot = static_cast<int32_t>( i );
                    teleport( spot.Position, spot.Heading, spot.bHasSector ? kSpotHeight : kNoSectorHeight );
                    streamWorld( true );
                }
            }
            ImGui::EndListBox();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled( "RMB (or LMB outside panel): look, Tab: capture mouse" );
    ImGui::TextDisabled( "WSAD / arrows: move, E/Space up, Q/Ctrl down" );
    ImGui::TextDisabled( "Shift: x2, wheel: base speed, F2: panel, F1: debug" );
    ImGui::TextDisabled( "F5 / F6 / F7: Ibiza / Hawai / Casino" );
    ImGui::End();
}
