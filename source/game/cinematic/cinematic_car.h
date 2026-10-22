#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_animation.h"
#include "render/eden/eden_model.h"

#include <functional>

class EdenRenderer;
struct WeatherPreset;
struct GPUTexture;

// A car of a cinematic (Entity__CinCar / the current car): the high
// resolution vehicule bank (vehicules/high/<file>.bnk), its rims and tires
// (common_car.bnk), drawn with the original car shaders.
class CinematicCar {
public:
    struct Desc {
        std::string FileName = "California";
        std::string RimFront = "ferrari/california_f_01";
        std::string RimRear = "ferrari/california_f_01";
        int32_t DiameterFront = 19;
        int32_t DiameterRear = 19;
        float TireRadius = 0.339f; // 245/40 R19
    };
    // Ground height at (x, z) searched below y.
    using SurfaceFunction = std::function<bool( float x, float z, float y, float &outHeight )>;

public:
    CinematicCar();
    ~CinematicCar();

    bool load( const Desc &desc );
    void unload();
    inline bool isLoaded() const { return car.isLoaded(); }
    inline EdenModel &getModel() { return car; }

    // Car world pose: the cinematic pose with the 4 tires resting on the
    // surface (plane through the hub targets).
    Matrix4x4f fitOnGround( Vector3f position, const Quaternionf &rotation,
                            const SurfaceFunction &surfaceHeight ) const;
    void assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube );
    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather,
               bool bEnvironmentAssigned );

private:
    Bank carBank;
    Bank carCommonBank;
    Bank rimBank;
    Bank rimRearBank;
    Bank interiorBank;
    EdenModel car;
    EdenModel interior;
    EdenModel rim;
    EdenModel rimRear;
    EdenModel tire;
    // Retractable roof (docs: /tmp/claude-0/spec_california.md): skeleton
    // <car>.xsb + Hardtop.anm posing the skinned HANIM_S node at
    // hardtopTime (0: closed, end of the clip: open).
    EdenSkeleton hardtopSkeleton;
    EdenAnimation hardtopAnimation;
    bool bHardtop = false;
    float hardtopTime = 0.0f;
    std::vector<Matrix4x4f> hardtopSkin;
    // Cockpit level of detail (FUN_00c66380): LOD 4 shows the interior
    // bank (COCK_HR, INT_HR_L/R), LOD 3 the exterior low resolution cockpit
    // (COCK_LR, INT_LR_L/R).
    std::vector<int32_t> cockpitLowNodes;    // car nodes
    std::vector<int32_t> interiorDrawnNodes; // interior nodes (COCK_HR subtree, INT_HR_L/R)
    bool bLastInterior = false;
    std::vector<int32_t> wheelNodes;
    std::vector<int32_t> wheelShadowNodes;
    int32_t tireNodeFront = -1;
    int32_t tireNodeRear = -1;
    int32_t rimDiameterFront = 19;
    int32_t rimDiameterRear = 19;
    float tireRadius = 0.339f;
};
