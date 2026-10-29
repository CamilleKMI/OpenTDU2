#include "shared.h"
#include "gs_file.h"

#include "system/cmd_line.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

GSFile *gpFile = nullptr;

namespace {
std::string NormalizeGamePath( const char *pPath ) {
    std::string normalized;
    normalized.reserve( strlen( pPath ) );
    for ( const char *p = pPath; *p != '\0'; p++ ) {
        char c = ( *p == '\\' ) ? '/' : *p;
        if ( c == '/' && !normalized.empty() && normalized.back() == '/' ) {
            continue;
        }
        normalized += c;
    }
    while ( normalized.size() > 2 && normalized[0] == '.' &&
            normalized[1] == '/' ) {
        normalized.erase( 0, 2 );
    }
    return normalized;
}

bool HasBankFolder( const std::filesystem::path &root ) {
    std::error_code ec;
    if ( !std::filesystem::is_directory( root, ec ) ) {
        return false;
    }
    for ( const auto &entry : std::filesystem::directory_iterator( root, ec ) ) {
        if ( entry.is_directory() &&
             ToLower( entry.path().filename().string() ) == "euro" ) {
            for ( const auto &sub :
                  std::filesystem::directory_iterator( entry.path(), ec ) ) {
                if ( sub.is_directory() &&
                     ToLower( sub.path().filename().string() ) == "bnk" ) {
                    return true;
                }
            }
        }
    }
    return false;
}
} // namespace

GSFile::GSFile() { gpFile = this; }

GSFile::~GSFile() { gpFile = nullptr; }

bool GSFile::findGameRoot() {
    std::vector<std::filesystem::path> candidates;

    const char *pGameDirArg = CmdLine::GetArgValue( "-game" );
    if ( pGameDirArg != nullptr ) {
        candidates.push_back( pGameDirArg );
    }

    const char *pGameDirEnv = SDL_getenv( "OTDU2_GAME_DIR" );
    if ( pGameDirEnv != nullptr ) {
        candidates.push_back( pGameDirEnv );
    }

    std::error_code ec;
    candidates.push_back( std::filesystem::current_path( ec ) );

    const char *pBasePath = SDL_GetBasePath();
    if ( pBasePath != nullptr ) {
        std::filesystem::path basePath( pBasePath );
        for ( uint32_t i = 0; i < 5 && !basePath.empty(); i++ ) {
            candidates.push_back( basePath );
            basePath = basePath.parent_path();
        }
    }

    for ( const auto &candidate : candidates ) {
        if ( HasBankFolder( candidate ) ) {
            gameRoot = std::filesystem::absolute( candidate, ec );
            return true;
        }
    }
    return false;
}

bool GSFile::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );

    if ( !findGameRoot() ) {
        OTDU_LOG_ERROR(
            "Could not find the TDU2 game folder (a folder containing "
            "Euro/Bnk). Use '-game <path>' or set OTDU2_GAME_DIR.\n" );
        return false;
    }
    OTDU_LOG_INFO( "Game root: '%s'\n", gameRoot.string().c_str() );

    char *pPrefPath = SDL_GetPrefPath( "OpenTDU2", "OpenTDU2" );
    if ( pPrefPath != nullptr ) {
        userDataFolder = pPrefPath;
        SDL_free( pPrefPath );
    } else {
        userDataFolder = gameRoot / "OpenTDU2";
    }
    std::error_code ec;
    std::filesystem::create_directories( userDataFolder, ec );
    OTDU_LOG_INFO( "User data folder: '%s'\n",
                   userDataFolder.string().c_str() );
    return true;
}

void GSFile::terminate() { resolvedPathCache.clear(); }

std::optional<std::filesystem::path>
GSFile::resolveCaseInsensitive( const std::string &normalizedPath ) {
    std::filesystem::path current = gameRoot;
    size_t start = 0;
    std::error_code ec;
    while ( start < normalizedPath.size() ) {
        size_t end = normalizedPath.find( '/', start );
        if ( end == std::string::npos ) {
            end = normalizedPath.size();
        }
        std::string component = normalizedPath.substr( start, end - start );
        start = end + 1;
        if ( component.empty() || component == "." ) {
            continue;
        }

        std::filesystem::path direct = current / component;
        if ( std::filesystem::exists( direct, ec ) ) {
            current = direct;
            continue;
        }

        const std::string lowerComponent = ToLower( component );
        bool bFound = false;
        for ( const auto &entry :
              std::filesystem::directory_iterator( current, ec ) ) {
            if ( ToLower( entry.path().filename().string() ) ==
                 lowerComponent ) {
                current = entry.path();
                bFound = true;
                break;
            }
        }
        if ( !bFound ) {
            return std::nullopt;
        }
    }
    return current;
}

std::optional<std::filesystem::path> GSFile::resolvePath( const char *pPath ) {
    if ( pPath == nullptr || *pPath == '\0' ) {
        return std::nullopt;
    }

    std::filesystem::path asIs( pPath );
    std::error_code ec;
    if ( asIs.is_absolute() && std::filesystem::exists( asIs, ec ) ) {
        return asIs;
    }

    std::string normalized = NormalizeGamePath( pPath );
    std::string key = ToLower( normalized );
    {
        std::lock_guard<std::mutex> lock( cacheMutex );
        auto it = resolvedPathCache.find( key );
        if ( it != resolvedPathCache.end() ) {
            return it->second;
        }
    }

    auto resolved = resolveCaseInsensitive( normalized );
    if ( resolved.has_value() ) {
        std::lock_guard<std::mutex> lock( cacheMutex );
        resolvedPathCache[key] = *resolved;
    } else {
        OTDU_LOG_DEBUG( "Path not resolved: '%s'\n", pPath );
    }
    return resolved;
}

bool GSFile::fileExists( const char *pPath ) {
    auto resolved = resolvePath( pPath );
    std::error_code ec;
    return resolved.has_value() &&
           std::filesystem::is_regular_file( *resolved, ec );
}

bool GSFile::loadFile( const char *pPath,
                       void **ppOutBuffer,
                       uint32_t *pOutSize ) {
    *ppOutBuffer = nullptr;
    *pOutSize = 0;

    auto resolved = resolvePath( pPath );
    if ( !resolved.has_value() ) {
        OTDU_LOG_DEBUG( "File not found: '%s'\n", pPath );
        return false;
    }

    std::ifstream stream( *resolved, std::ios::binary | std::ios::ate );
    if ( !stream.is_open() ) {
        OTDU_LOG_WARN( "Failed to open '%s'\n", resolved->string().c_str() );
        return false;
    }

    std::streamsize size = stream.tellg();
    stream.seekg( 0, std::ios::beg );

    void *pBuffer = TestDrive::AllocAligned( static_cast<size_t>( size ) + 16, 16 );
    if ( pBuffer == nullptr ) {
        return false;
    }
    if ( !stream.read( static_cast<char *>( pBuffer ), size ) ) {
        TestDrive::FreeAligned( pBuffer );
        return false;
    }
    memset( static_cast<uint8_t *>( pBuffer ) + size, 0, 16 );

    *ppOutBuffer = pBuffer;
    *pOutSize = static_cast<uint32_t>( size );
    return true;
}

bool GSFile::loadFile( const char *pPath, std::vector<uint8_t> &outBuffer ) {
    outBuffer.clear();
    auto resolved = resolvePath( pPath );
    if ( !resolved.has_value() ) {
        OTDU_LOG_DEBUG( "File not found: '%s'\n", pPath );
        return false;
    }

    std::ifstream stream( *resolved, std::ios::binary | std::ios::ate );
    if ( !stream.is_open() ) {
        return false;
    }
    std::streamsize size = stream.tellg();
    stream.seekg( 0, std::ios::beg );
    outBuffer.resize( static_cast<size_t>( size ) );
    return static_cast<bool>(
        stream.read( reinterpret_cast<char *>( outBuffer.data() ), size ) );
}

std::vector<std::filesystem::path> GSFile::listFolder( const char *pPath,
                                                       const char *pExtension ) {
    std::vector<std::filesystem::path> files;
    auto resolved = resolvePath( pPath );
    if ( !resolved.has_value() ) {
        return files;
    }

    std::string lowerExtension = pExtension != nullptr ? ToLower( pExtension ) : "";
    std::error_code ec;
    for ( const auto &entry :
          std::filesystem::directory_iterator( *resolved, ec ) ) {
        if ( !entry.is_regular_file() ) {
            continue;
        }
        if ( !lowerExtension.empty() &&
             ToLower( entry.path().extension().string() ) != lowerExtension ) {
            continue;
        }
        files.push_back( entry.path() );
    }
    std::sort( files.begin(), files.end() );
    return files;
}
