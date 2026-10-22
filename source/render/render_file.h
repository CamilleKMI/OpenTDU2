#pragma once

// Common container of every Eden render resource (.2DB, .2DM, .3DD, .3DG...).
// A file starts with a Header followed by a tree of Sections: when
// Section::Size > Section::DataSize, the remaining bytes hold child sections.
// Embedded pointers are 32-bit file offsets, relocated at runtime using the
// "REAL" section (list of offsets to patch).
class RenderFile {
public:
    struct Header {
        uint16_t VersionMajor;
        uint16_t VersionMinor;
        uint32_t Flags;
        uint32_t Size;
        uint32_t Magic; // ".2DB", ".3DG", ...
    };

    struct Section {
        uint32_t Type;
        uint16_t VersionMajor;
        uint16_t VersionMinor;
        uint32_t Size;
        uint32_t DataSize;
    };

    // Section visitor. Returns false to stop the iteration.
    using SectionVisitor =
        std::function<bool( const Section &section, uint32_t offset, uint32_t depth )>;

    static bool IsValid( const uint8_t *pFile, uint32_t fileSize, uint32_t magic );
    static void VisitSections( const uint8_t *pFile,
                               uint32_t fileSize,
                               const SectionVisitor &visitor );
};

OTDU_SIZE_MUST_MATCH( RenderFile::Header, 0x10 );
OTDU_SIZE_MUST_MATCH( RenderFile::Section, 0x10 );

constexpr uint32_t MakeFourCC( char a, char b, char c, char d ) {
    return static_cast<uint32_t>( static_cast<uint8_t>( a ) ) |
           ( static_cast<uint32_t>( static_cast<uint8_t>( b ) ) << 8 ) |
           ( static_cast<uint32_t>( static_cast<uint8_t>( c ) ) << 16 ) |
           ( static_cast<uint32_t>( static_cast<uint8_t>( d ) ) << 24 );
}
