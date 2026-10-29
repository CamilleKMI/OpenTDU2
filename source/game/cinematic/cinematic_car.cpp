#include "shared.h"
#include "cinematic_car.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "render/2dm.h"
#include "render/gs_render.h"
#include "render/eden/eden_weather.h"
#include "render/eden/eden_scene_view.h"
#include "system/cmd_line.h"

#include <cstring>
#include <unordered_map>

namespace {
// 8 byte scene node name (character i added into byte i & 7, little endian):
// the 3DD instance hashcode of names longer than 8 characters.
uint64_t NodeNameHash( const char *pName ) {
    uint8_t bytes[8] = {};
    for ( size_t i = 0; pName[i] != '\0'; i++ ) {
        bytes[i & 7] = static_cast<uint8_t>( bytes[i & 7] + static_cast<uint8_t>( pName[i] ) );
    }
    uint64_t hash = 0;
    for ( int i = 7; i >= 0; i-- ) {
        hash = ( hash << 8 ) | bytes[i];
    }
    return hash;
}

int32_t FindNodeByHash( const Render3DD &scene, const char *pName ) {
    const uint64_t hash = NodeNameHash( pName );
    const std::vector<SceneNode> &nodes = scene.getNodes();
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        if ( nodes[i].Hashcode == hash ) {
            return static_cast<int32_t>( i );
        }
    }
    return -1;
}

// DBCarPhysicsData._car_body of the car bank (the runtime car record +0x8c:
// 0x470 byte records, _id_car at +0x21c).
int32_t FindCarBody( const std::string &fileName ) {
    if ( gpDatabase == nullptr ) {
        return -1;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( car["_car_file_name"].asString().c_str(), fileName.c_str() ) == 0 ) {
            return static_cast<int32_t>( car["_car_body"].asInt( -1 ) );
        }
    }
    return -1;
}
} // namespace

CinematicCar::CinematicCar() {}

CinematicCar::~CinematicCar() { unload(); }

void CinematicCar::unload() {
    car.destroy();
    interior.destroy();
    rim.destroy();
    rimRear.destroy();
    tire.destroy();
    beam.destroy();
    for ( Bank *pBank : { &carBank, &carCommonBank, &rimBank, &rimRearBank, &interiorBank, &fxBank } ) {
        if ( !pBank->getEntries().empty() ) {
            gpRender->getTextureCache().releaseOwner( pBank->getPath().c_str() );
        }
        pBank->unload();
    }
    wheelNodes.clear();
    wheelShadowNodes.clear();
    bHardtop = false;
    hardtopTime = 0.0f;
    hardtopSkin.clear();
    cockpitLowNodes.clear();
    interiorDrawnNodes.clear();
    for ( DoorNodes &door : doors ) {
        door = DoorNodes();
    }
    doorAngle[0] = doorAngle[1] = 0.0f;
    for ( int32_t side = 0; side < 2; side++ ) {
        windows[side] = WindowNodes();
        windowLowering[side] = 0.0f;
    }
    bHardtopNode = false;
}

void CinematicCar::assignEnvironment( EdenRenderer &, GPUTexture *pCube, bool bDynamic ) {
    // Car environment cube binding (docs/re/car_glass_spec.md):
    // FUN_00d58df0 (dynamic cube, DAT_011c9f00/04 + 0x48) and FUN_00d58d20
    // (static cube, e.g. the garage CUBECAR) both end in FUN_00d58a90: for
    // the materials of the car families below, FUN_00d56df0 replaces the
    // texture of every slot whose texture hashcode is the default car cube
    // (DAT_011f8e88: "CUBEMAP" of common_car.bnk [INF: the global is set in
    // encrypted code; FUN_00c4b0e0 looks the default cube up by it]); other
    // cubes (CUBEINT of the interior) are kept. When the texture changed
    // and S_USEREFLECTION (spec 3) is set, FUN_00d58980 writes 1 (static
    // cube) or 0 (dynamic cube) to SPECPARM.z (TD_CarPaint family) or
    // RAMPPOW.z (TD_CarPaint2 family): the car shaders scale the
    // reflection by (1 - cube.a * z).
    static const uint64_t kDefaultCube = GetIdentifier64bit( "CUBEMAP" );
    static const uint64_t kSpecParm = GetIdentifier64bit( "SPECPARM" );
    static const uint64_t kRampPow = GetIdentifier64bit( "RAMPPOW" );
    if ( pCube == nullptr ) {
        return;
    }
    for ( EdenModel *pModel : { &car, &interior, &rim, &rimRear, &tire } ) {
        for ( EdenMaterial &material : pModel->getMaterials() ) {
            const uint64_t family = material.ShaderSetGUID;
            // FUN_00d58a90 family list (TD_Mirror only with the car +0x2ab6
            // flag, 0 at creation [INF]: left out).
            const bool bCarFamily = family == 0x183157b81c6c7357ull || family == 0xca00000000000010ull ||
                                    family == 0x183157b81c6c5377ull || family == 0xdddddddd1c6c7357ull ||
                                    family == 0xb39c35015789e65cull || family == 0xca00000000000020ull ||
                                    family == 0x6638348331d66466ull || family == 0x1b38348331cacacaull;
            if ( !bCarFamily ) {
                continue;
            }
            bool bChanged = false;
            for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
                if ( material.TextureHashes[i] == kDefaultCube && material.pTextures[i] != pCube ) {
                    material.pTextures[i] = pCube;
                    bChanged = true;
                }
            }
            if ( !bChanged || material.Spec[3] == 0 ) {
                continue;
            }
            uint64_t parameter = 0;
            if ( family == 0x183157b81c6c7357ull || family == 0xca00000000000010ull || family == 0x1b38348331cacacaull ) {
                parameter = kSpecParm;
            } else if ( family == 0x183157b81c6c5377ull || family == 0x183157b81c6ccacaull ) {
                parameter = kRampPow;
            }
            auto it = parameter != 0 ? material.Parameters.find( parameter ) : material.Parameters.end();
            if ( it != material.Parameters.end() ) {
                it->second.z = bDynamic ? 0.0f : 1.0f;
            }
        }
    }
}

bool CinematicCar::load( const Desc &requested ) {
    unload();
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const std::string folder = gpFile->getBankFolder() + "vehicules/";
    Desc desc = requested;
    if ( !gpFile->fileExists( ( folder + "high/" + ToLower( desc.FileName ) + ".bnk" ).c_str() ) ) {
        OTDU_LOG_WARN( "CinematicCar: '%s' not available, using the California\n", desc.FileName.c_str() );
        desc = Desc();
    }
    fileName = desc.FileName;
    const std::string carName = ToLower( desc.FileName );
    const std::string carPath = folder + "high/" + carName + ".bnk";
    const std::string commonPath = folder + "common_car.bnk";
    if ( !carCommonBank.load( commonPath.c_str() ) || !carBank.load( carPath.c_str() ) ) {
        return false;
    }
    TextureCache &textures = gpRender->getTextureCache();
    textures.loadBankTextures( carCommonBank, carCommonBank.getPath().c_str() );
    textures.loadBankTextures( carBank, carBank.getPath().c_str() );
    // Interior bank (vehicules/interiors/<car>_i.bnk): the cockpit maps
    // (california_int*.2db...) used by the interior materials.
    const std::string interiorPath = folder + "interiors/" + carName + "_i.bnk";
    if ( gpFile->fileExists( interiorPath.c_str() ) && interiorBank.load( interiorPath.c_str() ) ) {
        textures.loadBankTextures( interiorBank, interiorBank.getPath().c_str() );
        // The cockpit model (<car>_i.3dd, car frame) [INF: when the game
        // draws it is not read; drawn with the car].
        interior.load( *pRenderer, interiorBank, ( carName + "_i" ).c_str(), nullptr, interiorBank.getPath().c_str() );
    }
    if ( !car.load( *pRenderer, carBank, carName.c_str(), nullptr, carBank.getPath().c_str() ) ) {
        return false;
    }
    if ( rimBank.load( ( folder + "rim/" + desc.RimFront + ".bnk" ).c_str() ) ) {
        textures.loadBankTextures( rimBank, rimBank.getPath().c_str() );
        rim.load( *pRenderer, rimBank, nullptr, nullptr, rimBank.getPath().c_str() );
        // The rims of several cars (One-77...) are wound the other way round
        // than their culling state: closed meshes, drawn without culling.
        for ( EdenMaterial &material : rim.getMaterials() ) {
            material.CullMode = 1;
        }
    }
    if ( desc.RimRear != desc.RimFront && rimRearBank.load( ( folder + "rim/" + desc.RimRear + ".bnk" ).c_str() ) ) {
        textures.loadBankTextures( rimRearBank, rimRearBank.getPath().c_str() );
        rimRear.load( *pRenderer, rimRearBank, nullptr, nullptr, rimRearBank.getPath().c_str() );
        for ( EdenMaterial &material : rimRear.getMaterials() ) {
            material.CullMode = 1;
        }
    }
    tire.load( *pRenderer, carCommonBank, "tire", nullptr, carCommonBank.getPath().c_str() );

    // Exterior, high resolution (FUN_00c66380, LOD 3 / 4: D_CAR shown,
    // BODY_MR / BODY_LR and the static roofs HARDTOP_MR / HARDTOP_LR hidden;
    // the retractable roof is the skinned HANIM_S of D_CAR), *_LR paints,
    // the light volumes / caustics (night only) and the projected shadow
    // boxes (they need the scene depth) hidden.
    const Render3DD &scene = car.getScene();
    const std::vector<SceneNode> &nodes = scene.getNodes();
    std::vector<int32_t> hiddenRoots;
    const uint64_t hardtopMR = NodeNameHash( "HARDTOP_MR" ), hardtopLR = NodeNameHash( "HARDTOP_LR" );
    const uint64_t hanimMR = NodeNameHash( "HANIM_MR" );
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        const std::string &name = nodes[i].Name;
        if ( name == "BODY_MR" || name == "BODY_LR" || nodes[i].Hashcode == hardtopMR ||
             nodes[i].Hashcode == hardtopLR || nodes[i].Hashcode == hanimMR ||
             name.find( "BREAKI" ) != std::string::npos || name.find( "EADLI" ) != std::string::npos ||
             name.find( "EVERS" ) != std::string::npos || name.rfind( "?A_", 0 ) == 0 ) {
            hiddenRoots.push_back( static_cast<int32_t>( i ) );
        }
    }
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        for ( int32_t root : hiddenRoots ) {
            if ( static_cast<int32_t>( i ) == root || scene.isDescendantOf( static_cast<int32_t>( i ), root ) ) {
                car.setNodeVisible( static_cast<int32_t>( i ), false );
            }
        }
    }
    for ( const EdenModel::Item &item : car.getItems() ) {
        const std::string &materialName = car.getMaterials()[item.MaterialIndex].Name;
        if ( materialName == "Paint_LR" || materialName == "Plate_LR" ) {
            car.setNodeVisible( item.NodeIndex, false );
        }
    }
    collectLights();
    loadBeams();
    // Cockpit (FUN_00c66380): COCK_LR and the door panels INT_LR_L / R
    // (children of DOOR_L / R) at LOD 2 / 3; at LOD 4 the interior bank
    // instances (FUN_00c69e30): COCK_HR (its seat belts BELT_L / R only
    // when flagged, +0xadd8 / +0xadd9, 0 at creation) and INT_HR_L / R.
    // The interior root GPS and GLASS_I (material carrier of the inside
    // glass, FUN_00c53d10) are not instanced.
    cockpitLowNodes.clear();
    for ( const char *pName : { "COCK_LR", "INT_LR_L", "INT_LR_R" } ) {
        const int32_t root = scene.findNode( pName );
        for ( size_t i = 0; root >= 0 && i < nodes.size(); i++ ) {
            if ( static_cast<int32_t>( i ) == root || scene.isDescendantOf( static_cast<int32_t>( i ), root ) ) {
                cockpitLowNodes.push_back( static_cast<int32_t>( i ) );
            }
        }
    }
    interiorDrawnNodes.clear();
    if ( interior.isLoaded() ) {
        const Render3DD &interiorScene = interior.getScene();
        const std::vector<SceneNode> &interiorNodes = interiorScene.getNodes();
        std::vector<int32_t> roots, excluded;
        for ( const char *pName : { "COCK_HR", "INT_HR_L", "INT_HR_R" } ) {
            const int32_t node = interiorScene.findNode( pName );
            if ( node >= 0 ) {
                roots.push_back( node );
            }
        }
        for ( const char *pName : { "BELT_L", "BELT_R" } ) {
            const int32_t node = interiorScene.findNode( pName );
            if ( node >= 0 ) {
                excluded.push_back( node );
            }
        }
        for ( size_t i = 0; i < interiorNodes.size(); i++ ) {
            const int32_t node = static_cast<int32_t>( i );
            auto inSubtree = [&]( const std::vector<int32_t> &list ) {
                for ( int32_t root : list ) {
                    if ( node == root || interiorScene.isDescendantOf( node, root ) ) {
                        return true;
                    }
                }
                return false;
            };
            const bool bDrawn = inSubtree( roots ) && !inSubtree( excluded );
            interior.setNodeVisible( node, bDrawn );
            if ( bDrawn ) {
                interiorDrawnNodes.push_back( node );
            }
        }
    }

    // Retractable hardtop: HANIM_S skinned on <car>.xsb, posed by the
    // car's 'Hardtop' animation player (FUN_00c67560: \HardTop.anm; player
    // at +0xb8d0, time +0x8c4 = 0 and mode +0x8d0 = 2 at creation,
    // FUN_00c68c80). Time 0 is the closed roof (FUN_00c662b0 == 1 shows the
    // static HARDTOP_MR / LR, the car wash closes it with FUN_00c668c0);
    // FUN_00c3fcc0 opens it (FUN_00c66920(1.0): time = 0.99999 x duration)
    // and lowers the side windows (FUN_00c66150 / FUN_00c66110(1.0): WND_x1
    // moved onto WND_x2, FUN_00c674a0) for the cars whose _car_body is 2 or
    // 4 (roadsters / convertibles) when they are created (FUN_00a51aa0) or
    // given back at the first freeride (FUN_00b10da0 step 3).
    bHardtop = false;
    hardtopSkin.clear();
    // Side windows (FUN_00c6a610): WND_L1 / WND_L2 under DOOR_L, WND_R1 /
    // WND_R2 under DOOR_R (names compared as 'WND_' + "L1" / "L2" / "R1" /
    // "R2"), poses decomposed by FUN_009544e0 into car +0xa6b0.. (left) /
    // +0xa720.. (right); none for the car id 999 [CODE]. Rest = window up.
    // HARDTOP: the detachable roof node (slot 39, car +0x15b0).
    bHardtopNode = scene.findNode( "HARDTOP" ) >= 0;
    for ( int32_t side = 0; side < 2; side++ ) {
        WindowNodes &window = windows[side];
        window = WindowNodes();
        windowLowering[side] = 0.0f;
        const int32_t up = scene.findNode( side == 0 ? "WND_L1" : "WND_R1" );
        const int32_t down = scene.findNode( side == 0 ? "WND_L2" : "WND_R2" );
        if ( up >= 0 && down >= 0 ) {
            window.Window = up;
            window.Up = car.getNodeTransform( up );
            window.Down = car.getNodeTransform( down );
        }
    }
    const BankEntry *pSkeleton = carBank.findEntry( ( carName + ".xsb" ).c_str() );
    if ( pSkeleton == nullptr ) {
        const std::vector<const BankEntry *> skeletons = carBank.findEntriesByExtension( "xsb" );
        pSkeleton = skeletons.empty() ? nullptr : skeletons.front();
    }
    const BankEntry *pHardtop = carBank.findEntry( "Hardtop.anm" );
    if ( pSkeleton != nullptr && pHardtop != nullptr && hardtopSkeleton.parse( pSkeleton->pData, pSkeleton->Size ) &&
         hardtopAnimation.parse( pHardtop->pData, pHardtop->Size ) ) {
        const int32_t carBody = FindCarBody( desc.FileName );
        const bool bOpen = carBody == 2 || carBody == 4;
        hardtopTime = bOpen ? 0.99999f * hardtopAnimation.getDuration() : 0.0f;
        static const int32_t forced = CmdLine::GetArgInt( "-hardtop", -1 );
        if ( forced == 0 || forced == 1 ) {
            hardtopTime = forced == 1 ? 0.99999f * hardtopAnimation.getDuration() : 0.0f;
        }
        std::vector<BoneTransform> locals( hardtopSkeleton.getNumBones() );
        for ( uint32_t bone = 0; bone < hardtopSkeleton.getNumBones(); bone++ ) {
            locals[bone] = hardtopSkeleton.getBindLocal( bone );
        }
        hardtopAnimation.sample( hardtopSkeleton, hardtopTime, false, locals );
        std::vector<Matrix4x4f> boneWorld;
        hardtopSkeleton.computeSkinMatrices( locals, boneWorld, hardtopSkin );
        bHardtop = true;
        if ( hardtopTime > 0.0f ) {
            // FUN_00c66150 / FUN_00c66110(1.0): windows down (WND_x1 on WND_x2).
            setWindowLowering( 0, 1.0f );
            setWindowLowering( 1, 1.0f );
        }
        OTDU_LOG_INFO( "CinematicCar: '%s' _car_body %d, hardtop %s (Hardtop.anm t = %.2f / %.2f)\n",
                       desc.FileName.c_str(), carBody, hardtopTime > 0.0f ? "open" : "closed", hardtopTime,
                       hardtopAnimation.getDuration() );
    } else {
        // No skeleton / Hardtop.anm: no pose to skin HANIM_S with, hidden.
        const int32_t hanim = scene.findNode( "HANIM_S" );
        for ( size_t i = 0; hanim >= 0 && i < nodes.size(); i++ ) {
            if ( static_cast<int32_t>( i ) == hanim || scene.isDescendantOf( static_cast<int32_t>( i ), hanim ) ) {
                car.setNodeVisible( static_cast<int32_t>( i ), false );
            }
        }
    }

    // Doors (FUN_00c6a610): DOOR_L / DOOR_R (slots 29 / 30); their key
    // nodes DOOR_x_A_1..4 (strings 0x00ef5d38 / 0x00ef5d2c, up to 3 channels
    // A..C) do not exist in california.3dd [DATA], so the door is rotated
    // about its own Y axis (FUN_00c67020 fallback). Rest transforms are taken
    // after the window placement above.
    for ( int32_t side = 0; side < 2; side++ ) {
        DoorNodes &door = doors[side];
        door = DoorNodes();
        door.Door = scene.findNode( side == 0 ? "DOOR_L" : "DOOR_R" );
        if ( door.Door < 0 ) {
            continue;
        }
        door.DoorRest = car.getNodeTransform( door.Door );
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            const int32_t node = static_cast<int32_t>( i );
            if ( node == door.Door || scene.isDescendantOf( node, door.Door ) ) {
                door.CarRest.emplace_back( node, car.getNodeTransform( node ) );
            }
        }
        if ( interior.isLoaded() ) {
            const Render3DD &interiorScene = interior.getScene();
            const int32_t panel = interiorScene.findNode( side == 0 ? "INT_HR_L" : "INT_HR_R" );
            for ( size_t i = 0; panel >= 0 && i < interiorScene.getNodes().size(); i++ ) {
                const int32_t node = static_cast<int32_t>( i );
                if ( node == panel || interiorScene.isDescendantOf( node, panel ) ) {
                    door.InteriorRest.emplace_back( node, interior.getNodeTransform( node ) );
                }
            }
        }
    }
    doorAngle[0] = doorAngle[1] = 0.0f;

    // Wheels: rim at D_RIM_xx, tire at the next node (?_TIRE_xx). Tire mesh
    // of the rim diameter (19" for the California: T_19P).
    wheelNodes.clear();
    for ( size_t i = 0; i + 1 < nodes.size(); i++ ) {
        if ( nodes[i].Name.rfind( "D_RIM_", 0 ) == 0 ) {
            wheelNodes.push_back( static_cast<int32_t>( i ) );
        }
    }
    for ( int32_t node : wheelNodes ) {
        const Matrix4x4f &m = nodes[node + 1].World;
        OTDU_LOG_DEBUG( "wheel %s: %.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f\n", nodes[node].Name.c_str(),
                        m.m[0][0], m.m[0][1], m.m[0][2], m.m[1][0], m.m[1][1], m.m[1][2], m.m[2][0], m.m[2][1],
                        m.m[2][2] );
    }
    // Wheel contact shadow boxes (children of D_SBOX, unit boxes placed at
    // run time under each tire).
    wheelShadowNodes.clear();
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        if ( nodes[i].Name.find( "WHEEL_" ) != std::string::npos && nodes[i].Parent >= 0 &&
             nodes[nodes[i].Parent].Name == "D_SBOX" ) {
            wheelShadowNodes.push_back( static_cast<int32_t>( i ) );
        }
    }
    rimDiameterFront = desc.DiameterFront;
    tireRadius = desc.TireRadius;
    rimDiameterRear = desc.DiameterRear;
    if ( tire.isLoaded() ) {
        tireNodeFront = tire.getScene().findNode( ( "T_" + std::to_string( desc.DiameterFront ) + "P" ).c_str() );
        tireNodeRear = tire.getScene().findNode( ( "T_" + std::to_string( desc.DiameterRear ) + "P" ).c_str() );
    }

    if ( CmdLine::HasArg( "-dumpcar" ) ) {
        for ( EdenModel *pModel : { &car, &interior } ) {
            if ( !pModel->isLoaded() ) {
                continue;
            }
            const std::vector<SceneNode> &dumpNodes = pModel->getScene().getNodes();
            for ( size_t i = 0; i < dumpNodes.size(); i++ ) {
                const Vector3f t = dumpNodes[i].World.getTranslation();
                const Matrix4x4f &l = dumpNodes[i].Local;
                OTDU_LOG_INFO( "DUMP %s node %zu parent %d '%s' obj %d t (%.3f %.3f %.3f) local [%.3f %.3f %.3f | %.3f "
                               "%.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f]\n",
                               pModel == &car ? "ext" : "int", i, dumpNodes[i].Parent, dumpNodes[i].Name.c_str(),
                               dumpNodes[i].ObjectHashcode != 0, t.x, t.y, t.z, l.m[0][0], l.m[0][1], l.m[0][2],
                               l.m[1][0], l.m[1][1], l.m[1][2], l.m[2][0], l.m[2][1], l.m[2][2], l.m[3][0], l.m[3][1],
                               l.m[3][2] );
            }
            for ( const EdenModel::Item &item : pModel->getItems() ) {
                const EdenMaterial &material = pModel->getMaterials()[item.MaterialIndex];
                std::string palette;
                for ( uint32_t bone : item.BonePalette ) {
                    palette += std::to_string( bone ) + ",";
                }
                OTDU_LOG_INFO( "DUMP %s item node %d '%s' mat '%s' prog %s cull %d blend %d/%d at %d bounds (%.3f %.3f "
                               "%.3f)-(%.3f %.3f %.3f) pal [%s]\n",
                               pModel == &car ? "ext" : "int", item.NodeIndex,
                               pModel->getScene().getNodes()[item.NodeIndex].Name.c_str(), material.Name.c_str(),
                               material.pProgram != nullptr ? material.pProgram->Name.c_str() : "-", material.CullMode,
                               material.SrcBlend, material.DstBlend, material.bAlphaTest, item.BoundsMin.x,
                               item.BoundsMin.y, item.BoundsMin.z, item.BoundsMax.x, item.BoundsMax.y,
                               item.BoundsMax.z, palette.c_str() );
            }
        }
    }
    if ( Logger::IsVerbose() ) {
        for ( const EdenModel::Item &item : car.getItems() ) {
            if ( car.getMaterials()[item.MaterialIndex].ShaderSetGUID != 0x132435465768798aull ) {
                continue;
            }
            const Matrix4x4f &m = car.getNodeTransform( item.NodeIndex );
            const float det = m.m[0][0] * ( m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1] ) -
                              m.m[0][1] * ( m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0] ) +
                              m.m[0][2] * ( m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0] );
            OTDU_LOG_DEBUG( "  det %.3f\n", det );
            const Vector3f a = m.transformPoint( item.BoundsMin ), b = m.transformPoint( item.BoundsMax );
            OTDU_LOG_DEBUG( "shadow box node %d '%s' car space (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n", item.NodeIndex,
                            car.getScene().getNodes()[item.NodeIndex].Name.c_str(), a.x, a.y, a.z, b.x, b.y, b.z );
        }
        for ( const EdenModel::Item &item : rim.getItems() ) {
            const SceneNode &node = rim.getScene().getNodes()[item.NodeIndex];
            const Vector3f t = node.World.getTranslation();
            OTDU_LOG_DEBUG( "rim item node %d '%s' (%.3f %.3f %.3f) scale %.2f bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f) '%s'\n",
                            item.NodeIndex, node.Name.c_str(), t.x, t.y, t.z, node.World.m[1][1], item.BoundsMin.x,
                            item.BoundsMin.y, item.BoundsMin.z, item.BoundsMax.x, item.BoundsMax.y, item.BoundsMax.z,
                            rim.getMaterials()[item.MaterialIndex].Name.c_str() );
        }
        for ( EdenModel *pModel : { &car, &rim, &tire } ) {
            for ( const EdenMaterial &material : pModel->getMaterials() ) {
                std::string states;
                for ( uint32_t i = 0; i < 16; i++ ) {
                    char buffer[4];
                    snprintf( buffer, sizeof( buffer ), "%02x", material.States[i] );
                    states += buffer;
                }
                OTDU_LOG_DEBUG( "car material '%s' -> %s states %s alphatest %d cull %d blend %d/%d\n",
                                material.Name.c_str(),
                                material.pProgram != nullptr ? material.pProgram->Name.c_str() : "(none)",
                                states.c_str(), material.bAlphaTest, material.CullMode, material.SrcBlend,
                                material.DstBlend );
                for ( uint32_t i = 0; i < material.NumSlots; i++ ) {
                    OTDU_LOG_DEBUG( "  '%s' slot %u %s = %s (%s)\n", material.Name.c_str(), i,
                                    IdentifierToString( material.SlotTypes[i] ).c_str(),
                                    IdentifierToString( material.TextureHashes[i] ).c_str(),
                                    material.pTextures[i] != nullptr ? "ok" : "missing" );
                }
                for ( const auto &parameter : material.Parameters ) {
                    OTDU_LOG_DEBUG( "  '%s' param %s = %.3f %.3f %.3f %.3f\n", material.Name.c_str(),
                                    IdentifierToString( parameter.first ).c_str(), parameter.second.x,
                                    parameter.second.y, parameter.second.z, parameter.second.w );
                }
            }
        }
    }
    return true;
}

void CinematicCar::setDoorAngle( int32_t side, float angle ) {
    DoorNodes &door = doors[side & 1];
    doorAngle[side & 1] = angle;
    if ( door.Door < 0 ) {
        return;
    }
    // FUN_00ddbcd0(angle): rotation about the node Y axis (matrix entries
    // [0][0], [0][2], [2][0], [2][2]; the sin / cos helpers are encrypted
    // [ENC]); applied to the door node frame [INF: post-multiplied local]. A
    // negative angle swings the left door outwards (+X is the car's left).
    const Matrix4x4f rotation = Matrix4x4f::FromQuaternion(
        Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), angle ) );
    const Matrix4x4f delta = door.DoorRest * rotation * door.DoorRest.inverse();
    for ( const auto &rest : door.CarRest ) {
        car.setNodeTransform( rest.first, delta * rest.second );
    }
    for ( const auto &rest : door.InteriorRest ) {
        interior.setNodeTransform( rest.first, delta * rest.second );
    }
    // The window of the door follows it at its current lowering.
    applyWindow( side & 1 );
}

void CinematicCar::setHardtopTime( float time ) {
    if ( !bHardtop ) {
        return;
    }
    time = Clamp( time, 0.0f, hardtopAnimation.getDuration() );
    if ( time == hardtopTime && !hardtopSkin.empty() ) {
        return;
    }
    // FUN_00c4a390: the clip sampled at t into the HANIM_S palette (car
    // +0xae50) when t changed [CODE].
    hardtopTime = time;
    std::vector<BoneTransform> locals( hardtopSkeleton.getNumBones() );
    for ( uint32_t bone = 0; bone < hardtopSkeleton.getNumBones(); bone++ ) {
        locals[bone] = hardtopSkeleton.getBindLocal( bone );
    }
    hardtopAnimation.sample( hardtopSkeleton, hardtopTime, false, locals );
    std::vector<Matrix4x4f> boneWorld;
    hardtopSkeleton.computeSkinMatrices( locals, boneWorld, hardtopSkin );
}

void CinematicCar::setWindowLowering( int32_t side, float value ) {
    // FUN_00c66150 / FUN_00c66110: clamp to [0, 1] (DAT_00edf668 = 1.0),
    // dirty flag car +0xa693 -> FUN_00c674a0 places the window [CODE].
    windowLowering[side & 1] = Clamp( value, 0.0f, 1.0f );
    applyWindow( side & 1 );
}

void CinematicCar::applyWindow( int32_t side ) {
    const WindowNodes &window = windows[side];
    if ( window.Window < 0 ) {
        return;
    }
    // FUN_00c674a0: FUN_00451560(key poses, value) [ENC] between WND_x1 (0)
    // and WND_x2 (1): taken as a linear blend of the two poses [INF] (exact
    // for a pure translation; the California WND_L2 is 0.39 m lower than
    // WND_L1 and tilted by ~16 degrees about Z [DATA], docs/re/accessories_spec.md 5).
    const float f = windowLowering[side];
    Matrix4x4f pose = window.Up;
    for ( int32_t row = 0; row < 4; row++ ) {
        for ( int32_t column = 0; column < 4; column++ ) {
            pose.m[row][column] = window.Up.m[row][column] + ( window.Down.m[row][column] - window.Up.m[row][column] ) * f;
        }
    }
    // Window nodes live under DOOR_x: the door rotation applies on top.
    const DoorNodes &door = doors[side];
    if ( door.Door >= 0 && doorAngle[side] != 0.0f && car.getScene().isDescendantOf( window.Window, door.Door ) ) {
        const Matrix4x4f rotation = Matrix4x4f::FromQuaternion(
            Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), doorAngle[side] ) );
        pose = door.DoorRest * rotation * door.DoorRest.inverse() * pose;
    }
    car.setNodeTransform( window.Window, pose );
}

bool CinematicCar::FindDatabaseDesc( int32_t idCar, uint64_t rimsHashcode, Desc &outDesc ) {
    if ( gpDatabase == nullptr ) {
        return false;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    bool bFound = false;
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ) && !bFound; i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( car["_id_car"].asInt( -2 ) == idCar ) {
            outDesc.FileName = car["_car_file_name"].asString();
            if ( rimsHashcode == 0 ) {
                rimsHashcode = car["_default_rims"].asUInt64();
            }
            bFound = true;
        }
    }
    if ( !bFound ) {
        return false;
    }
    for ( uint32_t i = 0; i < root.getArraySize( "DBRims" ); i++ ) {
        XmbValue rim = root.getArrayElement( "DBRims", i );
        if ( rim["_hashcode"].asUInt64() != rimsHashcode ) {
            continue;
        }
        const std::string path = ToLower( rim["_rsc_path"].asString() );
        outDesc.RimFront = path + "/" + ToLower( rim["_rsc_file_name_front"].asString() );
        outDesc.RimRear = path + "/" + ToLower( rim["_rsc_file_name_rear"].asString() );
        outDesc.DiameterFront = static_cast<int32_t>( rim["_diameter_front"].asInt( 18 ) );
        outDesc.DiameterRear = static_cast<int32_t>( rim["_diameter_rear"].asInt( 18 ) );
        outDesc.TireRadius = outDesc.DiameterFront * 0.0254f * 0.5f +
                             rim["_largeur_front"].asFloat( 245.0f ) * rim["_hauteur_front"].asFloat( 40.0f ) * 1e-5f;
        return true;
    }
    return true;
}

bool CinematicCar::applyColor( const std::string &colorId ) {
    if ( colorId.empty() || colorId == "##" || !car.isLoaded() ) {
        return false;
    }
    const BankEntry *pLibrary = nullptr;
    for ( const BankEntry *pEntry : carBank.findEntriesByExtension( "2dm" ) ) {
        std::string name = ToLower( pEntry->Name );
        const size_t dot = name.rfind( '.' );
        if ( dot != std::string::npos ) {
            name = name.substr( 0, dot );
        }
        if ( name.size() > 6 && name.compare( name.size() - 6, 6, "_color" ) == 0 ) {
            pLibrary = pEntry;
        }
    }
    Render2DM colors;
    if ( pLibrary == nullptr || !colors.parse( pLibrary->pData, pLibrary->Size ) ) {
        OTDU_LOG_WARN( "CinematicCar: %s: no colour library (%s)\n", fileName.c_str(),
                       pLibrary != nullptr ? pLibrary->Name.c_str() : "none" );
        return false;
    }
    const MaterialDesc *pColor = colors.find( GetIdentifier64bit( colorId.c_str() ) );
    if ( pColor == nullptr ) {
        OTDU_LOG_WARN( "CinematicCar: %s: colour %s not in %s (%zu materials)\n", fileName.c_str(), colorId.c_str(),
                       pLibrary->Name.c_str(), colors.getMaterials().size() );
        return false;
    }
    static const char *const kPaintParameters[] = { "MATPARAM",  "SPECPARM",  "SPECBLINN", "FLAKEABNS",
                                                    "RAMPPOW",   "RAMPCLR1",  "RAMPCLR2",  "RAMPCLR3",
                                                    "RAMPFLAKE", "BLINNCLRA", "BLINNCLRB" };
    static const char *const kLowParameters[] = { "RAMPCLR1", "RAMPCLR2" };
    bool bApplied = false;
    for ( EdenMaterial &material : car.getMaterials() ) {
        if ( StringCmpI( material.Name.c_str(), "Paint" ) == 0 ) {
            for ( const char *pName : kPaintParameters ) {
                auto it = pColor->Parameters.find( GetIdentifier64bit( pName ) );
                if ( it != pColor->Parameters.end() ) {
                    material.Parameters[it->first] = it->second;
                }
            }
            material.Diffuse = pColor->DiffuseColor;
            material.Ambient = pColor->AmbientColor;
            material.Specular = pColor->SpecularColor;
            bApplied = true;
        } else if ( StringCmpI( material.Name.c_str(), "Paint_LR" ) == 0 ) {
            for ( const char *pName : kLowParameters ) {
                auto it = pColor->Parameters.find( GetIdentifier64bit( pName ) );
                if ( it != pColor->Parameters.end() ) {
                    material.Parameters[it->first] = it->second;
                }
            }
            auto it = pColor->Parameters.find( GetIdentifier64bit( "RAMPPOW" ) );
            auto target = material.Parameters.find( GetIdentifier64bit( "RAMPPOW" ) );
            if ( it != pColor->Parameters.end() && target != material.Parameters.end() ) {
                target->second.x = it->second.x;
                target->second.y = it->second.y;
            }
        }
    }
    return bApplied;
}

int32_t CinematicCar::getIdCar() const {
    if ( gpDatabase == nullptr || fileName.empty() ) {
        return -1;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue record = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( record["_car_file_name"].asString().c_str(), fileName.c_str() ) == 0 ) {
            return static_cast<int32_t>( record["_id_car"].asInt( -1 ) );
        }
    }
    return -1;
}

bool CinematicCar::getNodeModelTransform( const char *pName, Matrix4x4f &outTransform ) const {
    if ( !car.isLoaded() ) {
        return false;
    }
    int32_t node = car.getScene().findNode( pName );
    if ( node < 0 ) {
        node = FindNodeByHash( car.getScene(), pName );
    }
    if ( node < 0 ) {
        return false;
    }
    outTransform = car.getNodeTransform( node );
    return true;
}

bool CinematicCar::getBodyBounds( Vector3f &outMin, Vector3f &outMax ) const {
    if ( !car.isLoaded() ) {
        return false;
    }
    const int32_t body = car.getScene().findNode( "BODY_HR" );
    return body >= 0 && car.getNodeBounds( body, outMin, outMax );
}

Matrix4x4f CinematicCar::fitOnGround( Vector3f position, const Quaternionf &rotation,
                                      const SurfaceFunction &surfaceHeight ) const {
    const Matrix4x4f cinematicRotation = EdenRotationFromQuaternion( rotation );
    Matrix4x4f tilt = Matrix4x4f::Identity();
    // The cinematic gives the car pose above the road (heading only): in
    // game the physics drops it on its wheels. Rest the 4 tires on the road
    // surface (sector road meshes or heightfield): plane through the hub
    // targets (surface + tire radius), car up = plane normal.
    if ( wheelNodes.size() >= 3 && !CmdLine::HasArg( "-nocarground" ) ) {
        const Matrix4x4f pose = Matrix4x4f::Translation( position ) * cinematicRotation;
        std::vector<Vector3f> targets;
        float hubHeight = 0.0f;
        for ( int32_t node : wheelNodes ) {
            const Vector3f hubLocal = car.getNodeTransform( node ).getTranslation();
            const Vector3f hub = pose.transformPoint( hubLocal );
            float surface = 0.0f;
            if ( surfaceHeight( hub.x, hub.z, hub.y + 1.5f, surface ) ) {
                targets.push_back( Vector3f( hub.x - position.x, surface + tireRadius, hub.z - position.z ) );
                hubHeight += hubLocal.y;
            }
        }
        if ( targets.size() >= 3 ) {
            hubHeight /= static_cast<float>( targets.size() );
            // Least squares y = a x + b z + c.
            double sxx = 0, sxz = 0, szz = 0, sx = 0, sz = 0, sy = 0, sxy = 0, szy = 0;
            for ( const Vector3f &t : targets ) {
                sxx += t.x * t.x;
                sxz += t.x * t.z;
                szz += t.z * t.z;
                sx += t.x;
                sz += t.z;
                sy += t.y;
                sxy += t.x * t.y;
                szy += t.z * t.y;
            }
            const double n = static_cast<double>( targets.size() );
            // Normal equations [sxx sxz sx; sxz szz sz; sx sz n] (a b c) = (sxy szy sy)
            const double m[3][3] = { { sxx, sxz, sx }, { sxz, szz, sz }, { sx, sz, n } };
            const double r[3] = { sxy, szy, sy };
            const double det = m[0][0] * ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) -
                               m[0][1] * ( m[1][0] * m[2][2] - m[1][2] * m[2][0] ) +
                               m[0][2] * ( m[1][0] * m[2][1] - m[1][1] * m[2][0] );
            if ( std::abs( det ) > 1e-9 ) {
                auto solve = [&]( int column ) {
                    double c[3][3];
                    for ( int i = 0; i < 3; i++ ) {
                        for ( int j = 0; j < 3; j++ ) {
                            c[i][j] = j == column ? r[i] : m[i][j];
                        }
                    }
                    return ( c[0][0] * ( c[1][1] * c[2][2] - c[1][2] * c[2][1] ) -
                             c[0][1] * ( c[1][0] * c[2][2] - c[1][2] * c[2][0] ) +
                             c[0][2] * ( c[1][0] * c[2][1] - c[1][1] * c[2][0] ) ) /
                           det;
                };
                const float slopeX = static_cast<float>( solve( 0 ) );
                const float slopeZ = static_cast<float>( solve( 1 ) );
                const float height = static_cast<float>( solve( 2 ) );
                const Vector3f normal = Vector3f( -slopeX, 1.0f, -slopeZ ).normalized();
                const Vector3f axis = Vector3f( 0.0f, 1.0f, 0.0f ).cross( normal );
                const float sine = axis.length();
                if ( sine > 1e-6f ) {
                    tilt = Matrix4x4f::FromQuaternion(
                        Quaternionf::FromAxisAngle( axis * ( 1.0f / sine ), std::atan2( sine, normal.y ) ) );
                }
                // Hub plane at the car centre, car origin hubHeight below it.
                position.y = height;
                position = position - normal * hubHeight;
            }
        }
    }
    if ( CmdLine::HasArg( "-logcar" ) ) {
        const Matrix4x4f world = Matrix4x4f::Translation( position ) * tilt * cinematicRotation;
        for ( int32_t node : wheelNodes ) {
            const Vector3f hub = world.transformPoint( car.getNodeTransform( node ).getTranslation() );
            float surface = -1000.0f;
            surfaceHeight( hub.x, hub.z, hub.y + 1.5f, surface );
            OTDU_LOG_INFO( "wheel %d hub (%.3f %.3f %.3f) bottom %.3f surface %.3f\n", node, hub.x, hub.y, hub.z,
                           hub.y - tireRadius, surface );
        }
    }
    static const bool bLogCar = CmdLine::HasArg( "-logcar" );
    if ( bLogCar ) {
        OTDU_LOG_INFO( "Car pos (%.3f %.3f %.3f) q (%.4f %.4f %.4f %.4f)\n", position.x, position.y, position.z,
                       rotation.x, rotation.y, rotation.z, rotation.w );
    }
    return Matrix4x4f::Translation( position ) * tilt * cinematicRotation;
}

void CinematicCar::draw( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather,
                         bool bEnvironmentAssigned ) {
    if ( !car.isLoaded() ) {
        return;
    }
    // Car lighting factors of the weather (EnvCoeff, in the car shaders:
    // x: reflection (x MatParam.x), y: specular, z / w: fresnel coefficient /
    // power (x MatParam.zw)). The static cubemap of common_car.bnk stands in
    // for the dynamic one.
    const Vector4f fresnel = pWeather != nullptr ? pWeather->getVector4( "car_fresnel_coeff_power_boost_specular",
                                                                         Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ) )
                                                 : Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    auto factor = [pWeather]( const char *pName, float defaultValue ) {
        return pWeather != nullptr ? pWeather->getFloat( pName, defaultValue ) : defaultValue;
    };
    const float specular = fresnel.w * factor( "car_sun_specular_factor", 1.0f );
    const float staticFactor = bEnvironmentAssigned ? 1.0f : factor( "car_envmap_factor_static_cubemap", 0.7f );
    const Vector4f envCoeff( factor( "car_envmap_factor", 1.0f ) * staticFactor, specular, fresnel.x, fresnel.y );
    const Vector4f rimEnvCoeff( factor( "car_rim_envmap_factor", 1.0f ) * staticFactor, specular, fresnel.x,
                                fresnel.y );
    static const uint64_t kEnvCoeff = GetIdentifier64bit( "ENVCOEFF" );
    for ( EdenModel *pModel : { &car, &rim, &rimRear, &tire } ) {
        for ( EdenMaterial &material : pModel->getMaterials() ) {
            auto it = material.Parameters.find( kEnvCoeff );
            if ( it != material.Parameters.end() ) {
                it->second = ( pModel == &rim || pModel == &rimRear ) ? rimEnvCoeff : envCoeff;
            }
        }
    }

    for ( size_t i = 0; i < wheelShadowNodes.size() && i < wheelNodes.size(); i++ ) {
        const Vector3f hub = car.getNodeTransform( wheelNodes[i] ).getTranslation();
        const Matrix4x4f box = Matrix4x4f::Translation( Vector3f( hub.x, hub.y - tireRadius, hub.z ) ) *
                               Matrix4x4f::Scale( Vector3f( 0.25f, 0.12f, 0.45f ) );
        car.setNodeTransform( wheelShadowNodes[i], box );
    }
    // Car level of detail (FUN_00c4e630 / FUN_00c4dcb0): distance from the
    // camera x camera factor (+0x17c, taken as 1 [INF]) against the table
    // 0x00f7e788 {250, 125, 15, 5, 0}: below 5 m LOD 4 (interior shown).
    // LODs 0..2 are not used here (high resolution kept beyond 15 m).
    // The level is chosen once per frame for the view camera: the shadow
    // caster and reflection passes keep the last one.
    bool bInterior = bLastInterior;
    if ( !renderer.isShadowCasterMode() && !renderer.isReflectionPass() ) {
        const float distance = ( world.getTranslation() - renderer.getSceneParams().CameraPosition ).length();
        static const int32_t forcedLod = CmdLine::GetArgInt( "-carlod", -1 );
        bInterior = interior.isLoaded() && !interiorDrawnNodes.empty() &&
                    ( forcedLod >= 0 ? forcedLod == 4 : distance < 5.0f );
        static const bool bLogLod = CmdLine::HasArg( "-logcar" );
        if ( bLogLod && bInterior != bLastInterior ) {
            OTDU_LOG_INFO( "CinematicCar: camera %.2f m, LOD %d\n", distance, bInterior ? 4 : 3 );
        }
        bLastInterior = bInterior;
    }
    for ( int32_t node : cockpitLowNodes ) {
        car.setNodeVisible( node, !bInterior );
    }
    applyLights( pWeather );
    if ( bHardtop ) {
        car.drawSkinned( renderer, world, hardtopSkin );
    } else {
        car.draw( renderer, world );
    }
    if ( bInterior ) {
        interior.draw( renderer, world );
    }
    drawBeams( renderer, world, pWeather );
    for ( int32_t node : wheelNodes ) {
        // D_RIM_xx: rim size (scale) and side (right wheels turned by 180
        // degrees); the next node (?_TIRE_xx) places the tire.
        const bool bRear = car.getScene().getNodes()[node].Name.find( "_R" ) != std::string::npos;
        const EdenModel &wheelRim = ( bRear && rimRear.isLoaded() ) ? rimRear : rim;
        static const bool bNoRim = CmdLine::HasArg( "-norim" );
        if ( wheelRim.isLoaded() && !bNoRim ) {
            // Rim meshes are normalized (radius 0.212 m). When the car scene
            // does not carry the rim radius (D_RIM_xx scale 1), it comes from
            // the database diameter (+ 2.2 cm of lip, the California bakes
            // 1.24 for its 19" rims).
            Matrix4x4f rimTransform = car.getNodeTransform( node );
            const Vector3f up( rimTransform.m[1][0], rimTransform.m[1][1], rimTransform.m[1][2] );
            float radialScale = 1.0f;
            if ( std::abs( up.length() - 1.0f ) < 0.01f ) {
                const int32_t diameter = bRear ? rimDiameterRear : rimDiameterFront;
                radialScale = ( diameter * 0.0254f * 0.5f + 0.0217f ) / 0.212f;
            }
            rimTransform = rimTransform * Matrix4x4f::Scale( Vector3f( 1.0f, radialScale, radialScale ) );
            wheelRim.draw( renderer, world * rimTransform );
        }
        const int32_t tireNode = bRear ? tireNodeRear : tireNodeFront;
        static const bool bNoTire = CmdLine::HasArg( "-notire" );
        if ( tireNode >= 0 && !bNoTire ) {
            tire.drawNode( renderer, tireNode, world * car.getNodeTransform( node + 1 ) );
        }
    }
}

// ---------------------------------------------------------------------------
// Car lights (docs/re/car_lights_spec.md).

namespace {
// Light material names (table 0x00f7e700, 9 x 8 bytes, compared with the
// material name hashcodes by FUN_00c5ee20): group k -> the vector at car
// +0x783c + k * 0xc of (material, GLOSSFCT) pairs [CODE + DATA].
struct LightMaterialName {
    uint64_t Hashcode;
    bool bLeft;
    bool bFront;
};
constexpr uint64_t Bytes8( const char ( &s )[9] ) {
    uint64_t v = 0;
    for ( int i = 7; i >= 0; i-- ) {
        v = ( v << 8 ) | static_cast<uint8_t>( s[i] );
    }
    return v;
}
// Side / front from the names (L / R = left / right, F = front) [INF];
// LGT_R_S (group 6) taken as a right rear lamp [INF].
const LightMaterialName kLightMaterials[9] = {
    { Bytes8( "LGT_FL\0\0" ), true, true },     { Bytes8( "LGT_RL\0\0" ), true, false },
    { Bytes8( "\x97GT_RL_S" ), true, false },   { Bytes8( "LGT_FR\0\0" ), false, true },
    { Bytes8( "LGT_RR\0\0" ), false, false },   { Bytes8( "\x97GT_RR_S" ), false, false },
    { Bytes8( "LGT_R_S\0" ), false, false },    { Bytes8( "LGT_L\0\0\0" ), true, false },
    { Bytes8( "LGT_R\0\0\0" ), false, false },
};
} // namespace

void CinematicCar::collectLights() {
    lightMaterials.clear();
    causticNodes.clear();
    flareNodes.clear();
    for ( int32_t i = 0; i < 3; i++ ) {
        lightBoxNodes[i] = -1;
        lightBoxMaterials[i].clear();
    }
    if ( !car.isLoaded() ) {
        return;
    }
    std::vector<EdenMaterial> &materials = car.getMaterials();
    static const uint64_t kGlossFct = GetIdentifier64bit( "GLOSSFCT" );
    for ( uint32_t m = 0; m < materials.size(); m++ ) {
        const uint64_t hashcode = GetIdentifier64bit( materials[m].Name.c_str() );
        for ( const LightMaterialName &entry : kLightMaterials ) {
            if ( entry.Hashcode == hashcode && materials[m].Parameters.count( kGlossFct ) != 0 ) {
                lightMaterials.push_back( { m, entry.bLeft, entry.bFront } );
            }
        }
    }
    const Render3DD &scene = car.getScene();
    const std::vector<SceneNode> &nodes = scene.getNodes();
    // Lamp nodes (FUN_00c5f060, once for all cars: hashcodes of
    // "Fa_%c_FL" / "Fa_%c_FR" / "Fa_%c_RL" / "Fa_%c_RR" and the same with
    // "%d" = 1..5, for %c in "LBSWR" (string 0x00ef5fb8), each also with "_C"
    // appended; then per car the nodes of those names: car +0x1f84 /
    // +0x1ffc / +0x2074 / +0x20ec (RL, RR, FL, FR, flare sprites) and
    // +0x232c / +0x23a4 / +0x241c / +0x2494 (the "_C" glow meshes) [CODE]).
    // The California only has the "_C" nodes [DATA]: flare sprites
    // FA_<c>_<pos>[n] (TD_Sprite FRL_<x> materials) exist on 6 of the 133
    // car banks (ac_427, charger_srt8_police, desmosedici, diavel, fatboy,
    // viper_srt10_police).
    static const char *const kPositions[4] = { "FL", "FR", "RL", "RR" };
    std::unordered_map<uint64_t, LightNode> lampNames;
    for ( const char *pType = "LBSWR"; *pType != '\0'; pType++ ) {
        for ( int32_t p = 0; p < 4; p++ ) {
            for ( int32_t n = 0; n < 6; n++ ) {
                char name[32];
                if ( n == 0 ) {
                    snprintf( name, sizeof( name ), "Fa_%c_%s", *pType, kPositions[p] );
                } else {
                    snprintf( name, sizeof( name ), "Fa_%c_%s%d", *pType, kPositions[p], n );
                }
                LightNode light;
                light.Function = *pType;
                light.bLeft = kPositions[p][1] == 'L';
                light.bFront = kPositions[p][0] == 'F';
                light.bFlare = true;
                lampNames[GetIdentifier64bit( name )] = light;
                strncat( name, "_C", sizeof( name ) - strlen( name ) - 1 );
                light.bFlare = false;
                lampNames[GetIdentifier64bit( name )] = light;
            }
        }
    }
    for ( size_t i = 0; i < nodes.size(); i++ ) {
        const std::string &name = nodes[i].Name;
        auto lamp = lampNames.find( nodes[i].Hashcode );
        if ( lamp != lampNames.end() ) {
            LightNode light = lamp->second;
            light.Node = static_cast<int32_t>( i );
            ( light.bFlare ? flareNodes : causticNodes ).push_back( light );
        }
        // Car node slots 13 BREAKING, 14 REVERSING, 15 HEADLIGHT (names
        // 0x00edef08) [CODE + DATA].
        const int32_t box = name.find( "BREAKI" ) != std::string::npos  ? 0
                            : name.find( "EVERS" ) != std::string::npos ? 1
                            : name.find( "EADLI" ) != std::string::npos ? 2
                                                                         : -1;
        if ( box >= 0 && nodes[i].Parent == 0 ) {
            lightBoxNodes[box] = static_cast<int32_t>( i );
        }
    }
    static const uint64_t kIntensity = GetIdentifier64bit( "INTENSITY" );
    for ( const EdenModel::Item &item : car.getItems() ) {
        for ( int32_t box = 0; box < 3; box++ ) {
            if ( item.NodeIndex == lightBoxNodes[box] && materials[item.MaterialIndex].Parameters.count( kIntensity ) != 0 ) {
                lightBoxMaterials[box].push_back( item.MaterialIndex );
            }
        }
    }
    OTDU_LOG_INFO( "CinematicCar: %zu light materials, %zu lamp glow nodes, %zu flare nodes, light boxes %d %d %d\n",
                   lightMaterials.size(), causticNodes.size(), flareNodes.size(), lightBoxNodes[0], lightBoxNodes[1],
                   lightBoxNodes[2] );
}

void CinematicCar::updateLights( float dt ) {
    // Test switch: -indicator left|right|hazard overrides the state.
    static const char *pForced = CmdLine::GetArgValue( "-indicator" );
    if ( pForced != nullptr ) {
        indicator = StringCmpI( pForced, "left" ) == 0    ? eIndicator::Left
                    : StringCmpI( pForced, "right" ) == 0 ? eIndicator::Right
                    : StringCmpI( pForced, "hazard" ) == 0 ? eIndicator::Hazard
                                                           : eIndicator::Off;
    }
    static const int32_t forcedLights = CmdLine::GetArgInt( "-carlights", -2 );
    if ( forcedLights >= -1 ) {
        headlightMode = forcedLights;
    }
    const int32_t mode = headlightMode < 0 ? 1 : headlightMode;
    const bool bLeft = indicator == eIndicator::Left || indicator == eIndicator::Hazard;
    const bool bRight = indicator == eIndicator::Right || indicator == eIndicator::Hazard;
    // FUN_00c57ef0(dt, value), value = 1 (0 in light mode 0) [CODE]:
    // timer +0x1f10 = -1 while no blinker; 0 when one starts; then
    // t = fmod(t + dt, 1.0) (0xe2dc12, period QWORD 0x00f44c70 = 1.0), the
    // lamps lit while t < 0.6 (0x00ef2754); left / right value kept only for
    // the active side(s) (+0x1db / +0x1dc).
    const float value = mode == 0 ? 0.0f : 1.0f;
    if ( indicatorTimer < 0.0f ) {
        if ( bLeft || bRight ) {
            indicatorTimer = 0.0f;
        }
    } else if ( !bLeft && !bRight ) {
        indicatorTimer = -1.0f;
    }
    float left = value, right = value;
    if ( indicatorTimer < 0.0f ) {
        left = right = 0.0f;
    } else {
        indicatorTimer = std::fmod( indicatorTimer + dt, 1.0f );
        if ( indicatorTimer >= 0.6f ) {
            left = right = 0.0f;
        }
        if ( !bLeft ) {
            left = 0.0f;
        }
        if ( !bRight ) {
            right = 0.0f;
        }
    }
    indicatorValue[0] = left;
    indicatorValue[1] = right;
    bIndicatorLamp = left > 0.0f || right > 0.0f;
    // High beam (mode 2): timer +0x1f24 += dt, the position light function
    // gets the flare value after 0.2 s (0x00f49b58); reset in mode 1 [CODE].
    if ( mode == 1 ) {
        highBeamTimer = 0.0f;
    } else {
        highBeamTimer += dt;
    }
}

void CinematicCar::setTunnelDistance( float distance, float dt ) {
    // FUN_00c62a70 [CODE]: car +0x2acc = distance to the nearest tunnel end
    // (FUN_00c41d20, 0 outside tunnels or when the camera is 100 m or more
    // from the car), +0x2ad4 = clamp((d - 10) / 10, 0, 1) (QWORD 0x00f49820
    // = 10.0), +0x2ad0 = k x +0x2ad0 + (1 - k) x +0x2ad4 with k =
    // exp(log(0.3) x dt / (1 / 30)) (0x00f4a178 = 0.3, 0x00f4a270 =
    // 0.0333; 0xe2dc0c log, 0xe2db8a exp). The light values use the tunnel_
    // weather keys when +0x2acc >= 5 (0x00edf65c, FUN_00c573c0...); +0x2ad0
    // blends the car lighting toward tunnel_light_* (FUN_00c4df00).
    tunnelDistance = distance;
    float target = ( distance - 10.0f ) / 10.0f;
    target = std::min( std::max( target, 0.0f ), 1.0f );
    const float k = std::exp( std::log( 0.3f ) * dt / 0.033333335f );
    tunnelBlend = k * tunnelBlend + ( 1.0f - k ) * target;
    static const int32_t forcedTunnel = CmdLine::GetArgInt( "-tunnel", -1 );
    bInTunnel = forcedTunnel >= 0 ? forcedTunnel != 0 : distance >= 5.0f;
}

void CinematicCar::applyLights( const WeatherPreset *pWeather ) {
    auto weather = [pWeather, this]( const char *pName ) {
        // FUN_00c4e710: the car_* values, tunnel_car_* when car +0x2acc >= 5.
        if ( pWeather == nullptr ) {
            return 0.0f;
        }
        return pWeather->getFloat( ( std::string( bInTunnel ? "tunnel_" : "" ) + pName ).c_str(), 0.0f );
    };
    const int32_t mode = headlightMode < 0 ? 1 : headlightMode;
    // FUN_00c588d0 [CODE]: flare = car_headlight_flare_intensity (0 when
    // < 0.05); headlight function (table +0x60): mode 2 flare + 1, mode 1
    // flare, mode 0 0; position function (+0x68): car_poslight_intensity
    // (0 when < 0.05), the raw flare in mode 2 after 0.2 s, 0 in mode 0;
    // brake (+0x00) = +0x1d0, reverse (+0x38) = +0x1d1 (FUN_00c63a30).
    const float rawFlare = weather( "car_headlight_flare_intensity" );
    const float flare = rawFlare < 0.05f ? 0.0f : rawFlare;
    const float poslight = weather( "car_poslight_intensity" );
    float headlightValue = mode == 2 ? flare + 1.0f : mode == 1 ? flare : 0.0f;
    float positionValue = poslight < 0.05f ? 0.0f : poslight;
    if ( mode == 2 && highBeamTimer > 0.2f ) {
        positionValue = rawFlare;
    }
    if ( mode == 0 ) {
        headlightValue = positionValue = 0.0f;
    }
    // Headlight slot (FUN_00921260 / FUN_00923230 [CODE]): +0x10 =
    // car_headlights_intensity (tunnel_ variant, FUN_00c57450), used clamped
    // to [0, 1] and squared; lamps lit = the headlight function.
    beamHeadlightIntensity = mode == 0 ? 0.0f : weather( "car_headlights_intensity" );
    bBeamLampsOn = headlightValue > 0.0f;
    const float brakeValue = bBraking ? 1.0f : 0.0f;
    const float reverseValue = bReverse ? 1.0f : 0.0f;
    // GLOSSFCT (c17 of the TD_CarPaint_NM light shaders): emissive =
    // TEXLIGHT x dot(GLOSSLIGHT (r, g, b, 1 - a), GLOSSFCT), diffuse x
    // prod(1 - GLOSSFCT.i x mask.i) [SHADER]. The functions that write it are
    // encrypted (table 0x01230500, FUN_00e33810 -> 0x452c00...) [ENC]; the
    // channels follow the TEXLIGHT colour under each GLOSSLIGHT mask
    // (california_lgt_g / lgt_on: r red, g white, b headlamp + rear ring,
    // 1 - a amber) [DATA + INF]: x brake, y reverse, z position (front:
    // max with the headlights), w blinker of the material side.
    static const uint64_t kGlossFct = GetIdentifier64bit( "GLOSSFCT" );
    std::vector<EdenMaterial> &materials = car.getMaterials();
    for ( const LightMaterial &light : lightMaterials ) {
        auto it = materials[light.Material].Parameters.find( kGlossFct );
        if ( it == materials[light.Material].Parameters.end() ) {
            continue;
        }
        it->second = Vector4f( light.bFront ? 0.0f : brakeValue, light.bFront ? 0.0f : reverseValue,
                               light.bFront ? std::max( positionValue, headlightValue ) : positionValue,
                               indicatorValue[light.bLeft ? 0 : 1] );
    }
    // Lamp glow meshes "Fa_<c>_<pos>_C" (additive TD_CarPaint ps3) and flare
    // sprites "Fa_<c>_<pos>[n]" (TD_Sprite): shown with their function
    // [INF: the node flags (4 = hidden) are written by the encrypted table
    // functions; the letters of "LBSWR" are read as L front lights
    // (headlights), B back (rear position) lights, S stop, W warning
    // (blinker of the node side), R reverse, from the node positions and the
    // flare colours (FA_W orange, FA_S red vertex colour, common_mesh.3dd)].
    for ( const std::vector<LightNode> *pNodes : { &causticNodes, &flareNodes } ) {
        for ( const LightNode &node : *pNodes ) {
            float value = 0.0f;
            switch ( node.Function ) {
            case 'L': value = headlightValue; break;
            case 'B': value = positionValue; break;
            case 'S': value = brakeValue; break;
            case 'R': value = reverseValue; break;
            case 'W': value = indicatorValue[node.bLeft ? 0 : 1]; break;
            default: break;
            }
            static const bool bNoFlares = CmdLine::HasArg( "-noflares" );
            car.setNodeVisible( node.Node, value > 0.0f && !( node.bFlare && bNoFlares ) );
        }
    }
    // Projected light boxes (TD_ShadowBox ps1, additive: base texture x
    // Intensity). FUN_00c63a30 / FUN_00c638d0 [CODE]: v = car_caustics_intensity
    // (tunnel_), 0 in mode 0, + 1 in mode 2; BREAKING (slot 13) when v > 0.05
    // and +0x1d0, REVERSING (14) when v > 0.05 and +0x1d1, INTENSITY = v;
    // HEADLIGHT (15) INTENSITY = v (x 0.3 for the vehicle kind 1).
    float caustics = weather( "car_caustics_intensity" );
    if ( mode == 0 ) {
        caustics = 0.0f;
    } else if ( mode == 2 ) {
        caustics += 1.0f;
    }
    static const bool bNoLightBoxes = CmdLine::HasArg( "-nolightboxes" );
    const bool bBoxOn[3] = { caustics > 0.05f && bBraking, caustics > 0.05f && bReverse, caustics > 0.0f };
    static const uint64_t kIntensity = GetIdentifier64bit( "INTENSITY" );
    for ( int32_t box = 0; box < 3; box++ ) {
        if ( lightBoxNodes[box] < 0 ) {
            continue;
        }
        car.setNodeVisible( lightBoxNodes[box], bBoxOn[box] && !bNoLightBoxes );
        for ( uint32_t m : lightBoxMaterials[box] ) {
            auto it = materials[m].Parameters.find( kIntensity );
            if ( it != materials[m].Parameters.end() ) {
                it->second = Vector4f( caustics, 0.0f, 0.0f, 0.0f );
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Light beams (docs/re/car_lights_spec.md 7).

void CinematicCar::loadBeams() {
    // FUN_00922d60 -> FUN_00964a30 x 6 per headlight slot [CODE]: the beam
    // model (manager +0xfb20 / +0xfb24 bank ids) whose material is cloned
    // with "_WithZ" (0x00f2d364) and gets the LightBeam_RT target in a
    // sampler; +0x50 = its LightBeamFadeParams (0x938692998cdbdcda). The
    // model: fx/weatherfx.bnk lightbeam_rt.3dd (node LIGHTBEAM_RT..., material
    // LightBeam_RT_01, TD_LightBeam 693b66820e317b71, USERTYPE1/2 = the "RT"
    // permutations: analytic cone ray march against gZpass, s3) [DATA +
    // SHADER] [INF: the bank ids are not read; lightbeam_rt is the only car
    // beam of the data, lightbeam_std the street lamp one].
    if ( beam.isLoaded() || CmdLine::HasArg( "-nobeams" ) ) {
        return;
    }
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const std::string path = gpFile->getBankFolder() + "fx/weatherfx.bnk";
    if ( pRenderer == nullptr || !gpFile->fileExists( path.c_str() ) || !fxBank.load( path.c_str() ) ) {
        return;
    }
    gpRender->getTextureCache().loadBankTextures( fxBank, fxBank.getPath().c_str() );
    if ( !beam.load( *pRenderer, fxBank, "lightbeam_rt", nullptr, fxBank.getPath().c_str() ) ) {
        OTDU_LOG_WARN( "CinematicCar: lightbeam_rt not loaded\n" );
        return;
    }
    for ( EdenMaterial &material : beam.getMaterials() ) {
        // gZpass (s3) is not a .2DM slot: the screen depth (FullscreenDepth
        // encoding, as the soft particles), point sampled.
        if ( material.pProgram != nullptr && ( material.pProgram->PSSamplerMask & 8u ) != 0 && material.NumSlots <= 3 ) {
            material.ScreenDepthSlot = 3;
            material.SamplerIndex[3] = 3;
        }
        OTDU_LOG_INFO( "CinematicCar: beam material '%s' program %s slots %u depth slot %d\n", material.Name.c_str(),
                       material.pProgram != nullptr ? material.pProgram->Name.c_str() : "-", material.NumSlots,
                       material.ScreenDepthSlot );
    }
}

void CinematicCar::drawBeams( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather ) {
    if ( !beam.isLoaded() || renderer.isShadowCasterMode() || renderer.isReflectionPass() || !bBeamLampsOn ) {
        return;
    }
    // FUN_00923230 [CODE]: fade = 1 below manager +0xfaa0 = 400 m from the
    // camera, 1 - (d - 400) / (650 - 400) beyond (+0xfaa4 = 650); I = clamp(
    // slot +0x10, 0, 1); beam value = I x I x fade (local_1c0). FUN_00964b90:
    // LightBeamFadeParams = (400 x 0.18, 1 / ((650 - 400) x 0.18), I x I x
    // fade, car_lightbeam_intensity) (0x00f4b098 = 0.18; weather key
    // 0x96e9fce2f3e5c8eb); blend SRCALPHA / ONE (0x00f90d08 / 0x00f90d0c).
    // Beams are drawn when the manager flag +0x11 (1, ctor FUN_00921d90)
    // and slot +0x2d8 (FUN_0042b1d0 / FUN_00c3f6d0 of the driver, [INF]
    // taken as the player car) are set.
    const float distance = ( world.getTranslation() - renderer.getSceneParams().CameraPosition ).length();
    const float fade = distance < 400.0f ? 1.0f : std::max( 0.0f, 1.0f - ( distance - 400.0f ) / 250.0f );
    const float intensity = std::min( std::max( beamHeadlightIntensity, 0.0f ), 1.0f );
    const float value = intensity * intensity * fade;
    const float weatherBeam = pWeather != nullptr ? pWeather->getFloat( "car_lightbeam_intensity", 0.0f ) : 0.0f;
    static const char *pForcedBeam = CmdLine::GetArgValue( "-beamintensity" );
    static const float forcedBeam = pForcedBeam != nullptr ? static_cast<float>( atof( pForcedBeam ) ) : -1.0f;
    const float beamIntensity = forcedBeam >= 0.0f ? forcedBeam : weatherBeam;
    if ( value <= 0.0f || beamIntensity <= 0.0f ) {
        return;
    }
    static const uint64_t kFadeParams = GetIdentifier64bit( "LightBeamFadeParams" );
    for ( EdenMaterial &material : beam.getMaterials() ) {
        auto it = material.Parameters.find( kFadeParams );
        if ( it != material.Parameters.end() ) {
            it->second = Vector4f( 400.0f * 0.18f, 1.0f / ( 250.0f * 0.18f ), value, beamIntensity );
        }
        material.SrcBlend = 5; // D3DBLEND_SRCALPHA
        material.DstBlend = 2; // D3DBLEND_ONE
    }
    // One beam per front lamp (slot matrices +0x50 + k x 0x40, written by the
    // encrypted member functions of the table 0x012307f0 [ENC]) [INF: the
    // front light nodes Fa_L_FL* / Fa_L_FR* (flare or _C glow node) give the
    // lamp positions, the car axes the orientation; one beam per side].
    bool bSide[2] = { false, false };
    for ( const std::vector<LightNode> *pNodes : { &flareNodes, &causticNodes } ) {
        for ( const LightNode &node : *pNodes ) {
            if ( node.Function != 'L' || !node.bFront || bSide[node.bLeft ? 0 : 1] ) {
                continue;
            }
            bSide[node.bLeft ? 0 : 1] = true;
            const Vector3f lamp = car.getNodeTransform( node.Node ).getTranslation();
            beam.draw( renderer, world * Matrix4x4f::Translation( lamp ) );
        }
    }
}

// ---------------------------------------------------------------------------
// Projected headlight (docs/re/car_lights_spec.md 8).

bool CinematicCar::getHeadlightSpot( const Matrix4x4f &world, const Vector3f &cameraPosition,
                                     EdenSceneView::HeadlightSpot &outSpot ) const {
    if ( !car.isLoaded() || !bBeamLampsOn || beamHeadlightIntensity <= 0.0f ) {
        return false;
    }
    // Manager constants (ctor FUN_00921d90 [CODE]), variant 0 (the player
    // car: slot +0x2d8 set, not a bike; 1 other cars, 2 / 3 bikes):
    //   +0xfaf8 3.1   (height offset, slot +0x18)   +0xfb08 -1.6 (slot +0x1c = 1.6)
    //   +0xfae8 0.4712 (tilt about X, slot +0x20)   +0xfab8 102 (far, slot +0x360)
    //   +0xfac8 1.4835 (slot +0x364 / +0x36c)      +0xfad8 1.6057 (fov, slot +0x368)
    static const float kHeight = 3.1f, kBack = -1.6f, kTilt = 0.47123888f;
    static const float kFar = 102.0f, kNear = 1.4835298f, kFov = 1.6057029f;
    // FUN_00921260: spot frame M = RotX(tilt) x T(0, 3.1 + sy / 2, -1.6 + sz / 2)
    // x car (row vectors; sy / sz = 0.5 x the vector of the driver vfunc
    // +0x18 [INF: read as the car size (height, length): BODY_HR bounds]).
    Vector3f boundsMin( -1.0f, 0.0f, -2.2f ), boundsMax( 1.0f, 1.3f, 2.2f );
    getBodyBounds( boundsMin, boundsMax );
    const Vector3f size = boundsMax - boundsMin;
    const Vector3f local( 0.0f, kHeight + 0.5f * size.y, kBack + 0.5f * size.z );
    const float c = std::cos( kTilt ), s = std::sin( kTilt );
    const Vector3f carX = world.transformVector( Vector3f( 1.0f, 0.0f, 0.0f ) ).normalized();
    const Vector3f carY = world.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized();
    const Vector3f carZ = world.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ).normalized();
    outSpot.Position = world.transformPoint( local );
    outSpot.AxisX = carX;
    outSpot.AxisY = ( carY * c + carZ * s ).normalized();
    outSpot.Direction = ( carY * -s + carZ * c ).normalized();
    outSpot.Far = kFar;
    outSpot.Near = kNear;
    // FUN_00da07c0: a = f(slot +0x368 / 2), b = f(slot +0x36c / 2) with f =
    // 0x6c8d32 [ENC; INF: cos, it is compared with a dot product].
    outSpot.CosOuter = std::cos( 0.5f * kFov );
    outSpot.CosInner = std::cos( 0.5f * kNear );
    // ProjMatrix (FUN_00e27470, fov slot +0x368 x 57.3 then x 0.0174533 x
    // 0.5) [CODE; INF: symmetric, aspect 1].
    outSpot.TanHalfFov = std::tan( 0.5f * kFov );
    // Spot_PositionDropOffNear.w (FUN_009227c0): m = max(sqrt(3.1^2 +
    // 1.6^2), g(pi / 2 - tilt - fov / 2) x 3.1) (g = 0x40d470 [ENC; INF:
    // tan]); f = 1.01 x far x (1 / far) = 1.01; w = f > m - 0.5 ? 0.5 + f /
    // 1.01 : m. Slot +0x18 also gets + the x of the headlight matrix when
    // the member +0x2b8 succeeds [ENC], taken as 0.
    const float r = std::sqrt( kHeight * kHeight + kBack * kBack );
    const float m = std::max( r, std::tan( 1.5707964f - kTilt - 0.5f * kFov ) * kHeight );
    const float f = 1.01f;
    outSpot.DropOffNear = f > m - 0.5f ? 0.5f + f / 1.01f : m;
    // FUN_00923230: vars.z = I' x clamp(I, 0, 1)^2 x fade, I = slot +0x10 =
    // car_headlights_intensity (tunnel_), I' = I (1 when high beam and I <
    // 1), fade = 1 below 400 m (+0xfaa0), linear to 0 at 650 m (+0xfaa4);
    // Spot_Color.x [INF: 1, not written by readable code].
    const float distance = ( outSpot.Position - cameraPosition ).length();
    if ( distance > 650.0f ) {
        return false;
    }
    const float fade = distance < 400.0f ? 1.0f : 1.0f - ( distance - 400.0f ) / 250.0f;
    const float clamped = std::min( std::max( beamHeadlightIntensity, 0.0f ), 1.0f );
    const int32_t mode = headlightMode < 0 ? 1 : headlightMode;
    float scale = beamHeadlightIntensity;
    if ( mode == 2 && scale < 1.0f ) {
        scale = 1.0f;
    }
    outSpot.Intensity = scale * clamped * clamped * fade;
    // gProjTexture: projectivespotlighttexture (fx/weatherfx.bnk), the
    // texture of the "No Texture" material of weatherfx/headlight/headlight.3dd
    // [DATA], the model the manager loads by bank ids (+0xfb18 / +0xfb1c,
    // FUN_00920660 / FUN_00920890) [CODE; INF: ids not resolved].
    outSpot.pTexture = gpRender->getTextureCache().find( fxBank.getPath().c_str(), "projectivespotlighttexture" );
    return outSpot.pTexture != nullptr && outSpot.Intensity > 0.0f;
}
