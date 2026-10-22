#include "shared.h"
#include "cpr.h"

#include "filesystem/gs_file.h"

// DAT_00f763bc
static constexpr uint32_t kCPRKey[4] = { 0x4fe23c4a, 0x80bac211, 0x6917bd3a,
                                         0xf0528ebd };

void XTEADecipher( uint32_t v[2], const uint32_t key[4] ) {
    // FUN_0095ea60
    uint32_t v0 = v[0];
    uint32_t v1 = v[1];
    uint32_t sum = 0xc6ef3720;
    for ( uint32_t i = 0; i < 32; i++ ) {
        v1 -= ( ( ( v0 >> 5 ) ^ ( v0 << 4 ) ) + v0 ) ^
              ( key[( sum >> 11 ) & 3] + sum );
        sum += 0x61c88647;
        v0 -= ( ( ( v1 >> 5 ) ^ ( v1 << 4 ) ) + v1 ) ^ ( key[sum & 3] + sum );
    }
    v[0] = v0;
    v[1] = v1;
}

bool DecryptCPR( const uint8_t *pInput,
                 size_t inputSize,
                 std::vector<uint8_t> &output ) {
    // FUN_0095eb40
    if ( inputSize < 16 ) {
        return false;
    }

    const size_t numBlocks = ( inputSize - 8 ) / 8;
    output.resize( numBlocks * 8 );

    for ( size_t i = 0; i < numBlocks; i++ ) {
        uint32_t current[2];
        uint32_t next[2];
        memcpy( current, pInput + i * 8, 8 );
        memcpy( next, pInput + ( i + 1 ) * 8, 8 );

        XTEADecipher( next, kCPRKey );
        current[0] ^= next[0];
        current[1] ^= next[1];
        memcpy( output.data() + i * 8, current, 8 );
    }

    return true;
}

bool LoadCPRText( const char *pPath, std::string &outText ) {
    std::vector<uint8_t> buffer;
    if ( gpFile == nullptr || !gpFile->loadFile( pPath, buffer ) ) {
        return false;
    }

    std::vector<uint8_t> decrypted;
    if ( !DecryptCPR( buffer.data(), buffer.size(), decrypted ) ) {
        return false;
    }

    size_t length = strnlen( reinterpret_cast<const char *>( decrypted.data() ),
                             decrypted.size() );
    outText.assign( reinterpret_cast<const char *>( decrypted.data() ),
                    length );
    return true;
}
