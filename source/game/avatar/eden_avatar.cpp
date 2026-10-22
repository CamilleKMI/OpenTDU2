#include "shared.h"
#include "eden_avatar.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"

namespace {
constexpr uint64_t kHairShaderSet = 0xd5013ed0dbdededeull;

// FUN_00cb43e0: BodyID -> body mesh, body material variant, head.
struct BodyEntry {
    const char *pBody;
    const char *pMaterials;
    const char *pHead;
};
const BodyEntry kBodies[6] = {
    { "man_body", "man_body", "man_head_occ" },           { "woman_body", "woman_body", "woman_head_occ" },
    { "man_body", "man_body_asia", "man_head_asia" },     { "woman_body", "woman_body_asia", "woman_head_asia" },
    { "man_body", "man_body_afrik", "man_head_afrik" },   { "woman_body", "woman_body_afrik", "woman_head_afrik" },
};

// Body zones (FUN_00cada30, material = fold of the full name) and the
// matching DBClothe flags (0: the garment covers the zone).
const char *kZoneMaterials[21] = { "BASE",
                                   "BOTTOM_FEET",
                                   "BOTTOM_ANKLES",
                                   "BOTTOM_CALVES",
                                   "BOTTOM_KNEES",
                                   "BOTTOM_THIGHS",
                                   "BOTTOM_PELVIS",
                                   "TOP_LOWER_ABDOMEN",
                                   "TOP_ABDOMEN",
                                   "TOP_LOWER_BACK",
                                   "TOP_BACK",
                                   "TOP_UPPER_BACK_CENTER",
                                   "TOP_UPPER_BACK_SIDES",
                                   "TOP_CLEAVAGE_LARGE",
                                   "TOP_CLEAVAGE",
                                   "TOP_CLEAVAGE_SIDES",
                                   "TOP_UPPER_CHEST",
                                   "TOP_ARMS_SHOULDERS",
                                   "TOP_ARMS",
                                   "TOP_ARMS_FOREARMS",
                                   "TOP_ARMS_HANDS" };
const char *kZoneFlags[21] = { "_base",       "_feet",          "_ankles",           "_calves",
                               "_knees",      "_thighs",        "_pelvis",           "_lower_abdomen",
                               "_abdomen",    "_lower_back",    "_back",             "_upper_back_center",
                               "_upper_back_sides", "_cleavage_large", "_cleavage", "_cleavage_sides",
                               "_upper_chest", "_shoulders",    "_arms",             "_forearms",
                               "_hands" };

const char *kSlots[10] = { "hair", "top", "bottom", "dress", "shoes", "glasses", "gloves", "helmet", "accessories",
                           "beard" };

// Clothes directory from the code of the file name (W_PN_..., M_HR_...).
const char *ClothesDirectory( const std::string &file ) {
    const std::string code = file.size() >= 4 ? ToUpper( file.substr( 2, 2 ) ) : "";
    static const std::pair<const char *, const char *> kDirectories[] = {
        { "HR", "hair" },       { "PN", "pnj" },      { "SP", "sportswear" }, { "SW", "streetwear" },
        { "FH", "fashion" },    { "HL", "highlife" }, { "CA", "casino" },     { "ED", "eden" },
    };
    for ( const auto &entry : kDirectories ) {
        if ( code == entry.first ) {
            return entry.second;
        }
    }
    return nullptr;
}

// .avt: big endian tree { u16 size; u16 key; value[size - 2] }, BodyID key
// 0xcd26 (u16).
bool FindAvtValue( const uint8_t *pData, size_t begin, size_t end, uint16_t wantedKey, uint16_t &outValue ) {
    size_t offset = begin;
    while ( offset + 4 <= end ) {
        const uint16_t entrySize = static_cast<uint16_t>( ( pData[offset] << 8 ) | pData[offset + 1] );
        const uint16_t key = static_cast<uint16_t>( ( pData[offset + 2] << 8 ) | pData[offset + 3] );
        if ( entrySize < 2 ) {
            return false;
        }
        const size_t valueSize = entrySize - 2u;
        if ( offset + 4 + valueSize > end ) {
            return false;
        }
        if ( key == wantedKey && valueSize == 2 ) {
            outValue = static_cast<uint16_t>( ( pData[offset + 4] << 8 ) | pData[offset + 5] );
            return true;
        }
        if ( valueSize > 16 && FindAvtValue( pData, offset + 4, offset + 4 + valueSize, wantedKey, outValue ) ) {
            return true;
        }
        offset += 4 + valueSize;
    }
    return false;
}

// Raw value bytes of a key (big endian tree as above).
bool FindAvtBytes( const uint8_t *pData, size_t begin, size_t end, uint16_t wantedKey, const uint8_t *&outValue,
                   size_t &outSize ) {
    size_t offset = begin;
    while ( offset + 4 <= end ) {
        const uint16_t entrySize = static_cast<uint16_t>( ( pData[offset] << 8 ) | pData[offset + 1] );
        const uint16_t key = static_cast<uint16_t>( ( pData[offset + 2] << 8 ) | pData[offset + 3] );
        if ( entrySize < 2 ) {
            return false;
        }
        const size_t valueSize = entrySize - 2u;
        if ( offset + 4 + valueSize > end ) {
            return false;
        }
        if ( key == wantedKey ) {
            outValue = pData + offset + 4;
            outSize = valueSize;
            return true;
        }
        if ( valueSize > 16 && FindAvtBytes( pData, offset + 4, offset + 4 + valueSize, wantedKey, outValue, outSize ) ) {
            return true;
        }
        offset += 4 + valueSize;
    }
    return false;
}

// Big endian f32 values of a key (count read, 0 if absent).
uint32_t FindAvtFloats( const BankEntry *pAvt, uint16_t key, float *pOut, uint32_t maxCount ) {
    if ( pAvt == nullptr || pAvt->Size <= 2 ) {
        return 0;
    }
    const uint8_t *pValue = nullptr;
    size_t size = 0;
    if ( !FindAvtBytes( pAvt->pData, 2, pAvt->Size, key, pValue, size ) ) {
        return 0;
    }
    uint32_t count = 0;
    for ( ; count < maxCount && ( count + 1 ) * 4 <= size; count++ ) {
        const uint32_t bits = ( static_cast<uint32_t>( pValue[count * 4] ) << 24 ) | ( pValue[count * 4 + 1] << 16 ) |
                              ( pValue[count * 4 + 2] << 8 ) | pValue[count * 4 + 3];
        memcpy( &pOut[count], &bits, 4 );
    }
    return count;
}

XmbValue FindRow( const char *pTable, const char *pField, const std::function<bool( XmbValue )> &match ) {
    if ( gpDatabase == nullptr ) {
        return XmbValue();
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( pTable ); i++ ) {
        XmbValue row = root.getArrayElement( pTable, i );
        if ( match( row[pField] ) ) {
            return row;
        }
    }
    return XmbValue();
}
} // namespace

EdenAvatarLibrary::EdenAvatarLibrary() {}

EdenAvatarLibrary::~EdenAvatarLibrary() { unload(); }

bool EdenAvatarLibrary::load() {
    unload();
    const std::string folder = gpFile->getBankFolder() + "avatar/";
    TextureCache &textures = gpRender->getTextureCache();
    // COMMON_BODIES (hires), COMMON_BODIES_LIGHT as the fallback (FUN_00ca9040).
    if ( !bodiesBank.load( ( folder + "common_bodies.bnk" ).c_str() ) &&
         !bodiesBank.load( ( folder + "common_bodies_light.bnk" ).c_str() ) ) {
        OTDU_LOG_WARN( "Avatars: common_bodies.bnk not found\n" );
        return false;
    }
    textures.loadBankTextures( bodiesBank, bodiesBank.getPath().c_str() );
    if ( clothesCommonBank.load( ( folder + "clothes/common_clothes.bnk" ).c_str() ) ) {
        textures.loadBankTextures( clothesCommonBank, clothesCommonBank.getPath().c_str() );
        if ( const BankEntry *pPalette = clothesCommonBank.findEntry( "hair_mat", "2dm" ) ) {
            hairPalette.parse( pPalette->pData, pPalette->Size );
        }
    }
    configBank.load( ( folder + "config/pnj.bnk" ).c_str() );
    const BankEntry *pMan = bodiesBank.findEntry( "man_body", "xsb" );
    const BankEntry *pWoman = bodiesBank.findEntry( "woman_body", "xsb" );
    bLoaded = pMan != nullptr && pWoman != nullptr && manSkeleton.parse( pMan->pData, pMan->Size ) &&
              womanSkeleton.parse( pWoman->pData, pWoman->Size );
    OTDU_LOG_INFO( "Avatars: skeletons %u / %u bones\n", manSkeleton.getNumBones(), womanSkeleton.getNumBones() );
    return bLoaded;
}

void EdenAvatarLibrary::unload() {
    if ( gpRender != nullptr ) {
        TextureCache &textures = gpRender->getTextureCache();
        for ( Bank *pBank : { &bodiesBank, &clothesCommonBank } ) {
            if ( pBank->isLoaded() ) {
                textures.releaseOwner( pBank->getPath().c_str() );
            }
        }
        for ( auto &entry : clothesBanks ) {
            if ( entry.second != nullptr && entry.second->isLoaded() ) {
                textures.releaseOwner( entry.second->getPath().c_str() );
            }
        }
    }
    clothesBanks.clear();
    bodiesBank.unload();
    clothesCommonBank.unload();
    configBank.unload();
    bLoaded = false;
}

const Bank *EdenAvatarLibrary::getClothesBank( const std::string &file ) {
    auto it = clothesBanks.find( file );
    if ( it != clothesBanks.end() ) {
        return it->second.get();
    }
    const char *pDirectory = ClothesDirectory( file );
    std::unique_ptr<Bank> pBank;
    if ( pDirectory != nullptr ) {
        const std::string path =
            gpFile->getBankFolder() + "avatar/clothes/" + pDirectory + "/" + ToLower( file ) + ".bnk";
        pBank = std::make_unique<Bank>();
        if ( pBank->load( path.c_str() ) ) {
            gpRender->getTextureCache().loadBankTextures( *pBank, pBank->getPath().c_str() );
        } else {
            OTDU_LOG_WARN( "Avatars: clothes bank '%s' not found\n", path.c_str() );
            pBank.reset();
        }
    }
    const Bank *pResult = pBank.get();
    clothesBanks[file] = std::move( pBank );
    return pResult;
}

EdenAvatar::EdenAvatar() {}

EdenAvatar::~EdenAvatar() { unload(); }

void EdenAvatar::unload() {
    parts.clear();
    pSkeleton = nullptr;
}

bool EdenAvatar::load( EdenAvatarLibrary &library, const char *pConfigId ) {
    unload();
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !library.isLoaded() || pRenderer == nullptr ) {
        return false;
    }
    XmbValue pnj = FindRow( "DBPNJ", "_configid",
                            [pConfigId]( XmbValue v ) { return StringCmpI( v.asString().c_str(), pConfigId ) == 0; } );
    if ( !pnj.isValid() ) {
        OTDU_LOG_WARN( "Avatars: DBPNJ '%s' not found\n", pConfigId );
        return false;
    }
    bWoman = pnj["_sex"].asInt() == 1;
    // .avt BodyID (default: occidental body of the sex).
    uint16_t bodyId = bWoman ? 1 : 0;
    const std::string avtName = pnj["_fileconfigid"].asString();
    if ( const BankEntry *pAvt = library.getConfigBank().findEntry( avtName.c_str(), "avt" ) ) {
        uint16_t value = 0;
        if ( pAvt->Size > 2 && FindAvtValue( pAvt->pData, 2, pAvt->Size, 0xcd26, value ) && value < 6 ) {
            bodyId = value;
        }
    }
    pSkeleton = &library.getSkeleton( ( bodyId & 1 ) != 0 );
    const BodyEntry &body = kBodies[bodyId];

    // Clothes rows (and the zones they cover).
    bool zoneVisible[21];
    for ( bool &bVisible : zoneVisible ) {
        bVisible = true;
    }
    struct Worn {
        std::string File;
        int32_t Color;
        bool bHair;
    };
    std::vector<Worn> worn;
    for ( const char *pSlot : kSlots ) {
        const uint64_t clotheHash = pnj[( std::string( "_clothe_" ) + pSlot ).c_str()].asUInt64();
        XmbValue clothe = FindRow( "DBClothe", "_hashcode", [clotheHash]( XmbValue v ) { return v.asUInt64() == clotheHash; } );
        if ( !clothe.isValid() ) {
            continue;
        }
        // FUN_00cac1b0: only the top, bottom, dress, shoes and gloves slots
        // hide body zones (whatever their file).
        const bool bZoneSlot = strcmp( pSlot, "top" ) == 0 || strcmp( pSlot, "bottom" ) == 0 ||
                               strcmp( pSlot, "dress" ) == 0 || strcmp( pSlot, "shoes" ) == 0 ||
                               strcmp( pSlot, "gloves" ) == 0;
        if ( bZoneSlot ) {
            for ( uint32_t z = 0; z < 21; z++ ) {
                if ( clothe[kZoneFlags[z]].asInt( 1 ) == 0 ) {
                    zoneVisible[z] = false;
                }
            }
        }
        const std::string file = clothe["_file"].asString();
        if ( file.empty() || file.find( "NO_" ) != std::string::npos ) {
            continue;
        }
        worn.push_back( { file, static_cast<int32_t>( pnj[( std::string( "_" ) + pSlot + "_color" ).c_str()].asInt() ),
                          strcmp( pSlot, "hair" ) == 0 || strcmp( pSlot, "beard" ) == 0 } );
    }

    // Body (material variant of the BodyID) and head.
    const Bank &bodies = library.getBodiesBank();
    auto loadPart = [&]( const Bank &bank, const char *pMesh, const char *pMaterials ) -> EdenModel * {
        const BankEntry *pScene = bank.findEntry( pMesh, "3dd" );
        const BankEntry *pGeometry = bank.findEntry( pMesh, "3dg" );
        const BankEntry *pMaterialEntry = bank.findEntry( pMaterials, "2dm" );
        auto pModel = std::make_unique<EdenModel>();
        if ( pScene == nullptr || pGeometry == nullptr ||
             !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterialEntry, nullptr, bank.getPath().c_str() ) ) {
            OTDU_LOG_WARN( "Avatars: part '%s' not loaded\n", pMesh );
            return nullptr;
        }
        parts.push_back( std::move( pModel ) );
        return parts.back().get();
    };
    if ( EdenModel *pBody = loadPart( bodies, body.pBody, body.pMaterials ) ) {
        for ( EdenMaterial &material : pBody->getMaterials() ) {
            for ( uint32_t z = 0; z < 21; z++ ) {
                if ( !zoneVisible[z] && material.Hashcode == GetIdentifier64bit( kZoneMaterials[z] ) ) {
                    material.pProgram = nullptr; // hidden zone
                }
            }
        }
    }
    loadPart( bodies, body.pHead, body.pHead );
    applySkin( library.getConfigBank().findEntry( avtName.c_str(), "avt" ) );

    for ( const Worn &item : worn ) {
        const Bank *pBank = library.getClothesBank( item.File );
        if ( pBank == nullptr ) {
            continue;
        }
        const std::string base = ToLower( item.File );
        EdenModel *pModel = loadPart( *pBank, base.c_str(), base.c_str() );
        if ( pModel == nullptr ) {
            continue;
        }
        if ( item.bHair ) {
            // FUN_00cb2080: hair colour index -> HAIR_MAT palette material
            // (14 = keep the hair's own colours).
            const auto &palette = library.getHairPalette().getOrderedMaterials();
            if ( item.Color >= 0 && item.Color < static_cast<int32_t>( palette.size() ) ) {
                const MaterialDesc &colors = palette[item.Color];
                for ( EdenMaterial &material : pModel->getMaterials() ) {
                    if ( material.ShaderSetGUID != kHairShaderSet ) {
                        continue;
                    }
                    for ( const char *pName : { "PRIMCLR", "SCDCLR" } ) {
                        auto it = colors.Parameters.find( GetIdentifier64bit( pName ) );
                        if ( it != colors.Parameters.end() ) {
                            material.Parameters[it->first] = it->second;
                        }
                    }
                }
            }
            continue;
        }
        // Colour variant #index of <file>_mat.2dm.
        const BankEntry *pVariants = pBank->findEntry( ( base + "_mat" ).c_str(), "2dm" );
        if ( pVariants == nullptr ) {
            continue;
        }
        Render2DM variants;
        variants.parse( pVariants->pData, pVariants->Size );
        for ( EdenMaterial &material : pModel->getMaterials() ) {
            int32_t index = 0;
            for ( const MaterialDesc &variant : variants.getOrderedMaterials() ) {
                if ( variant.Hashcode != material.Hashcode ) {
                    continue;
                }
                if ( index++ == item.Color ) {
                    for ( const auto &parameter : variant.Parameters ) {
                        material.Parameters[parameter.first] = parameter.second;
                    }
                    break;
                }
            }
        }
    }
    OTDU_LOG_DEBUG( "Avatars: '%s' body %u, %zu parts\n", pConfigId, bodyId, parts.size() );
    return !parts.empty();
}

void EdenAvatar::draw( EdenRenderer &renderer, const Matrix4x4f &world,
                       const std::vector<Matrix4x4f> &skinMatrices, const EdenAvatarLighting *pLighting ) const {
    for ( const auto &pPart : parts ) {
        pPart->drawSkinned( renderer, world, skinMatrices, pLighting );
    }
}

void EdenAvatar::applySkin( const BankEntry *pAvt ) {
    // .avt keys (FUN_00cbd030): 6cb0 SkinAmbiant, 1a6c SkinDiffuse, e173
    // SkinSpecular, cbfd (SpecCoeff, SpecPow, RIMCoeff), e9ce (DiffWrap,
    // ScatteringCoeff), 8d6d age, 3f89 freckle. Defaults (FUN_00cbd680):
    // colours 1, age 0, freckle 0; Spec/Wrap from the 2DM.
    float ambient[3] = { 1.0f, 1.0f, 1.0f };
    float diffuse[3] = { 1.0f, 1.0f, 1.0f };
    float specular[3] = { 1.0f, 1.0f, 1.0f };
    float spec[3] = {};
    float wrap[2] = {};
    float age = 0.0f;
    float freckle = 0.0f;
    FindAvtFloats( pAvt, 0x6cb0, ambient, 3 );
    FindAvtFloats( pAvt, 0x1a6c, diffuse, 3 );
    FindAvtFloats( pAvt, 0xe173, specular, 3 );
    const uint32_t numSpec = FindAvtFloats( pAvt, 0xcbfd, spec, 3 );
    const uint32_t numWrap = FindAvtFloats( pAvt, 0xe9ce, wrap, 2 );
    FindAvtFloats( pAvt, 0x8d6d, &age, 1 );
    FindAvtFloats( pAvt, 0x3f89, &freckle, 1 );
    const uint64_t specParmId = GetIdentifier64bit( "SPECPARM" );
    const uint64_t coeffsId = GetIdentifier64bit( "COEFFS" );
    for ( auto &pPart : parts ) {
        for ( EdenMaterial &material : pPart->getMaterials() ) {
            if ( material.ShaderSetGUID != 0xd5013ed0dbcdcdcdull && material.ShaderSetGUID != 0xd5013ed0dbcddbcaull ) {
                continue;
            }
            material.Ambient = Vector4f( ambient[0], ambient[1], ambient[2], 1.0f );
            material.Diffuse = Vector4f( diffuse[0], diffuse[1], diffuse[2], 1.0f );
            material.Specular = Vector4f( specular[0], specular[1], specular[2], 1.0f );
            Vector4f &specParm = material.Parameters[specParmId];
            if ( numSpec >= 2 ) {
                specParm.x = spec[0] * 0.5f;
                specParm.y = spec[1];
                specParm.z = 0.1f; // float @0xede03c (RIMCoeff not used)
            }
            specParm.w = age;
            Vector4f &coeffs = material.Parameters[coeffsId];
            if ( numWrap >= 2 ) {
                coeffs.x = wrap[0];
                coeffs.z = wrap[1];
            }
            coeffs.y = freckle;
        }
    }
}
