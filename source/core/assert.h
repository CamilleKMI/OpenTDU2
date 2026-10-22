#pragma once

namespace Assert {
extern bool gbBreakOnFailure;
extern bool gbDisabled;
void OnFailure( const char *pCondition,
                const char *pFile,
                int32_t line,
                bool bFatal );
} // namespace Assert

#define OTDU_ASSERT( condition )                                               \
    do {                                                                       \
        if ( !( condition ) ) {                                                \
            Assert::OnFailure( #condition, __FILE__, __LINE__, false );        \
        }                                                                      \
    } while ( 0 )

#define OTDU_ASSERT_TRANSIENT( condition ) OTDU_ASSERT( condition )

#define OTDU_ASSERT_FATAL( condition )                                         \
    do {                                                                       \
        if ( !( condition ) ) {                                                \
            Assert::OnFailure( #condition, __FILE__, __LINE__, true );         \
        }                                                                      \
    } while ( 0 )
