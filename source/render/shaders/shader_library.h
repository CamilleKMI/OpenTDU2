#pragma once

#include "core/xmb.h"
#include "render/shaders/dxso_translator.h"

// Eden shader library (.sob), stored in Euro/Shaders. Big endian XMB:
//
// ShaderSet {
//   String  name;          // "TD_CarPaint"
//   UInt64  guid;
//   UInt8   verMaj, verMin, scntypes, disScnTypes;
//   Bool    debug;
//   Profile profArr[];     // one per shader stage
// }
// Profile {
//   UInt8    id;           // 2: vs_3_0, 6: ps_3_0
//   UInt8    maxlod;
//   SymTable symTab;       // { Sym symbols[] }  Sym = { UInt64 name; UInt16 fmt, size }
//   LitTable litTab;       // { Object literals[] }
//   Attrib   attribs[];    // compiled shaders
//   Item     shaders[];    // permutations
// }
// Attrib {
//   UInt8 frame, scene, skin;
//   UInt8 matID[], dynID[], symID[], litID[];
//   UInt8 bin[];           // D3D9 bytecode
// }
// Item {
//   UInt64 id;             // permutation hashcode (same in every profile)
//   UInt16 attrib;         // index in Profile::attribs
//   UInt8  spec[];         // permutation switches
// }
class ShaderLibrary {
public:
    enum eProfile : uint8_t {
        SP_VertexShader = 2,
        SP_PixelShader = 6,
    };

    struct Symbol {
        uint64_t Name = 0;
        uint16_t Format = 0;
        uint16_t Size = 0;
    };

    struct Attrib {
        uint8_t Frame = 0;
        uint8_t Scene = 0;
        uint8_t Skin = 0;
        std::vector<uint8_t> MaterialIDs;
        std::vector<uint8_t> DynamicIDs;
        std::vector<uint8_t> SymbolIDs;
        std::vector<uint8_t> LiteralIDs;
        const uint8_t *pBytecode = nullptr;
        uint32_t BytecodeSize = 0;
    };

    struct Item {
        uint64_t Hashcode = 0;
        uint16_t AttribIndex = 0;
        std::vector<uint8_t> Spec;
    };

    struct Profile {
        uint8_t ID = 0;
        uint8_t MaxLOD = 0;
        std::vector<Symbol> Symbols;
        std::vector<Attrib> Attribs;
        std::vector<Item> Items;
    };

public:
    inline const std::string &getName() const { return name; }
    inline uint64_t getGUID() const { return guid; }
    inline const std::vector<Profile> &getProfiles() const { return profiles; }
    inline uint32_t getNumPermutations() const {
        return profiles.empty() ? 0u
                                : static_cast<uint32_t>( profiles[0].Items.size() );
    }

public:
    bool load( const char *pPath );
    bool loadFromMemory( const uint8_t *pBuffer, size_t size );

    const Profile *getProfile( eProfile profileId ) const;

    // Returns the compiled shader for a given permutation (nullptr if the
    // permutation does not exist).
    const Attrib *findShader( eProfile profileId, uint64_t permutation ) const;
    const Attrib *getShaderByPermutationIndex( eProfile profileId,
                                               uint32_t index ) const;

    // Translates (and caches) a shader to SPIR-V.
    const TranslatedShader *getTranslatedShader( eProfile profileId,
                                                 uint32_t attribIndex );

private:
    XmbDocument document;
    std::string name;
    uint64_t guid = 0;
    std::vector<Profile> profiles;
    std::unordered_map<uint64_t, std::unique_ptr<TranslatedShader>> translatedCache;
};
