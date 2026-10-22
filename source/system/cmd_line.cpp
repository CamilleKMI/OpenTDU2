#include "shared.h"
#include "cmd_line.h"

const char **gpCmdLineArgs = nullptr;
int32_t gCmdLineArgCount = 0;

void CmdLine::Initialize( int32_t argc, const char **argv ) {
    gpCmdLineArgs = argv;
    gCmdLineArgCount = argc;
}

bool CmdLine::HasArg( const char *pArg ) {
    for ( int32_t i = 1; i < gCmdLineArgCount; i++ ) {
        if ( StringCmpI( gpCmdLineArgs[i], pArg ) == 0 ) {
            return true;
        }
    }
    return false;
}

const char *CmdLine::GetArgValue( const char *pArg ) {
    for ( int32_t i = 1; i < gCmdLineArgCount - 1; i++ ) {
        if ( StringCmpI( gpCmdLineArgs[i], pArg ) == 0 ) {
            return gpCmdLineArgs[i + 1];
        }
    }
    return nullptr;
}

int32_t CmdLine::GetArgInt( const char *pArg, int32_t defaultValue ) {
    const char *pValue = GetArgValue( pArg );
    return pValue != nullptr ? atoi( pValue ) : defaultValue;
}
