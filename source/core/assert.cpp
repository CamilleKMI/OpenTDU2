#include "shared.h"
#include "assert.h"

#if defined( OTDU_WIN32 )
#include <intrin.h>
#define OTDU_DEBUG_BREAK() __debugbreak()
#else
#include <csignal>
#define OTDU_DEBUG_BREAK() raise( SIGTRAP )
#endif

bool Assert::gbBreakOnFailure = false;
bool Assert::gbDisabled = false;

void Assert::OnFailure( const char *pCondition,
                        const char *pFile,
                        int32_t line,
                        bool bFatal ) {
    if ( gbDisabled && !bFatal ) {
        return;
    }

    Logger::Write( eLogLevel::LL_Error, pFile, line, "Assertion failed: %s\n",
                   pCondition );
    if ( gbBreakOnFailure ) {
        OTDU_DEBUG_BREAK();
    }
    if ( bFatal ) {
        Logger::Shutdown();
        abort();
    }
}
