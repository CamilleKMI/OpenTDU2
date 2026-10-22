#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_model.h"
#include "world/pmi.h"

// Island streaming data (Euro/Bnk/islands/<island>/level), rendered with the
// original shaders.
//
// Layout (worldconfig.cpr + FUN_00990020 in TestDrive2.exe):
//   - areaColumns x areaRows areas of 8 x 8 sectors, a sector is 768 m wide
//     (0x300). Area (ax, ay) covers [8 ax 768, 8 (ax + 1) 768] on X and the
//     same with ay on Z.
//   - Sector (ax, ay, sx, sy) corner: X = (8 ax + sx) 768 (+ 384 when sy is
//     odd: the rows are staggered like bricks), Z = (8 ay + sy) 768.
//     Path "Area-%d\Area-%d-%d\Sector-%d-%d-%d-%d.bnk".
//   - Sector bank: heightfield (sector-*.3dg: 257 x 257 heights, 3 m
//     spacing, R32 height + half4 normal, no index buffer, TD_HeightMap),
//     color / shadow maps (-c / -s .2db), objects (-o .3dd/.3dg/.2dm, sector
//     local), instance lists (.pmi), roads, collision...
//   - Area bank: low resolution heightfield (24 m spacing) + far objects.
//   - commonworld.bnk: shared models/materials/textures (library.*).
class EdenWorld {
public:
    static constexpr float kSectorSize = 768.0f;
    static constexpr uint32_t kSectorsPerArea = 8;
    static constexpr uint32_t kHeightfieldSize = 257;

    struct SectorId {
        int32_t AreaX, AreaY, X, Y;
        bool operator==( const SectorId &o ) const {
            return AreaX == o.AreaX && AreaY == o.AreaY && X == o.X && Y == o.Y;
        }
    };

public:
    EdenWorld();
    ~EdenWorld();

    bool initialize( EdenRenderer &renderer, const char *pIsland );
    void destroy();

    // Loads (synchronously) the sectors around a position and every area.
    void loadAround( const Vector3f &position, int32_t sectorRadius );
    void draw( EdenRenderer &renderer, const Vector3f &cameraPosition );
    // Small instance culling (size / distance below the ratio are skipped):
    // low resolution passes (environment cube faces). 0: off.
    inline void setMinInstanceRatio( float ratio ) { minInstanceRatio = ratio; }
    // Ocean on / off (island ocean, the TD_Ocean nodes of the areas and
    // sectors): closed rooms rendered without the island.
    void setOceanVisible( bool bVisible ) {
        bOceanVisible = bVisible;
        bOceanVisibilityDirty = true;
    }
    inline bool isOceanVisible() const { return bOceanVisible; }
    // TD_Ocean materials of the loaded levels: kind 0 sector, 1 area, 2
    // island (<island>-o), with the level colour map (heightmap COLOR).
    void forEachOceanMaterial( const std::function<void( EdenMaterial &material, uint8_t kind,
                                                         GPUTexture *pColorMap )> &callback );
    // Wet ground (WFX_WetGrd, FUN_00aa0b60 / FUN_00aa0890): rain permutation
    // of the TD_Road materials (S_USERTYPE9 = 1, slots 6 ripples, 7 the
    // dynamic environment cube, 8 PUDDLES) and RainSpecs / RainRippleSpecs
    // from the weather. pEnvironmentCube nullptr or no wet weather: dry.
    void updateWetGround( EdenRenderer &renderer, const struct WeatherPreset *pWeather, GPUTexture *pEnvironmentCube,
                          float deltaTime );

    SectorId getSectorAt( const Vector3f &position ) const;
    Vector3f getSectorOrigin( const SectorId &id ) const;
    // Heightfield sampling (loaded sectors only), returns false outside.
    bool getHeight( float x, float z, float &outHeight ) const;
    // Highest surface (heightfield or sector objects: roads...) below
    // maxHeight.
    bool getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const;

    inline const Render2DM &getLibraryMaterials() const { return libraryMaterials; }
    inline const Bank &getCommonBank() const { return commonBank; }

private:
    struct Heightfield {
        EdenMesh Mesh;
        uint32_t MaterialIndex = 0;
        std::vector<EdenMaterial> Materials;
        GPUBuffer *pBuffer = nullptr;
        std::vector<float> Heights;
        float Spacing = 3.0f;
    };

    struct Sector {
        SectorId Id;
        Vector3f Origin;
        std::string BankPath;
        Bank SectorBank;
        Heightfield Terrain;
        EdenModel Objects;
        // .pmi lists (V / O static library models, A actors).
        std::vector<PmiInstance> Instances;
        std::vector<int32_t> InstanceModels;   // library node or actor model index
        std::vector<float> InstanceDistances;  // max draw distance
    };

    struct Area {
        int32_t X = 0, Y = 0;
        Vector3f Origin;
        std::string BankPath;
        Bank AreaBank;
        Heightfield Terrain;
        EdenModel Objects;
        GPUBuffer *pIndexBuffer = nullptr; // quads not covered by loaded sectors
        uint32_t NumIndices = 0;
        bool bIndicesDirty = true;
    };

    EdenRenderer *pRenderer;
    std::string islandFolder;
    int32_t areaColumns;
    int32_t areaRows;
    Bank commonBank;
    Render2DM libraryMaterials;
    GPUBuffer *pGridBuffer;   // shared heightfield texcoords (x, z)
    GPUBuffer *pGridIndices;  // shared heightfield triangles
    uint32_t numGridIndices;
    std::vector<std::unique_ptr<Sector>> sectors;
    std::vector<std::unique_ptr<Area>> areas;
    std::unordered_set<uint32_t> failedSectors; // row * 1024 + column
    // commonworld.bnk library.3dd (models of the V / O .pmi lists).
    EdenModel libraryModel;
    std::unordered_map<uint64_t, int32_t> libraryNodes;
    // scenarics/packs/worldactors.bnk (actors of the A .pmi lists: road signs).
    Bank actorBank;
    std::unordered_map<uint64_t, std::string> actorNames;           // __Hc -> __Name
    std::unordered_map<uint64_t, const BankEntry *> actorScenes;    // root node hash -> 3dd
    std::unordered_map<uint64_t, int32_t> actorModelIndices;        // __Hc -> actorModels index (-1: none)
    std::vector<std::unique_ptr<EdenModel>> actorModels;
    // Island ocean (<island>.bnk <island>-o: AREA-x-y tiles + BORDER-* to the
    // horizon) and its colour map.
    Bank islandBank;
    EdenModel islandOcean;
    GPUTexture *pIslandColorMap = nullptr;
    bool bOceanVisibilityDirty = true;
    bool bOceanVisible = true;
    void updateOceanVisibility();
    // fx/weatherfx.bnk: RIPPLE_01..16, PUDDLES.
    Bank weatherFxBank;
    GPUTexture *pRipples[16] = {};
    GPUTexture *pPuddles = nullptr;
    float minInstanceRatio = 0.0f;
    float rippleScroll = 0.0f;
    float rippleTimer = 0.0f;
    uint32_t rippleFrame = 0;

private:
    bool loadHeightfield( const Bank &bank, const char *pBaseName, const char *pTextureOwner, Heightfield &out );
    bool loadSector( const SectorId &id );
    bool loadArea( int32_t x, int32_t y );
    void updateAreaIndices( Area &area );
    bool isSectorLoaded( const SectorId &id ) const;
    void loadInstances( Sector &sector, const std::string &name );
    int32_t getActorModel( uint64_t hashcode );
    void drawInstances( EdenRenderer &renderer, const Sector &sector, const Vector3f &cameraPosition );
};
