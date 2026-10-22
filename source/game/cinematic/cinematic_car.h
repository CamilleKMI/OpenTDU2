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
    // Door opening (docs: /tmp/claude-0/spec_garage_car.md 3): side 0 left
    // (DOOR_L, car slot 29), 1 right (DOOR_R, 30). FUN_00c67230 /
    // FUN_00c67200 store the angle at car +0xa794 / +0xa790; without the
    // DOOR_x_A_n key nodes (absent on the California) FUN_00c67020 rotates the
    // door node about its local Y axis by that angle (FUN_00ddbcd0). The
    // door subtree (mirror, window, INT_LR_x) and the interior bank panel
    // INT_HR_x (instanced on the door, FUN_00c69e30) follow.
    void setDoorAngle( int32_t side, float angle );
    inline float getDoorAngle( int32_t side ) const { return doorAngle[side & 1]; }
    // Model space transform of a car node (names longer than 8 characters
    // are folded, e.g. DVRSEAT_LR); false if absent.
    bool getNodeModelTransform( const char *pName, Matrix4x4f &outTransform ) const;
    // Model space bounds of the high resolution body (BODY_HR).
    bool getBodyBounds( Vector3f &outMin, Vector3f &outMax ) const;
    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather,
               bool bEnvironmentAssigned );

    // DBCarPhysicsData._id_car of the loaded car file (-1 if unknown).
    int32_t getIdCar() const;

private:
    std::string fileName; // Desc::FileName actually loaded
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
    struct DoorNodes {
        int32_t Door = -1;
        Matrix4x4f DoorRest;
        std::vector<std::pair<int32_t, Matrix4x4f>> CarRest;      // DOOR_x subtree
        std::vector<std::pair<int32_t, Matrix4x4f>> InteriorRest; // INT_HR_x subtree
    };
    DoorNodes doors[2];
    float doorAngle[2] = { 0.0f, 0.0f };
    std::vector<int32_t> wheelNodes;
    std::vector<int32_t> wheelShadowNodes;
    int32_t tireNodeFront = -1;
    int32_t tireNodeRear = -1;
    int32_t rimDiameterFront = 19;
    int32_t rimDiameterRear = 19;
    float tireRadius = 0.339f;
};
