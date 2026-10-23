#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_animation.h"
#include "render/eden/eden_model.h"

// Avatars (players and PNJ) assembled like the original (docs/re/avatar_spec.md
// section 3): DBPNJ(_configid) -> .avt (avatar/config/pnj.bnk: BodyID) + the
// clothes of the DBPNJ row (DBClothe._file -> avatar/clothes/<dir>/<file>.bnk,
// colour = material variant of <file>_mat.2dm, hair colour = palette of
// common_clothes hair_mat.2dm), body zones hidden by the worn garments,
// skinned on the man / woman skeleton of common_bodies.bnk.
class EdenAvatarLibrary {
public:
    EdenAvatarLibrary();
    ~EdenAvatarLibrary();

    bool load();
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    const Bank &getBodiesBank() const { return bodiesBank; }
    const EdenSkeleton &getSkeleton( bool bWoman ) const { return bWoman ? womanSkeleton : manSkeleton; }
    const Bank *getClothesBank( const std::string &file );
    const Bank &getConfigBank() const { return configBank; }
    const Render2DM &getHairPalette() const { return hairPalette; }

private:
    Bank bodiesBank;
    Bank clothesCommonBank;
    Bank configBank;
    Render2DM hairPalette;
    EdenSkeleton manSkeleton;
    EdenSkeleton womanSkeleton;
    std::unordered_map<std::string, std::unique_ptr<Bank>> clothesBanks;
    bool bLoaded = false;
};

class EdenAvatar {
public:
    EdenAvatar();
    ~EdenAvatar();

    // configId: DBPNJ _configid (e.g. "STO_Party_Woman02").
    bool load( EdenAvatarLibrary &library, const char *pConfigId );
    void unload();
    inline bool isLoaded() const { return pSkeleton != nullptr; }
    inline const EdenSkeleton &getSkeleton() const { return *pSkeleton; }
    inline bool isWoman() const { return bWoman; }
    // .avt FaceParamArray (key 9d93, 49 signed bytes, struct +0xa2; the
    // FaceFX customisation registers, docs/re/facefx_spec.md).
    inline const int8_t *getFaceParams() const { return faceParams; }
    // TD_Skinface facialAnimCoef1 / facialAnimCoef2 (FaceFX normal map
    // weights, FUN_00caf700).
    void setFacialCoefficients( const Vector4f &coef1, const Vector4f &coef2 );

    // skinMatrices: EdenSkeleton::computeSkinMatrices of the current pose.
    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const std::vector<Matrix4x4f> &skinMatrices,
               const struct EdenAvatarLighting *pLighting = nullptr, bool bDrawHead = true ) const;

private:
    const EdenSkeleton *pSkeleton = nullptr;
    bool bWoman = false;
    std::vector<std::unique_ptr<EdenModel>> parts;
    std::vector<size_t> headParts; // head mesh and the clothes worn on it
    int8_t faceParams[49] = {};

    // FUN_00cbe2a0 / FUN_00cbe1a0 / FUN_00cbe220: .avt skin values into the
    // TD_Skin2 / TD_Skinface materials (docs/re/skin_zones_spec.md).
    void applySkin( const struct BankEntry *pAvt );
};
