#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_particles.h"
#include "render/eden/eden_scene_view.h"

// Exterior of a spot (islands/<island>/exterior/<spots.bin bank name>.bnk,
// e.g. spot EPH_L3DI_5067I -> eph_l3di.bnk, ECAS -> ecas.bnk), drawn at the
// spot frame T(position) R_y(heading) like FirstOutro (FREERIDE house):
// Scenery models, Entity__Actor__Mesh / __Interior in the bank frame,
// Entity__Actor at entityTRS * rootLocal^-1 (spec_pool_interior 6). Several
// spots may share one bank (same building type): one instance per frame.
//
// Per bank resources, like the original spot exterior setup FUN_00981b60
// [CODE]: .UVA texture animations bound by material slot
// (EdenModel::bindUVAnimations), NIGHTLMAP materials (FUN_00981870 ->
// registerLightmaps), the neon set (FUN_00985e40: WeatherEmissive from
// building_neon_strength, also in registerLightmaps) and the GSReflection registration FUN_00d7e950(bank)
// (every TD_Water U4 / TD_Furnitures U7 material; the projectors of the
// Entity__Actor__Mesh D_REFLECT scene). Whether GSReflection then draws
// depends on the game mode (table 0x00f817e0, EdenSceneView::
// setPlanarReflectionsEnabled): off, the TD_Water U4 switch and its
// REFRACTION slot are cleared (no white projective slot).
class SpotExterior {
public:
    SpotExterior() = default;
    ~SpotExterior();

    bool load( EdenSceneView &view, const std::string &bankPath );
    void unload();
    // Lightmap day / night blend of the models (call again after the view
    // lightmap list was cleared).
    void registerLightmaps( EdenSceneView &view );
    // GSReflection projectors of the D_REFLECT scenes, one set per frame
    // (call after the frames are set).
    void registerReflections( EdenSceneView &view );
    // Particle effects of the bank (Entity__Fx with AutoStart: ECAS
    // fountains, docs/re/particles_spec.md), one emitter set per frame
    // (call after the frames are set).
    uint32_t startEffects( EdenRenderer &renderer );
    // Sound loops of the same Entity__Fx (Fx__SourceData SoundList ->
    // Fx__Sound .wav of the bank, .bfx " DNS" node parameters, e.g. ECAS
    // s_fountain_loop_01 / 02, client AmbienceExternalSfx): one 3D voice
    // per entity and frame at the entity position (called by startEffects).
    uint32_t startSounds();
    void stopSounds();
    void update( float deltaTime );
    void draw( EdenRenderer &renderer );
    // Texture slots whose 2DM texture is in another bank (ECAS windows_05
    // REFLECTION "CUBEMAP" = cubemap.2db of elandca.bnk): looked up again in
    // every loaded bank, once the exteriors around are loaded
    // (docs/re/spot_exterior_spec.md 5). clearForeignTextures forgets them
    // (before another exterior, which may own them, is unloaded).
    uint32_t resolveForeignTextures( EdenRenderer &renderer );
    void clearForeignTextures();

    inline const std::string &getPath() const { return path; }
    // The exterior bank (FREE_DRIVE: its <spot>_sc_*.shk collision).
    inline const Bank &getBank() const { return bank; }
    inline std::vector<Matrix4x4f> &getFrames() { return frames; }
    inline size_t getNumModels() const { return sets.size() + props.size(); }

private:
    struct Prop {
        std::unique_ptr<EdenModel> Model;
        Matrix4x4f Local;
    };

    struct ReflectScene {
        const EdenModel *pModel = nullptr;
        std::string ActorLayer1;
        std::string ActorLayer2;
    };

    std::string path;
    EdenSceneView *pView = nullptr; // GSReflection registrations
    std::vector<ReflectScene> reflectScenes;
    Bank bank;
    BankHashcodes hashcodes;
    std::vector<std::unique_ptr<EdenModel>> sets;
    std::vector<Prop> props;
    std::vector<Matrix4x4f> frames; // spot frames using this bank
    std::vector<std::pair<EdenMaterial *, uint32_t>> foreignSlots; // resolveForeignTextures
    EdenParticleSet effects;
    CinematicSounds sounds;
    std::vector<GSAudio::VoiceHandle> voices;
};
