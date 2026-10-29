#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_animation.h"
#include "render/eden/eden_model.h"
#include "render/eden/eden_scene_view.h"

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
    // Desc of a database car (DBCarPhysicsData _id_car): _car_file_name and
    // the DBRims row of a rims hashcode (0: the car's _default_rims): rim
    // banks <_rsc_path>/<_rsc_file_name_front|rear>, diameters, tire radius =
    // rim radius + sidewall (_largeur x _hauteur %).
    static bool FindDatabaseDesc( int32_t idCar, uint64_t rimsHashcode, Desc &outDesc );
    // DB colour of the car (FUN_00d8e6a0 -> FUN_00c5ff20(disp, _color_id_1,
    // 0), docs/re/paint_spec.md 3.2 / 3.3): the material of that name
    // (GetIdentifier64bit) in the car's colour library (<car>_color.2dm,
    // group 21 of the car bank, registered by FUN_00c50300) gives the paint
    // parameters (Diffuse / Ambient / Specular, MatParam, SpecParm,
    // SpecBlinn, FlakeABNS, RampPow, RampClr1..3, RampFlake, BlinnClrA / B)
    // [CODE]; they are written into the car paint materials "Paint"
    // (TD_CarPaint2, same parameter set) and "Paint_LR" (RampPow.xy,
    // RampClr1 / 2, setters FUN_00c56b90 / FUN_00c56e10) [INF: the paint
    // state -> material step is encrypted]. False if the colour is absent
    // ("" / "##": no change).
    bool applyColor( const std::string &colorId );
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
    // Environment cube of the car reflections: the dynamic cube rendered
    // around the car, or a static cube (bDynamic false: the CUBECAR texture
    // of the garage bank).
    void assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube, bool bDynamic = true );
    // Door opening (docs: /tmp/claude-0/spec_garage_car.md 3): side 0 left
    // (DOOR_L, car slot 29), 1 right (DOOR_R, 30). FUN_00c67230 /
    // FUN_00c67200 store the angle at car +0xa794 / +0xa790; without the
    // DOOR_x_A_n key nodes (absent on the California) FUN_00c67020 rotates the
    // door node about its local Y axis by that angle (FUN_00ddbcd0). The
    // door subtree (mirror, window, INT_LR_x) and the interior bank panel
    // INT_HR_x (instanced on the door, FUN_00c69e30) follow.
    void setDoorAngle( int32_t side, float angle );
    inline float getDoorAngle( int32_t side ) const { return doorAngle[side & 1]; }

    // Roof and side windows (accessories, docs/re/accessories_spec.md 4 / 5).
    // Hardtop animation player (car +0xb8d0, Hardtop.anm on <car>.xsb): time
    // 0 = closed, duration = open; setHardtopTime resamples the HANIM_S pose
    // when the time changed (FUN_00c4a390).
    inline bool hasHardtopAnimation() const { return bHardtop; }
    inline float getHardtopDuration() const { return bHardtop ? hardtopAnimation.getDuration() : 0.0f; }
    inline float getHardtopTime() const { return hardtopTime; }
    void setHardtopTime( float time );
    // Detachable hardtop object (node slot 39 HARDTOP, car +0x15b0).
    inline bool hasHardtopNode() const { return bHardtopNode; }
    // Side window lowering (car +0xa780 left / +0xa784 right, FUN_00c66150 /
    // FUN_00c66110, clamped to [0, 1]): 0 = WND_x1 at its rest pose (up),
    // 1 = on WND_x2 (down); FUN_00c674a0 interpolates the decomposed WND_x1 /
    // WND_x2 poses (FUN_009544e0) with FUN_00451560 [ENC] -> translation lerp
    // + rotation slerp [INF].
    void setWindowLowering( int32_t side, float value );
    inline float getWindowLowering( int32_t side ) const { return windowLowering[side & 1]; }
    // Model space transform of a car node (names longer than 8 characters
    // are folded, e.g. DVRSEAT_LR); false if absent.
    bool getNodeModelTransform( const char *pName, Matrix4x4f &outTransform ) const;
    // Model space bounds of the high resolution body (BODY_HR).
    bool getBodyBounds( Vector3f &outMin, Vector3f &outMax ) const;
    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather,
               bool bEnvironmentAssigned );

    // DBCarPhysicsData._id_car of the loaded car file (-1 if unknown).
    int32_t getIdCar() const;

    // Car lights (docs/re/car_lights_spec.md, /tmp/claude-0/car_lights_api_note.md).
    // Driver light state (the car +0x280 object: +0x1d0 brake, +0x1d1
    // reverse, +0x1db / +0x1dc left / right blinker, +0x1d4 light mode).
    enum class eIndicator : int32_t { Off = 0, Left = 1, Right = 2, Hazard = 3 };
    void setIndicator( eIndicator mode ) { indicator = mode; }
    inline eIndicator getIndicator() const { return indicator; }
    void setBraking( bool bOn ) { bBraking = bOn; }
    void setReverse( bool bOn ) { bReverse = bOn; }
    // Light mode (+0x1d4, FUN_00c588d0): 0 every light off (blinkers
    // included), 1 = -1 = the default: position lights / headlights from the
    // weather (car_poslight_intensity, car_headlight_flare_intensity: 0 by
    // day, 1 at night), 2 headlights forced (high beam: flare + 1).
    void setHeadlights( int32_t mode ) { headlightMode = mode; }
    // Tunnel (car +0x2acc >= 5): the tunnel_car_* weather values.
    void setInTunnel( bool bOn ) { bInTunnel = bOn; }
    // Per frame tunnel state from the road network (RoadNetwork::
    // getCarTunnelDistance, FUN_00c62a70): sets the tunnel flag (d >= 5 m)
    // and the smoothed tunnel lighting blend (car +0x2ad0). -tunnel 0|1
    // forces the flag.
    void setTunnelDistance( float distance, float dt );
    inline float getTunnelDistance() const { return tunnelDistance; }
    inline float getTunnelBlend() const { return tunnelBlend; }
    inline bool isInTunnel() const { return bInTunnel; }
    void setNight( bool bOn ) { bInTunnel = bOn; } // deprecated alias of setInTunnel
    // Blinker timer (FUN_00c57ef0) and high beam delay, once per frame.
    void updateLights( float dt );
    // Projected headlight spot of the car for the HEADLIGHTS pass
    // (FUN_00921260 / FUN_009227c0 / FUN_00923230, docs/re/car_lights_spec.md
    // 8); false when the lights are off. Call after draw() / applyLights of
    // the frame (uses the light state of the last applyLights).
    bool getHeadlightSpot( const Matrix4x4f &world, const Vector3f &cameraPosition,
                           EdenSceneView::HeadlightSpot &outSpot ) const;
    // Blink phase lit (left or right lamp currently on).
    inline bool isIndicatorLampOn() const { return bIndicatorLamp; }

private:
    void collectLights();
    void applyLights( const WeatherPreset *pWeather );
    // Light beams (TD_LightBeam cone lightbeam_rt of fx/weatherfx.bnk,
    // FUN_00923230 / FUN_00964b90), docs/re/car_lights_spec.md 7.
    void loadBeams();
    void drawBeams( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather );
    Bank fxBank;
    EdenModel beam;
    float beamHeadlightIntensity = 0.0f; // projector / beam intensity (headlight slot +0x10)
    bool bBeamLampsOn = false;
    eIndicator indicator = eIndicator::Off;
    bool bBraking = false;
    bool bReverse = false;
    bool bInTunnel = false;
    float tunnelDistance = 0.0f; // car +0x2acc
    float tunnelBlend = 0.0f;    // car +0x2ad0
    int32_t headlightMode = -1;
    float indicatorTimer = -1.0f; // car +0x1f10 (-1: blinkers off)
    float highBeamTimer = 0.0f;   // car +0x1f24
    bool bIndicatorLamp = false;
    float indicatorValue[2] = { 0.0f, 0.0f }; // left, right (FUN_00c57ef0 outputs)
    // Light materials (FUN_00c5ee20: materials named after the table
    // 0x00f7e700, their GLOSSFCT vector).
    struct LightMaterial {
        uint32_t Material = 0;
        bool bLeft = false;
        bool bFront = false;
    };
    std::vector<LightMaterial> lightMaterials;
    // Lamp glow meshes ?A_<L|S|R|W>_<FL|FR|RL|RR> and the projected light
    // boxes BREAKING / REVERSING / HEADLIGHT (car slots 13 / 14 / 15).
    struct LightNode {
        int32_t Node = -1;
        char Function = 0; // "LBSWR": 'L' front lights, 'B' rear position, 'S' stop, 'W' warning, 'R' reverse
        bool bLeft = false;
        bool bFront = false;
        bool bFlare = false; // Fa_<c>_<pos>[n] flare sprite (else the _C glow mesh)
    };
    std::vector<LightNode> causticNodes;
    std::vector<LightNode> flareNodes;
    int32_t lightBoxNodes[3] = { -1, -1, -1 }; // BREAKING, REVERSING, HEADLIGHT
    std::vector<uint32_t> lightBoxMaterials[3];
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
    // Side windows (accessories): WND_L1 / WND_R1 and their lowered pose
    // WND_L2 / WND_R2 (rest transforms of the car scene).
    struct WindowNodes {
        int32_t Window = -1;
        Matrix4x4f Up, Down;
    };
    WindowNodes windows[2];
    float windowLowering[2] = { 0.0f, 0.0f };
    bool bHardtopNode = false;
    void applyWindow( int32_t side );
    std::vector<int32_t> wheelNodes;
    std::vector<int32_t> wheelShadowNodes;
    int32_t tireNodeFront = -1;
    int32_t tireNodeRear = -1;
    int32_t rimDiameterFront = 19;
    int32_t rimDiameterRear = 19;
    float tireRadius = 0.339f;
};
