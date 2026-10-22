#pragma once

// Eden resource bank (.bnk), TDU2 flavor.
//
// File layout (little endian):
//   0x00 u32  Header section size
//   0x04 u32  Header section CRC
//   0x08 BankHeader (magic "KNAB")
//   ...  "STNICC2000 RULEZPADDING " padding
//   Sections (each prefixed with { u32 Size; u32 CRC }; offsets in the
//   header point to the section content - 8):
//     - Table:     BankSection[NumEntries + 1]
//     - Types:     BankType[NumEntries]
//     - Directory: compressed path tree (see BankInstance::parseDirectory)
//     - Indexes:   u8/u16/u32[NumEntries] (directory order -> entry index)
//     - Reversed indexes
//
// Difference with TDU1: BankSection has an extra u32 (alignment), i.e. 0x14
// bytes per entry instead of 0x10.
enum class eBankGroup : int16_t {
    BG_Default = 0,
    BG_FrontEnd = 3,
    BG_Vehicle = 9,
    BG_Physics = 0xb,
    BG_Common = 0xe,
};

#pragma pack( push, 1 )
struct BankHeader {
    uint8_t Magic[4];
    uint32_t Flags;
    uint32_t Version;
    int16_t Type;
    int16_t TypeUnknown;
    int32_t Size;
    int32_t SizeWithoutPadding;
    int32_t SectionAlignment;
    int32_t DataAlignment;
    int32_t NumEntries;
    int32_t CreationDate;
    int32_t SectionOffsetTable;
    int32_t SectionOffsetTypes;
    int32_t SectionOffsetDirectory;
    int32_t SectionOffsetIndexes;
    int32_t SectionOffsetReversedIndexes;
    uint32_t SectionOffsetData;
};
OTDU_SIZE_MUST_MATCH( BankHeader, 0x40 );

struct BankSection {
    int32_t Offset;
    int32_t Size;
    int32_t Date;
    int32_t CRC;
    int32_t Alignment;
};
OTDU_SIZE_MUST_MATCH( BankSection, 0x14 );

struct BankType {
    int16_t SubType;
    int16_t Group;
};
OTDU_SIZE_MUST_MATCH( BankType, 0x4 );
#pragma pack( pop )

struct BankEntry {
    std::string Path;      // Original path, e.g. "D:\...\FrontEnd\intro\fla_0042.2db"
    std::string Filename;  // "fla_0042.2db"
    std::string Name;      // "fla_0042"
    std::string Extension; // "2db" (lowercase, no dot)
    const uint8_t *pData = nullptr;
    uint32_t Size = 0;
    int16_t Group = 0;
    int16_t SubType = 0;
    int32_t Index = -1;
};

class Bank {
public:
    inline bool isLoaded() const { return !buffer.empty(); }
    inline uint32_t getNumEntries() const {
        return static_cast<uint32_t>( entries.size() );
    }
    inline const BankEntry &getEntry( uint32_t index ) const {
        return entries[index];
    }
    inline const std::vector<BankEntry> &getEntries() const { return entries; }
    inline const std::string &getPath() const { return bankPath; }

public:
    Bank();
    ~Bank();

    // pPath is relative to the game root (e.g. "Euro/Bnk/FrontEnd/HiRes/Intro.bnk")
    bool load( const char *pPath );
    bool loadFromMemory( std::vector<uint8_t> &&bankBuffer, const char *pName );
    void unload();

    // Case insensitive lookup by filename ("fla_0042.2db") or by name and
    // extension ("fla_0042", "2db").
    const BankEntry *findEntry( const char *pFilename ) const;
    const BankEntry *findEntry( const char *pName,
                                const char *pExtension ) const;
    std::vector<const BankEntry *>
    findEntriesByExtension( const char *pExtension ) const;

private:
    std::string bankPath;
    std::vector<uint8_t> buffer;
    std::vector<BankEntry> entries;

private:
    bool parse();
    void parseDirectory( const uint8_t *pDirectory,
                         const uint8_t *pDirectoryEnd,
                         std::vector<std::string> &outPaths );
    uint32_t resolveIndex( const uint8_t *pIndexes,
                           uint32_t numEntries,
                           uint32_t index ) const;
};
