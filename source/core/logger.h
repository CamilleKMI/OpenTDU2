#pragma once

#include <cstdint>

enum class eLogLevel : uint32_t {
    LL_Debug = 0,
    LL_Info,
    LL_Warn,
    LL_Error,
    LL_Always,
};

namespace Logger {
void Initialize( const char *pLogPath, bool bVerbose, bool bSilent );
void Shutdown();
bool IsVerbose();
void Write( eLogLevel level,
            const char *pFile,
            int32_t line,
            const char *pFormat,
            ... );
} // namespace Logger

#define OTDU_LOG_DEBUG( ... )                                                  \
    ::Logger::Write( eLogLevel::LL_Debug, __FILE__, __LINE__, __VA_ARGS__ )
#define OTDU_LOG_INFO( ... )                                                   \
    ::Logger::Write( eLogLevel::LL_Info, __FILE__, __LINE__, __VA_ARGS__ )
#define OTDU_LOG_WARN( ... )                                                   \
    ::Logger::Write( eLogLevel::LL_Warn, __FILE__, __LINE__, __VA_ARGS__ )
#define OTDU_LOG_ERROR( ... )                                                  \
    ::Logger::Write( eLogLevel::LL_Error, __FILE__, __LINE__, __VA_ARGS__ )
#define OTDU_LOG_ALWAYS( ... )                                                 \
    ::Logger::Write( eLogLevel::LL_Always, __FILE__, __LINE__, __VA_ARGS__ )
