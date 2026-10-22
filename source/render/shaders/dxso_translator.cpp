#include "shared.h"
#include "dxso_translator.h"

#include "dxso/dxso_analysis.h"
#include "dxso/dxso_compiler.h"
#include "dxso/dxso_module.h"
#include "dxso/dxso_reader.h"

namespace {
constexpr uint32_t kD3DSIO_COMMENT = 0xfffe;
constexpr uint32_t kD3DSIO_END = 0x0000ffff;
constexpr uint32_t kCTABFourCC = 0x42415443; // CTAB

uint32_t ReadToken( const uint8_t *p ) {
    uint32_t token;
    memcpy( &token, p, sizeof( uint32_t ) );
    return token;
}

uint64_t HashBytecode( const uint8_t *p, size_t size ) {
    // FNV-1a 64
    uint64_t hash = 0xcbf29ce484222325ull;
    for ( size_t i = 0; i < size; i++ ) {
        hash ^= p[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}
} // namespace

size_t DXSO::GetBytecodeSize( const uint8_t *pBytecode, size_t maxSize ) {
    if ( maxSize < 8 ) {
        return 0;
    }
    uint32_t version = ReadToken( pBytecode );
    if ( ( version & 0xfffe0000 ) != 0xfffe0000 ) {
        return 0;
    }

    size_t offset = 4;
    while ( offset + 4 <= maxSize ) {
        uint32_t token = ReadToken( pBytecode + offset );
        if ( token == kD3DSIO_END ) {
            return offset + 4;
        }
        if ( ( token & 0xffff ) == kD3DSIO_COMMENT ) {
            offset += 4 + ( ( token >> 16 ) & 0x7fff ) * 4;
            continue;
        }
        offset += 4;
    }
    return 0;
}

bool DXSO::ParseConstantTable( const uint8_t *pBytecode,
                               size_t size,
                               std::vector<ShaderConstantInfo> &outConstants ) {
    outConstants.clear();
    size_t offset = 4;
    while ( offset + 8 <= size ) {
        uint32_t token = ReadToken( pBytecode + offset );
        if ( token == kD3DSIO_END ) {
            break;
        }
        if ( ( token & 0xffff ) != kD3DSIO_COMMENT ) {
            offset += 4;
            continue;
        }

        const uint32_t commentSize = ( ( token >> 16 ) & 0x7fff ) * 4;
        const uint8_t *pComment = pBytecode + offset + 4;
        offset += 4 + commentSize;
        if ( commentSize < 4 + 28 || ReadToken( pComment ) != kCTABFourCC ) {
            continue;
        }

        const uint8_t *pTable = pComment + 4;
        const uint32_t tableSize = commentSize - 4;
        const uint32_t numConstants = ReadToken( pTable + 12 );
        const uint32_t constantInfoOffset = ReadToken( pTable + 16 );
        for ( uint32_t i = 0; i < numConstants; i++ ) {
            uint32_t infoOffset = constantInfoOffset + i * 20;
            if ( infoOffset + 20 > tableSize ) {
                break;
            }
            const uint8_t *pInfo = pTable + infoOffset;
            ShaderConstantInfo constant;
            uint32_t nameOffset = ReadToken( pInfo );
            if ( nameOffset < tableSize ) {
                const char *pName = reinterpret_cast<const char *>( pTable + nameOffset );
                constant.Name.assign( pName, strnlen( pName, tableSize - nameOffset ) );
            }
            memcpy( &constant.RegisterSet, pInfo + 4, sizeof( uint16_t ) );
            memcpy( &constant.RegisterIndex, pInfo + 6, sizeof( uint16_t ) );
            memcpy( &constant.RegisterCount, pInfo + 8, sizeof( uint16_t ) );
            outConstants.push_back( constant );
        }
        return true;
    }
    return false;
}

namespace {
struct CTABType {
    uint16_t Class, Type, Rows, Columns, Elements, StructMembers;
    uint32_t StructMemberInfo;
};

CTABType ReadType( const uint8_t *pTable, uint32_t offset ) {
    CTABType type;
    memcpy( &type.Class, pTable + offset + 0, 2 );
    memcpy( &type.Type, pTable + offset + 2, 2 );
    memcpy( &type.Rows, pTable + offset + 4, 2 );
    memcpy( &type.Columns, pTable + offset + 6, 2 );
    memcpy( &type.Elements, pTable + offset + 8, 2 );
    memcpy( &type.StructMembers, pTable + offset + 10, 2 );
    memcpy( &type.StructMemberInfo, pTable + offset + 12, 4 );
    return type;
}

uint32_t TypeRegisterCount( const CTABType &type ) {
    uint32_t perElement = 1;
    if ( type.Class == 2 ) { // matrix rows
        perElement = type.Rows;
    } else if ( type.Class == 3 ) { // matrix columns
        perElement = type.Columns;
    }
    return perElement * Max<uint32_t>( type.Elements, 1u );
}
} // namespace

bool DXSO::ParseConstantMembers( const uint8_t *pBytecode, size_t size,
                                 std::vector<ShaderConstantMember> &outMembers ) {
    outMembers.clear();
    size_t offset = 4;
    while ( offset + 8 <= size ) {
        const uint32_t token = ReadToken( pBytecode + offset );
        if ( token == kD3DSIO_END ) {
            break;
        }
        if ( ( token & 0xffff ) != kD3DSIO_COMMENT ) {
            offset += 4;
            continue;
        }
        const uint32_t commentSize = ( ( token >> 16 ) & 0x7fff ) * 4;
        const uint8_t *pComment = pBytecode + offset + 4;
        offset += 4 + commentSize;
        if ( commentSize < 4 + 28 || ReadToken( pComment ) != kCTABFourCC ) {
            continue;
        }
        const uint8_t *pTable = pComment + 4;
        const uint32_t tableSize = commentSize - 4;
        auto readString = [&]( uint32_t stringOffset ) {
            if ( stringOffset >= tableSize ) {
                return std::string();
            }
            const char *p = reinterpret_cast<const char *>( pTable + stringOffset );
            return std::string( p, strnlen( p, tableSize - stringOffset ) );
        };
        const uint32_t numConstants = ReadToken( pTable + 12 );
        const uint32_t constantInfoOffset = ReadToken( pTable + 16 );
        for ( uint32_t i = 0; i < numConstants; i++ ) {
            const uint32_t infoOffset = constantInfoOffset + i * 20;
            if ( infoOffset + 20 > tableSize ) {
                break;
            }
            const uint8_t *pInfo = pTable + infoOffset;
            uint16_t registerSet, registerIndex, registerCount;
            memcpy( &registerSet, pInfo + 4, 2 );
            memcpy( &registerIndex, pInfo + 6, 2 );
            memcpy( &registerCount, pInfo + 8, 2 );
            const std::string name = readString( ReadToken( pInfo ) );
            const uint32_t typeOffset = ReadToken( pInfo + 12 );
            if ( typeOffset + 16 > tableSize ) {
                continue;
            }
            const CTABType type = ReadType( pTable, typeOffset );
            if ( type.StructMembers == 0 ) {
                ShaderConstantMember member;
                member.Name = name;
                member.RegisterSet = registerSet;
                member.Register = registerIndex;
                member.NumRegisters = registerCount;
                member.Class = static_cast<uint8_t>( type.Class );
                member.Rows = static_cast<uint8_t>( type.Rows );
                member.Columns = static_cast<uint8_t>( type.Columns );
                member.Elements = type.Elements;
                outMembers.push_back( member );
                continue;
            }
            uint32_t memberRegister = 0;
            for ( uint32_t j = 0; j < type.StructMembers; j++ ) {
                const uint32_t memberInfo = type.StructMemberInfo + j * 8;
                if ( memberInfo + 8 > tableSize ) {
                    break;
                }
                const std::string memberName = readString( ReadToken( pTable + memberInfo ) );
                const uint32_t memberTypeOffset = ReadToken( pTable + memberInfo + 4 );
                if ( memberTypeOffset + 16 > tableSize ) {
                    break;
                }
                const CTABType memberType = ReadType( pTable, memberTypeOffset );
                const uint32_t numRegisters = TypeRegisterCount( memberType );
                if ( memberRegister < registerCount ) {
                    ShaderConstantMember member;
                    member.Name = memberName;
                    member.StructName = name;
                    member.RegisterSet = registerSet;
                    member.Register = static_cast<uint16_t>( registerIndex + memberRegister );
                    member.NumRegisters =
                        static_cast<uint16_t>( Min<uint32_t>( numRegisters, registerCount - memberRegister ) );
                    member.Class = static_cast<uint8_t>( memberType.Class );
                    member.Rows = static_cast<uint8_t>( memberType.Rows );
                    member.Columns = static_cast<uint8_t>( memberType.Columns );
                    member.Elements = memberType.Elements;
                    outMembers.push_back( member );
                }
                memberRegister += numRegisters;
            }
        }
        return true;
    }
    return false;
}

void DXSO::ParseSamplerTypes( const uint8_t *pBytecode, size_t size, uint8_t outTypes[16], uint32_t &outMask ) {
    memset( outTypes, 0, 16 );
    outMask = 0;
    if ( size < 8 ) {
        return;
    }
    const uint32_t version = ReadToken( pBytecode );
    const uint32_t majorVersion = ( version >> 8 ) & 0xff;
    size_t offset = 4;
    while ( offset + 4 <= size ) {
        const uint32_t token = ReadToken( pBytecode + offset );
        if ( token == kD3DSIO_END ) {
            break;
        }
        const uint32_t opcode = token & 0xffff;
        if ( opcode == kD3DSIO_COMMENT ) {
            offset += 4 + ( ( token >> 16 ) & 0x7fff ) * 4;
            continue;
        }
        uint32_t length = ( token >> 24 ) & 0x0f;
        if ( majorVersion < 2 ) {
            length = 0; // not used by the TDU2 libraries (sm 3)
        }
        if ( opcode == 0x1f && offset + 12 <= size ) { // dcl
            const uint32_t dclToken = ReadToken( pBytecode + offset + 4 );
            const uint32_t registerToken = ReadToken( pBytecode + offset + 8 );
            const uint32_t registerType = ( ( registerToken >> 28 ) & 0x7 ) | ( ( ( registerToken >> 11 ) & 0x3 ) << 3 );
            const uint32_t registerNumber = registerToken & 0x7ff;
            if ( registerType == 10 && registerNumber < 16 ) { // D3DSPR_SAMPLER
                outTypes[registerNumber] = static_cast<uint8_t>( ( dclToken >> 27 ) & 0xf );
                outMask |= 1u << registerNumber;
            }
        }
        offset += 4 + length * 4;
    }
}

bool DXSO::Translate( const uint8_t *pBytecode,
                      size_t size,
                      const char *pDebugName,
                      TranslatedShader &outShader ) {
    outShader = TranslatedShader();

    size_t bytecodeSize = GetBytecodeSize( pBytecode, size );
    if ( bytecodeSize == 0 ) {
        OTDU_LOG_WARN( "Invalid D3D9 bytecode for shader '%s'\n", pDebugName );
        return false;
    }

    const uint32_t version = ReadToken( pBytecode );
    outShader.Stage = ( ( version >> 16 ) == 0xfffe ) ? eShaderType::ST_Vertex
                                                       : eShaderType::ST_Pixel;
    outShader.BytecodeHash = HashBytecode( pBytecode, bytecodeSize );
    ParseConstantTable( pBytecode, bytecodeSize, outShader.Constants );

    // DXVK expects a copy it can read past the end token.
    std::vector<uint8_t> bytecode( pBytecode, pBytecode + bytecodeSize );
    bytecode.resize( bytecode.size() + 16, 0 );

    dxvk::D3D9ConstantLayout layout;
    if ( outShader.Stage == eShaderType::ST_Vertex ) {
        layout.floatCount = 256;
    } else {
        layout.floatCount = 224;
    }
    layout.intCount = 16;
    layout.boolCount = 16;
    layout.bitmaskCount = Align( layout.boolCount, 32u ) / 32;

    try {
        dxvk::DxsoReader reader( reinterpret_cast<const char *>( bytecode.data() ) );
        dxvk::DxsoModule module( reader );
        dxvk::DxsoAnalysisInfo analysis = module.analyze();
        dxvk::DxsoModuleInfo moduleInfo;
        // No robustness2 on our devices (MoltenVK): bound-check relative constant
        // accesses in the shader instead. Depth-compare aliases are not emitted.
        moduleInfo.options.robustness2Supported = false;
        moduleInfo.options.disableDepthCompareSamplers = true;

        std::unique_ptr<dxvk::OpenTDUOutput> pOutput( module.compile(
            moduleInfo, pDebugName != nullptr ? pDebugName : "shader",
            analysis, layout ) );
        if ( pOutput == nullptr ) {
            return false;
        }

        const uint32_t numDwords = static_cast<uint32_t>( pOutput->Code.dwords() );
        outShader.SPIRV.assign( pOutput->Code.data(),
                                pOutput->Code.data() + numDwords );
        outShader.InputMask = pOutput->Infos.inputMask;
        outShader.OutputMask = pOutput->Infos.outputMask;
        outShader.PushConstantsSize = pOutput->Infos.localPushData.getSize();
        outShader.UsedSamplers = module.usedSamplers();
        outShader.MaxDefinedConstant = module.maxDefinedConstant();

        if ( outShader.Stage == eShaderType::ST_Vertex ) {
            const dxvk::DxsoIsgn &isgn = module.isgn();
            for ( uint32_t i = 0; i < isgn.elemCount; i++ ) {
                ShaderInputInfo input;
                input.Usage = static_cast<uint8_t>( isgn.elems[i].semantic.usage );
                input.UsageIndex = static_cast<uint8_t>( isgn.elems[i].semantic.usageIndex );
                input.Location = static_cast<uint8_t>( isgn.elems[i].slot );
                outShader.Inputs.push_back( input );
            }
        }
    } catch ( ... ) {
        OTDU_LOG_WARN( "DXSO translation failed for shader '%s'\n", pDebugName );
        return false;
    }

    return !outShader.SPIRV.empty();
}
