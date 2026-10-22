#include "shared.h"
#include "shader_library.h"

#include "filesystem/gs_file.h"
#include "render/shaders/shader_cache.h"

namespace {
std::vector<uint8_t> ReadByteArray( const XmbValue &value, const char *pField ) {
    uint32_t count = 0;
    const uint8_t *pData = value.getArrayData( pField, &count );
    if ( pData == nullptr || count == 0 ) {
        return {};
    }
    return std::vector<uint8_t>( pData, pData + count );
}
} // namespace

bool ShaderLibrary::load( const char *pPath ) {
    std::vector<uint8_t> buffer;
    if ( gpFile == nullptr || !gpFile->loadFile( pPath, buffer ) ) {
        OTDU_LOG_WARN( "Failed to load shader library '%s'\n", pPath );
        return false;
    }
    return loadFromMemory( buffer.data(), buffer.size() );
}

bool ShaderLibrary::loadFromMemory( const uint8_t *pBuffer, size_t size ) {
    profiles.clear();
    translatedCache.clear();

    if ( !document.load( pBuffer, size ) ) {
        return false;
    }

    XmbValue root = document.getRoot();
    const XmbType *pRootType = root.getType();
    if ( pRootType == nullptr || pRootType->Name != "ShaderSet" ) {
        OTDU_LOG_WARN( "Not a shader library (root type '%s')\n",
                       pRootType != nullptr ? pRootType->Name.c_str() : "?" );
        return false;
    }

    name = root["name"].asString();
    guid = root["guid"].asUInt64();

    const uint32_t numProfiles = root.getArraySize( "profArr" );
    profiles.resize( numProfiles );
    for ( uint32_t i = 0; i < numProfiles; i++ ) {
        XmbValue profileValue = root.getArrayElement( "profArr", i );
        Profile &profile = profiles[i];
        profile.ID = static_cast<uint8_t>( profileValue["id"].asInt() );
        profile.MaxLOD = static_cast<uint8_t>( profileValue["maxlod"].asInt() );

        XmbValue symTable = profileValue["symTab"];
        const uint32_t numSymbols = symTable.getArraySize( "symbols" );
        for ( uint32_t j = 0; j < numSymbols; j++ ) {
            XmbValue symbolValue = symTable.getArrayElement( "symbols", j );
            Symbol symbol;
            symbol.Name = symbolValue["name"].asUInt64();
            symbol.Format = static_cast<uint16_t>( symbolValue["fmt"].asInt() );
            symbol.Size = static_cast<uint16_t>( symbolValue["size"].asInt() );
            profile.Symbols.push_back( symbol );
        }

        const uint32_t numAttribs = profileValue.getArraySize( "attribs" );
        profile.Attribs.resize( numAttribs );
        for ( uint32_t j = 0; j < numAttribs; j++ ) {
            XmbValue attribValue = profileValue.getArrayElement( "attribs", j );
            Attrib &attrib = profile.Attribs[j];
            attrib.Frame = static_cast<uint8_t>( attribValue["frame"].asInt() );
            attrib.Scene = static_cast<uint8_t>( attribValue["scene"].asInt() );
            attrib.Skin = static_cast<uint8_t>( attribValue["skin"].asInt() );
            attrib.MaterialIDs = ReadByteArray( attribValue, "matID" );
            attrib.DynamicIDs = ReadByteArray( attribValue, "dynID" );
            attrib.SymbolIDs = ReadByteArray( attribValue, "symID" );
            attrib.LiteralIDs = ReadByteArray( attribValue, "litID" );

            uint32_t bytecodeSize = 0;
            attrib.pBytecode = attribValue.getArrayData( "bin", &bytecodeSize );
            attrib.BytecodeSize = bytecodeSize;
        }

        const uint32_t numItems = profileValue.getArraySize( "shaders" );
        profile.Items.resize( numItems );
        for ( uint32_t j = 0; j < numItems; j++ ) {
            XmbValue itemValue = profileValue.getArrayElement( "shaders", j );
            Item &item = profile.Items[j];
            item.Hashcode = itemValue["id"].asUInt64();
            item.AttribIndex = static_cast<uint16_t>( itemValue["attrib"].asInt() );
            item.Spec = ReadByteArray( itemValue, "spec" );
        }
    }

    return true;
}

const ShaderLibrary::Profile *ShaderLibrary::getProfile( eProfile profileId ) const {
    for ( const Profile &profile : profiles ) {
        if ( profile.ID == profileId ) {
            return &profile;
        }
    }
    return nullptr;
}

const ShaderLibrary::Attrib *ShaderLibrary::findShader( eProfile profileId,
                                                        uint64_t permutation ) const {
    const Profile *pProfile = getProfile( profileId );
    if ( pProfile == nullptr ) {
        return nullptr;
    }
    for ( const Item &item : pProfile->Items ) {
        if ( item.Hashcode == permutation && item.AttribIndex < pProfile->Attribs.size() ) {
            return &pProfile->Attribs[item.AttribIndex];
        }
    }
    return nullptr;
}

const ShaderLibrary::Attrib *
ShaderLibrary::getShaderByPermutationIndex( eProfile profileId,
                                            uint32_t index ) const {
    const Profile *pProfile = getProfile( profileId );
    if ( pProfile == nullptr || index >= pProfile->Items.size() ) {
        return nullptr;
    }
    uint16_t attribIndex = pProfile->Items[index].AttribIndex;
    return attribIndex < pProfile->Attribs.size() ? &pProfile->Attribs[attribIndex]
                                                  : nullptr;
}

const TranslatedShader *ShaderLibrary::getTranslatedShader( eProfile profileId,
                                                            uint32_t attribIndex ) {
    const Profile *pProfile = getProfile( profileId );
    if ( pProfile == nullptr || attribIndex >= pProfile->Attribs.size() ) {
        return nullptr;
    }

    const uint64_t key = ( static_cast<uint64_t>( profileId ) << 32 ) | attribIndex;
    auto it = translatedCache.find( key );
    if ( it != translatedCache.end() ) {
        return it->second.get();
    }

    const Attrib &attrib = pProfile->Attribs[attribIndex];
    std::string debugName = name + ( profileId == SP_VertexShader ? "_vs_" : "_ps_" ) +
                            std::to_string( attribIndex );

    auto pShader = std::make_unique<TranslatedShader>();
    if ( !ShaderCache::GetOrTranslate( attrib.pBytecode, attrib.BytecodeSize,
                                       debugName.c_str(), *pShader ) ) {
        return nullptr;
    }

    const TranslatedShader *pResult = pShader.get();
    translatedCache[key] = std::move( pShader );
    return pResult;
}
