#include "shared.h"
#include "logger.h"

namespace {
std::mutex gLogMutex;
FILE *gpLogFile = nullptr;
bool gbVerbose = false;
bool gbSilent = false;

const char *GetLevelTag( eLogLevel level ) {
    switch ( level ) {
    case eLogLevel::LL_Debug:
        return "DEBUG";
    case eLogLevel::LL_Info:
        return "INFO";
    case eLogLevel::LL_Warn:
        return "WARN";
    case eLogLevel::LL_Error:
        return "ERROR";
    default:
        return "LOG";
    }
}

const char *GetFilename( const char *pPath ) {
    const char *pFilename = pPath;
    for ( const char *p = pPath; *p != '\0'; p++ ) {
        if ( *p == '/' || *p == '\\' ) {
            pFilename = p + 1;
        }
    }
    return pFilename;
}
} // namespace

void Logger::Initialize( const char *pLogPath, bool bVerbose, bool bSilent ) {
    std::lock_guard<std::mutex> lock( gLogMutex );
    gbVerbose = bVerbose;
    gbSilent = bSilent;
    if ( pLogPath != nullptr && !bSilent ) {
        gpLogFile = fopen( pLogPath, "w" );
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock( gLogMutex );
    if ( gpLogFile != nullptr ) {
        fclose( gpLogFile );
        gpLogFile = nullptr;
    }
}

void Logger::Write( eLogLevel level,
                    const char *pFile,
                    int32_t line,
                    const char *pFormat,
                    ... ) {
    if ( gbSilent || ( level == eLogLevel::LL_Debug && !gbVerbose ) ) {
        return;
    }

    char message[2048];
    va_list args;
    va_start( args, pFormat );
    vsnprintf( message, sizeof( message ), pFormat, args );
    va_end( args );

    size_t length = strlen( message );
    const bool bNeedNewLine = length == 0 || message[length - 1] != '\n';

    std::lock_guard<std::mutex> lock( gLogMutex );
    FILE *pOutput = ( level >= eLogLevel::LL_Warn ) ? stderr : stdout;
    fprintf( pOutput, "[%s] %s:%d: %s%s", GetLevelTag( level ),
             GetFilename( pFile ), line, message, bNeedNewLine ? "\n" : "" );
    if ( gpLogFile != nullptr ) {
        fprintf( gpLogFile, "[%s] %s:%d: %s%s", GetLevelTag( level ),
                 GetFilename( pFile ), line, message,
                 bNeedNewLine ? "\n" : "" );
        fflush( gpLogFile );
    }
}
bool Logger::IsVerbose() { return gbVerbose; }
