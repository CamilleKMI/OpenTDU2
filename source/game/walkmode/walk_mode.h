#pragma once

#include "game/walkmode/walk_controller.h"

#include <string>

class IntroCinematic;

// Walk mode manager (DAT_00fd95e0, docs/re/tess_garage_spec.md 4.1) on an
// IntroCinematic scene: first person controller on the scene collision
// (.shk), camera, FpSelectable selection with the reticle ray (50 m,
// 0.2 s re-selection cooldown), INTRO movie ItemType('over'/'out').
class WalkMode {
public:
    // Starts at an Entity__Camera (cam_entree_xx / cam_entree).
    bool start( IntroCinematic &scene, const std::string &cameraName, float fov );
    void update( IntroCinematic &scene, float deltaTime, bool bInputEnabled );
    // Camera back to the walk pose (after a cinematic).
    void resume( IntroCinematic &scene );
    void clearSelection( IntroCinematic &scene );

    inline const Vector3f &getPosition() const { return controller.getPosition(); }
    inline int32_t getSelected() const { return selected; }
    // /:SelItemType of the selection ("garage", "car"...), empty if none.
    const char *getSelectedType( const IntroCinematic &scene ) const;

private:
    WalkController controller;
    float fov = 1.0472f;
    float eyeHeight = 1.48f;
    int32_t selected = -1;
    float cooldown = 0.0f;

    Vector3f collide( const IntroCinematic &scene, const Vector3f &from, const Vector3f &to ) const;
    void updateSelection( IntroCinematic &scene, float deltaTime );
};
