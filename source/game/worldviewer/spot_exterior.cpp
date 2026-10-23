#include "shared.h"
#include "spot_exterior.h"

#include "core/xmb.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"

SpotExterior::~SpotExterior() { unload(); }

bool SpotExterior::load( EdenSceneView &view, const std::string &bankPath ) {
    unload();
    path = bankPath;
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( pRenderer == nullptr || !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "SpotExterior: '%s' could not be loaded\n", path.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( bank, bank.getPath().c_str() );
    hashcodes.load( bank );
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "SpotExterior: '%s' has no entities.xmb\n", path.c_str() );
        return false;
    }
    const Render2DM *pLibrary = &view.getWorld().getLibraryMaterials();
    auto loadModel = [&]( uint64_t rsc ) -> std::unique_ptr<EdenModel> {
        const BankEntry *pScene = rsc != 0 ? hashcodes.find( rsc, 0, 0 ) : nullptr;
        if ( pScene == nullptr ) {
            return nullptr;
        }
        const BankEntry *pGeometry = bank.findEntry( pScene->Name.c_str(), "3dg" );
        const BankEntry *pMaterials = bank.findEntry( pScene->Name.c_str(), "2dm" );
        auto pModel = std::make_unique<EdenModel>();
        if ( pGeometry == nullptr ||
             !pModel->load( *pRenderer, *pScene, *pGeometry, pMaterials, pLibrary, bank.getPath().c_str() ) ) {
            return nullptr;
        }
        pModel->bindUVAnimations( bank );
        return pModel;
    };
    auto firstMesh = []( XmbValue entry, const char *pField ) -> uint64_t {
        for ( uint32_t j = 0; j < entry.getArraySize( pField ); j++ ) {
            XmbValue mesh = entry.getArrayElement( pField, j );
            if ( mesh["rscSubType"].asInt() == 0 ) {
                return mesh["rsc"].asUInt64();
            }
        }
        return 0;
    };
    XmbValue scenery = entities.getRoot()["ExportType_Scenery"];
    for ( uint32_t i = 0; i < scenery.getArraySize( "ArrayOf_Scenery" ); i++ ) {
        XmbValue entry = scenery.getArrayElement( "ArrayOf_Scenery", i );
        for ( uint32_t j = 0; j < entry.getArraySize( "Model" ); j++ ) {
            XmbValue model = entry.getArrayElement( "Model", j );
            if ( model["rscSubType"].asInt() == 0 ) {
                if ( auto pModel = loadModel( model["rsc"].asUInt64() ) ) {
                    sets.push_back( std::move( pModel ) );
                }
            }
        }
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    for ( const char *pArray : { "ArrayOf_Entity__Actor__Mesh", "ArrayOf_Entity__Actor__Interior" } ) {
        for ( uint32_t i = 0; i < entityExport.getArraySize( pArray ); i++ ) {
            if ( auto pModel = loadModel( firstMesh( entityExport.getArrayElement( pArray, i ), "mesh" ) ) ) {
                sets.push_back( std::move( pModel ) );
            }
        }
    }
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Actor" ); i++ ) {
        XmbValue actor = entityExport.getArrayElement( "ArrayOf_Entity__Actor", i );
        Prop prop;
        prop.Model = loadModel( firstMesh( actor, "mesh" ) );
        if ( !prop.Model ) {
            continue;
        }
        XmbValue placement = actor["orient_pos_scale"];
        const Quaternionf rotation( placement["rot"]["X"].asFloat(), placement["rot"]["Y"].asFloat(),
                                    placement["rot"]["Z"].asFloat(), placement["rot"]["W"].asFloat( 1.0f ) );
        const Vector3f position( placement["pos"]["X"].asFloat(), placement["pos"]["Y"].asFloat(),
                                 placement["pos"]["Z"].asFloat() );
        Matrix4x4f rootInverse;
        for ( const SceneNode &node : prop.Model->getScene().getNodes() ) {
            if ( node.Parent < 0 ) {
                rootInverse = node.Local.inverse();
                break;
            }
        }
        prop.Local = Matrix4x4f::Translation( position ) * EdenRotationFromQuaternion( rotation ) * rootInverse;
        props.push_back( std::move( prop ) );
    }
    registerLightmaps( view );
    OTDU_LOG_INFO( "SpotExterior: %s: %zu set models, %zu props\n", path.c_str(), sets.size(), props.size() );
    return true;
}

void SpotExterior::registerLightmaps( EdenSceneView &view ) {
    for ( auto &pSet : sets ) {
        view.registerLightmapMaterials( *pSet, EdenSceneView::eLightmapScope::Exterior );
    }
    for ( Prop &prop : props ) {
        view.registerLightmapMaterials( *prop.Model, EdenSceneView::eLightmapScope::Exterior );
    }
}

void SpotExterior::unload() {
    sets.clear();
    props.clear();
    if ( !bank.getEntries().empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
    }
    bank.unload();
}

void SpotExterior::draw( EdenRenderer &renderer ) const {
    for ( const Matrix4x4f &frame : frames ) {
        for ( const auto &pSet : sets ) {
            pSet->draw( renderer, frame );
        }
        for ( const Prop &prop : props ) {
            prop.Model->draw( renderer, frame * prop.Local );
        }
    }
}
