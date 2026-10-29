#pragma once

#include "render/2dm.h"
#include "render/3dg.h"
#include "render/eden/eden_shader_system.h"
#include "render/eden/eden_uv_animation.h"
#include "render/vulkan/render_device.h"

class TextureCache;

// Renderer driving the original TDU2 shaders (.sob, translated to SPIR-V
// through DXVK's DXSO front-end) with the original data, in the original
// left handed D3D space: positions, matrices and constants are the ones the
// game feeds to Direct3D 9. The viewport is flipped so the D3D clip space
// convention (y up) is kept.
//
// Shader constants are bound by name, from the constant tables the HLSL
// compiler left in the bytecode: Var_Frame_VS (c0), Var_Scene_VS/PS,
// Var_VS (c60), Var_Skin_VS (c148), Var_PS... Engine parameters use the
// "__Name" convention (__World_View_Proj__float4x4, __DirLight_Dir_0...),
// the other members are material parameters (.2DM parameter list, keyed by
// the Eden identifier of the upper case name: "sectrOff" -> SECTROFF).
// Material texture slot i is bound to sampler s<i>.

// Raw vertex/index data uploaded once (one buffer per model).
struct EdenMesh {
    static constexpr uint32_t kMaxStreams = 3;
    GPUBuffer *pStreamBuffers[kMaxStreams] = {}; // Shared, not owned
    GPUBuffer *pIndexBuffer = nullptr;           // Shared, not owned
    VkDeviceSize StreamOffsets[kMaxStreams] = {};
    uint32_t Strides[kMaxStreams] = {};
    uint32_t NumStreams = 0;
    std::vector<RawVertexAttribute> Attributes;
    uint32_t FirstIndex = 0;
    uint32_t IndexCount = 0;
    int32_t VertexOffset = 0;
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    uint64_t LayoutKey = 0;

    void computeLayoutKey();
};

struct EdenMaterial {
    std::string Name;
    uint64_t Hashcode = 0;
    uint64_t ShaderSetGUID = 0;
    uint8_t Spec[EdenShaderSystem::kNumSpecs] = {};
    uint8_t States[16] = {}; // Raw .2DM render states (MAT. +0x30)
    int32_t Bucket = 0x39;   // Render bucket (ApplyEdenMaterialHandler)
    const EdenProgram *pProgram = nullptr;
    GPUTexture *pTextures[16] = {};
    uint8_t SamplerIndex[16] = {};
    bool bNormalSlot[16] = {};
    uint64_t TextureHashes[16] = {};
    uint64_t SlotTypes[16] = {};
    uint32_t NumSlots = 0;
    uint8_t HeightfieldGrid = 0; // TD_HeightMap: 0 sector (3 m), 1 area (24 m)
    std::unordered_map<uint64_t, Vector4f> Parameters;
    // Texture coordinate animations by slot (__UVA_k; null: identity).
    std::shared_ptr<const EdenUVAnimation> UVAnimations[4];
    // .UVA ids referenced by texture slots 0..3 (MaterialTextureSlot::
    // UVAnimationId; 0: none), resolved by EdenModel::bindUVAnimations.
    uint64_t UVAnimationIds[4] = {};
    // GSReflection projective map slot (TD_Water U4: 4, TD_Furnitures U7:
    // 6), -1: none. Bound to the frame projective texture (render target of
    // the REFLECT layers) when the renderer has one.
    int8_t ProjectiveSlot = -1;
    // Runtime slot of the screen depth buffer (FullscreenDepthTarget,
    // "gViewPos": B / A encoded view depth), -1: none. TD_Particle soft
    // particle permutations (ps 2 / 3, sampler gZpass s1): the .2DM only
    // has the COLOR slot, the depth is bound at draw time
    // (docs/re/particles_spec.md 6). Bound to the frame screen texture
    // (setScreenTexture) when there is one, else to the white default (B =
    // A = 1: very far, no fade).
    int8_t ScreenDepthSlot = -1;
    // Runtime constants by shader constant name, per stage (0 VS, 1 PS).
    std::unordered_map<std::string, std::vector<Vector4f>> StageConstants[2];
    Vector4f Diffuse = Vector4f( 1, 1, 1, 1 );
    Vector4f Ambient = Vector4f( 1, 1, 1, 1 );
    Vector4f Specular = Vector4f( 0, 0, 0, 1 );
    Vector4f Emissive = Vector4f( 0, 0, 0, 1 );
    uint8_t SrcBlend = 2;
    uint8_t DstBlend = 1;
    uint8_t CullMode = 3; // D3DCULL
    uint8_t AlphaRef = 0;
    uint8_t DepthFunc = 0; // D3DCMPFUNC, 0: LESSEQUAL
    uint8_t BlendOp = 1;   // D3DBLENDOP (1 add, 2 subtract, 3 revsubtract, 4 min, 5 max)
    uint8_t ColorWriteMask = 0xf; // D3DCOLORWRITEENABLE bits (R 1, G 2, B 4, A 8)
    bool bAlphaTest = false;
    bool bDepthTest = true;
    bool bDepthWrite = true;

    bool isBlended() const { return !( SrcBlend == 2 && DstBlend == 1 ); }
};

// Per-frame / per-scene values (the frame & scene parameter structs).
struct EdenSceneParams {
    Matrix4x4f View;       // D3D row-vector convention (Matrix4x4f memory = D3D layout)
    Matrix4x4f Projection; // D3D LH, z in [0, 1]
    Vector3f CameraPosition;
    Vector3f CameraDirection;
    float Time = 0.0f;
    Vector3f SunDirection = Vector3f( 0.3f, -0.8f, 0.4f ); // direction the light travels
    Vector3f SunDiffuse = Vector3f( 1.0f, 0.95f, 0.85f );
    Vector3f SunSpecular = Vector3f( 1.0f, 1.0f, 1.0f );
    Vector3f Ambient = Vector3f( 0.45f, 0.5f, 0.6f );
    Vector3f FogColor = Vector3f( 0.7f, 0.75f, 0.8f );
    Vector4f FogConfig = Vector4f( 0.0f, 0.0f, 0.0f, 0.0f );
    Vector4f UserVectors[8] = {};
    uint32_t Width = 1280;
    uint32_t Height = 720;
    // Mirroring view (environment cube faces): the culling flips.
    bool bMirroredView = false;
    // Scene wide named constants (sky scattering, shared by td_dome, the
    // clouds and the heightmap aerial perspective).
    std::unordered_map<std::string, Vector4f> Vectors;
    std::unordered_map<std::string, Matrix4x4f> Matrices;
};

// Optional per-draw overrides (constants by name).
// Avatar light of a character (FUN_00b87aa0 / FUN_00a4ca50 / FUN_00caf850,
// docs/re/avatar_lighting_spec.md), world space.
struct EdenAvatarLighting {
    Vector3f Direction = Vector3f( 0.0f, -1.0f, 0.0f ); // direction the light travels
    Vector3f Ambient, Diffuse, Specular;
    Vector3f SpotPosition;                              // camera light
    Vector3f SpotAmbient, SpotDiffuse, SpotSpecular;
    float Shadow = 1.0f;                                // CARINTEG.z
    float Environment = 0.7f;                           // ATTEFACT.w
};

struct EdenDrawParams {
    std::unordered_map<std::string, Vector4f> Vectors;
    std::unordered_map<std::string, Matrix4x4f> Matrices;
    std::unordered_map<std::string, std::vector<Vector4f>> Arrays; // "balance__array3"...
    std::unordered_map<std::string, Vector4f> VertexVectors;       // stage specific
    std::unordered_map<std::string, Vector4f> PixelVectors;
    const Matrix4x4f *pSkinMatrices = nullptr;
    uint32_t NumSkinMatrices = 0;
    const EdenAvatarLighting *pAvatarLighting = nullptr;
    // Material parameter overrides of one model instance (parameter blocks
    // attached to (material, model instance), FUN_00448310 [ENC]): used in
    // place of material.Parameters[Parameter] for that material only (.pmi
    // neon AnimValue, see EdenNeons).
    struct MaterialVector {
        const EdenMaterial *pMaterial = nullptr;
        uint64_t Parameter = 0;
        Vector4f Value;
    };
    std::vector<MaterialVector> MaterialVectors;
    // Texture slot overrides of one model instance: the slot of type
    // SlotType of the materials of shader set ShaderSetGUID samples pTexture
    // with sampler heap index Sampler (the .pmi instance list override
    // record list +0x7670 of FUN_00987e80 / FUN_00982af0: TD_Ground SHADOW =
    // the sector -s map, D3DTADDRESS 2 / 2 [CODE], night_lighting_spec 2.1).
    struct TextureOverride {
        uint64_t ShaderSetGUID = 0;
        uint64_t SlotType = 0;
        GPUTexture *pTexture = nullptr;
        uint16_t Sampler = 0;
    };
    std::vector<TextureOverride> TextureOverrides;
    // Blended draws with parameters are recorded immediately unless set
    // (then deferred and sorted with the other blended draws; the skin
    // matrices are copied).
    bool bDeferBlended = false;
    std::vector<Matrix4x4f> SkinStorage;
};

class EdenRenderer {
public:
    EdenRenderer();
    ~EdenRenderer();

    bool initialize( RenderDevice *pDevice, TextureCache *pTextureCache );
    void destroy();
    inline bool isReady() const { return bReady; }
    inline EdenShaderSystem &getShaderSystem() { return shaderSystem; }

    // Builds the runtime material (shader permutation, textures) of a .2DM entry.
    void setupMaterial( const MaterialDesc &desc, EdenMaterial &outMaterial, const char *pTextureOwner = nullptr );
    // Texture slots of a material whose 2DM texture was not loaded when the
    // material was set up, looked up again by hashcode in every loaded bank
    // (a spot exterior texture packed in another bank of the same spot:
    // ECAS windows_05 REFLECTION "CUBEMAP" = spots/casino/ecas/maps/cubemap.2db
    // of elandca.bnk, docs/re/spot_exterior_spec.md 5). Returns the number
    // of slots resolved.
    uint32_t resolveMissingTextures( EdenMaterial &material );
    // Runtime texture assignment (e.g. sector shadow maps), call updateProgram after.
    bool assignTexture( EdenMaterial &material, const char *pSlotType, GPUTexture *pTexture );
    void updateProgram( EdenMaterial &material );
    // Blend / depth / cull / alpha test from EdenMaterial::States.
    static void applyRenderStates( EdenMaterial &material );

    void beginScene( const EdenSceneParams &params );
    void draw( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
               const EdenDrawParams *pParams = nullptr );
    // Blended draws are deferred and sorted back to front.
    // Re-applies the scene viewport (after switching render passes; the
    // deferred blended draws are kept).
    void resumeScene();
    void endScene();
    // Records the deferred blended draws now (back to front).
    void flushDeferred();
    // Records the kept opaque water draws now (also done by flushDeferred
    // and endScene).
    void flushWater();
    // Screen space buffer of the frame ("gViewPos": R sun visibility, B/A
    // view depth), bound to s15 (gTexSamplerShadowRealTime) and to the
    // runtime slot of TD_ShadowBox. nullptr: 1x1 default (lit, no depth).
    inline void setScreenTexture( GPUTexture *pTexture ) { pScreenTexture = pTexture; }
    // gTexSamplerShadowRealTime (s15) of the scene shaders: R sun
    // visibility, G / B additional light, A light multiplier. nullptr: lit.
    inline void setShadowRealTimeTexture( GPUTexture *pTexture ) { pShadowRealTimeOverride = pTexture; }
    // GSReflection render target (REFLECT 2D / actor layers), bound to the
    // EdenMaterial::ProjectiveSlot of the registered materials. nullptr:
    // transparent black.
    inline void setProjectiveTexture( GPUTexture *pTexture ) { pProjectiveTexture = pTexture; }
    // Depth only pass (Z pre-pass): color writes off, blended draws skipped.
    inline void setDepthOnly( bool bEnable ) { bDepthOnly = bEnable; }
    // Shadow caster pass: opaque draws use their caster permutation
    // (S_PASS_SHADOWCAST of the vegetation families, TD_ShadowCaster for
    // the others: R32F = clip z * 2 - 1); blended draws are skipped.
    inline void setShadowCasterMode( bool bEnable ) { bShadowCaster = bEnable; }
    // Ocean reflection pass (SEAREF_N): no blended draws, no water.
    inline void setReflectionPass( bool bEnable ) { bReflectionPass = bEnable; }
    inline bool isShadowCasterMode() const { return bShadowCaster; }
    inline bool isReflectionPass() const { return bReflectionPass; }
    void clearShadowCasterCache() {
        casterMaterials.clear();
        zPassMaterials.clear();
    }

    uint32_t getNumDrawCalls() const { return numDrawCalls; }
    inline const Matrix4x4f &getViewProjection() const { return viewProjection; }
    inline const EdenSceneParams &getSceneParams() const { return scene; }

private:
    struct DeferredDraw {
        EdenMesh Mesh;
        const EdenMaterial *pMaterial;
        Matrix4x4f World;
        float Distance;
        std::shared_ptr<EdenDrawParams> pParams;
    };

    RenderDevice *pDevice;
    TextureCache *pTextureCache;
    EdenShaderSystem shaderSystem;
    VkDescriptorSetLayout setLayouts[2];
    VkPipelineLayout pipelineLayout;
    VkDescriptorPool staticPool;
    VkDescriptorPool framePools[RenderDevice::kFramesInFlight];
    // Extra pools of the frame slot when framePools[slot] is full.
    std::vector<VkDescriptorPool> overflowPools[RenderDevice::kFramesInFlight];
    uint32_t overflowUsed[RenderDevice::kFramesInFlight] = {};
    uint32_t setsAllocated[RenderDevice::kFramesInFlight] = {};
    VkDescriptorSet allocateFrameSet();
    uint64_t framePoolNumbers[RenderDevice::kFramesInFlight];
    VkDescriptorSet samplerSet;
    std::vector<VkSampler> samplerHeap;
    GPUBuffer *pZeroBuffer;
    GPUBuffer *pDefaultAttributes;
    GPUTexture *pWhite2D;
    GPUTexture *pWhiteCube;
    GPUTexture *pWhite3D;
    GPUTexture *pShadowRealTime;
    GPUTexture *pFlatNormal;
    GPUTexture *pScreenTexture;
    GPUTexture *pShadowRealTimeOverride = nullptr;
    GPUTexture *pProjectiveTexture = nullptr;
    GPUTexture *pBlack2D = nullptr; // (0, 0, 0, 0)
    bool bDepthOnly = false;
    bool bShadowCaster = false;
    bool bReflectionPass = false;
    std::unordered_map<const EdenMaterial *, std::pair<uint64_t, std::unique_ptr<EdenMaterial>>> casterMaterials;
    std::unordered_map<const EdenMaterial *, std::pair<uint64_t, std::unique_ptr<EdenMaterial>>> zPassMaterials;
    // ZPREPASS permutation (S_PASS_Z) of a material, nullptr when its
    // family has none.
    const EdenMaterial *getZPassMaterial( const EdenMaterial &material );
    const EdenMaterial *getCasterMaterial( const EdenMaterial &material );
    std::unordered_map<std::string, VkPipeline> pipelines;
    std::unordered_set<std::string> warnedParameters;
    std::unordered_set<std::string> dumpedPrograms;
    EdenSceneParams scene;
    Matrix4x4f viewProjection;
    std::vector<DeferredDraw> deferredDraws;
    std::vector<DeferredDraw> waterDraws; // opaque water buckets, submission order
    uint32_t numDrawCalls;
    bool bInScene;
    bool bReady;

private:
    VkPipeline getPipeline( const EdenMesh &mesh, const EdenMaterial &material, bool bMirrored );
    void recordDraw( const EdenMesh &mesh, const EdenMaterial &material, const Matrix4x4f &world,
                     const EdenDrawParams *pParams );
    void fillConstants( const std::vector<ShaderConstantMember> &members, float *pRegisters, uint32_t numRegisters,
                        const EdenMaterial &material, const Matrix4x4f &world, const EdenDrawParams *pParams,
                        const char *pProgramName , bool bPixel );
    GPUTexture *createWhiteTexture( VkImageViewType viewType, const char *pName );
};
