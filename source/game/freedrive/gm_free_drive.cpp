#include "shared.h"
#include "gm_free_drive.h"

#include "audio/gs_audio.h"
#include "database/gs_database.h"
#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "game/worldviewer/gm_world_viewer.h"
#include "input/game_key_map.h"
#include "input/gs_input.h"
#include "physics/havok_collision.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace {
constexpr float kPi = 3.14159265f;
// Developer tool values [INF]: road search radius at a spot / for "back on
// the road", spot exteriors streamed within the first distance and dropped
// beyond the second (same as the World Viewer).
constexpr float kSpotSectorRoadSearch = 60.0f;
constexpr float kSpotRoadSearch = 300.0f;
constexpr float kRespawnNearSearch = 50.0f;
constexpr float kRespawnRoadSearch = 400.0f;
constexpr float kExteriorLoadDistance = 1500.0f;
constexpr float kExteriorUnloadDistance = 2000.0f;
// Car model origin put this far above the surface when placed (it settles on
// its suspension) [INF].
constexpr float kDropHeight = 0.3f;

const char *IslandLabel( const std::string &island ) {
    if ( island == "ibiza" ) {
        return "Ibiza";
    }
    if ( island == "hawai" ) {
        return "Hawai";
    }
    return "Casino";
}

std::string NormalizeIsland( std::string name ) {
    name = ToLower( name );
    if ( name == "hawaii" || name == "oahu" ) {
        name = "hawai";
    }
    for ( const char *pIsland : GMWorldViewer::kIslands ) {
        if ( name == pIsland ) {
            return name;
        }
    }
    OTDU_LOG_WARN( "FREE_DRIVE: unknown island '%s' (ibiza, hawai, casino), using ibiza\n", name.c_str() );
    return "ibiza";
}

int32_t SectorKey( const Vector3f &p ) {
    const EdenWorld::SectorId id = EdenWorld::getSectorAt( p );
    const int32_t column = id.AreaX * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + id.X;
    const int32_t row = id.AreaY * static_cast<int32_t>( EdenWorld::kSectorsPerArea ) + id.Y;
    return row * 1024 + column;
}

float HorizontalDistance( const Vector3f &a, const Vector3f &b ) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt( dx * dx + dz * dz );
}

Matrix4x4f WithoutTranslation( Matrix4x4f m ) {
    m.m[3][0] = m.m[3][1] = m.m[3][2] = 0.0f;
    return m;
}

// Closest point of the triangle (a, b, c) to p in the horizontal plane (x, z).
Vector3f ClosestPointXZ( const Vector3f &p, const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
    auto cross = []( float ax, float az, float bx, float bz ) { return ax * bz - az * bx; };
    const float d1 = cross( b.x - a.x, b.z - a.z, p.x - a.x, p.z - a.z );
    const float d2 = cross( c.x - b.x, c.z - b.z, p.x - b.x, p.z - b.z );
    const float d3 = cross( a.x - c.x, a.z - c.z, p.x - c.x, p.z - c.z );
    const bool bNeg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
    const bool bPos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
    if ( !( bNeg && bPos ) ) {
        return Vector3f( p.x, 0.0f, p.z ); // inside
    }
    Vector3f best;
    float bestDistance = 1e30f;
    const Vector3f *pEdges[3][2] = { { &a, &b }, { &b, &c }, { &c, &a } };
    for ( const auto &edge : pEdges ) {
        const Vector3f &e0 = *edge[0];
        const Vector3f &e1 = *edge[1];
        const float ex = e1.x - e0.x, ez = e1.z - e0.z;
        const float length2 = ex * ex + ez * ez;
        const float t = length2 > 1e-8f ? Clamp( ( ( p.x - e0.x ) * ex + ( p.z - e0.z ) * ez ) / length2, 0.0f, 1.0f ) : 0.0f;
        const float x = e0.x + ex * t, z = e0.z + ez * t;
        const float distance = ( x - p.x ) * ( x - p.x ) + ( z - p.z ) * ( z - p.z );
        if ( distance < bestDistance ) {
            bestDistance = distance;
            best = Vector3f( x, 0.0f, z );
        }
    }
    return best;
}

// Height of the triangle plane at (x, z) (y of the vertices if vertical).
float PlaneHeight( const Vector3f &a, const Vector3f &b, const Vector3f &c, float x, float z ) {
    const Vector3f n = ( b - a ).cross( c - a );
    if ( std::abs( n.y ) < 1e-6f ) {
        return ( a.y + b.y + c.y ) / 3.0f;
    }
    return a.y - ( n.x * ( x - a.x ) + n.z * ( z - a.z ) ) / n.y;
}

// Key of a Default_KB.xmb action (ChannelLocName of its first single-key
// entry), for the panel help; "" if none.
std::string GameKeyLabel( const char *pAction ) {
    const GameKeyMap &keys = GetGameKeyMap();
    const auto it = keys.find( pAction );
    if ( it == keys.end() ) {
        return std::string();
    }
    for ( const GameKeyBinding &binding : it->second ) {
        if ( binding.ChannelAnd.empty() && !binding.bAccessoryMenuOnly ) {
            return binding.LocName.empty() ? binding.Channel : binding.LocName;
        }
    }
    return std::string();
}

XmbValue FindDbRow( const char *pTable, const char *pField, uint64_t value ) {
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( pTable ); i++ ) {
        XmbValue row = root.getArrayElement( pTable, i );
        if ( row[pField].asUInt64() == value ) {
            return row;
        }
    }
    return XmbValue();
}
} // namespace

GMFreeDrive::GMFreeDrive() : GameMode( eGameMode::GM_FreeDrive ) {}

GMFreeDrive::~GMFreeDrive() { exit(); }

// ---------------------------------------------------------------------------
// Database car list

void GMFreeDrive::buildCarList() {
    cars.clear();
    if ( gpDatabase == nullptr ) {
        return;
    }
    const std::string folder = gpFile->getBankFolder() + "vehicules/";
    const bool bList = CmdLine::HasArg( "-listcars" );
    XmbValue root = gpDatabase->getDocument().getRoot();
    uint32_t numRows = 0;
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBCarPhysicsData", i );
        CarEntry entry;
        entry.IdCar = static_cast<int32_t>( row["_id_car"].asInt( -1 ) );
        entry.FileName = row["_car_file_name"].asString();
        if ( entry.FileName.empty() || entry.IdCar < 0 ) {
            continue;
        }
        numRows++;
        entry.HudFile = row["_hud_file_name"].asString();
        entry.Brand = FindDbRow( "DBBrands", "_hashcode", row["_car_brand"].asUInt64() )["_manufacturer_name"].asString();
        // "##": empty text field of the database.
        auto text = []( const std::string &value ) { return value == "##" ? std::string() : value; };
        entry.Label = text( entry.Brand ) + " " + text( row["_car_model"].asString() );
        const std::string version = text( row["_car_version"].asString() );
        if ( !version.empty() ) {
            entry.Label += " " + version;
        }
        const std::string carBank = folder + "high/" + ToLower( entry.FileName ) + ".bnk";
        CinematicCar::Desc desc;
        CinematicCar::FindDatabaseDesc( entry.IdCar, 0, desc );
        const std::string rimBank = folder + "rim/" + desc.RimFront + ".bnk";
        const std::string interiorBank = folder + "interiors/" + ToLower( entry.FileName ) + "_i.bnk";
        const std::string gaugeBank = gpFile->getBankFolder() + "frontend/hires/gauges/" + ToLower( entry.HudFile ) + ".bnk";
        const bool bCar = gpFile->fileExists( carBank.c_str() );
        if ( !gpFile->fileExists( rimBank.c_str() ) ) {
            entry.Missing += " rims";
        }
        if ( !gpFile->fileExists( interiorBank.c_str() ) ) {
            entry.Missing += " interior";
        }
        if ( entry.HudFile.empty() || !gpFile->fileExists( gaugeBank.c_str() ) ) {
            entry.Missing += " gauges";
        }
        if ( bList ) {
            OTDU_LOG_INFO( "FREE_DRIVE car %4d %-34s '%s': %s, rims %s, interior %s, gauges %s%s\n", entry.IdCar,
                           entry.FileName.c_str(), entry.Label.c_str(), carBank.c_str(), rimBank.c_str(),
                           interiorBank.c_str(), gaugeBank.c_str(), bCar ? "" : " (car bank missing)" );
        }
        // Several rows may share a car file (ids 371 / 501 / 502 Mustang
        // Fastback...): the first row in table order is listed.
        bool bListed = false;
        for ( const CarEntry &other : cars ) {
            bListed |= StringCmpI( other.FileName.c_str(), entry.FileName.c_str() ) == 0;
        }
        if ( bCar && !bListed ) {
            cars.push_back( entry );
        }
    }
    std::sort( cars.begin(), cars.end(), []( const CarEntry &a, const CarEntry &b ) {
        const int32_t brand = StringCmpI( a.Brand.c_str(), b.Brand.c_str() );
        return brand != 0 ? brand < 0 : StringCmpI( a.Label.c_str(), b.Label.c_str() ) < 0;
    } );
    OTDU_LOG_INFO( "FREE_DRIVE: %zu of %u database cars have their vehicules/high bank installed\n", cars.size(), numRows );
}

int32_t GMFreeDrive::findCar( const char *pText ) const {
    if ( pText == nullptr || pText[0] == '\0' ) {
        return -1;
    }
    const bool bNumber = pText[0] >= '0' && pText[0] <= '9';
    for ( size_t i = 0; i < cars.size(); i++ ) {
        if ( ( bNumber && cars[i].IdCar == atoi( pText ) ) || StringCmpI( cars[i].FileName.c_str(), pText ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    // Text of the label ("mustang", "esprit", "lancia delta").
    const std::string needle = ToLower( std::string( pText ) );
    for ( size_t i = 0; i < cars.size(); i++ ) {
        if ( ToLower( cars[i].Label ).find( needle ) != std::string::npos ||
             ToLower( cars[i].FileName ).find( needle ) != std::string::npos ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

void GMFreeDrive::buildColorList() {
    colors.clear();
    if ( gpDatabase == nullptr || currentCar < 0 ) {
        return;
    }
    // DBCarsAndColors rows of the car in table order (the shop colour list,
    // docs/re/paint_spec.md 1).
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarsAndColors" ); i++ ) {
        XmbValue row = root.getArrayElement( "DBCarsAndColors", i );
        if ( static_cast<int32_t>( row["_car"].asUInt64() ) != cars[static_cast<size_t>( currentCar )].IdCar ) {
            continue;
        }
        ColorEntry color;
        color.Name = row["_color_name"].asString();
        color.ColorId = row["_color_id_1"].asString();
        colors.push_back( color );
    }
}

int32_t GMFreeDrive::findColor( const char *pText ) const {
    if ( pText == nullptr || pText[0] == '\0' ) {
        return -1;
    }
    for ( size_t i = 0; i < colors.size(); i++ ) {
        if ( StringCmpI( colors[i].ColorId.c_str(), pText ) == 0 || StringCmpI( colors[i].Name.c_str(), pText ) == 0 ) {
            return static_cast<int32_t>( i );
        }
    }
    if ( pText[0] >= '0' && pText[0] <= '9' ) {
        const int32_t index = atoi( pText );
        return index < static_cast<int32_t>( colors.size() ) ? index : -1;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Mode

bool GMFreeDrive::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    sectorRadius = Clamp( CmdLine::GetArgInt( "-sectorradius", 1 ), 0, 4 );
    bExteriors = !CmdLine::HasArg( "-noexteriors" );
    bShowPanel = !CmdLine::HasArg( "-nopanel" );
    // Audio mix: the free roaming mode FREERIDE (audiosetup.xmb) [INF].
    if ( gpAudio != nullptr && gpAudio->isReady() ) {
        gpAudio->setGameMode( "FREERIDE" );
    }
    OTDU_LOG_INFO( "FREE_DRIVE: Default_KB keys: BACK_ON_TRACK [%s], CAR_LIGHT [%s], CAR_CAMERA [%s], CAR_HBRAKE [%s]\n",
                   GameKeyLabel( "BACK_ON_TRACK" ).c_str(), GameKeyLabel( "CAR_LIGHT" ).c_str(),
                   GameKeyLabel( "CAR_CAMERA" ).c_str(), GameKeyLabel( "CAR_HBRAKE" ).c_str() );
    buildCarList();
    if ( cars.empty() ) {
        OTDU_LOG_ERROR( "FREE_DRIVE: no car bank installed (Euro/Bnk/vehicules/high/<car>.bnk)\n" );
        return false;
    }
    const char *pCar = CmdLine::GetArgValue( "-car" );
    currentCar = findCar( pCar != nullptr ? pCar : "California" );
    if ( currentCar < 0 ) {
        if ( pCar != nullptr ) {
            OTDU_LOG_WARN( "FREE_DRIVE: car '%s' not found or its bank is not installed\n", pCar );
        }
        currentCar = findCar( "California" );
        currentCar = currentCar < 0 ? 0 : currentCar;
    }
    // -color applies to the first car (resolved once its colour list is known).
    currentColor = -2;
    const char *pIsland = CmdLine::GetArgValue( "-island" );
    loadIsland( NormalizeIsland( pIsland != nullptr ? pIsland : "ibiza" ) );
    // The mode stays active when the island fails to load: the panel lets
    // the user pick another one.
    return true;
}

void GMFreeDrive::loadSpots() {
    spots.clear();
    std::vector<uint8_t> data;
    const std::string path = gpFile->getBankFolder() + "islands/" + island + "/level/spots.bin";
    if ( !gpFile->loadFile( path.c_str(), data ) || data.size() < 0x20 || memcmp( data.data() + 0x10, "SPTA", 4 ) != 0 ) {
        OTDU_LOG_WARN( "FREE_DRIVE: '%s' not found or not a spot list\n", path.c_str() );
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
        memcpy( &spot.Position2, data.data() + offset + 0x28, 12 );
        memcpy( &spot.Heading2, data.data() + offset + 0x38, 4 );
        if ( spot.Position2.x == 0.0f && spot.Position2.z == 0.0f ) {
            spot.Position2 = spot.Position;
            spot.Heading2 = spot.Heading;
        }
        const EdenWorld::SectorId center = EdenWorld::getSectorAt( spot.Position2 );
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
    OTDU_LOG_INFO( "FREE_DRIVE: %s: %zu spots\n", island.c_str(), spots.size() );
}

bool GMFreeDrive::loadIsland( const std::string &name ) {
    const bool bFirst = bFirstIsland; // command line start pose: first island only
    bFirstIsland = false;
    unloadIsland();
    island = name;
    statusText.clear();
    loadSpots();

    // Start: -carpos (first island), -spot <bank>, else the spot with the most
    // sector banks installed around it, else the first spot.
    selectedSpot = -1;
    const char *pSpotArg = bFirst ? CmdLine::GetArgValue( "-spot" ) : nullptr;
    for ( size_t i = 0; i < spots.size() && pSpotArg != nullptr; i++ ) {
        if ( StringCmpI( spots[i].Bank.c_str(), pSpotArg ) == 0 ) {
            selectedSpot = static_cast<int32_t>( i );
            break;
        }
    }
    if ( pSpotArg != nullptr && selectedSpot < 0 ) {
        OTDU_LOG_WARN( "FREE_DRIVE: spot '%s' not in %s spots.bin\n", pSpotArg, island.c_str() );
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
    Vector3f start( 3072.0f, 0.0f, 3072.0f );
    float heading = 0.0f;
    if ( selectedSpot >= 0 ) {
        start = spots[static_cast<size_t>( selectedSpot )].Position2;
        heading = spots[static_cast<size_t>( selectedSpot )].Heading2;
    }
    const char *pCarPos = bFirst ? CmdLine::GetArgValue( "-carpos" ) : nullptr;
    if ( pCarPos != nullptr ) {
        Vector3f p;
        if ( sscanf( pCarPos, "%f,%f,%f", &p.x, &p.y, &p.z ) == 3 ) {
            start = p;
            selectedSpot = -1;
        }
        if ( const char *pHeading = CmdLine::GetArgValue( "-carheading" ) ) {
            heading = static_cast<float>( atof( pHeading ) ) * kPi / 180.0f;
        }
    }

    if ( !view.initialize( island.c_str(), start, sectorRadius ) ) {
        statusText = "island '" + island + "' could not be loaded (Euro/Bnk/islands/" + island + "/level)";
        OTDU_LOG_ERROR( "FREE_DRIVE: %s\n", statusText.c_str() );
        view.destroy();
        return false;
    }
    view.getWorld().setIslandTerrainEnabled( true );
    // Island ambient effects (-F.pmi + fx/vfx_env.bnk); -noworldfx: off.
    view.getWorld().setEffectsEnabled( !CmdLine::HasArg( "-noworldfx" ) );
    // Driving collision: sector .shk / -R.shk, the commonworld.bnk library
    // models placed by the sector .pmi, the heightfield where nothing else.
    pCollision = std::make_unique<VehicleWorldCollision>();
    pCollision->setIsland( island.c_str() );
    pCollision->setHeightFunction(
        [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    pCollision->loadLibrary( view.getWorld().getCommonBank() );
    roads.load( island.c_str() );

    weather.load( island );
    weather.applyArguments( view, "FREE_DRIVE" );
    pWeather = weather.getInitialWeather( view );
    view.setWeather( pWeather );
    view.setShadowConfig( EdenShadows::FindConfig( "FREERIDE" ) );
    // GSReflection: as FREERIDE (mode 1, table 0x00f817e0); -gsreflection on.
    view.setPlanarReflectionsEnabled( CmdLine::HasArg( "-gsreflection" ) ||
                                      EdenPlanarReflections::IsEnabledForGameMode( 0x01 ) );
    view.setDrawCallback( [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        if ( pass != EdenSceneView::ePass::ProjectorActors ) {
            for ( const auto &pExterior : exteriors ) {
                pExterior->draw( renderer );
            }
        }
        if ( pass != EdenSceneView::ePass::Environment && pass != EdenSceneView::ePass::Reflection && car.isLoaded() ) {
            car.draw( renderer, carWorld, view.getWeather(), bEnvironmentAssigned );
        }
    } );
    bWorldReady = true;
    lastSectorKey = INT32_MIN;

    loadCar( currentCar );
    placeCar( start, heading, true, Vector3f() );
    const EdenWorld &world = view.getWorld();
    OTDU_LOG_INFO( "FREE_DRIVE: %s ready: %zu areas, %zu sectors, start %s (%.1f, %.1f, %.1f), weather '%s'\n",
                   island.c_str(), world.getNumAreas(), world.getNumSectors(),
                   selectedSpot >= 0 ? spots[static_cast<size_t>( selectedSpot )].Bank.c_str() : "-carpos", start.x,
                   start.y, start.z, pWeather != nullptr ? pWeather->Name.c_str() : "(none)" );
    return true;
}

void GMFreeDrive::unloadIsland() {
    if ( gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
    }
    unloadCar();
    exteriors.clear();
    exteriorCollisions.clear();
    view.destroy();
    pCollision.reset();
    roads.unload();
    pWeather = nullptr;
    bWorldReady = false;
    lastSectorKey = INT32_MIN;
}

void GMFreeDrive::streamWorld( const Vector3f &position, bool bForce ) {
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
    pCollision->streamAround( position, 1 );
}

void GMFreeDrive::streamExteriors( const Vector3f &position ) {
    // Same streaming as the World Viewer (one SpotExterior per bank, one
    // frame per spot), plus the bank collision <spot>_sc_*.shk in each spot
    // frame (as FREERIDE adds the EPH_L3DI house), kept once added.
    bool bUnloaded = false;
    bool bLoadedAny = false;
    for ( auto it = exteriors.begin(); it != exteriors.end(); ) {
        bool bKeep = false;
        for ( const Spot &spot : spots ) {
            bKeep |= bExteriors && spot.ExteriorPath == ( *it )->getPath() &&
                     HorizontalDistance( spot.Position, position ) < kExteriorUnloadDistance;
        }
        if ( !bKeep ) {
            gpRender->getDevice().waitIdle();
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
        const bool bAddCollision = exteriorCollisions.insert( spot.ExteriorPath ).second;
        for ( const Spot &other : spots ) {
            if ( other.ExteriorPath != spot.ExteriorPath ) {
                continue;
            }
            const Matrix4x4f frame =
                Matrix4x4f::Translation( other.Position ) *
                Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), other.Heading ) );
            pExterior->getFrames().push_back( frame );
            if ( !bAddCollision ) {
                continue;
            }
            for ( const BankEntry *pEntry : pExterior->getBank().findEntriesByExtension( "shk" ) ) {
                HavokCollision spotCollision;
                if ( LoadHavokCollision( pEntry->pData, pEntry->Size, spotCollision, true ) ) {
                    pCollision->addCollision( spotCollision, frame, true );
                }
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
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            for ( auto &pExterior : exteriors ) {
                pExterior->resolveForeignTextures( *pRenderer );
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Car

void GMFreeDrive::unloadCar() {
    if ( !bCarReady ) {
        return;
    }
    if ( gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
    }
    hud.unload();
    car.unload();
    // The Z pre-pass / shadow caster material clones are keyed by material
    // address + hashcode: the next car's materials can reuse both (same
    // names "Paint"...), with the textures of the unloaded car.
    if ( EdenRenderer *pRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr ) {
        pRenderer->clearShadowCasterCache();
    }
    wheelNodes.clear();
    bCarReady = false;
    bEnvironmentAssigned = false;
}

bool GMFreeDrive::loadCar( int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( cars.size() ) ) {
        return false;
    }
    const CarEntry &entry = cars[static_cast<size_t>( index )];
    CinematicCar::Desc desc;
    if ( !CinematicCar::FindDatabaseDesc( entry.IdCar, 0, desc ) ) {
        OTDU_LOG_WARN( "FREE_DRIVE: car %d not in the database\n", entry.IdCar );
        return false;
    }
    unloadCar();
    const bool bNewCar = index != currentCar || colors.empty();
    currentCar = index;
    carDesc = desc;
    if ( !car.load( carDesc ) ) {
        OTDU_LOG_WARN( "FREE_DRIVE: car bank of '%s' could not be loaded (driving without a model)\n",
                       carDesc.FileName.c_str() );
    }
    if ( bNewCar ) {
        buildColorList();
        if ( currentColor == -2 ) {
            currentColor = Max( findColor( CmdLine::GetArgValue( "-color" ) ), 0 );
        } else {
            currentColor = 0;
        }
    }
    applyColor( currentColor );
    setupVehicle();
    chase.load( setup.CameraId != 0 ? setup.CameraId : setup.Db.CarId );
    hud.load( entry.HudFile.empty() ? entry.FileName : entry.HudFile );
    hud.setVisible( bShowHud );
    hud.setZoneVisible( island == "ibiza" );
    // Accessories (roof, windows, blinkers), commands reset like FUN_00c3fcc0.
    accessories.load( carDesc.FileName, car );
    car.setHeadlights( lightMode );
    bEnvironmentAssigned = false;
    bCarReady = true;
    OTDU_LOG_INFO( "FREE_DRIVE: car %d '%s' (%s), rims %s / %s R%d, tire radius %.3f m, %zu colours, camera %d, "
                   "gauges '%s', mass %.0f kg%s%s\n",
                   entry.IdCar, entry.Label.c_str(), carDesc.FileName.c_str(), carDesc.RimFront.c_str(),
                   carDesc.RimRear.c_str(), carDesc.DiameterFront, carDesc.TireRadius, colors.size(),
                   setup.CameraId, entry.HudFile.c_str(), setup.Db.Mass, entry.Missing.empty() ? "" : ", missing:",
                   entry.Missing.c_str() );
    return true;
}

void GMFreeDrive::applyColor( int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( colors.size() ) ) {
        return;
    }
    currentColor = index;
    const bool bApplied = car.applyColor( colors[static_cast<size_t>( index )].ColorId );
    OTDU_LOG_INFO( "FREE_DRIVE: colour %d '%s' (%s) %s\n", index, colors[static_cast<size_t>( index )].Name.c_str(),
                   colors[static_cast<size_t>( index )].ColorId.c_str(), bApplied ? "applied" : "not in the colour library" );
}

void GMFreeDrive::setupVehicle() {
    // Same rig as GMFreeRide / GMTestCar: the D_RIM_xx wheel nodes of the
    // model, the DBCarPhysicsData row of the car (LoadVehicleSetup).
    LoadVehicleSetup( carDesc.FileName, setup );
    std::vector<VehicleDynamics::WheelDesc> wheels;
    wheelNodes.clear();
    if ( car.isLoaded() ) {
        const EdenModel &model = car.getModel();
        const std::vector<SceneNode> &nodes = model.getScene().getNodes();
        for ( size_t i = 0; i + 1 < nodes.size(); i++ ) {
            if ( nodes[i].Name.rfind( "D_RIM_", 0 ) != 0 ) {
                continue;
            }
            WheelNode node;
            node.RimNode = static_cast<int32_t>( i );
            node.RimRest = model.getNodeTransform( node.RimNode );
            node.TireRest = model.getNodeTransform( node.RimNode + 1 );
            const Vector3f hub = node.RimRest.getTranslation();
            node.bFront = hub.z > 0.0f; // car frame: +z forward, +x left
            node.bLeft = hub.x > 0.0f;
            wheelNodes.push_back( node );
            VehicleDynamics::WheelDesc wheel;
            wheel.HubLocal = hub;
            wheel.bFront = node.bFront;
            wheel.bLeft = node.bLeft;
            wheel.Radius = carDesc.TireRadius;
            wheels.push_back( wheel );
        }
    }
    if ( wheels.empty() ) {
        // No model: wheels from the database (track, wheelbase [INF]).
        const float halfBase = 1.35f;
        for ( int32_t i = 0; i < 4; i++ ) {
            VehicleDynamics::WheelDesc wheel;
            wheel.bFront = i < 2;
            wheel.bLeft = ( i & 1 ) == 0;
            const float track = wheel.bFront ? setup.Db.TrackFront : setup.Db.TrackRear;
            wheel.HubLocal = Vector3f( ( wheel.bLeft ? 0.5f : -0.5f ) * track, carDesc.TireRadius, wheel.bFront ? halfBase : -halfBase );
            wheel.Radius = carDesc.TireRadius;
            wheels.push_back( wheel );
        }
    }
    vehicle.setup( setup, wheels );
}

void GMFreeDrive::updateWheels() {
    if ( !car.isLoaded() ) {
        return;
    }
    EdenModel &model = car.getModel();
    const std::vector<VehicleDynamics::WheelState> &states = vehicle.getWheels();
    for ( size_t i = 0; i < wheelNodes.size() && i < states.size(); i++ ) {
        const WheelNode &node = wheelNodes[i];
        const Matrix4x4f motion =
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0, 1, 0 ), node.bFront ? vehicle.getSteerAngle() : 0.0f ) ) *
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 1, 0, 0 ), states[i].SpinAngle ) );
        const Vector3f lift( 0.0f, vehicle.getWheelOffset( i ), 0.0f );
        model.setNodeTransform( node.RimNode, Matrix4x4f::Translation( node.RimRest.getTranslation() + lift ) * motion *
                                                  WithoutTranslation( node.RimRest ) );
        model.setNodeTransform( node.RimNode + 1, Matrix4x4f::Translation( node.TireRest.getTranslation() + lift ) * motion *
                                                      WithoutTranslation( node.TireRest ) );
    }
}

// ---------------------------------------------------------------------------
// Placement

bool GMFreeDrive::groundHeight( const Vector3f &position, float &outHeight ) const {
    VehicleWorldCollision::Hit hit;
    if ( pCollision != nullptr &&
         pCollision->raycast( position + Vector3f( 0.0f, 3.0f, 0.0f ), Vector3f( 0.0f, -1.0f, 0.0f ), 200.0f, hit ) ) {
        outHeight = hit.Position.y;
        return true;
    }
    return const_cast<EdenSceneView &>( view ).getWorld().getHeight( position.x, position.z, outHeight );
}

bool GMFreeDrive::findRoad( const Vector3f &position, float preferredHeading, float maxDistance, bool bSectorRoadsOnly,
                            bool bKeepHeadingNear, Vector3f &outPosition, float &outHeading ) const {
    // Nearest road triangle (sector -R.shk, + spot collision unless
    // bSectorRoadsOnly) facing up, the
    // horizontal distance plus a quarter of the height difference (bridges /
    // tunnels: the level of the car first) [INF].
    if ( pCollision == nullptr ) {
        return false;
    }
    float bestScore = 1e30f;
    Vector3f best;
    bool bFound = false;
    pCollision->forEachRoadTriangle( position, maxDistance, [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
        const Vector3f n = ( b - a ).cross( c - a );
        const float length = n.length();
        if ( length < 1e-5f || std::abs( n.y ) / length < 0.85f ) {
            return;
        }
        const Vector3f q = ClosestPointXZ( position, a, b, c );
        const float y = PlaneHeight( a, b, c, q.x, q.z );
        const float score = HorizontalDistance( q, position ) + 0.25f * std::abs( y - position.y );
        if ( score < bestScore ) {
            bestScore = score;
            best = Vector3f( q.x, y, q.z );
            bFound = true;
        }
    }, bSectorRoadsOnly );
    if ( !bFound || bestScore > maxDistance ) {
        return false;
    }
    // Road direction: the longest edges of the road triangles around the
    // point (road strips are long along the road), summed with one sign
    // [INF: the game takes it from the road graph].
    const Vector3f preferred( std::sin( preferredHeading ), 0.0f, std::cos( preferredHeading ) );
    Vector3f direction;
    pCollision->forEachRoadTriangle( best, 12.0f, [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
        const Vector3f centroid = ( a + b + c ) * ( 1.0f / 3.0f );
        if ( HorizontalDistance( centroid, best ) > 12.0f || std::abs( centroid.y - best.y ) > 3.0f ) {
            return;
        }
        const Vector3f edges[3] = { b - a, c - b, a - c };
        Vector3f longest;
        for ( const Vector3f &edge : edges ) {
            const Vector3f flat( edge.x, 0.0f, edge.z );
            if ( flat.length() > longest.length() ) {
                longest = flat;
            }
        }
        const Vector3f reference = direction.length() > 1e-3f ? direction : preferred;
        direction = direction + ( longest.dot( reference ) < 0.0f ? longest * -1.0f : longest );
    }, bSectorRoadsOnly );
    if ( direction.length() < 1e-3f ) {
        direction = preferred;
    }
    if ( direction.dot( preferred ) < 0.0f ) {
        direction = direction * -1.0f;
    }
    // Close to a spot street side transform (bKeepHeadingNear): the spot
    // heading is kept.
    const bool bKeepHeading = bKeepHeadingNear && HorizontalDistance( best, position ) < 15.0f;
    outHeading = bKeepHeading ? preferredHeading : std::atan2( direction.x, direction.z );
    outPosition = best;
    if ( bKeepHeading ) {
        return true;
    }
    // Across the road: the extent of the road surface on both sides
    // (0.5 m steps, up to 15 m), the car put in the right hand lane (a
    // quarter of the width right of the middle) [INF: lanes come from the
    // road graph in the game].
    const Vector3f forward = Vector3f( direction.x, 0.0f, direction.z ).normalized();
    const Vector3f right( -forward.z, 0.0f, forward.x ); // car frame: +z forward, +x left
    auto onRoad = [&]( const Vector3f &p ) {
        bool bHit = false;
        pCollision->forEachRoadTriangle( p, 1.0f, [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
            if ( bHit ) {
                return;
            }
            const Vector3f q = ClosestPointXZ( p, a, b, c );
            bHit = HorizontalDistance( q, p ) < 0.05f && std::abs( PlaneHeight( a, b, c, p.x, p.z ) - best.y ) < 1.5f;
        }, bSectorRoadsOnly );
        return bHit;
    };
    float extent[2] = { 0.0f, 0.0f };
    for ( int32_t side = 0; side < 2; side++ ) {
        const Vector3f step = right * ( side == 0 ? 0.5f : -0.5f );
        for ( int32_t i = 1; i <= 30 && onRoad( best + step * static_cast<float>( i ) ); i++ ) {
            extent[side] = 0.5f * static_cast<float>( i );
        }
    }
    const float width = extent[0] + extent[1];
    const float offset = 0.5f * ( extent[0] - extent[1] ) + ( width > 6.0f ? 0.25f * width : 0.0f );
    outPosition = best + right * offset;
    OTDU_LOG_INFO( "FREE_DRIVE: road %.1f m wide (%.1f right, %.1f left of the nearest point), car %.1f m to the right\n",
                   width, extent[0], extent[1], offset );
    return true;
}

void GMFreeDrive::placeCar( const Vector3f &target, float preferredHeading, bool bSpot, const Vector3f &velocity ) {
    if ( !bWorldReady ) {
        return;
    }
    streamWorld( target, true );
    // Spot exteriors first: their collision (driveways, car parks) counts as
    // road surface, as the FREERIDE house spot collision does.
    streamExteriors( target );
    Vector3f position = target;
    float heading = preferredHeading;
    // Search passes, first hit wins [INF]: a sector road (-R.shk) close by,
    // then any road surface close by (the spot driveways, car parks,
    // squares), then farther. Spot: 60 m sector roads, then 300 m any
    // surface (the street side transform is next to the spot). Back on the
    // road: 50 m sector / 50 m any / 400 m sector / 400 m any.
    struct Pass {
        float Distance;
        bool bSectorOnly;
    };
    static const Pass kSpotPasses[] = { { kSpotSectorRoadSearch, true }, { kSpotRoadSearch, false } };
    static const Pass kRespawnPasses[] = { { kRespawnNearSearch, true },
                                           { kRespawnNearSearch, false },
                                           { kRespawnRoadSearch, true },
                                           { kRespawnRoadSearch, false } };
    bool bRoad = false;
    const Pass *pPasses = bSpot ? kSpotPasses : kRespawnPasses;
    const size_t numPasses = bSpot ? 2 : 4;
    for ( size_t i = 0; i < numPasses && !bRoad; i++ ) {
        bRoad = findRoad( target, preferredHeading, pPasses[i].Distance, pPasses[i].bSectorOnly, bSpot, position, heading );
    }
    if ( bRoad ) {
        // The road may be in a sector not streamed yet around the target.
        streamWorld( position, true );
    }
    float ground = 0.0f;
    if ( groundHeight( position, ground ) ) {
        position.y = ground;
    }
    const Quaternionf orientation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), heading );
    vehicle.reset( position + Vector3f( 0.0f, kDropHeight, 0.0f ), orientation, velocity );
    carWorld = vehicle.getWorldMatrix();
    chase.reset( position, orientation.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
    camera = chase.update( 0.0f, carWorld, 0.0f,
                           [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    OTDU_LOG_INFO( "FREE_DRIVE: car placed at (%.2f, %.2f, %.2f) heading %.1f deg (%s, %.1f m from (%.1f, %.1f, %.1f))\n",
                   position.x, position.y, position.z, heading * 180.0f / kPi, bRoad ? "road" : "no road found",
                   HorizontalDistance( position, target ), target.x, target.y, target.z );
}

void GMFreeDrive::teleportToSpot( int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( spots.size() ) ) {
        return;
    }
    selectedSpot = index;
    const Spot &spot = spots[static_cast<size_t>( index )];
    placeCar( spot.Position2, spot.Heading2, true, Vector3f() );
}

float GMFreeDrive::getCarHeading() const {
    const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    return std::atan2( forward.x, forward.z );
}

// ---------------------------------------------------------------------------
// Frame

void GMFreeDrive::tick( float deltaTime ) {
    // Tool keys (raw keys, no numpad): F2 panel, F3 back on the road, F4 /
    // Shift+F4 next / previous car, F5..F7 islands, F8 colour, F9 lights.
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F2 ) ) {
        bShowPanel = !bShowPanel;
    }
    for ( uint32_t i = 0; i < 3; i++ ) {
        if ( gpInput->wasKeyPressed( SDL_SCANCODE_F5 + i ) && island != GMWorldViewer::kIslands[i] ) {
            pendingIsland = GMWorldViewer::kIslands[i];
        }
    }
    if ( !pendingIsland.empty() ) {
        const std::string name = pendingIsland;
        pendingIsland.clear();
        loadIsland( name );
    }
    if ( !bWorldReady ) {
        return;
    }
    const bool bShift = gpInput->isKeyDown( SDL_SCANCODE_LSHIFT ) || gpInput->isKeyDown( SDL_SCANCODE_RSHIFT );
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F4 ) && !cars.empty() ) {
        const int32_t count = static_cast<int32_t>( cars.size() );
        pendingCar = ( currentCar + ( bShift ? count - 1 : 1 ) ) % count;
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F8 ) && !colors.empty() ) {
        pendingColor = ( currentColor + 1 ) % static_cast<int32_t>( colors.size() );
    }
    // Light mode (+0x1d4): F9 or the game's CAR_LIGHT key cycles off / weather
    // driven / high beam [INF: the CAR_LIGHT handler is not identified].
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F9 ) || gpInput->wasPressed( eInputAction::IA_CarLight ) ) {
        lightMode = ( lightMode + 1 ) % 3;
        car.setHeadlights( lightMode );
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F3 ) || gpInput->wasPressed( eInputAction::IA_BackOnTrack ) ) {
        bPendingRespawn = true;
    }

    // Car switch at the current pose (position, orientation, velocity kept).
    if ( pendingCar >= 0 ) {
        const int32_t index = pendingCar;
        pendingCar = -1;
        const Vector3f position = vehicle.getPosition();
        const Quaternionf orientation = vehicle.getOrientation();
        const Vector3f velocity = vehicle.getVelocity();
        if ( loadCar( index ) ) {
            float ground = position.y;
            Vector3f start = position;
            if ( groundHeight( position, ground ) ) {
                start.y = Max( position.y, ground + 0.05f );
            }
            vehicle.reset( start, orientation, velocity );
            carWorld = vehicle.getWorldMatrix();
        }
    }
    if ( pendingColor >= 0 ) {
        applyColor( pendingColor );
        pendingColor = -1;
    }
    if ( pendingSpot >= 0 ) {
        teleportToSpot( pendingSpot );
        pendingSpot = -1;
    }
    if ( bPendingRespawn ) {
        bPendingRespawn = false;
        placeCar( carWorld.getTranslation(), getCarHeading(), false, Vector3f() );
    }

    // Driving (FREERIDE eState::Drive input).
    VehicleDynamics::Input input;
    input.Throttle = gpInput->getAnalog( eInputAction::IA_CarAccel );
    input.Brake = gpInput->getAnalog( eInputAction::IA_CarBrake );
    input.Steering = gpInput->getSteering();
    input.bHandBrake = gpInput->isDown( eInputAction::IA_CarHandBrake );
    if ( gpInput->wasPressed( eInputAction::IA_CarCamera ) ) {
        chase.nextMode();
    }
    lastBrakeInput = input.Brake;
    vehicle.step( deltaTime, input, *pCollision );
    carWorld = vehicle.getWorldMatrix();
    const Vector3f carPosition = carWorld.getTranslation();
    streamWorld( carPosition, false );
    streamExteriors( carPosition );
    for ( auto &pExterior : exteriors ) {
        pExterior->update( deltaTime );
    }
    camera = chase.update( deltaTime, carWorld, vehicle.getSpeedKmh(),
                           [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); } );
    updateWheels();
    // Accessories (FUN_00929be0 menu keys) and the roof / windows / blinkers,
    // car lights: brake / reverse from the vehicle, blinker timer.
    accessories.tick( deltaTime, true, car, hud );
    if ( bHazard ) {
        car.setIndicator( CinematicCar::eIndicator::Hazard );
    }
    car.setBraking( lastBrakeInput > 0.0f );
    car.setReverse( vehicle.getGear() == -1 );
    car.updateLights( deltaTime );
    // Projected headlight of the car (HEADLIGHTS pass), from the light
    // state of the last frame.
    {
        std::vector<EdenSceneView::HeadlightSpot> spots;
        EdenSceneView::HeadlightSpot spot;
        if ( car.getHeadlightSpot( carWorld, camera.Position, spot ) ) {
            spots.push_back( spot );
            static const bool bLogSpot = CmdLine::HasArg( "-loghl" );
            if ( bLogSpot && ( pGameInstance->getFrameIndex() % 30 ) == 0 ) {
                OTDU_LOG_INFO( "HL spot (%.2f %.2f %.2f) dir (%.3f %.3f %.3f) car (%.2f %.2f %.2f) cam (%.2f %.2f %.2f) I %.3f "
                               "drop %.2f\n",
                               spot.Position.x, spot.Position.y, spot.Position.z, spot.Direction.x, spot.Direction.y,
                               spot.Direction.z, carPosition.x, carPosition.y, carPosition.z, camera.Position.x,
                               camera.Position.y, camera.Position.z, spot.Intensity, spot.DropOffNear );
            }
        }
        view.setHeadlightSpots( spots );
    }
    // Tunnel (FUN_00c62a70): road network tunnel sections around the car.
    car.setTunnelDistance( roads.getCarTunnelDistance( carPosition, camera.Position ), deltaTime );
    static const bool bLogTunnel = CmdLine::HasArg( "-logtunnel" );
    if ( bLogTunnel && ( pGameInstance->getFrameIndex() % 10 ) == 0 ) {
        OTDU_LOG_INFO( "FREE_DRIVE tunnel: pos (%.1f %.1f %.1f) distance %.2f in %d blend %.3f\n", carPosition.x,
                       carPosition.y, carPosition.z, car.getTunnelDistance(), car.isInTunnel() ? 1 : 0,
                       car.getTunnelBlend() );
    }

    FreeRideHud::State hudState;
    hudState.SpeedKmh = vehicle.getSpeedKmh();
    hudState.Rpm = vehicle.getRpm();
    hudState.RedLine = setup.Db.RedLine;
    hudState.RpmMax = setup.Db.RPMMax;
    hudState.Gear = vehicle.getGear();
    hudState.CameraMode = chase.getMode();
    hudState.OdometerKm = vehicle.getOdometer() / 1000.0f;
    const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    const Vector3f flat = Vector3f( forward.x, 0.0f, forward.z ).normalized();
    hudState.CarX = carPosition.x;
    hudState.CarZ = carPosition.z;
    hudState.HeadingX = flat.x;
    hudState.HeadingZ = flat.z;
    hud.setVisible( bShowHud );
    hud.tick( deltaTime, hudState );

    view.setCamera( camera );
    pWeather = weather.tick( deltaTime, view );
    view.setEnvironmentCenter( carPosition, car.isLoaded() );
    if ( gpAudio != nullptr ) {
        const Vector3f cameraForward = EdenRotationFromQuaternion( camera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        gpAudio->setListener( camera.Position, cameraForward, Vector3f() );
    }
    view.tick( deltaTime );
    static const bool bLogDrive = CmdLine::HasArg( "-logdrive" );
    if ( bLogDrive && ( pGameInstance->getFrameIndex() % 10 ) == 0 ) {
        OTDU_LOG_INFO( "FREE_DRIVE pos (%.2f %.2f %.2f) %.1f km/h rpm %.0f gear %d\n", carPosition.x, carPosition.y,
                       carPosition.z, vehicle.getSpeedKmh(), vehicle.getRpm(), vehicle.getGear() );
    }
}

void GMFreeDrive::drawOffscreen() {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !bWorldReady || pRenderer == nullptr ) {
        return;
    }
    if ( !bEnvironmentAssigned && car.isLoaded() ) {
        if ( GPUTexture *pCube = view.getEnvironmentCube() ) {
            car.assignEnvironment( *pRenderer, pCube );
            bEnvironmentAssigned = true;
        }
    }
    view.renderOffscreen();
}

void GMFreeDrive::draw() {
    if ( !bWorldReady ) {
        return;
    }
    view.draw();
    if ( bCarReady ) {
        hud.draw( gpRender->get2DRenderer(), pCollision.get() );
    }
}

void GMFreeDrive::exit() {
    if ( bWorldReady || view.isReady() || bCarReady ) {
        unloadIsland();
    }
}

// ---------------------------------------------------------------------------
// Panel

void GMFreeDrive::drawUI() {
    if ( !bShowPanel ) {
        return;
    }
    ImGui::SetNextWindowPos( ImVec2( 10.0f, 10.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( ImVec2( 390.0f, 0.0f ), ImGuiCond_FirstUseEver );
    if ( !ImGui::Begin( "Free Drive" ) ) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted( "Island" );
    for ( const char *pIsland : GMWorldViewer::kIslands ) {
        ImGui::SameLine();
        if ( ImGui::RadioButton( IslandLabel( pIsland ), island == pIsland ) && island != pIsland ) {
            pendingIsland = pIsland;
        }
    }
    if ( !pendingIsland.empty() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "loading %s...", IslandLabel( pendingIsland ) );
    }
    if ( !statusText.empty() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.3f, 1.0f ), "%s", statusText.c_str() );
    }
    if ( bWorldReady ) {
        const Vector3f p = carWorld.getTranslation();
        const EdenWorld::SectorId id = EdenWorld::getSectorAt( p );
        ImGui::Text( "pos %.1f %.1f %.1f  sector-%d-%d-%d-%d", p.x, p.y, p.z, id.AreaX, id.AreaY, id.X, id.Y );
        ImGui::Text( "%.0f km/h, %.0f rpm, gear %d, camera %d", vehicle.getSpeedKmh(), vehicle.getRpm(), vehicle.getGear(),
                     chase.getMode() );
        if ( ImGui::Button( "Back on the road (F3)" ) ) {
            bPendingRespawn = true;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Camera (C)" ) ) {
            chase.nextMode();
        }
        ImGui::SameLine();
        ImGui::Checkbox( "HUD", &bShowHud );
    }

    // Car
    if ( currentCar >= 0 ) {
        const CarEntry &entry = cars[static_cast<size_t>( currentCar )];
        ImGui::Text( "Car: %s (%d, %s)", entry.Label.c_str(), entry.IdCar, entry.FileName.c_str() );
        if ( !entry.Missing.empty() ) {
            ImGui::TextColored( ImVec4( 1.0f, 0.7f, 0.3f, 1.0f ), "not installed:%s", entry.Missing.c_str() );
        }
    }
    if ( !colors.empty() ) {
        const char *pLabel = currentColor >= 0 && currentColor < static_cast<int32_t>( colors.size() )
                                 ? colors[static_cast<size_t>( currentColor )].Name.c_str()
                                 : "";
        if ( ImGui::BeginCombo( "colour (F8)", pLabel ) ) {
            for ( size_t i = 0; i < colors.size(); i++ ) {
                char item[128];
                snprintf( item, sizeof( item ), "%s (%s)##%zu", colors[i].Name.c_str(), colors[i].ColorId.c_str(), i );
                if ( ImGui::Selectable( item, static_cast<int32_t>( i ) == currentColor ) ) {
                    pendingColor = static_cast<int32_t>( i );
                }
            }
            ImGui::EndCombo();
        }
    }
    static const char *const kLightModes[] = { "off", "auto (weather)", "high beam" };
    if ( ImGui::Combo( "lights (F9)", &lightMode, kLightModes, 3 ) ) {
        car.setHeadlights( lightMode );
    }
    ImGui::Checkbox( "hazard lights", &bHazard );
    char carsHeader[64];
    snprintf( carsHeader, sizeof( carsHeader ), "Cars: %zu installed (F4 / Shift+F4)###cars", cars.size() );
    if ( ImGui::CollapsingHeader( carsHeader, ImGuiTreeNodeFlags_DefaultOpen ) ) {
        ImGui::InputText( "filter", carFilter, sizeof( carFilter ) );
        if ( ImGui::BeginListBox( "##cars", ImVec2( -1.0f, 150.0f ) ) ) {
            const std::string filter = ToLower( std::string( carFilter ) );
            for ( size_t i = 0; i < cars.size(); i++ ) {
                const CarEntry &entry = cars[i];
                if ( !filter.empty() && ToLower( entry.Label ).find( filter ) == std::string::npos &&
                     ToLower( entry.FileName ).find( filter ) == std::string::npos ) {
                    continue;
                }
                char label[160];
                snprintf( label, sizeof( label ), "%s%s##car%zu", entry.Label.c_str(), entry.Missing.empty() ? "" : "  *", i );
                if ( ImGui::Selectable( label, static_cast<int32_t>( i ) == currentCar ) ) {
                    pendingCar = static_cast<int32_t>( i );
                }
            }
            ImGui::EndListBox();
        }
        ImGui::TextDisabled( "click = switch now; * rims, interior or gauges missing" );
    }
    if ( ImGui::CollapsingHeader( "Time of day / weather" ) ) {
        weather.drawPanel( view );
    }
    char spotsHeader[64];
    snprintf( spotsHeader, sizeof( spotsHeader ), "Teleport: %zu spots###spots", spots.size() );
    if ( !spots.empty() && ImGui::CollapsingHeader( spotsHeader ) ) {
        ImGui::TextDisabled( "spots.bin street side, snapped on the road (n = sector banks /9)" );
        if ( ImGui::BeginListBox( "##spots", ImVec2( -1.0f, 140.0f ) ) ) {
            for ( size_t i = 0; i < spots.size(); i++ ) {
                const Spot &spot = spots[i];
                char label[96];
                snprintf( label, sizeof( label ), "%u %-8s (%.0f, %.0f, %.0f)##spot%zu", spot.NumSectors, spot.Bank.c_str(),
                          spot.Position2.x, spot.Position2.y, spot.Position2.z, i );
                if ( ImGui::Selectable( label, selectedSpot == static_cast<int32_t>( i ) ) ) {
                    pendingSpot = static_cast<int32_t>( i );
                }
            }
            ImGui::EndListBox();
        }
    }
    if ( ImGui::CollapsingHeader( "Controls" ) ) {
        ImGui::TextDisabled( "Up/W throttle, Down/S brake-reverse, Left/Right/A/D steer" );
        ImGui::TextDisabled( "Left Shift handbrake, C camera, 4 accessories (then 8/2/4/6)" );
        static const std::string backOnTrack = GameKeyLabel( "BACK_ON_TRACK" );
        static const std::string carLight = GameKeyLabel( "CAR_LIGHT" );
        ImGui::TextDisabled( "F3 (or BACK_ON_TRACK [%s]) back on the road", backOnTrack.c_str() );
        ImGui::TextDisabled( "F9 (or CAR_LIGHT [%s]) lights", carLight.c_str() );
        ImGui::TextDisabled( "F4 / Shift+F4 next / previous car, F8 colour, F9 lights" );
        ImGui::TextDisabled( "F5/F6/F7 Ibiza/Hawai/Casino, F2 panel, F1 debug" );
    }
    ImGui::End();
}

void GMFreeDrive::drawDebug() {
    if ( ImGui::Begin( "FREE_DRIVE" ) ) {
        if ( pCollision != nullptr ) {
            ImGui::Text( "collision: %zu sectors, %zu triangles", pCollision->getNumSectors(), pCollision->getNumTriangles() );
        }
        ImGui::Text( "exteriors: %zu banks", exteriors.size() );
        for ( const VehicleDynamics::WheelState &wheel : vehicle.getWheels() ) {
            ImGui::Text( "wheel %d load %.0f N comp %.3f slip %.3f / %.3f", wheel.bContact, wheel.Load, wheel.Compression,
                         wheel.SlipRatio, wheel.SlipAngle );
        }
    }
    ImGui::End();
}
