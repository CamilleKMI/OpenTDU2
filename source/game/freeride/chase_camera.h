#pragma once

#include "render/eden/eden_scene_view.h"

// In-game car camera (docs: /tmp/claude-0/spec_freeride.md 3), parameters
// from Euro/Bnk/Database/CamerasInGame.xmb (the exe loads
// "%sDataBase/CamerasInGame.xmb" and holds every field name): per car
// (id_car = DBCarPhysicsData _camera) a list of cameras by mode; the
// IA_CarCamera cycle is p_array_camera_switch {21, 20, 23, 24, 22}. Mode 20
// is the chase camera behind the car (the California has 20, 23, 24 and
// their back views 40, 43, 44). The update math of the original is not
// identified [INF]: offsets along the flattened car heading, lerped with
// the speed between follow_speed_offset and follow_speed_max_offset (km/h),
// heading followed with a time constant follow_delay / 60 s, fov lerp
// fov_min -> fov_max over fov_speed_min -> fov_speed_max.
class ChaseCamera {
public:
    struct Params {
        int32_t Mode = 20;
        float FovDeg = 45.0f;
        bool bFovLerp = true;
        float FovMin = 45.0f, FovMax = 65.0f;
        float FovSpeedMin = 0.0f, FovSpeedMax = 150.0f;
        float Near = 0.8f, Far = 40000.0f;
        float PosOffset = -6.45f, PosOffsetH = 1.5f;
        float TargOffset = 1.5f, TargOffsetH = 0.91f;
        float SpeedOffset = 50.0f;
        float PosMaxOffset = -6.4f, PosMaxOffsetH = 1.6f;
        float TargMaxOffset = 1.6f, TargMaxOffsetH = 1.3f;
        float SpeedMaxOffset = 250.0f;
        float DelayOffset = 12.0f, DelayMaxOffset = 15.0f;
        float LeanFactorOffset = 1.6348f, LeanFactorMaxOffset = 0.4f;
        // fixed_pos / fixed_targ (car frame, x left): inside views (23 driver, 24 bumper, near 0.05).
        Vector3f FixedPos = Vector3f( 0.0f, 1.1f, 0.0f );
        Vector3f FixedTarget = Vector3f( 0.0f, 0.5f, 1.0f );
    };

public:
    // carCameraId: _camera of the car; modes: list in the cycle order.
    bool load( int32_t carCameraId );
    inline const std::vector<Params> &getCameras() const { return cameras; }
    // Cycles through p_array_camera_switch (only the modes of the car).
    void nextMode();
    inline int32_t getMode() const { return current < cameras.size() ? cameras[current].Mode : 20; }

    void reset( const Vector3f &carPosition, const Vector3f &carForward );
    // carWorld: car model origin -> world; ground: height under a point.
    EdenSceneView::Camera update( float deltaTime, const Matrix4x4f &carWorld, float speedKmh,
                                  const std::function<bool( float x, float z, float &outHeight )> &ground );

    // Camera rotation (Eden convention: forward = conj(q) (0, 0, 1)) looking
    // from eye to target.
    static Quaternionf LookRotation( const Vector3f &eye, const Vector3f &target );

private:
    std::vector<Params> cameras; // modes of the car in the switch order
    size_t current = 0;
    Vector3f heading = Vector3f( 0.0f, 0.0f, 1.0f );
    bool bInitialized = false;
};
