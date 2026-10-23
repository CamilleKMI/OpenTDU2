#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cinematic_audio.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_scene_view.h"

// Exterior of a spot (islands/<island>/exterior/<spots.bin bank name>.bnk,
// e.g. spot EPH_L3DI_5067I -> eph_l3di.bnk, ECAS -> ecas.bnk), drawn at the
// spot frame T(position) R_y(heading) like FirstOutro (FREERIDE house):
// Scenery models, Entity__Actor__Mesh / __Interior in the bank frame,
// Entity__Actor at entityTRS * rootLocal^-1 (spec_pool_interior 6). Several
// spots may share one bank (same building type): one instance per frame.
class SpotExterior {
public:
    SpotExterior() = default;
    ~SpotExterior();

    bool load( EdenSceneView &view, const std::string &bankPath );
    void unload();
    // Lightmap day / night blend of the models (call again after the view
    // lightmap list was cleared).
    void registerLightmaps( EdenSceneView &view );
    void draw( EdenRenderer &renderer ) const;

    inline const std::string &getPath() const { return path; }
    inline std::vector<Matrix4x4f> &getFrames() { return frames; }
    inline size_t getNumModels() const { return sets.size() + props.size(); }

private:
    struct Prop {
        std::unique_ptr<EdenModel> Model;
        Matrix4x4f Local;
    };

    std::string path;
    Bank bank;
    BankHashcodes hashcodes;
    std::vector<std::unique_ptr<EdenModel>> sets;
    std::vector<Prop> props;
    std::vector<Matrix4x4f> frames; // spot frames using this bank
};
