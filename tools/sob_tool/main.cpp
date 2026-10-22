// otdu2_sob_tool: inspects a .sob shader library and translates its shaders
// to SPIR-V (optionally writing the .spv files to a folder).
//   otdu2_sob_tool <file.sob> [output folder]
#include "shared.h"

#include "filesystem/gs_file.h"
#include "render/shaders/dxso_translator.h"
#include "render/shaders/shader_cache.h"
#include "render/shaders/shader_library.h"
#include "system/cmd_line.h"

int main( int argc, const char **argv ) {
    CmdLine::Initialize( argc, argv );
    Logger::Initialize( nullptr, CmdLine::HasArg( "-verbose" ), false );
    GSFile file;
    ShaderCache::SetEnabled( false );

    if ( argc < 2 ) {
        printf( "usage: %s <file.sob> [output folder]\n", argv[0] );
        return 1;
    }

    ShaderLibrary library;
    if ( !library.load( argv[1] ) ) {
        return 1;
    }

    const char *pOutput = ( argc > 2 && argv[2][0] != '-' ) ? argv[2] : nullptr;
    if ( pOutput != nullptr ) {
        std::filesystem::create_directories( pOutput );
    }

    printf( "ShaderSet '%s' guid=%016llx, %u permutations\n", library.getName().c_str(),
            (unsigned long long)library.getGUID(), library.getNumPermutations() );

    if ( CmdLine::HasArg( "-dump" ) ) {
        auto id8 = []( uint64_t v ) {
            std::string r;
            for ( int b = 0; b < 8; b++ ) {
                char c = (char)( ( v >> ( b * 8 ) ) & 0xff );
                r += ( c >= 32 && c < 127 ) ? c : '.';
            }
            return r;
        };
        auto bytes = []( const std::vector<uint8_t> &v ) {
            std::string r;
            char tmp[8];
            for ( uint8_t b : v ) { snprintf( tmp, sizeof( tmp ), "%02x ", b ); r += tmp; }
            return r;
        };
        for ( const ShaderLibrary::Profile &profile : library.getProfiles() ) {
            printf( "== profile %u maxlod %u\n", profile.ID, profile.MaxLOD );
            for ( uint32_t i = 0; i < profile.Symbols.size(); i++ ) {
                printf( "  sym %u %s %016llx fmt=%u size=%u\n", i, id8( profile.Symbols[i].Name ).c_str(),
                        (unsigned long long)profile.Symbols[i].Name, profile.Symbols[i].Format,
                        profile.Symbols[i].Size );
            }
            for ( uint32_t i = 0; i < profile.Attribs.size(); i++ ) {
                const ShaderLibrary::Attrib &a = profile.Attribs[i];
                printf( "  attrib %u frame=%u scene=%u skin=%u mat[%s] dyn[%s] sym[%s] lit[%s] bin=%u\n", i, a.Frame,
                        a.Scene, a.Skin, bytes( a.MaterialIDs ).c_str(), bytes( a.DynamicIDs ).c_str(),
                        bytes( a.SymbolIDs ).c_str(), bytes( a.LiteralIDs ).c_str(), a.BytecodeSize );
            }
            uint32_t maxItems = CmdLine::HasArg( "-all" ) ? 1u << 30 : 64u;
            for ( uint32_t i = 0; i < profile.Items.size() && i < maxItems; i++ ) {
                const ShaderLibrary::Item &it = profile.Items[i];
                printf( "  item %u id=%016llx attrib=%u spec[%s]\n", i, (unsigned long long)it.Hashcode,
                        it.AttribIndex, bytes( it.Spec ).c_str() );
            }
        }
        return 0;
    }

    uint32_t numFailures = 0;
    for ( const ShaderLibrary::Profile &profile : library.getProfiles() ) {
        const char *pStage = profile.ID == ShaderLibrary::SP_VertexShader ? "vs" : "ps";
        printf( "Profile %u (%s): %zu shaders, %zu symbols\n", profile.ID, pStage,
                profile.Attribs.size(), profile.Symbols.size() );
        for ( uint32_t i = 0; i < profile.Attribs.size(); i++ ) {
            const TranslatedShader *pShader = library.getTranslatedShader(
                static_cast<ShaderLibrary::eProfile>( profile.ID ), i );
            if ( pShader == nullptr ) {
                printf( "  [%u] translation FAILED\n", i );
                numFailures++;
                continue;
            }
            printf( "  [%u] scene=%u skin=%u %zu bytes -> %zu dwords, push=%u, samplers=%x\n", i,
                    profile.Attribs[i].Scene, profile.Attribs[i].Skin,
                    (size_t)profile.Attribs[i].BytecodeSize, pShader->SPIRV.size(),
                    pShader->PushConstantsSize, pShader->UsedSamplers );
            for ( const ShaderConstantInfo &constant : pShader->Constants ) {
                printf( "        %s set=%u reg=%u count=%u\n", constant.Name.c_str(),
                        constant.RegisterSet, constant.RegisterIndex, constant.RegisterCount );
            }
            if ( pOutput != nullptr ) {
                char filename[256];
                snprintf( filename, sizeof( filename ), "%s/%s_%s_%u.spv", pOutput,
                          library.getName().c_str(), pStage, i );
                std::ofstream stream( filename, std::ios::binary );
                stream.write( reinterpret_cast<const char *>( pShader->SPIRV.data() ),
                              pShader->SPIRV.size() * sizeof( uint32_t ) );
                char dxsoFilename[256];
                snprintf( dxsoFilename, sizeof( dxsoFilename ), "%s/%s_%s_%u.dxso", pOutput,
                          library.getName().c_str(), pStage, i );
                std::ofstream dxsoStream( dxsoFilename, std::ios::binary );
                dxsoStream.write( reinterpret_cast<const char *>( profile.Attribs[i].pBytecode ),
                                  profile.Attribs[i].BytecodeSize );
            }
        }
    }
    printf( "%u failure(s)\n", numFailures );
    return numFailures == 0 ? 0 : 2;
}
