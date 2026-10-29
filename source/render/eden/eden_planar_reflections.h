#pragma once

#include "render/eden/eden_renderer.h"

class EdenModel;

// GSReflection (TestDrive2.exe object 0x011f8ca0, vtable 0x00ee727c)
// [CODE, spec_pool_interior 2]: projective maps of the water and of the
// reflective floors.
//
// - Material registration (FUN_00d7df50, table 0x00f817c0): TD_Water with
//   U4 (spec 0x23) = 1 -> slot 4 (REFRACTION), TD_Furnitures with U7 (spec
//   0x26) = 1 -> slot 6; the slot becomes a runtime texture sampled with
//   clamp (FUN_00d7de80). Disabled: the switch and the slot are cleared
//   (TD_Water then uses its ps 12/13 permutations, without s4).
// - Projectors (FUN_00d7e950) from the Entity__Actor__Mesh D_REFLECT scene
//   of a bank: a node without geometry starts a projector (plane: node Y
//   axis n, d = n . node position); "REFRACT" nodes are refraction
//   projectors, the others mirrors (M = I - 2 n n^T, t = 2 d n). The
//   geometry nodes that follow belong to the last projector (at most 0x41).
//   A mirror whose node is one of the entity AvatarReflectlayername1/2 is
//   an actor layer (at most 2, never for EPH / ECAS banks).
// - Per frame (FUN_00d7eea0, after HEADLIGHTS BLIT, before NEAR_OPAQUE):
//   one screen sized target (renderer+0x10880) holds the REFLECT 2D layer
//   (objects of the refraction projectors and of the plain mirrors, main
//   camera; the actors too while the actors flag is set) and the REFLECT
//   FOR ACTORS layers (the actors seen by the mirror camera). It is bound to
//   the slot of every registered material (FUN_00d7c030).
//
// Not readable (encrypted): the target size / format / clear colour, the
// use of the projector clip plane. Screen sized RGBA8 cleared to (0, 0, 0,
// 0) [INF: the shaders sample it with the screen position and use its alpha
// as the "something was drawn" mask]; no clip plane [INF]; the depth is
// cleared between the layers [INF].
class EdenPlanarReflections {
public:
    static constexpr uint32_t kMaxProjectors = 10;
    static constexpr uint32_t kMaxObjects = 0x41;
    static constexpr uint32_t kMaxActorLayers = 2;
    static constexpr uint32_t kMaxMaterials = 100;

    struct Projector {
        std::string Name;
        Vector3f Normal;
        float Distance = 0.0f;
        bool bRefraction = false; // +0x94
        bool bActorLayer = false; // +0x95
        Matrix4x4f Mirror;        // +0x40 (mirrors)
        const EdenModel *pModel = nullptr;
        Matrix4x4f World; // model placement
        std::vector<int32_t> Nodes; // geometry nodes (objects)
    };

    // Draws the actors (avatars, cars) for a projector pass.
    using DrawActors = std::function<void( EdenRenderer &renderer )>;

public:
    // Game mode table 0x00f817e0 { mode, enable } (original mode ids: LOGIN
    // 0x14 off, AVATAR_CHOICE 0x20 on, FIRST_WM 0x23 on...).
    static bool IsEnabledForGameMode( int32_t originalModeId );

    void destroy();

    // FUN_00d7c120: on / off switch (re-selects the registered materials).
    void setEnabled( EdenRenderer &renderer, bool bEnable );
    inline bool isEnabled() const { return bEnabled; }
    // mgr+0x24: the actors are drawn in the REFLECT 2D (refraction) layer
    // (set by the C_JACCUZZI / C_A4 cinematic hooks only).
    inline void setRefractionActors( bool bEnable ) { bRefractionActors = bEnable; }

    // FUN_00d7df50: registers the projective materials of a model (call for
    // every model of a registered bank). Returns the number registered.
    uint32_t registerMaterials( EdenRenderer &renderer, EdenModel &model );
    // FUN_00d7e950 (projectors part): builds the projectors of a D_REFLECT
    // model drawn at 'world'. pBankName: EPH / ECAS banks get no actor
    // layer; pActorLayer1 / 2: AvatarReflectlayername1 / 2 of the entity
    // ("ground2"), may be null. Returns the number of projectors added.
    uint32_t registerScene( const EdenModel &model, const Matrix4x4f &world, const char *pBankName,
                            const char *pActorLayer1, const char *pActorLayer2 );
    // Forgets the projectors and the registered materials (the models are
    // about to be destroyed).
    void clear();
    // Forgets the projectors and the registered materials of one model (the
    // model is about to be destroyed: streamed spot exteriors).
    void removeModel( EdenModel &model );

    inline const std::vector<Projector> &getProjectors() const { return projectors; }
    // Something to draw this frame.
    bool isActive() const;

    // Renders the layers for the main scene (before the opaque pass) and
    // binds the target (EdenRenderer::setProjectiveTexture). drawActors is
    // used for the actor layers / refraction actors.
    void render( EdenRenderer &renderer, const EdenSceneParams &mainScene, const DrawActors &drawActors );
    // Unbinds the target (end of frame).
    void unbind( EdenRenderer &renderer );
    GPUTexture *getTexture() const;

private:
    struct RegisteredMaterial {
        EdenMaterial *pMaterial = nullptr;
        uint8_t SpecIndex = 0;
        uint8_t Slot = 0;
    };

    std::vector<Projector> projectors;
    std::vector<RegisteredMaterial> materials;
    RenderTarget *pTarget = nullptr;
    bool bEnabled = false;
    bool bRefractionActors = false;

    void applyMaterial( EdenRenderer &renderer, const RegisteredMaterial &entry, bool bEnable );
    void ensureTarget( uint32_t width, uint32_t height );
};
