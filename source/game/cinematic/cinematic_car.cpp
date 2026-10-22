#include "shared.h"
#include "cinematic_car.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/eden/eden_weather.h"
#include "render/eden/eden_scene_view.h"
#include "system/cmd_line.h"

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
    for ( Bank *pBank : { &carBank, &carCommonBank, &rimBank, &rimRearBank, &interiorBank } ) {
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
}

void CinematicCar::assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube ) {
    for ( EdenModel *pModel : { &car, &interior, &rim, &rimRear, &tire } ) {
        for ( EdenMaterial &material : pModel->getMaterials() ) {
            renderer.assignTexture( material, "REFLECTION", pCube );
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
            for ( const char *pSide : { "L", "R" } ) {
                const int32_t window = scene.findNode( ( std::string( "WND_" ) + pSide + "1" ).c_str() );
                const int32_t lowered = scene.findNode( ( std::string( "WND_" ) + pSide + "2" ).c_str() );
                if ( window >= 0 && lowered >= 0 ) {
                    car.setNodeTransform( window, car.getNodeTransform( lowered ) );
                }
            }
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
    if ( bHardtop ) {
        car.drawSkinned( renderer, world, hardtopSkin );
    } else {
        car.draw( renderer, world );
    }
    if ( bInterior ) {
        interior.draw( renderer, world );
    }
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

