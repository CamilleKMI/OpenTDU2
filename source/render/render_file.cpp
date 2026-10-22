#include "shared.h"
#include "render_file.h"

bool RenderFile::IsValid( const uint8_t *pFile, uint32_t fileSize, uint32_t magic ) {
    if ( pFile == nullptr || fileSize < sizeof( Header ) ) {
        return false;
    }
    Header header;
    memcpy( &header, pFile, sizeof( Header ) );
    return header.Magic == magic;
}

void RenderFile::VisitSections( const uint8_t *pFile,
                                uint32_t fileSize,
                                const SectionVisitor &visitor ) {
    std::function<bool( uint32_t, uint32_t, uint32_t )> visitRange =
        [&]( uint32_t begin, uint32_t end, uint32_t depth ) -> bool {
        uint32_t offset = begin;
        while ( offset + sizeof( Section ) <= end ) {
            Section section;
            memcpy( &section, pFile + offset, sizeof( Section ) );
            if ( section.Size < sizeof( Section ) || offset + section.Size > end ) {
                return true;
            }
            if ( !visitor( section, offset, depth ) ) {
                return false;
            }
            const uint32_t dataSize = Max<uint32_t>( section.DataSize, sizeof( Section ) );
            if ( section.Size > dataSize ) {
                if ( !visitRange( offset + dataSize, offset + section.Size, depth + 1 ) ) {
                    return false;
                }
            }
            offset += section.Size;
        }
        return true;
    };

    Header header;
    memcpy( &header, pFile, sizeof( Header ) );
    visitRange( sizeof( Header ), Min( header.Size, fileSize ), 0 );
}
