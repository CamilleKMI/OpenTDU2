#pragma once

#include "core/xmb.h"
#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/eden/eden_particles.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_neons.h"
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
    // Unloads the sectors farther than sectorRadius (sector rows / columns)
    // from a position; returns the number of sectors unloaded. The caller
    // must drop the renderer material caches (clearShadowCasterCache).
    uint32_t unloadFar( const Vector3f &position, int32_t sectorRadius );
    // Island level heightfield (<island>.bnk <island>.3dd/.3dg/.2dm, 257 x
    // 257 heights, 384 m spacing: HMAP center 49152 = 128 x 384 [DATA],
    // TD_HeightMap S_USERTYPE4 = 2), drawn where no area is loaded. Off by
    // default (the world viewer turns it on).
    inline void setIslandTerrainEnabled( bool bEnable ) { bIslandTerrainEnabled = bEnable; }
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
    // Neon set and AnimValue blink states of the sectors, areas, library
    // and actor models (FUN_00985e40 registration at load, FUN_009834d0
    // update every frame, see EdenNeons): call once per frame.
    void updateNeons( const struct WeatherPreset *pWeather, float deltaTime );
    void updateWetGround( EdenRenderer &renderer, const struct WeatherPreset *pWeather, GPUTexture *pEnvironmentCube,
                          float deltaTime );

    // Island ambient effects (docs/re/particles_spec.md 7): the sector
    // "-F.pmi" lists (bank group 5 / sub 24, loader FUN_00c38f40 ->
    // FUN_00c38d40 [CODE]) place Fx__SourceData of islands/<island>/fx/
    // vfx_env.bnk (MOB. hashcode = the Fx__SourceData __Hc, at most 30
    // records per sector). Every frame (FUN_00c38b10 [CODE]) an effect whose
    // position is farther than the TriggerDistance of its Fx__Root__World
    // (default 1000, FUN_00b17930) from the camera is stopped, a closer one is
    // started (and its matrix set). Off by default (the World Viewer and
    // FREERIDE turn it on).
    void setEffectsEnabled( bool bEnable );
    inline bool areEffectsEnabled() const { return bEffectsEnabled; }
    void updateEffects( const Vector3f &cameraPosition, float deltaTime );
    // Blended draws of the running effects (deferred by the renderer).
    void drawEffects( EdenRenderer &renderer, const Vector3f &cameraPosition );
    // Placed / running effects of the loaded sectors.
    uint32_t getNumEffects() const;
    uint32_t getNumRunningEffects() const;

    static SectorId getSectorAt( const Vector3f &position );
    static Vector3f getSectorOrigin( const SectorId &id );
    // Heightfield sampling (loaded sectors only), returns false outside.
    bool getHeight( float x, float z, float &outHeight ) const;
    // Highest surface (heightfield or sector objects: roads...) below
    // maxHeight.
    bool getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const;

    inline const Render2DM &getLibraryMaterials() const { return libraryMaterials; }
    inline int32_t getAreaColumns() const { return areaColumns; }
    inline int32_t getAreaRows() const { return areaRows; }
    inline size_t getNumSectors() const { return sectors.size(); }
    inline size_t getNumAreas() const { return areas.size(); }
    inline size_t getNumFailedSectors() const { return failedSectors.size(); }
    inline bool hasIslandTerrain() const { return !islandTerrain.Materials.empty(); }
    // Water plane height: -1.8 on Hawai, 0 elsewhere (FUN_00963010 /
    // DAT_00fe5028 [CODE]; the Hawai ocean meshes are at y = -1.8 [DATA]).
    inline float getWaterHeight() const { return waterHeight; }
    // Sector bank path (<level>/area-x/area-x-y/sector-x-y-sx-sy.bnk).
    std::string getSectorBankPath( const SectorId &id ) const;
    static std::string GetSectorBankPath( const char *pIsland, const SectorId &id );
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
        // .pmi list of each instance (kind of FUN_00988d10: 0 V +0x28d0, 1 O
        // +0xa010, 2 S +0x11750; 3 actors A).
        std::vector<uint8_t> InstanceLists;
        // Sector -s map (sector resource +0x674: SHADOW of the TD_Ground /
        // TD_Ground_HM / TD_Road objects, FUN_00982af0) and the per instance
        // TD_Ground parameters of the O / S lists: SHADOW slot override
        // (list +0x7670 record) and sectrOff (FUN_00987ec0), see
        // night_lighting_spec.md 2.1.
        GPUTexture *pShadowMap = nullptr;
        EdenDrawParams GroundInstanceParams[3];
        // Neons of the sector objects (one model instance [INF]).
        std::vector<EdenMaterial *> NeonMaterials;
        std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> NeonBlinks;
        // AnimValue states of the .pmi instances (one per instance and
        // blinking material, FUN_00988d10 -> FUN_00985e40 per record):
        // instance i owns InstanceBlinks[InstanceBlinkStart[i] ..
        // InstanceBlinkStart[i + 1]).
        std::vector<uint32_t> InstanceBlinkStart;
        std::vector<std::pair<const EdenMaterial *, EdenNeons::Blink>> InstanceBlinks;
        // -F.pmi effects (FUN_00c38d40 entries, 0x70 bytes: +0x00 u64
        // Fx__SourceData hash, +0x10 matrix, +0x60 TriggerDistance^2, +0x64
        // cell, +0x68 scale x).
        struct Effect {
            uint64_t SourceHash = 0;
            Matrix4x4f World;
            float TriggerDistanceSq = 0.0f;
            std::vector<std::unique_ptr<EdenParticleInstance>> Instances; // running when not empty
        };
        std::vector<Effect> Effects;
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
        // Objects (-o: ocean tiles + far objects) grouped per sector cell:
        // dummy nodes "Sector-ax-ay-sx-sy" (FUN_00992890 [CODE], names
        // matched by hash [DATA]). ItemSectors[i]: sector of the group that
        // holds item i (AreaX = INT32_MIN: no group).
        std::vector<SectorId> ItemSectors;
        std::vector<EdenMaterial *> NeonMaterials;
        std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> NeonBlinks;
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
    // Neon set materials of the shared models (library, actors): written in
    // place (the parameter is the same for every instance).
    std::vector<EdenMaterial *> sharedNeonMaterials;
    // Blinking materials drawn by a library node / an actor model (cache).
    std::unordered_map<int32_t, std::vector<const EdenMaterial *>> libraryNodeBlinks;
    // Library nodes whose items (LOD 0, 1 or 2) use a TD_Ground material.
    std::unordered_map<int32_t, bool> libraryNodeGround;
    bool libraryNodeHasGround( int32_t node );
    std::unordered_map<int32_t, std::vector<const EdenMaterial *>> actorModelBlinks;
    const std::vector<const EdenMaterial *> &getInstanceBlinkMaterials( uint8_t type, int32_t model );
    static void RegisterNeons( EdenModel &model, uint32_t key, std::vector<EdenMaterial *> &outSet,
                               std::vector<std::pair<EdenMaterial *, EdenNeons::Blink>> &outBlinks );
    // Island ocean (<island>.bnk <island>-o: AREA-x-y tiles + BORDER-* to the
    // horizon) and its colour map.
    Bank islandBank;
    EdenModel islandOcean;
    Heightfield islandTerrain;
    GPUBuffer *pIslandIndexBuffer = nullptr; // quads not covered by loaded areas
    uint32_t numIslandIndices = 0;
    bool bIslandIndicesDirty = true;
    bool bIslandTerrainEnabled = false;
    void updateIslandIndices();
    bool loadWorldConfig( const char *pIsland );
    GPUTexture *pIslandColorMap = nullptr;
    float waterHeight = 0.0f;
    bool bOceanVisibilityDirty = true;
    bool bOceanVisible = true;
    void updateOceanVisibility();
    // fx/weatherfx.bnk: RIPPLE_01..16, PUDDLES.
    Bank weatherFxBank;
    GPUTexture *pRipples[16] = {};
    GPUTexture *pPuddles = nullptr;
    float minInstanceRatio = 0.0f;
    // fx/vfx_env.bnk (ambient effects): Fx__Root__World SourceData ->
    // TriggerDistance, particle systems.
    struct EffectSource {
        std::string Name;
        float TriggerDistance = 1000.0f;
        bool bResolved = false;
        std::vector<const EdenParticleLibrary::System *> Systems;
    };
    Bank fxBank;
    BankHashcodes fxHashcodes;
    XmbDocument fxEntities;
    EdenParticleLibrary fxLibrary;
    std::unordered_map<uint64_t, EffectSource> fxSources;
    bool bEffectsEnabled = false;
    void loadEffectsBank( const char *pIsland );
    void loadEffects( Sector &sector, const std::string &name );
    float rippleScroll = 0.0f;
    float rippleTimer = 0.0f;
    uint32_t rippleFrame = 0;

private:
    bool loadHeightfield( const Bank &bank, const char *pBaseName, const char *pTextureOwner, Heightfield &out );
    bool loadSector( const SectorId &id );
    bool loadArea( int32_t x, int32_t y );
    void updateAreaIndices( Area &area );
    void markAreasDirty( const SectorId &id );
    bool isSectorLoaded( const SectorId &id ) const;
    void loadInstances( Sector &sector, const std::string &name );
    int32_t getActorModel( uint64_t hashcode );
    void drawInstances( EdenRenderer &renderer, const Sector &sector, const Vector3f &cameraPosition );

    // Library LODs and tree impostors (eden_world_lod.cpp, docs/re/
    // lod_impostor_spec.md). libraryLodModels[k - 1] holds the LOD k object
    // of every library node (EdenModel::setLodLevel).
    struct NodeLod {
        uint8_t NumLods = 0;
        float Distance2[3] = {}; // INS lodDistance x 3.160494 (squared metres, FUN_00da0440)
        // Last LOD material is TD_Impostor (FUN_00989b50): the last LOD is a
        // camera facing quad built every frame from the template below.
        bool bImpostor = false;
        const EdenMaterial *pImpostorMaterial = nullptr;
        uint32_t NumSides = 0;        // NbImpostorSide (side views; atlas cell NumSides = top view)
        float HalfWidth = 0.0f;       // template quad (LOD object, model space): z = +-HalfWidth,
        float Top = 0.0f, Bottom = 0.0f; // y = Top (v = 0) .. Bottom (v = 1),
        float CellWidth = 0.0f;       // u = 0 .. CellWidth (= 1 / (NumSides + 1))
    };
    struct ImpostorDraw {
        GPUBuffer Stream; // transient buffer wrapper (not owned)
        EdenMesh Mesh;
        EdenDrawParams Params;
    };
    struct ImpostorVertex {
        float Position[3];
        float Texcoord[2];
    };
    EdenModel libraryLodModels[2];
    std::vector<NodeLod> libraryNodeLods;
    // Impostor quads queued by drawLibraryInstance, per material, drawn by
    // flushImpostors at the end of draw().
    std::unordered_map<const EdenMaterial *, std::vector<ImpostorVertex>> impostorQueue;
    uint32_t numQueuedImpostors = 0;
    std::vector<std::unique_ptr<ImpostorDraw>> impostorDraws; // alive until the next frame
    uint64_t impostorFrame = ~0ull;
    GPUBuffer *pImpostorIndexBuffer = nullptr;
    Vector3f impostorAmbient = Vector3f( 0.0f, 0.0f, 0.0f ); // weather impostor_ambient
    struct LodStats {
        uint32_t Meshes[3] = {};
        uint32_t Impostors = 0;
        uint32_t Faded = 0;
    } lodStats;
    void loadLibraryLods( EdenRenderer &renderer, const char *pTextureOwner );
    void destroyLibraryLods();
    void drawLibraryInstance( EdenRenderer &renderer, const PmiInstance &instance, int32_t node,
                              const Vector3f &cameraPosition, const EdenDrawParams *pParams );
    void flushImpostors( EdenRenderer &renderer );

public:
    // Library LOD / impostor counts of the last draw() (World Viewer stats).
    inline const LodStats &getLodStats() const { return lodStats; }
};
