#include "shared.h"
#include "ik_offsets.h"

#include "filesystem/gs_file.h"

bool LoadCarIKOffsets( int32_t idCar, bool bWoman, CarIKOffsets &out ) {
    out = CarIKOffsets();
    std::vector<uint8_t> file;
    const std::string folder = gpFile->getBankFolder();
    if ( !gpFile->loadFile( ( folder + "DataBase/IKOffsetDB.bin" ).c_str(), file ) &&
         !gpFile->loadFile( ( folder + "database/ikoffsetdb.bin" ).c_str(), file ) ) {
        return false;
    }
    auto u32 = [&]( size_t offset ) {
        return static_cast<uint32_t>( file[offset] ) << 24 | static_cast<uint32_t>( file[offset + 1] ) << 16 |
               static_cast<uint32_t>( file[offset + 2] ) << 8 | file[offset + 3];
    };
    auto f32 = [&]( size_t offset ) {
        const uint32_t bits = u32( offset );
        float value;
        memcpy( &value, &bits, 4 );
        return value;
    };
    constexpr size_t kSexSize = 5 * 4 + 27 * 24;
    constexpr size_t kCarSize = 4 + 2 * kSexSize + 11 * 12;
    if ( file.size() < 0x24 || memcmp( file.data() + 0x0c, "BDKI", 4 ) != 0 ||
         memcmp( file.data() + 0x10, "KIAC", 4 ) != 0 ) {
        return false;
    }
    const uint32_t numCars = u32( 0x20 );
    for ( uint32_t c = 0; c < numCars && 0x24 + ( c + 1 ) * kCarSize <= file.size(); c++ ) {
        const size_t car = 0x24 + c * kCarSize;
        if ( static_cast<int32_t>( u32( car ) ) != idCar ) {
            continue;
        }
        const size_t sex = car + 4 + ( bWoman ? kSexSize : 0 );
        for ( uint32_t r = 0; r < 5; r++ ) {
            out.Seat[r] = f32( sex + 4 * r );
        }
        for ( uint32_t n = 0; n < 27; n++ ) {
            const size_t entry = sex + 20 + n * 24;
            out.Node[n] = Vector3f( f32( entry ), f32( entry + 4 ), f32( entry + 8 ) );
        }
        out.bValid = true;
        return true;
    }
    return false;
}
