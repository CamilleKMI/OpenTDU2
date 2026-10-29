#pragma once

#include "core/math.h"

// In-game event script ("IGE2", Euro/Bnk/ig2/<_eventfile>.ig2, e.g.
// tesseven.ig2 of DBEvent[21] TESSEVEN; docs/re/story_spec.md 4.2).
// Big endian chunks { char Tag[4]; u32 Size (whole chunk) }:
//   "IGE2" (0x1c): u32 version (0x305; < 0x305 -> an extra conversion call
//          in the reader), 16 bytes 0.
//   "MDSC": u32 count, u32 0, u32 4, u32 version, then parameter records
//          { char Tag[4]; u8 3; u8 group; u16 Size; value[Size] } (4 or 8
//          byte integers, 0x40 byte strings). The record reader FUN_00492be0
//          is [ENC]; the layout is fixed by the bytes (the records end
//          exactly on the next chunk) [DATA].
//   "ENTI": u32 count, then entity chunks. FUN_00b25fc0 [CODE] counts them
//          by tag: "CHK2" / "CHKP" checkpoints (container +0x3dd4, objects
//          of 0x80 bytes at +0x3de0, CHKP = old format), "DEC2" / "DECO"
//          decors (+0x56e4), "VEHI" vehicles (+0x76f0), "CRUL" (+0x7fc0);
//          any other tag is skipped.
// CHK2 payload (0x74 bytes, big endian) [DATA]: u32 0, u32 id, u32 a, u32 b,
// u32 c, f32 k, 3x4 matrix rows {R00 R01 R02 Tx} {R10 R11 R12 Ty}
// {R20 R21 R22 Tz} ([R | T] column-vector convention: the axes are the
// columns of R; the third one of the start gate runs along the road),
// f32[4] (0, 0, 0, 1), u32 0, u32 0, f32 width (7.0 in tesseven; the editor
// add FUN_00b26990 defaults checkpoint +0x78 to 20.0 for types 0 / 3)
// [CODE + DATA], 4 x u32 -1. The runtime checkpoint position is
// checkpoint +0x50 (FUN_00b22500, default (0, 0, 0, 1)) [CODE].
class IgeFile {
public:
    struct Param {
        uint32_t Tag = 0;
        uint8_t Group = 0;
        std::vector<uint8_t> Value; // raw (big endian)
        int64_t asInt() const;
        std::string asString() const;
    };
    struct Checkpoint {
        uint32_t Id = 0;
        uint32_t Fields[3] = {}; // the 3 u32 after the id (meaning not decoded)
        float K = 0.0f;          // f32 before the matrix (not decoded)
        Vector3f Position;
        Vector3f Right, Up, Forward; // matrix columns (gate axes)
        float Width = 0.0f;
    };

    bool load( const char *pPath );
    bool parse( const uint8_t *pData, size_t size );

    static constexpr uint32_t MakeTag( const char ( &tag )[5] ) {
        return static_cast<uint32_t>( static_cast<uint8_t>( tag[0] ) ) << 24 |
               static_cast<uint32_t>( static_cast<uint8_t>( tag[1] ) ) << 16 |
               static_cast<uint32_t>( static_cast<uint8_t>( tag[2] ) ) << 8 | static_cast<uint8_t>( tag[3] );
    }
    const Param *find( uint32_t tag ) const;
    int64_t getInt( uint32_t tag, int64_t defaultValue = 0 ) const;
    inline uint32_t getVersion() const { return version; }
    inline const std::vector<Param> &getParams() const { return params; }
    inline const std::vector<Checkpoint> &getCheckpoints() const { return checkpoints; }
    inline uint32_t getNumEntities() const { return numEntities; }

private:
    uint32_t version = 0;
    uint32_t numEntities = 0;
    std::vector<Param> params;
    std::vector<Checkpoint> checkpoints;
};
