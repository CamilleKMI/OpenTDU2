#pragma once

#include "render/2dm.h"
#include "render/3dd.h"
#include "render/3dg.h"
#include "render/eden/eden_renderer.h"

class Bank;
struct BankEntry;

// Scene (.3DD + .3DG + .2DM) rendered with the original shaders, in the
// original D3D space.
class EdenModel {
public:
    struct Item {
        int32_t NodeIndex = -1;
        uint32_t MaterialIndex = 0;
        EdenMesh Mesh;
        Vector3f BoundsMin;
        Vector3f BoundsMax;
        std::vector<uint32_t> BonePalette; // skinned: skeleton bone of each palette entry
    };

    using NodeFilter = std::function<bool( const Render3DD &scene, int32_t nodeIndex )>;
    using MaterialSetup = std::function<void( EdenMaterial &material )>;

    inline bool isLoaded() const { return pVertexBuffer != nullptr; }
    inline const Render3DD &getScene() const { return scene; }
    inline const std::vector<Item> &getItems() const { return items; }
    inline std::vector<EdenMaterial> &getMaterials() { return materials; }
    inline const Vector3f &getBoundsMin() const { return boundsMin; }
    inline const Vector3f &getBoundsMax() const { return boundsMax; }
    inline const Matrix4x4f &getNodeTransform( int32_t nodeIndex ) const { return nodeTransforms[nodeIndex]; }
    void setNodeTransform( int32_t nodeIndex, const Matrix4x4f &transform ) { nodeTransforms[nodeIndex] = transform; }
    void setNodeVisible( int32_t nodeIndex, bool bVisible ) { nodeVisible[nodeIndex] = bVisible; }

public:
    EdenModel();
    ~EdenModel();

    // pFallbackMaterials: shared material library (e.g. commonworld library.2dm).
    bool load( EdenRenderer &renderer, const BankEntry &sceneEntry, const BankEntry &geometryEntry,
               const BankEntry *pMaterialEntry, const Render2DM *pFallbackMaterials, const char *pTextureOwner,
               const NodeFilter &filter = nullptr, const MaterialSetup &materialSetup = nullptr );
    // Convenience: first 3dd/3dg/2dm of a bank (or the entries named pBaseName).
    bool load( EdenRenderer &renderer, const Bank &bank, const char *pBaseName, const Render2DM *pFallbackMaterials,
               const char *pTextureOwner, const NodeFilter &filter = nullptr,
               const MaterialSetup &materialSetup = nullptr );
    void destroy();

    // Texture coordinate animations (.UVA entries of 'bank', see
    // EdenUVAnimation) of this model's materials: texture slot k whose 2DM
    // MTP. reference names a .UVA id (EdenMaterial::UVAnimationIds) gets it
    // as __UVA_k. A UVAA id no slot references falls back to the file name
    // rule. Returns the number of bound (material, slot) animations.
    uint32_t bindUVAnimations( const Bank &bank );
    inline const Render2DM &getLocalMaterials() const { return localMaterials; }
    // EdenRenderer::resolveMissingTextures for every material (call once the
    // banks loaded together are in). Returns the number of slots resolved.
    uint32_t resolveMissingTextures( EdenRenderer &renderer );

    // Keeps a CPU copy of the triangles (model space) for height queries
    // (set before load).
    inline void setKeepCollision( bool bKeep ) { bKeepCollision = bKeep; }
    // Loads the object of LOD 'level' of every node (SceneNode::LodObjects)
    // instead of LOD 0 (set before load; nodes without that LOD get no
    // item). Used for the library low LODs and impostor quads.
    inline void setLodLevel( uint32_t level ) { lodLevel = level; }
    // Highest surface below maxHeight at (x, z) (model space).
    bool getSurfaceHeight( float x, float z, float maxHeight, float &outHeight ) const;

    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const EdenDrawParams *pParams = nullptr ) const;
    // Skinned draw: skinMatrices[bone] = bone world * inverse bind (model
    // space, column vectors); each primitive uploads its palette
    // (__Skin_Matrix, 32 bones x 3 rows). Non skinned items use their node.
    void drawSkinned( EdenRenderer &renderer, const Matrix4x4f &world,
                      const std::vector<Matrix4x4f> &skinMatrices,
                      const struct EdenAvatarLighting *pLighting = nullptr ) const;
    // Draws a single node subtree item list (instancing helpers).
    void drawNode( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world ) const;
    // Draws the items of one node with 'world' only (the node transform is
    // ignored: library models instanced by the .pmi lists).
    void drawNodeItems( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world,
                        const EdenDrawParams *pParams = nullptr ) const;
    // Same with a per instance skeleton (skinned library models of the -S
    // .pmi lists, the guard rails): pBones[b] = skin matrix of skeleton bone
    // b (model space), each skinned item uploads bones[BonePalette[i]].
    void drawNodeItemsSkinned( EdenRenderer &renderer, int32_t nodeIndex, const Matrix4x4f &world,
                               const Matrix4x4f *pBones, uint32_t numBones,
                               const EdenDrawParams *pParams = nullptr ) const;
    // Items of one node: first item, count.
    std::pair<uint32_t, uint32_t> getNodeItemRange( int32_t nodeIndex ) const {
        return nodeIndex >= 0 && nodeIndex < static_cast<int32_t>( nodeItems.size() )
                   ? nodeItems[nodeIndex]
                   : std::pair<uint32_t, uint32_t>( 0, 0 );
    }
    // Model space bounds of the items of a node; false if it has none.
    bool getNodeBounds( int32_t nodeIndex, Vector3f &outMin, Vector3f &outMax ) const;

private:
    Render3DD scene;
    Render2DM localMaterials;
    std::vector<EdenMaterial> materials;
    std::vector<Item> items;
    std::vector<Matrix4x4f> nodeTransforms;
    std::vector<bool> nodeVisible;
    std::vector<std::pair<uint32_t, uint32_t>> nodeItems; // first item, count
    std::vector<Vector3f> collisionVertices;
    std::vector<uint32_t> collisionIndices;
    bool bKeepCollision = false;
    uint32_t lodLevel = 0;
    GPUBuffer *pVertexBuffer;
    GPUBuffer *pIndexBuffer;
    Vector3f boundsMin;
    Vector3f boundsMax;
};
