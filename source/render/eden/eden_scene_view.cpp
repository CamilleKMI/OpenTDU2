#include "shared.h"
#include "eden_scene_view.h"

#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/shaders/builtin/blit_frag.h"
#include "render/shaders/builtin/viewpos_frag.h"
#include "system/cmd_line.h"

namespace {
constexpr uint32_t kEnvironmentSize = 256;
constexpr uint64_t kTD_Building = 0x183157b81c6cff57ull;
constexpr uint64_t kTD_Furnitures = 0xd5013ed0db79afafull;
constexpr uint64_t kTD_FurnituresAlt = 0xd5013ed0dcacafafull;
constexpr uint32_t kMaxExteriorLightmaps = 63; // FUN_00981870 (< 0x3f)
constexpr uint32_t kMaxInteriorLightmaps = 64; // FUN_00a4d7e0 (<= 0x3f)
const uint64_t kNightLightmap = GetIdentifier64bit( "NIGHTLMAP" );
} // namespace

// D3DXMatrixPerspectiveFovRH (Matrix4x4f memory = D3D row major layout).
// Eden view space is right handed (camera looking down -Z): the shaders
// rebuilding view positions from the depth (TD_ShadowBox, post effects) use
// z = -depth.
Matrix4x4f EdenPerspectiveFovRH( float fovY, float aspect, float zNear, float zFar ) {
    Matrix4x4f m = Matrix4x4f::Identity();
    const float h = 1.0f / std::tan( fovY * 0.5f );
    m.m[0][0] = h / aspect;
    m.m[1][1] = h;
    m.m[2][2] = zFar / ( zNear - zFar );
    m.m[2][3] = -1.0f;
    m.m[3][2] = zNear * zFar / ( zNear - zFar );
    m.m[3][3] = 0.0f;
    return m;
}

// Eden quaternions rotate the local axes the other way round compared to
// Matrix4x4f::FromQuaternion (the camera forward is +Z rotated by the
// conjugate).
Matrix4x4f EdenRotationFromQuaternion( const Quaternionf &q ) {
    return Matrix4x4f::FromQuaternion( Quaternionf( -q.x, -q.y, -q.z, q.w ) );
}

EdenSceneView::EdenSceneView() {}

EdenSceneView::~EdenSceneView() { destroy(); }

bool EdenSceneView::initialize( const char *pIsland, const Vector3f &streamCenter, int32_t sectorRadius ) {
    destroy();
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( pRenderer == nullptr ) {
        return false;
    }
    const std::string islandFolder = gpFile->getBankFolder() + "islands/" + pIsland + "/";
    weathers.load( ( islandFolder + "fx/cinematic_weathers.xmb" ).c_str() );
    weathers.loadModifiers( ( islandFolder + "fx/cinematic_modifiers.xmb" ).c_str() );
    if ( !world.initialize( *pRenderer, pIsland ) ) {
        return false;
    }
    world.loadAround( streamCenter, sectorRadius );
    sky.initialize( *pRenderer );
    sun.initialize( *pRenderer );
    postFx.initialize( *pRenderer );
    if ( !CmdLine::HasArg( "-noshadows" ) ) {
        // Resolution 2048 (Shadows.cpr SET_SHADOWS_RESOLUTION); the config
        // (slices, bias...) is set by the cinematic (DefaultShadowConfig,
        // "shad:" messages).
        EdenShadows::Config shadowConfig;
        shadowConfig.Resolution = static_cast<uint32_t>( Max( 256, CmdLine::GetArgInt( "-shadowres", 2048 ) ) );
        shadows.initialize( *pRenderer, shadowConfig );
    }
    if ( !CmdLine::HasArg( "-noocean" ) ) {
        ocean.initialize( *pRenderer );
    }
    loadClouds( islandFolder );
    bReady = true;
    return true;
}

void EdenSceneView::destroy() {
    destroyTargets();
    fullscreen.destroy();
    sky.destroy();
    sun.destroy();
    postFx.destroy();
    shadows.destroy();
    ocean.destroy();
    planarReflections.destroy();
    lightmapMaterials.clear();
    if ( gpRender != nullptr ) {
        if ( EdenRenderer *pEden = gpRender->getEdenRenderer() ) {
            pEden->clearShadowCasterCache();
        }
        if ( !cloudsBank.getEntries().empty() ) {
            gpRender->getTextureCache().releaseOwner( cloudsBank.getPath().c_str() );
        }
    }
    clouds.destroy();
    cloudsBank.unload();
    cloudsSuffix.clear();
    world.destroy();
    pWeather = nullptr;
    bReady = false;
}

GPUTexture *EdenSceneView::getEnvironmentCube() const {
    return pEnvironmentTarget != nullptr && pEnvironmentTarget->bInitialized && pEnvironmentTarget->InitializedLayers == 0x3f
               ? &pEnvironmentTarget->Color
               : nullptr;
}

void EdenSceneView::setEnvironmentCenter( const Vector3f &center, bool bEnable ) {
    environmentCenter = center;
    bEnvironmentEnabled = bEnable;
}

void EdenSceneView::setOceanEnabled( bool bEnable ) {
    bOceanEnabled = bEnable;
    world.setOceanVisible( bEnable );
}

void EdenSceneView::setPlanarReflectionsEnabled( bool bEnable ) {
    if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
        planarReflections.setEnabled( *pRenderer, bEnable );
    }
}

uint32_t EdenSceneView::registerProjectiveMaterials( EdenModel &model ) {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    return pRenderer != nullptr ? planarReflections.registerMaterials( *pRenderer, model ) : 0;
}

uint32_t EdenSceneView::registerReflectionScene( const EdenModel &model, const Matrix4x4f &world,
                                                 const char *pBankName, const char *pActorLayer1,
                                                 const char *pActorLayer2 ) {
    return planarReflections.registerScene( model, world, pBankName, pActorLayer1, pActorLayer2 );
}

void EdenSceneView::clearPlanarReflections() {
    planarReflections.clear();
    if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
        planarReflections.unbind( *pRenderer );
    }
}

uint32_t EdenSceneView::registerLightmapMaterials( EdenModel &model, eLightmapScope scope ) {
    // Exterior (FUN_00981870): TD_Building with U6 (MAT. +0x335) = 1.
    // Interior (FUN_00a4d7e0): TD_Furnitures, TD_Building, d5013ed0dcacafaf.
    // Both: materials that have the NIGHTLMAP parameter.
    uint32_t numExterior = 0;
    uint32_t numInterior = 0;
    for ( const auto &entry : lightmapMaterials ) {
        ( entry.second == eLightmapScope::Exterior ? numExterior : numInterior )++;
    }
    uint32_t numRegistered = 0;
    for ( EdenMaterial &material : model.getMaterials() ) {
        if ( material.Parameters.find( kNightLightmap ) == material.Parameters.end() ) {
            continue;
        }
        bool bMatch = false;
        if ( scope == eLightmapScope::Exterior ) {
            bMatch = material.ShaderSetGUID == kTD_Building && material.Spec[0x1f + 6] == 1 &&
                     numExterior < kMaxExteriorLightmaps;
        } else {
            bMatch = ( material.ShaderSetGUID == kTD_Building || material.ShaderSetGUID == kTD_Furnitures ||
                       material.ShaderSetGUID == kTD_FurnituresAlt ) &&
                     numInterior < kMaxInteriorLightmaps;
        }
        if ( !bMatch ) {
            continue;
        }
        bool bKnown = false;
        for ( const auto &entry : lightmapMaterials ) {
            bKnown |= entry.first == &material;
        }
        if ( bKnown ) {
            continue;
        }
        lightmapMaterials.emplace_back( &material, scope );
        ( scope == eLightmapScope::Exterior ? numExterior : numInterior )++;
        numRegistered++;
    }
    return numRegistered;
}

void EdenSceneView::updateLightmapMaterials() {
    // FUN_00980e50 (exterior: NIGHTLMAP = (x, y, 0, 0)) and FUN_00a4ee30
    // (interior: x, y written, z, w kept). Weathers without the parameter
    // leave the material value [INF].
    if ( pWeather == nullptr || lightmapMaterials.empty() ) {
        return;
    }
    static const bool bDisabled = CmdLine::HasArg( "-nonightlmap" );
    if ( bDisabled ) {
        return;
    }
    auto exteriorIt = pWeather->Values.find( "spot_exterior_lightmap_day_night" );
    auto interiorIt = pWeather->Values.find( "spot_interior_lightmap_day_night" );
    for ( auto &entry : lightmapMaterials ) {
        auto parameter = entry.first->Parameters.find( kNightLightmap );
        if ( parameter == entry.first->Parameters.end() ) {
            continue;
        }
        if ( entry.second == eLightmapScope::Exterior ) {
            if ( exteriorIt != pWeather->Values.end() ) {
                parameter->second = Vector4f( exteriorIt->second.x, exteriorIt->second.y, 0.0f, 0.0f );
            }
        } else if ( interiorIt != pWeather->Values.end() ) {
            parameter->second.x = interiorIt->second.x;
            parameter->second.y = interiorIt->second.y;
        }
    }
}

void EdenSceneView::tick( float deltaTime ) {
    time += deltaTime;
    frameDelta = deltaTime;
}

void EdenSceneView::setupScene( EdenSceneParams &params ) {
    const float aspectRatio =
        static_cast<float>( pSceneTarget->Width ) / Max( 1.0f, static_cast<float>( pSceneTarget->Height ) );
    // Eden camera: right handed, looking down its local -Z. The track
    // quaternion (conjugated) gives a frame looking down +Z: turned by 180
    // degrees around Y (determinant +1: no mirroring).
    const Matrix4x4f cameraWorld = Matrix4x4f::Translation( camera.Position ) *
                                   EdenRotationFromQuaternion( camera.Rotation ) *
                                   Matrix4x4f::Scale( Vector3f( -1.0f, 1.0f, -1.0f ) );
    params.View = cameraWorld.inverse();
    // FUN_00c9f680 (reference aspect 16/9): screens narrower than 16:9 get
    // the FOV scaled by (16/9) / aspect.
    static constexpr float kReferenceAspect = 16.0f / 9.0f;
    const float fov = aspectRatio < kReferenceAspect ? camera.Fov * kReferenceAspect / aspectRatio : camera.Fov;
    params.Projection = EdenPerspectiveFovRH( fov, aspectRatio, camera.Near, camera.Far );
    params.CameraPosition = camera.Position;
    params.CameraDirection = cameraWorld.transformVector( Vector3f( 0.0f, 0.0f, -1.0f ) );
    params.Time = time;
    if ( pWeather != nullptr ) {
        params.SunDirection = pWeather->getVector3( "sundirection", params.SunDirection ).normalized();
        static const bool bLogSun = CmdLine::HasArg( "-logsun" );
        if ( bLogSun ) {
            OTDU_LOG_INFO( "Sun dir (%.3f %.3f %.3f) camera dir (%.3f %.3f %.3f) pos (%.1f %.1f %.1f)\n",
                           params.SunDirection.x, params.SunDirection.y, params.SunDirection.z,
                           params.CameraDirection.x, params.CameraDirection.y, params.CameraDirection.z,
                           camera.Position.x, camera.Position.y, camera.Position.z );
        }
        params.SunDiffuse = pWeather->getVector3( "sun_diffuse", params.SunDiffuse );
        params.SunSpecular = pWeather->getVector3( "sun_specular", params.SunSpecular );
        params.Ambient = pWeather->getVector3( "sun_ambient", params.Ambient );
        params.FogColor = pWeather->getVector3( "fog_color", params.FogColor );
        const float fogStart = pWeather->getFloat( "fog_start", 100.0f );
        const float fogEnd = pWeather->getFloat( "fog_end", 2000.0f );
        const float fogMax = pWeather->getFloat( "fog_max", 1.0f );
        params.FogConfig = Vector4f( fogStart, fogEnd, fogMax, 1.0f / Max( fogEnd - fogStart, 1.0f ) );
    }
    // Frame user vectors (Var_Frame_VS). UserVector1: x = sun visibility
    // factor, y = night lights factor (TD_HeightMap: shadow map * x, night
    // light map * 2 y).
    params.UserVectors[1] = Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
    EdenSky::setupAtmosphere( params, pWeather );
    params.Width = pSceneTarget->Width;
    params.Height = pSceneTarget->Height;
}

void EdenSceneView::ensureTargets() {
    RenderDevice &device = gpRender->getDevice();
    const uint32_t width = device.getBackbufferWidth();
    const uint32_t height = device.getBackbufferHeight();
    if ( pSceneTarget != nullptr && pSceneTarget->Width == width && pSceneTarget->Height == height ) {
        return;
    }
    RenderTarget *pEnvironment = pEnvironmentTarget;
    pEnvironmentTarget = nullptr;
    destroyTargets();
    pEnvironmentTarget = pEnvironment;
    pSceneTarget = device.createRenderTarget( width, height, VK_FORMAT_UNDEFINED, true, "EdenScene" );
    pViewPosTarget = device.createRenderTarget( width, height, VK_FORMAT_R8G8B8A8_UNORM, false, "gViewPos" );
    pFinalTarget = device.createRenderTarget( width, height, VK_FORMAT_UNDEFINED, false, "EdenSceneFinal" );
}

void EdenSceneView::destroyTargets() {
    if ( gpRender == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    device.destroyRenderTarget( pSceneTarget );
    device.destroyRenderTarget( pViewPosTarget );
    device.destroyRenderTarget( pEnvironmentTarget );
    device.destroyRenderTarget( pFinalTarget );
    pSceneTarget = pViewPosTarget = pEnvironmentTarget = pFinalTarget = nullptr;
}

void EdenSceneView::drawObjects( EdenRenderer &renderer, ePass pass ) {
    if ( drawCallback ) {
        drawCallback( renderer, pass );
    }
}

void EdenSceneView::renderEnvironment( const EdenSceneParams &mainScene, bool bAllFaces ) {
    // Dynamic environment cube around the car (car_envmap_factor; the static
    // cubemap of common_car.bnk is the fallback). The car shaders sample it
    // with (x, y, -z): the faces are rendered for the z mirrored directions.
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    RenderDevice &device = gpRender->getDevice();
    if ( pEnvironmentTarget == nullptr ) {
        pEnvironmentTarget = device.createCubeRenderTarget( kEnvironmentSize, true, "CarEnvironment" );
        bAllFaces = true;
    }
    static const Vector3f kForward[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const Vector3f kUp[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
    auto mirror = []( const Vector3f &v ) { return Vector3f( v.x, v.y, -v.z ); };
    const uint32_t firstFace = bAllFaces ? 0 : environmentFace;
    const uint32_t lastFace = bAllFaces ? 6 : environmentFace + 1;
    environmentFace = ( environmentFace + 1 ) % 6;
    for ( uint32_t face = firstFace; face < lastFace; face++ ) {
        const Vector3f forward = mirror( kForward[face] );
        const Vector3f up = mirror( kUp[face] );
        const Vector3f right = mirror( kUp[face].cross( kForward[face] ) );
        Matrix4x4f cameraWorld = Matrix4x4f::Identity();
        cameraWorld.m[0][0] = right.x, cameraWorld.m[0][1] = right.y, cameraWorld.m[0][2] = right.z;
        cameraWorld.m[1][0] = up.x, cameraWorld.m[1][1] = up.y, cameraWorld.m[1][2] = up.z;
        cameraWorld.m[2][0] = -forward.x, cameraWorld.m[2][1] = -forward.y, cameraWorld.m[2][2] = -forward.z;
        cameraWorld.m[3][0] = environmentCenter.x, cameraWorld.m[3][1] = environmentCenter.y + 1.0f;
        cameraWorld.m[3][2] = environmentCenter.z;

        EdenSceneParams params = mainScene;
        params.View = cameraWorld.inverse();
        params.Projection = EdenPerspectiveFovRH( 90.0f * kDegToRad, 1.0f, camera.Near, camera.Far );
        params.CameraPosition = cameraWorld.getTranslation();
        params.CameraDirection = forward;
        params.Width = params.Height = kEnvironmentSize;
        params.bMirroredView = false; // (right, up, back) frame: determinant +1

        device.beginRenderTarget( pEnvironmentTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ), face );
        pRenderer->beginScene( params );
        sky.draw( *pRenderer );
        drawClouds( *pRenderer );
        world.setMinInstanceRatio( 0.02f );
        world.draw( *pRenderer, params.CameraPosition );
        world.setMinInstanceRatio( 0.0f );
        drawObjects( *pRenderer, ePass::Environment );
        pRenderer->endScene();
        device.endRenderTarget();
    }
}

void EdenSceneView::generateViewPos() {
    RenderDevice &device = gpRender->getDevice();
    device.beginRenderTarget( pViewPosTarget, true );
    const float viewPosParams[4] = { camera.Near, camera.Far, 0.0f, 0.0f };
    GPUTexture *pDepth = &pSceneTarget->Depth;
    fullscreen.draw( device, kSPIRV_viewpos_frag, sizeof( kSPIRV_viewpos_frag ), &pDepth, 1, viewPosParams,
                     sizeof( viewPosParams ), eBlendMode::BM_Opaque, eSamplerFilter::SF_Point );
    device.endRenderTarget();
}

void EdenSceneView::renderOffscreen() {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !bReady || pRenderer == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    ensureTargets();
    EdenSceneParams params;
    setupScene( params );

    // Environment cube (one face per frame). The cube faces are drawn with
    // the dry road (the wet road samples this cube).
    static const bool bStaticCubemap = CmdLine::HasArg( "-staticcubemap" );
    if ( bEnvironmentEnabled && !bStaticCubemap ) {
        world.updateWetGround( *pRenderer, nullptr, nullptr, 0.0f );
        renderEnvironment( params, false );
    }

    // Sun visibility: depth around the sun (read back a few frames later).
    sun.updateVisibility( params, pSceneTarget );

    static const bool bNoWet = CmdLine::HasArg( "-nowet" );
    world.updateWetGround( *pRenderer, bNoWet ? nullptr : pWeather, getEnvironmentCube(), frameDelta );

    updateLightmapMaterials();

    // Ocean: noise normal map, mirror reflection, TD_Ocean runtime setup.
    if ( ocean.isReady() && bOceanEnabled ) {
        ocean.updateNormalMap( *pRenderer, frameDelta, pWeather );
        const Vector3f mainCamera = params.CameraPosition;
        ocean.renderReflection( *pRenderer, params, pWeather,
                                [this, mainCamera]( EdenRenderer &renderer, const EdenSceneParams & ) {
                                    world.draw( renderer, mainCamera );
                                    drawObjects( renderer, ePass::Reflection );
                                } );
        world.forEachOceanMaterial( [this, pRenderer]( EdenMaterial &material, uint8_t kind, GPUTexture *pColorMap ) {
            ocean.setupMaterial( *pRenderer, material, static_cast<EdenOcean::eKind>( kind ), pColorMap, pWeather );
        } );
    }

    // 0. Z pre-pass (ZPREPASS layer: opaque and alpha tested draws) ->
    //    gViewPos (FullscreenDepthTarget), read by the haze, the projected
    //    shadow boxes and the sun shadow pass; then the real time sun
    //    shadows: caster maps of the 3 slices, screen shadow buffer (s15).
    static const bool bNoShadows = CmdLine::HasArg( "-noshadows" );
    GPUTexture *pShadowBuffer = nullptr;
    shadows.setupSceneConstants( params, pWeather );
    device.beginRenderTarget( pSceneTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    pRenderer->setDepthOnly( true );
    pRenderer->beginScene( params );
    world.draw( *pRenderer, params.CameraPosition );
    drawObjects( *pRenderer, ePass::DepthPrepass );
    pRenderer->endScene();
    pRenderer->setDepthOnly( false );
    device.endRenderTarget();
    generateViewPos();
    if ( !bNoShadows ) {
        const Vector3f cameraPosition = params.CameraPosition;
        shadows.renderCasters( *pRenderer, params,
                               [this, cameraPosition]( EdenRenderer &renderer, const EdenSceneParams & ) {
                                   world.draw( renderer, cameraPosition );
                                   drawObjects( renderer, ePass::ShadowCasters );
                               } );
        pShadowBuffer = shadows.resolve( *pRenderer, params, &pViewPosTarget->Color, pWeather );
    }
    pRenderer->setShadowRealTimeTexture( pShadowBuffer );

    // GSReflection layers (after HEADLIGHTS BLIT, before NEAR_OPAQUE):
    // projective map of the water / reflective floors.
    if ( bProjectorActors ) {
        planarReflections.render( *pRenderer, params,
                                  [this]( EdenRenderer &renderer ) { drawObjects( renderer, ePass::ProjectorActors ); } );
    } else {
        planarReflections.render( *pRenderer, params, nullptr );
    }

    // 1. Opaque scene (blended draws are deferred by the renderer).
    device.beginRenderTarget( pSceneTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 1.0f ) );
    pRenderer->setScreenTexture( nullptr );
    pRenderer->beginScene( params );
    sky.draw( *pRenderer );
    sun.drawHalo( *pRenderer, params, pWeather );
    drawClouds( *pRenderer );
    world.draw( *pRenderer, params.CameraPosition );
    drawObjects( *pRenderer, ePass::Opaque );
    sun.drawFlares( *pRenderer, params, pWeather );
    device.endRenderTarget();
    static const bool bShowDraws = CmdLine::HasArg( "-showdraws" );
    if ( bShowDraws ) {
        OTDU_LOG_INFO( "EdenSceneView: %u draws\n", pRenderer->getNumDrawCalls() );
    }

    // 2. gViewPos comes from the Z pre-pass (step 0).

    // 3. Blended draws (projected shadow boxes read gViewPos, glass,
    //    clouds...).
    device.beginRenderTarget( pSceneTarget, false );
    pRenderer->setScreenTexture( &pViewPosTarget->Color );
    pRenderer->resumeScene();
    // Blended scene draws (heightfields with their alpha blended borders,
    // glass...) before the haze: the haze covers the whole scene.
    pRenderer->flushDeferred();
    static const bool bNoHaze = CmdLine::HasArg( "-nohaze" );
    if ( !bNoHaze ) {
        postFx.drawHaze( *pRenderer, &pViewPosTarget->Color, pWeather );
    }
    pRenderer->endScene();
    device.endRenderTarget();
    pRenderer->setScreenTexture( nullptr );
    pRenderer->setShadowRealTimeTexture( nullptr );
    planarReflections.unbind( *pRenderer );

    // 4. Photoshop (color balance / soft light / overlay / bloom LUT).
    static const bool bNoPhotoshop = CmdLine::HasArg( "-nophotoshop" );
    if ( !bNoPhotoshop ) {
        postFx.applyPhotoshop( *pRenderer, pSceneTarget, pFinalTarget, pWeather );
    }
    sceneParams = params;
}

void EdenSceneView::draw() {
    if ( !bReady || pSceneTarget == nullptr || !pSceneTarget->bInitialized ) {
        return;
    }
    GPUTexture *pColor =
        pFinalTarget != nullptr && pFinalTarget->bInitialized ? &pFinalTarget->Color : &pSceneTarget->Color;
    static const bool bShowViewPos = CmdLine::HasArg( "-showviewpos" );
    if ( bShowViewPos ) {
        pColor = &pViewPosTarget->Color;
    }
    static const bool bShowShadow = CmdLine::HasArg( "-showshadowbuffer" );
    if ( bShowShadow && shadows.getScreenShadow() != nullptr ) {
        pColor = &shadows.getScreenShadow()->Color;
    }
    static const int32_t showFace = CmdLine::HasArg( "-showenvface" ) ? CmdLine::GetArgInt( "-showenvface", 0 ) : -1;
    GPUTexture face;
    if ( showFace >= 0 && showFace < 6 && pEnvironmentTarget != nullptr ) {
        face = pEnvironmentTarget->Color;
        face.View = pEnvironmentTarget->LayerViews[showFace];
        face.ViewType = VK_IMAGE_VIEW_TYPE_2D;
        pColor = &face;
    }
    fullscreen.draw( gpRender->getDevice(), kSPIRV_blit_frag, sizeof( kSPIRV_blit_frag ), &pColor, 1 );
}

bool EdenSceneView::loadClouds( const std::string &islandFolder ) {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    const std::string path = islandFolder + "fx/clouds.bnk";
    if ( !cloudsBank.load( path.c_str() ) ) {
        return false;
    }
    gpRender->getTextureCache().loadBankTextures( cloudsBank, cloudsBank.getPath().c_str() );
    return clouds.load( *pRenderer, cloudsBank, "clouds", nullptr, cloudsBank.getPath().c_str() );
}

void EdenSceneView::drawClouds( EdenRenderer &renderer ) {
    if ( !clouds.isLoaded() ) {
        return;
    }
    // clouds_cloud3dname "Cloud_X": CLOUD_X (billboard layers) + SKYCLO_X
    // (dome mesh) subtrees of clouds.3dd.
    const std::string name = pWeather != nullptr ? ToUpper( pWeather->getString( "clouds_cloud3dname" ) ) : "CLOUD_B";
    const std::string suffix = name.size() >= 2 ? "_" + name.substr( name.size() - 1 ) + "_" : "_B_";
    if ( suffix != cloudsSuffix ) {
        cloudsSuffix = suffix;
        const Render3DD &scene = clouds.getScene();
        const std::vector<SceneNode> &nodes = scene.getNodes();
        std::vector<int32_t> roots;
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            const std::string &nodeName = nodes[i].Name;
            if ( nodeName.find( "UD" + suffix ) != std::string::npos ||
                 nodeName.find( "CLO" + suffix ) != std::string::npos ) {
                roots.push_back( static_cast<int32_t>( i ) );
            }
        }
        for ( size_t i = 0; i < nodes.size(); i++ ) {
            bool bVisible = false;
            for ( int32_t root : roots ) {
                bVisible |= static_cast<int32_t>( i ) == root || scene.isDescendantOf( static_cast<int32_t>( i ), root );
            }
            clouds.setNodeVisible( static_cast<int32_t>( i ), bVisible );
        }
    }
    auto vector4 = [this]( const char *pName, const Vector4f &defaultValue ) {
        return pWeather != nullptr ? pWeather->getVector4( pName, defaultValue ) : defaultValue;
    };
    EdenDrawParams params;
    params.Vectors["__Instance_Color"] = vector4( "clouds_cloud3dcolor", Vector4f( 1.0f, 1.0f, 1.0f, 0.0f ) );
    params.Vectors["ColorDown"] = vector4( "clouds_cloud3dcolor2", Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
    params.Vectors["AlphaBlend"] = vector4( "clouds_cloud3dAlphaBlend", Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
    params.Vectors["__DirLight_Emissive_0"] = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    const float haze = pWeather != nullptr ? pWeather->getFloat( "Skydome_haze", 0.0f ) : 0.0f;
    params.Vectors["Haze"] = Vector4f( haze, haze, haze, haze );
    clouds.draw( renderer, Matrix4x4f::Identity(), &params );
}
