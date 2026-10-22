#pragma once

namespace CmdLine {
void Initialize( int32_t argc, const char **argv );
bool HasArg( const char *pArg );
// Returns the token following pArg (e.g. "-game /path"), or nullptr.
const char *GetArgValue( const char *pArg );
int32_t GetArgInt( const char *pArg, int32_t defaultValue );
} // namespace CmdLine
