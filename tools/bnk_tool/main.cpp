// otdu2_bnk_tool: lists / extracts / decrypts TDU2 resources.
//   otdu2_bnk_tool list <file.bnk>
//   otdu2_bnk_tool extract <file.bnk> <output folder>
//   otdu2_bnk_tool decrypt <file.cpr> <output file>
//   otdu2_bnk_tool xmb <file.xmb|file.cpr|file.sob>   (dumps the schema)
#include "shared.h"

#include "core/crypto/cpr.h"
#include "core/xmb.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

static int ListBank( const char *pPath ) {
    Bank bank;
    if ( !bank.load( pPath ) ) {
        return 1;
    }
    for ( const BankEntry &entry : bank.getEntries() ) {
        printf( "%4d  group=%2d sub=%2d  %8u  %s\n", entry.Index, entry.Group,
                entry.SubType, entry.Size, entry.Path.c_str() );
    }
    return 0;
}

static int ExtractBank( const char *pPath, const char *pOutput ) {
    Bank bank;
    if ( !bank.load( pPath ) ) {
        return 1;
    }
    std::filesystem::create_directories( pOutput );
    for ( const BankEntry &entry : bank.getEntries() ) {
        std::filesystem::path outPath = std::filesystem::path( pOutput ) / entry.Filename;
        std::ofstream stream( outPath, std::ios::binary );
        stream.write( reinterpret_cast<const char *>( entry.pData ), entry.Size );
        printf( "%s\n", outPath.string().c_str() );
    }
    return 0;
}

static int Decrypt( const char *pPath, const char *pOutput ) {
    std::vector<uint8_t> buffer;
    if ( !gpFile->loadFile( pPath, buffer ) ) {
        return 1;
    }
    std::vector<uint8_t> decrypted;
    if ( !DecryptCPR( buffer.data(), buffer.size(), decrypted ) ) {
        return 1;
    }
    std::ofstream stream( pOutput, std::ios::binary );
    stream.write( reinterpret_cast<const char *>( decrypted.data() ), decrypted.size() );
    return 0;
}

static void DumpXmbValue( const XmbDocument &document, uint32_t typeIndex, uint32_t offset, int depth,
                          const char *pName ) {
    const XmbType &type = document.getType( typeIndex );
    std::string indent( depth * 2, ' ' );
    if ( depth > 24 ) {
        return;
    }
    if ( type.bComposite ) {
        printf( "%s%s: %s {\n", indent.c_str(), pName, type.Name.c_str() );
        for ( const XmbType::Field &field : type.Fields ) {
            if ( field.bArray ) {
                const uint32_t count = document.readU32( offset + field.Offset );
                const uint32_t arrayOffset = document.readU32( offset + field.Offset + 4 );
                const uint32_t elementSize = document.getType( field.TypeIndex ).Size;
                printf( "%s  %s[%u]\n", indent.c_str(), field.Name.c_str(), count );
                for ( uint32_t i = 0; i < count && i < 4096; i++ ) {
                    std::string name = field.Name + "[" + std::to_string( i ) + "]";
                    DumpXmbValue( document, field.TypeIndex, arrayOffset + i * elementSize, depth + 2, name.c_str() );
                }
            } else {
                DumpXmbValue( document, field.TypeIndex, offset + field.Offset, depth + 1, field.Name.c_str() );
            }
        }
        printf( "%s}\n", indent.c_str() );
        return;
    }
    XmbValue value( &document, typeIndex, offset );
    switch ( type.Primitive ) {
    case eXmbPrimitive::XP_String:
        printf( "%s%s = \"%s\"\n", indent.c_str(), pName, value.asString().c_str() );
        break;
    case eXmbPrimitive::XP_Float:
    case eXmbPrimitive::XP_Double:
        printf( "%s%s = %g\n", indent.c_str(), pName, value.asDouble() );
        break;
    case eXmbPrimitive::XP_UInt64:
        printf( "%s%s = 0x%016llx\n", indent.c_str(), pName, (unsigned long long)value.asUInt64() );
        break;
    case eXmbPrimitive::XP_Virtual: {
        const uint32_t typeWord = document.readU32( offset );
        const uint32_t target = document.readU32( offset + 4 );
        const uint32_t targetType = typeWord / 2;
        if ( targetType < document.getTypes().size() && target != 0 ) {
            DumpXmbValue( document, targetType, target, depth, pName );
        } else {
            printf( "%s%s = virtual(%u, %u)\n", indent.c_str(), pName, typeWord, target );
        }
        break;
    }
    default:
        printf( "%s%s = %lld\n", indent.c_str(), pName, (long long)value.asInt() );
        break;
    }
}

static int DumpXmbValues( const char *pPath ) {
    XmbDocument document;
    if ( !LoadXmbFromFile( pPath, document ) ) {
        return 1;
    }
    XmbValue root = document.getRoot();
    DumpXmbValue( document, root.getTypeIndex(), root.getOffset(), 0, "root" );
    return 0;
}

static int DumpXmb( const char *pPath ) {
    XmbDocument document;
    if ( !LoadXmbFromFile( pPath, document ) ) {
        return 1;
    }
    const auto &types = document.getTypes();
    for ( size_t i = 0; i < types.size(); i++ ) {
        const XmbType &type = types[i];
        if ( !type.bComposite ) {
            continue;
        }
        printf( "%s (size %u)\n", type.Name.c_str(), type.Size );
        for ( const XmbType::Field &field : type.Fields ) {
            printf( "    +%-4u %s%s %s\n", field.Offset,
                    types[field.TypeIndex].Name.c_str(), field.bArray ? "[]" : "",
                    field.Name.c_str() );
        }
    }
    printf( "root: %s\n", document.getRoot().getType()->Name.c_str() );
    return 0;
}

int main( int argc, const char **argv ) {
    CmdLine::Initialize( argc, argv );
    Logger::Initialize( nullptr, false, false );
    GSFile file;

    if ( argc < 3 ) {
        printf( "usage: %s list|extract|decrypt|xmb <file> [output]\n", argv[0] );
        return 1;
    }
    std::string command = argv[1];
    if ( command == "list" ) {
        return ListBank( argv[2] );
    } else if ( command == "extract" && argc > 3 ) {
        return ExtractBank( argv[2], argv[3] );
    } else if ( command == "decrypt" && argc > 3 ) {
        return Decrypt( argv[2], argv[3] );
    } else if ( command == "xmb" ) {
        return DumpXmb( argv[2] );
    } else if ( command == "xmbdump" ) {
        return DumpXmbValues( argv[2] );
    }
    return 1;
}
