#include "shared.h"
#include "eden_scene_view.h"
#include "eden_neons.h"

#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/shaders/builtin/blit_frag.h"
#include "render/shaders/builtin/headlight_frag.h"
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
        ocean.setWaterHeight( world.getWaterHeight() );
    }
    loadClouds( islandFolder );
    rain.initialize( *pRenderer, islandFolder ); // RAIN (rain engineer)
    bReady = true;
    return true;
}

void EdenSceneView::destroy() {
    destroyTargets();
    fullscreen.destroy();
    headlightPass.destroy();
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
    cloudSets.clear();
    bCloudLayers = false;
    gameTimeDelta = -1.0f;
    rain.destroy(); // RAIN (rain engineer)
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

void EdenSceneView::removePlanarReflectionModel( EdenModel &model ) { planarReflections.removeModel( model ); }

uint32_t EdenSceneView::registerLightmapMaterials( EdenModel &model, eLightmapScope scope ) {
    // Exterior (FUN_00981870): TD_Building with U6 (MAT. +0x335) = 1.
    // Interior (FUN_00a4d7e0): TD_Furnitures, TD_Building, d5013ed0dcacafaf.
    // Both: materials that have the NIGHTLMAP parameter.
    uint32_t numExterior = 0;
    uint32_t numInterior = 0;
    for ( const auto &entry : lightmapMaterials ) {
        numExterior += entry.second == eLightmapScope::Exterior ? 1 : 0;
        numInterior += entry.second == eLightmapScope::Interior ? 1 : 0;
    }
    uint32_t numRegistered = 0;
    if ( scope == eLightmapScope::Exterior ) {
        // NIGHT (rain / night engineer): FUN_00981870 also collects the
        // sprites (GUID fcdaa96bdeadbeef) that have OcclusionDayNight
        // (0x97909ca3a58487f1, at most 127) and FUN_00980d30 the nodes drawn
        // with TD_Lamp (c913741cbe17490d, at most 64; node flags |= 0x30).
        // FUN_00980e50 writes OcclusionDayNight.z = 1 - x and the node colour
        // .w (+0x8c, __Instance_Color of TD_Lamp: emissive * colour.rgb *
        // colour.w + ambient) = y of spot_exterior_lightmap_day_night [CODE
        // + SHADER]. Node colour rgb kept (1, 1, 1 [INF]).
        static const uint64_t kOcclusionDayNight = 0x97909ca3a58487f1ull;
        uint32_t numSprites = 0;
        uint32_t numLamps = 0;
        for ( const auto &entry : lightmapMaterials ) {
            numSprites += entry.second == eLightmapScope::ExteriorSprite ? 1 : 0;
            numLamps += entry.second == eLightmapScope::ExteriorLamp ? 1 : 0;
        }
        for ( EdenMaterial &material : model.getMaterials() ) {
            eLightmapScope extra = eLightmapScope::Exterior;
            if ( material.ShaderSetGUID == 0xfcdaa96bdeadbeefull &&
                 material.Parameters.find( kOcclusionDayNight ) != material.Parameters.end() && numSprites < 127 ) {
                extra = eLightmapScope::ExteriorSprite;
                numSprites++;
            } else if ( material.ShaderSetGUID == 0xc913741cbe17490dull && numLamps < 64 ) {
                extra = eLightmapScope::ExteriorLamp;
                numLamps++;
            } else {
                continue;
            }
            bool bKnown = false;
            for ( const auto &entry : lightmapMaterials ) {
                bKnown |= entry.first == &material && entry.second == extra;
            }
            if ( !bKnown ) {
                lightmapMaterials.emplace_back( &material, extra );
                OTDU_LOG_DEBUG( "EdenSceneView: exterior %s '%s'\n",
                                extra == eLightmapScope::ExteriorLamp ? "lamp" : "sprite", material.Name.c_str() );
            }
        }
    }
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

uint32_t EdenSceneView::registerNeonMaterials( EdenModel &model ) {
    // FUN_00985e40 for the model instance (see EdenNeons): neon set + one
    // blink state per blinking material of this model.
    uint32_t numRegistered = 0;
    for ( EdenMaterial &material : model.getMaterials() ) {
        if ( !EdenNeons::IsNeonSetMaterial( material ) && !EdenNeons::IsBlinkMaterial( material ) ) {
            continue;
        }
        bool bKnown = false;
        for ( const auto &entry : lightmapMaterials ) {
            bKnown |= entry.first == &material;
        }
        if ( bKnown ) {
            continue;
        }
        if ( EdenNeons::IsNeonSetMaterial( material ) ) {
            lightmapMaterials.emplace_back( &material, eLightmapScope::Neon );
        }
        if ( EdenNeons::IsBlinkMaterial( material ) ) {
            EdenNeons::Blink blink;
            blink.initialize( static_cast<uint32_t>( material.Hashcode ^ ( material.Hashcode >> 32 ) ) +
                              static_cast<uint32_t>( neonBlinks.size() ) );
            material.Parameters[EdenNeons::kAnimValue] = blink.Value;
            neonBlinks.emplace_back( &material, blink );
        }
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
    float neonStrength = 0.0f;
    const bool bNeonStrength = EdenNeons::GetStrength( pWeather, neonStrength );
    for ( auto &entry : lightmapMaterials ) {
        if ( entry.second == eLightmapScope::Neon ) {
            // FUN_009829d0: only the parameters the material has.
            if ( bNeonStrength ) {
                EdenNeons::ApplyStrength( *entry.first, neonStrength );
            }
            continue;
        }
        if ( entry.second == eLightmapScope::ExteriorSprite || entry.second == eLightmapScope::ExteriorLamp ) {
            if ( exteriorIt == pWeather->Values.end() ) {
                continue;
            }
            if ( entry.second == eLightmapScope::ExteriorSprite ) {
                auto occlusion = entry.first->Parameters.find( 0x97909ca3a58487f1ull );
                if ( occlusion != entry.first->Parameters.end() ) {
                    occlusion->second.z = 1.0f - exteriorIt->second.x; // 1.0 (DAT_00f44c70) - x
                }
            } else {
                entry.first->StageConstants[0]["__Instance_Color"] = { Vector4f( 1.0f, 1.0f, 1.0f, exteriorIt->second.y ) };
            }
            continue;
        }
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

void EdenSceneView::updateNeons() {
    // FUN_009875e0 -> FUN_009834d0(mgr, dt) every frame [CODE]: the neon set
    // (in updateLightmapMaterials for the view's own models, EdenWorld for
    // the sectors, areas and .pmi instances), then every AnimValue entry
    // (FUN_009821a0), whatever the weather.
    world.updateNeons( pWeather, frameDelta );
    for ( auto &entry : neonBlinks ) {
        entry.second.update( frameDelta );
        entry.first->Parameters[EdenNeons::kAnimValue] = entry.second.Value;
    }
}

void EdenSceneView::setCloudLayers( const std::string &source, const std::string &destination, float blend ) {
    cloudSource = source;
    cloudDestination = destination;
    cloudBlend = Clamp( blend, 0.0f, 1.0f );
    bCloudLayers = true;
}

void EdenSceneView::tick( float deltaTime ) {
    time += deltaTime;
    frameDelta = deltaTime;
    // Cloud rotation by the game time of the frame (GSWeather +0x4f0).
    updateClouds( gameTimeDelta >= 0.0f ? gameTimeDelta : deltaTime * WeatherSystem::kGameTimeSpeed );
    gameTimeDelta = -1.0f;
    // Island ambient effects (-F.pmi, FUN_00c38b10): started / stopped by
    // the camera distance.
    world.updateEffects( camera.Position, deltaTime );
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
    // NIGHT (rain / night engineer): FUN_00d769e0 (0xd76ce6..0xd76d70)
    // writes UserVector1 (0x00ffb5f0) = (1, outside_street_lightmap, 1,
    // building_nightwindow_frequency) and UserVector2 (0x00ffb600) =
    // (building_shop_front_back_lighting.xy, 0, 0) every frame [CODE]:
    // TD_HeightMap / TD_Ground / TD_Road add the night light map (sector -s
    // map G) * 2 * y, TD_Building / TD_HardwareInstance switch the night
    // windows on with w >= 0.5 [SHADER] (docs/re/night_lighting_spec.md).
    if ( pWeather != nullptr ) {
        params.UserVectors[1] = Vector4f( 1.0f, pWeather->getFloat( "outside_street_lightmap", 0.0f ), 1.0f,
                                          pWeather->getFloat( "building_nightwindow_frequency", 0.0f ) );
        const Vector4f shop = pWeather->get( "building_shop_front_back_lighting", Vector4f() );
        params.UserVectors[2] = Vector4f( shop.x, shop.y, 0.0f, 0.0f );
    }
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
    // HeadLightsRT (FUN_00d75d00: screen size, format of FUN_009206d0 [CODE];
    // R16F as postfx_spec 6.1 [INF]).
    pHeadLightsTarget = device.createRenderTarget( width, height, VK_FORMAT_R16_SFLOAT, false, "HeadLightsRT" );
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
    device.destroyRenderTarget( pHeadLightsTarget );
    pSceneTarget = pViewPosTarget = pEnvironmentTarget = pFinalTarget = pHeadLightsTarget = nullptr;
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
        sky.drawMoon( *pRenderer, params, pWeather );
        drawClouds( *pRenderer );
        world.setMinInstanceRatio( 0.02f );
        world.draw( *pRenderer, params.CameraPosition );
        world.setMinInstanceRatio( 0.0f );
        world.drawEffects( *pRenderer, params.CameraPosition );
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

GPUTexture *EdenSceneView::renderHeadlights( const EdenSceneParams &params ) {
    // HEADLIGHTS (docs/re/car_lights_spec.md 8): HeadLightsRT cleared to 0,
    // each spot adds its light (edrender_pfx_lightness ps_1 / ps_2: the
    // second and next slots add the previous content, sFrameBuffer [SHADER];
    // additive blending here). The result goes to G / B of the s15 buffer
    // (HEADLIGHTS BLIT, shadow combine pass).
    static const bool bNoHeadlights = CmdLine::HasArg( "-noprojector" );
    if ( pHeadLightsTarget == nullptr || headlightSpots.empty() || bNoHeadlights ) {
        return nullptr;
    }
    RenderDevice &device = gpRender->getDevice();
    device.beginRenderTarget( pHeadLightsTarget, true, ColorRGBA( 0.0f, 0.0f, 0.0f, 0.0f ) );
    // Camera frame (x right, y up, z forward) of the view depth buffer.
    const Matrix4x4f cameraWorld = params.View.inverse();
    Vector3f right = cameraWorld.transformVector( Vector3f( 1.0f, 0.0f, 0.0f ) ).normalized();
    Vector3f up = cameraWorld.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized();
    const Vector3f forward = params.CameraDirection.normalized();
    struct Push {
        float Recon[4];
        float ToSpot[3][4];
        float Attenuation[4];
        float Params[4];
        float Cone[4];
    };
    for ( const HeadlightSpot &spot : headlightSpots ) {
        if ( spot.pTexture == nullptr || spot.Intensity <= 0.0f ) {
            continue;
        }
        Push push = {};
        push.Recon[0] = 1.0f / std::fabs( params.Projection.m[0][0] );
        push.Recon[1] = 1.0f / std::fabs( params.Projection.m[1][1] );
        // camera frame -> world -> spot frame
        const Vector3f axes[3] = { spot.AxisX, spot.AxisY, spot.Direction };
        const Vector3f origin = params.CameraPosition - spot.Position;
        for ( int32_t r = 0; r < 3; r++ ) {
            push.ToSpot[r][0] = axes[r].dot( right );
            push.ToSpot[r][1] = axes[r].dot( up );
            push.ToSpot[r][2] = axes[r].dot( forward );
            push.ToSpot[r][3] = axes[r].dot( origin );
        }
        // Spot_Attenuation (slot +0x350..+0x35c): far / (far - near),
        // 1 / (far - near) (FUN_009208f0), -a / (b - a), 1 / (b - a) with
        // a = cos(outer / 2), b = cos(inner / 2) (FUN_00da07c0) [CODE].
        const float range = spot.Far - spot.Near;
        const float cone = spot.CosInner - spot.CosOuter;
        push.Attenuation[0] = range > 1.1920929e-07f ? spot.Far / range : spot.Far * 8388608.0f;
        push.Attenuation[1] = range > 1.1920929e-07f ? 1.0f / range : 8388608.0f;
        push.Attenuation[3] = cone > 1.1920929e-07f ? 1.0f / cone : 8388608.0f;
        push.Attenuation[2] = -spot.CosOuter * push.Attenuation[3];
        push.Params[0] = spot.DropOffNear;
        push.Params[1] = spot.Intensity;
        push.Params[2] = spot.TanHalfFov;
        push.Params[3] = spot.Far;
        push.Cone[0] = spot.CosOuter;
        static const int32_t debugOutput = CmdLine::GetArgInt( "-hldebug", 0 );
        push.Cone[1] = static_cast<float>( debugOutput );
        GPUTexture *textures[2] = { &pViewPosTarget->Color, spot.pTexture };
        headlightPass.draw( device, kSPIRV_headlight_frag, sizeof( kSPIRV_headlight_frag ), textures, 2, &push,
                            sizeof( push ), eBlendMode::BM_Additive, eSamplerFilter::SF_Linear );
    }
    device.endRenderTarget();
    return &pHeadLightsTarget->Color;
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
    // RAIN (rain engineer): rain values of the frame (forced by the World
    // Viewer checkbox), the falling rain and the wet ground read them.
    const WeatherPreset *pRainWeather = rain.resolveWeather( pWeather, bRainEnabled );
    {
        const Matrix4x4f cameraWorld = params.View.inverse();
        EdenRain::Frame rainFrame;
        rainFrame.Position = params.CameraPosition;
        rainFrame.Right = cameraWorld.transformVector( Vector3f( 1.0f, 0.0f, 0.0f ) ).normalized();
        rainFrame.Up = cameraWorld.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ).normalized();
        rainFrame.Forward = params.CameraDirection.normalized();
        rain.update( frameDelta, rainFrame, world, pRainWeather );
    }
    world.updateWetGround( *pRenderer, bNoWet ? nullptr : pRainWeather, getEnvironmentCube(), frameDelta );

    updateLightmapMaterials();
    updateNeons();

    // Ocean: noise normal map, mirror reflection, TD_Ocean runtime setup.
    if ( ocean.isReady() && bOceanEnabled ) {
        ocean.updateNormalMap( *pRenderer, frameDelta, pWeather );
        const Vector3f mainCamera = params.CameraPosition;
        ocean.renderReflection( *pRenderer, params, pWeather,
                                [this, mainCamera]( EdenRenderer &renderer, const EdenSceneParams & ) {
                                    world.draw( renderer, mainCamera );
                                    world.drawEffects( renderer, mainCamera );
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
        GPUTexture *pHeadLights = renderHeadlights( params );
        pShadowBuffer = shadows.resolve( *pRenderer, params, &pViewPosTarget->Color, pWeather, pHeadLights );
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
    sky.drawMoon( *pRenderer, params, pWeather );
    sun.drawHalo( *pRenderer, params, pWeather );
    drawClouds( *pRenderer );
    world.draw( *pRenderer, params.CameraPosition );
    world.drawEffects( *pRenderer, params.CameraPosition );
    rain.draw( *pRenderer, params.CameraPosition ); // RAIN (rain engineer): main view only
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
    static const bool bShowHeadlights = CmdLine::HasArg( "-showheadlights" );
    if ( bShowHeadlights && pHeadLightsTarget != nullptr ) {
        pColor = &pHeadLightsTarget->Color;
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

EdenSceneView::CloudSet *EdenSceneView::findCloudSet( const std::string &name ) {
    if ( !clouds.isLoaded() || name.empty() ) {
        return nullptr;
    }
    const std::string key = ToUpper( name );
    auto it = cloudSets.find( key );
    if ( it != cloudSets.end() ) {
        return it->second.Roots.empty() ? nullptr : &it->second;
    }
    // FUN_00932390 / FUN_00931da0: the cloud objects "<name>_0" ... "_3"
    // (each with its "_N" near and "_F" far sub-layers) found by name hash in
    // clouds.3dd. The TD_CloudMesh dome meshes ("?YCLO_<letter>_" geometry,
    // full names unknown) are not reached by these lookups; they are drawn
    // with the set of their letter [INF].
    CloudSet set;
    const Render3DD &scene = clouds.getScene();
    const std::vector<SceneNode> &nodes = scene.getNodes();
    for ( int32_t i = 0; i < 4; i++ ) {
        const std::string objectName = key + "_" + std::to_string( i );
        const uint64_t hashcode = GetIdentifier64bit( objectName.c_str() );
        for ( size_t n = 0; n < nodes.size(); n++ ) {
            if ( nodes[n].Hashcode == hashcode ) {
                set.Roots.push_back( static_cast<int32_t>( n ) );
            }
        }
    }
    if ( key.size() >= 2 ) {
        const std::string meshSuffix = "CLO_" + key.substr( key.size() - 1 ) + "_";
        for ( size_t n = 0; n < nodes.size(); n++ ) {
            if ( nodes[n].ObjectHashcode != 0 && nodes[n].Name.find( meshSuffix ) != std::string::npos ) {
                // Topmost ancestor below the CLOUDS group: rotation pivot.
                int32_t root = static_cast<int32_t>( n );
                while ( nodes[root].Parent >= 0 && nodes[nodes[root].Parent].Parent >= 0 &&
                        nodes[nodes[nodes[root].Parent].Parent].Parent >= 0 ) {
                    root = nodes[root].Parent;
                }
                set.Roots.push_back( root );
            }
        }
    }
    set.Nodes.assign( nodes.size(), false );
    for ( size_t n = 0; n < nodes.size(); n++ ) {
        for ( int32_t root : set.Roots ) {
            if ( scene.isDescendantOf( static_cast<int32_t>( n ), root ) ) {
                set.Nodes[n] = true;
            }
        }
    }
    OTDU_LOG_DEBUG( "EdenSceneView: cloud set '%s': %zu objects\n", key.c_str(), set.Roots.size() );
    auto result = cloudSets.emplace( key, std::move( set ) );
    return result.first->second.Roots.empty() ? nullptr : &result.first->second;
}

void EdenSceneView::updateClouds( float gameDelta ) {
    if ( !clouds.isLoaded() || pWeather == nullptr ) {
        return;
    }
    // FUN_009301c0: angle = clouds_cloud3dSpeed.x (slots 0 / 1; .y for the
    // cyclone slots 2 / 3, not in the island data) / 1000 * game time of the
    // frame, applied to every cloud object of the slot. The rotation itself
    // is FUN_0043a940 [ENC]: about the vertical axis of the object [INF].
    const float speed = pWeather->getVector4( "clouds_cloud3dSpeed", Vector4f( 0.0f, 0.0f, 0.0f, 0.0f ) ).x;
    const float angle = speed / 1000.0f * gameDelta;
    const std::string source = bCloudLayers ? cloudSource : pWeather->getString( "clouds_cloud3dname" );
    const std::string destination = bCloudLayers ? cloudDestination : source;
    CloudSet *pSets[2] = { findCloudSet( source ), findCloudSet( destination ) };
    for ( int32_t slot = 0; slot < 2; slot++ ) {
        if ( pSets[slot] != nullptr && ( slot == 0 || pSets[1] != pSets[0] ) ) {
            pSets[slot]->Angle = std::fmod( pSets[slot]->Angle + angle, 2.0f * kPI );
        }
    }
}

void EdenSceneView::drawClouds( EdenRenderer &renderer ) {
    if ( !clouds.isLoaded() ) {
        return;
    }
    auto vector4 = [this]( const char *pName, const Vector4f &defaultValue ) {
        return pWeather != nullptr ? pWeather->getVector4( pName, defaultValue ) : defaultValue;
    };
    // FUN_00930e70, per slot (0: source clouds, 1: destination clouds of the
    // forecast blend f = GSWeather +0x28): slot fade = f, 1 - f
    // (FUN_0092dfa0), FUN_00930460: AlphaBlend = clouds_cloud3dAlphaBlend
    // with y *= 1 - fade (slots 0 / 1), the node colour = clouds_cloud3dcolor
    // (FUN_009306f0 -> node +0x80), clouds_cloud3dcolor2 -> ColorDown.
    // TD_CloudMesh: alpha = AlphaBlend.x, height scale = AlphaBlend.z;
    // TD_Clouds (billboards): alpha = AlphaBlend.y.
    const std::string defaultName = pWeather != nullptr ? pWeather->getString( "clouds_cloud3dname" ) : "CLOUD_B";
    const std::string source = bCloudLayers ? cloudSource : defaultName;
    const std::string destination = bCloudLayers ? cloudDestination : defaultName;
    CloudSet *pSets[2] = { findCloudSet( source ), findCloudSet( destination ) };
    float fades[2] = { bCloudLayers ? cloudBlend : 0.0f, bCloudLayers ? 1.0f - cloudBlend : 1.0f };
    if ( pSets[1] == pSets[0] ) {
        // Same set in both slots: one draw with the whole weight [INF].
        pSets[1] = nullptr;
        fades[0] = 0.0f;
    }
    const Vector4f alphaBlend = vector4( "clouds_cloud3dAlphaBlend", Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
    const float haze = pWeather != nullptr ? pWeather->getFloat( "Skydome_haze", 0.0f ) : 0.0f;
    const Render3DD &scene = clouds.getScene();
    const std::vector<SceneNode> &nodes = scene.getNodes();
    for ( int32_t slot = 0; slot < 2; slot++ ) {
        CloudSet *pSet = pSets[slot];
        if ( pSet == nullptr ) {
            continue;
        }
        for ( size_t n = 0; n < nodes.size(); n++ ) {
            clouds.setNodeVisible( static_cast<int32_t>( n ), pSet->Nodes[n] );
        }
        // Rotation of each object about its own origin (the position is kept,
        // FUN_009301c0 rewrites node +0x40 after the rotation).
        const Matrix4x4f rotation =
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), pSet->Angle ) );
        for ( size_t n = 0; n < nodes.size(); n++ ) {
            if ( !pSet->Nodes[n] ) {
                continue;
            }
            int32_t root = -1;
            for ( int32_t candidate : pSet->Roots ) {
                if ( scene.isDescendantOf( static_cast<int32_t>( n ), candidate ) ) {
                    root = candidate;
                }
            }
            const Vector3f pivot = nodes[root].World.getTranslation();
            clouds.setNodeTransform( static_cast<int32_t>( n ), Matrix4x4f::Translation( pivot ) * rotation *
                                                                    Matrix4x4f::Translation( -pivot ) * nodes[n].World );
        }
        EdenDrawParams params;
        params.Vectors["__Instance_Color"] = vector4( "clouds_cloud3dcolor", Vector4f( 1.0f, 1.0f, 1.0f, 0.0f ) );
        params.Vectors["ColorDown"] = vector4( "clouds_cloud3dcolor2", Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
        params.Vectors["AlphaBlend"] =
            Vector4f( alphaBlend.x, ( 1.0f - fades[slot] ) * alphaBlend.y, alphaBlend.z, alphaBlend.w );
        params.Vectors["__DirLight_Emissive_0"] = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
        params.Vectors["Haze"] = Vector4f( haze, haze, haze, haze );
        clouds.draw( renderer, Matrix4x4f::Identity(), &params );
    }
}
