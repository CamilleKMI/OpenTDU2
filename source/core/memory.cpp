#include "shared.h"

namespace TestDrive {
void *Alloc( size_t size ) { return malloc( size ); }

void *AllocAligned( size_t size, size_t alignment ) {
    alignment = Max<size_t>( alignment, sizeof( void * ) );
#if defined( OTDU_WIN32 )
    return _aligned_malloc( size, alignment );
#else
    void *pMemory = nullptr;
    if ( posix_memalign( &pMemory, alignment, Align( size, alignment ) ) != 0 ) {
        return nullptr;
    }
    return pMemory;
#endif
}

void Free( void *pMemory ) { free( pMemory ); }

void FreeAligned( void *pMemory ) {
#if defined( OTDU_WIN32 )
    _aligned_free( pMemory );
#else
    free( pMemory );
#endif
}
} // namespace TestDrive
