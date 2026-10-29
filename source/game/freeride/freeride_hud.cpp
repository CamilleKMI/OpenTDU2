#include "shared.h"
#include "freeride_hud.h"

#include "database/gs_database.h"
#include "flash/flash_localization.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "flash/flash_movie.h"
#include "flash/gs_flash.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"
#include "physics/vehicle_collision.h"
#include "render/font.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

namespace {
uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) { return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ); }

void PushTriangle( std::vector<Vertex2D> &out, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color ) {
    out.push_back( { x0, y0, 0.0f, 0.0f, color } );
    out.push_back( { x1, y1, 0.0f, 0.0f, color } );
    out.push_back( { x2, y2, 0.0f, 0.0f, color } );
}

void PushQuad( std::vector<Vertex2D> &out, const Vector2f &a, const Vector2f &b, const Vector2f &c, const Vector2f &d,
               uint32_t color ) {
    PushTriangle( out, a.x, a.y, b.x, b.y, c.x, c.y, color );
    PushTriangle( out, a.x, a.y, c.x, c.y, d.x, d.y, color );
}

// Arc band between radii r0 / r1 from angle a0 to a1 (radians, screen: 0 =
// right, positive = clockwise since y points down).
void PushArc( std::vector<Vertex2D> &out, float cx, float cy, float r0, float r1, float a0, float a1, uint32_t color ) {
    const int32_t segments = Max( 2, static_cast<int32_t>( std::abs( a1 - a0 ) / 0.05f ) );
    for ( int32_t i = 0; i < segments; i++ ) {
        const float t0 = a0 + ( a1 - a0 ) * i / segments, t1 = a0 + ( a1 - a0 ) * ( i + 1 ) / segments;
        PushQuad( out, { cx + std::cos( t0 ) * r0, cy + std::sin( t0 ) * r0 }, { cx + std::cos( t0 ) * r1, cy + std::sin( t0 ) * r1 },
                  { cx + std::cos( t1 ) * r1, cy + std::sin( t1 ) * r1 }, { cx + std::cos( t1 ) * r0, cy + std::sin( t1 ) * r0 },
                  color );
    }
}

// Sutherland-Hodgman against an axis aligned rectangle.
std::vector<Vector2f> ClipToRect( std::vector<Vector2f> polygon, float x0, float y0, float x1, float y1 ) {
    for ( int32_t edge = 0; edge < 4 && !polygon.empty(); edge++ ) {
        auto inside = [&]( const Vector2f &p ) {
            switch ( edge ) {
            case 0: return p.x >= x0;
            case 1: return p.x <= x1;
            case 2: return p.y >= y0;
            default: return p.y <= y1;
            }
        };
        auto intersect = [&]( const Vector2f &a, const Vector2f &b ) {
            float t = 0.0f;
            switch ( edge ) {
            case 0: t = ( x0 - a.x ) / ( b.x - a.x ); break;
            case 1: t = ( x1 - a.x ) / ( b.x - a.x ); break;
            case 2: t = ( y0 - a.y ) / ( b.y - a.y ); break;
            default: t = ( y1 - a.y ) / ( b.y - a.y ); break;
            }
            return a + ( b - a ) * t;
        };
        std::vector<Vector2f> output;
        for ( size_t i = 0; i < polygon.size(); i++ ) {
            const Vector2f &current = polygon[i];
            const Vector2f &previous = polygon[( i + polygon.size() - 1 ) % polygon.size()];
            if ( inside( current ) ) {
                if ( !inside( previous ) ) {
                    output.push_back( intersect( previous, current ) );
                }
                output.push_back( current );
            } else if ( inside( previous ) ) {
                output.push_back( intersect( previous, current ) );
            }
        }
        polygon = std::move( output );
    }
    return polygon;
}

// Reference screenshot layout (1280 x 720 canvas) [INF: GAUGES.vmf missing].
constexpr float kGaugeX = 1107.0f, kGaugeY = 570.0f, kGaugeRadius = 84.0f;
constexpr float kMapX = 115.0f, kMapY = 454.0f, kMapSize = 190.0f;
constexpr float kMapMetersPerPixel = 3.0f; // [INF]
} // namespace

bool FreeRideHud::load( const std::string &hudFileName ) {
    unload();
    auto onCommand = []( FlashPlayer &player ) {
        return [&player]( const std::string &command, const std::string & ) {
            if ( command == "sbdd" ) {
                ApplyFlashLocalization( player );
            }
        };
    };
    hud.setFSCommandCallback( onCommand( hud ) );
    general.setFSCommandCallback( onCommand( general ) );
    bool bHud = false;
    if ( FlashMovie *pMovie = gpFlash->getMovie( "hud" ) ) {
        bHud = hud.load( pMovie );
        ApplyFlashLocalization( hud );
        // FREERIDE: the root clip HUD and its minimap frame shown (the root
        // `default` action hides them) [INF].
        hud.setClipVisible( "/HUD", true );
        hud.setClipVisible( "/HUD/minimap", true );
        if ( CmdLine::HasArg( "-dumphud" ) ) {
            dumpClips( *hud.getRoot(), "", 0 );
            // Timeline of a sprite (-dumpsprite <id>).
            FlashDefinitions definitions;
            definitions.build( pMovie->getVMF(), pMovie );
            if ( const FlashTimeline *pSprite = definitions.getSprite( static_cast<uint16_t>( CmdLine::GetArgInt( "-dumpsprite", 150 ) ) ) ) {
                for ( const auto &label : pSprite->Labels ) {
                    OTDU_LOG_INFO( "HUD sprite label '%s' frame %u\n", label.first.c_str(), label.second );
                }
                for ( size_t f = 0; f < pSprite->Frames.size(); f++ ) {
                    for ( const FlashControlTag &tag : pSprite->Frames[f] ) {
                        if ( tag.Type == eFlashControlType::CT_Place ) {
                            OTDU_LOG_INFO( "HUD sprite f%zu place depth %u char %u move %d name '%s'\n", f, tag.Place.Depth,
                                           tag.Place.bHasCharacter ? tag.Place.CharacterId : 0, tag.Place.bMove ? 1 : 0,
                                           tag.Place.Name.c_str() );
                        } else if ( tag.Type == eFlashControlType::CT_Remove ) {
                            OTDU_LOG_INFO( "HUD sprite f%zu remove depth %u\n", f, tag.Depth );
                        } else if ( tag.Type == eFlashControlType::CT_Action ) {
                            OTDU_LOG_INFO( "HUD sprite f%zu action at 0x%x len %u\n", f, tag.ActionOffset, tag.ActionLength );
                        }
                    }
                }
            }
            definitions.destroy();
        }
    } else {
        OTDU_LOG_WARN( "FreeRideHud: HUD movie (frontend/hires/hud.bnk) not found\n" );
    }
    if ( FlashMovie *pMovie = gpFlash->getMovie( "general" ) ) {
        general.load( pMovie );
        ApplyFlashLocalization( general );
    }
    // GAUGES (FUN_00965810): /:WhatFormat "4_3" / "16_9" (DAT_00fe4c78
    // [INF: the 16:9 flag]), /:Gear "0", Speed_c/d/u digits, /:Km, /:Centaine.
    if ( FlashMovie *pMovie = gpFlash->getMovie( "gauges" ) ) {
        gauges.setFSCommandCallback( onCommand( gauges ) );
        if ( gauges.load( pMovie ) ) {
            ApplyFlashLocalization( gauges );
            gaugeDefinitions.build( pMovie->getVMF(), pMovie );
            const bool b169 = gpConfig != nullptr && gpConfig->getVideoSettings().ScreenRatio == 1;
            gauges.setVariable( "/:WhatFormat", b169 ? "16_9" : "4_3" );
            gauges.setVariable( "/:Gear", "0" );
            // Root label "on" (HUD fade in) [INF: the movie enable call
            // FUN_00d7fbe0 is not followed].
            gauges.gotoLabel( "/", "on", true );
            loadGaugeBank( hudFileName );
        }
    } else {
        OTDU_LOG_WARN( "FreeRideHud: GAUGES movie (frontend/hires/gauges.bnk) not found: placeholder gauge [INF]\n" );
    }
    return bHud;
}

void FreeRideHud::unload() {
    hud.unload();
    general.unload();
    gauges.unload();
    gaugeDefinitions.destroy();
    if ( !gaugeBank.getEntries().empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( gaugeBank.getPath().c_str() );
    }
    gaugeBank.unload();
    pDialTexture = pNeedleTexture = nullptr;
    needleTables.clear();
    goalTimer = -1.0f;
}

void FreeRideHud::dumpClips( const FlashPlayer::Clip &clip, const std::string &path, int32_t depth ) {
    for ( const auto &entry : clip.DisplayList ) {
        const FlashPlayer::Instance &instance = *entry.second;
        const std::string name = instance.Name.empty() ? "#" + std::to_string( instance.Depth ) : instance.Name;
        OTDU_LOG_INFO( "HUD %s/%s char %u at (%.1f, %.1f) scale (%.2f, %.2f) visible %d frames %u\n", path.c_str(),
                       name.c_str(), instance.CharacterId, instance.Matrix.TX, instance.Matrix.TY, instance.Matrix.A,
                       instance.Matrix.D, instance.bVisible ? 1 : 0,
                       instance.pClip && instance.pClip->pTimeline ? instance.pClip->pTimeline->FrameCount : 0 );
        if ( instance.pClip && depth < 4 ) {
            dumpClips( *instance.pClip, path + "/" + name, depth + 1 );
        }
    }
}

void FreeRideHud::showGoal( const std::string &text ) {
    // FUN_00d61790: /:GoalTxt, pop_goal goto "open" (/:IsWalkmode false),
    // 30 s timer (DAT_00ede1fc).
    if ( !general.isLoaded() ) {
        return;
    }
    general.setVariableBool( "/:IsWalkmode", false );
    general.setVariable( "/:GoalTxt", text );
    general.gotoLabel( "/pop_goal", "open", true );
    goalTimer = 30.0f;
}

void FreeRideHud::closeGoal() {
    if ( general.isLoaded() && goalTimer >= 0.0f ) {
        general.invoke( "/pop_goal", "close_popup", {} );
    }
    goalTimer = -1.0f;
}

void FreeRideHud::setTestDrive( bool bEnable ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    hud.setVariableBool( "/:IsTest", bEnable );
    hud.setClipVisible( "/HUD/time_cd", bEnable );
    if ( bEnable ) {
        hud.gotoLabel( "/HUD/time_cd", "open", true );
    }
}

void FreeRideHud::openAccessoryMenu( bool bHasHood, bool bHasWindows, bool bHasTurnSignals ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    // FUN_00928840 [CODE]: /:HasHood = car vfunc +0x34 (FUN_00c66250),
    // /:HasWindows = FUN_009287b0, /:HasTurnSignals = FUN_00928720. /:IsBike
    // (SetIcons picks the bike icon set when it is not false) = vehicle
    // category 0x12 / 0x13 / 0x16 (e.g. 0x00a7676a) [CODE]: false for a car.
    // Variant type 7 (int 0 / 1) for the Has* flags, 5 (bool) for IsBike.
    hud.setVariableNumber( "/:HasHood", bHasHood ? 1.0 : 0.0 );
    hud.setVariableNumber( "/:HasWindows", bHasWindows ? 1.0 : 0.0 );
    hud.setVariableNumber( "/:HasTurnSignals", bHasTurnSignals ? 1.0 : 0.0 );
    hud.setVariableBool( "/:IsBike", false );
    // FUN_00928c10: "open" on HUD/ig_menu [CODE] (the HUD/radio vfunc +0x100
    // (2.0) call next to it is not identified [INF: left out]).
    hud.setClipVisible( "/HUD/ig_menu", true );
    hud.gotoLabel( "/HUD/ig_menu", "open", true );
}

void FreeRideHud::validAccessory( const std::string &label ) {
    if ( hud.isLoaded() ) {
        // FUN_00928e70: HUD/ig_menu vfunc +0xe4 ("ValidIGM", label) [CODE].
        hud.invoke( "/HUD/ig_menu", "ValidIGM", { label } );
    }
}

void FreeRideHud::closeAccessoryMenu() {
    if ( hud.isLoaded() ) {
        hud.gotoLabel( "/HUD/ig_menu", "close", true );
    }
}

void FreeRideHud::setCountdown( float remaining ) {
    if ( !hud.isLoaded() ) {
        return;
    }
    // FUN_00a11120: ms = (uint)ROUND(t * 1000.0 + 0.5); min = ms / 60000,
    // sec = ms % 60000 / 1000, cent = ms % 1000 / 10 [CODE]. Digits "%02d"
    // (binding width 2) [INF].
    const uint32_t ms = static_cast<uint32_t>( std::lround( std::max( remaining, 0.0f ) * 1000.0 + 0.5 ) );
    char text[16];
    snprintf( text, sizeof( text ), "%02u", ms / 60000u );
    hud.setVariable( "/:MinCd", text );
    snprintf( text, sizeof( text ), "%02u", ( ms % 60000u ) / 1000u );
    hud.setVariable( "/:SecCd", text );
    snprintf( text, sizeof( text ), "%02u", ( ( ms % 60000u ) % 1000u ) / 10u );
    hud.setVariable( "/:CentCd", text );
}

void FreeRideHud::tick( float deltaTime, const State &newState ) {
    state = newState;
    if ( goalTimer >= 0.0f ) {
        goalTimer -= deltaTime;
        if ( goalTimer < 0.0f ) {
            closeGoal();
        }
    }
    if ( hud.isLoaded() ) {
        // HUD root vars read by the movie (WhatGear "auto": automatic box).
        hud.setVariable( "/:WhatGear", "auto" );
        // FUN_009365a0 (GPS on the minimap): no GPS target (story step 2
        // clears it: DAT_01025938 = "", DAT_01025a60 / 64 = 0) ->
        // obj_Distance = "" and gps_Unit = "" (the movie default texts
        // "2500" / "mil" disappear) [CODE].
        hud.setVariable( "/:obj_Distance", "" );
        // FUN_00967f10: /:Rpm (int) in the HUD movie too.
        hud.setVariableNumber( "/:Rpm", static_cast<int32_t>( newState.Rpm ) );
        hud.setVariable( "/:gps_Unit", "" );
        hud.tick( deltaTime );
    }
    if ( general.isLoaded() ) {
        general.tick( deltaTime );
    }
    if ( gauges.isLoaded() ) {
        updateGauges();
        gauges.tick( deltaTime );
    }
    // Zone (DBCruisingZones i1 / i2) [INF lookup: area X >= 4 -> zone 2].
    const int32_t areaX = static_cast<int32_t>( std::floor( state.CarX / ( 8.0f * 768.0f ) ) );
    zoneIndex = areaX >= 4 ? 2 : 1;
    zoneName = LocStr( zoneIndex == 2 ? 3337 : 3336, zoneIndex == 2 ? "Ibiza Area 2" : "Ibiza Area 1" );
}

void FreeRideHud::draw( Renderer2D &renderer, const VehicleWorldCollision *pCollision ) {
    if ( !bShown ) {
        if ( general.isLoaded() ) {
            general.drawClipOnly( renderer, "/pop_goal" );
        }
        return;
    }
    if ( hud.isLoaded() ) {
        hud.draw( renderer );
    }
    renderer.begin( 1280.0f, 720.0f );
    drawMinimap( renderer, pCollision );
    if ( !gauges.isLoaded() ) {
        drawGauge( renderer );
    }
    if ( bZoneShown ) {
        drawZone( renderer );
    }
    renderer.end();
    if ( gauges.isLoaded() ) {
        // Dial texture (FUN_009661b0 / FUN_00966070 draw callbacks), the
        // movie, then the needle with the bank texture (FUN_009667a0 swaps
        // the aiguille bitmap) [INF: draw order].
        drawGaugeTextures( renderer, true );
        gauges.draw( renderer );
        drawGaugeTextures( renderer, false );
    }
    if ( general.isLoaded() ) {
        general.drawClipOnly( renderer, "/pop_goal" );
    }
}

void FreeRideHud::drawGauge( Renderer2D &renderer ) {
    // Placeholder of the GAUGES movie [INF layout from the reference
    // screenshot]: rev counter 0..rpm_max (x1000), red from _red_line, speed
    // (km/h), gear, odometer.
    std::vector<Vertex2D> vertices;
    const float a0 = 0.75f * kPI, a1 = 2.25f * kPI; // 270 degrees, clockwise from bottom left
    const float maxRpm = std::ceil( state.RpmMax / 1000.0f ) * 1000.0f;
    PushArc( vertices, kGaugeX, kGaugeY, 0.0f, kGaugeRadius, 0.0f, 2.0f * kPI, Rgba( 0, 0, 0, 110 ) );
    PushArc( vertices, kGaugeX, kGaugeY, kGaugeRadius - 4.0f, kGaugeRadius, a0, a1, Rgba( 230, 230, 230, 220 ) );
    const float redStart = a0 + ( a1 - a0 ) * Clamp( state.RedLine / maxRpm, 0.0f, 1.0f );
    PushArc( vertices, kGaugeX, kGaugeY, kGaugeRadius - 14.0f, kGaugeRadius - 4.0f, redStart, a1, Rgba( 200, 20, 20, 230 ) );
    for ( int32_t k = 0; k <= static_cast<int32_t>( maxRpm / 1000.0f ); k++ ) {
        const float a = a0 + ( a1 - a0 ) * k * 1000.0f / maxRpm;
        PushArc( vertices, kGaugeX, kGaugeY, kGaugeRadius - 16.0f, kGaugeRadius - 4.0f, a - 0.012f, a + 0.012f,
                 Rgba( 240, 240, 240, 255 ) );
    }
    const float needle = a0 + ( a1 - a0 ) * Clamp( state.Rpm / maxRpm, 0.0f, 1.0f );
    const Vector2f dir( std::cos( needle ), std::sin( needle ) ), side( -dir.y, dir.x );
    const Vector2f c( kGaugeX, kGaugeY );
    PushTriangle( vertices, c.x + side.x * 3.0f, c.y + side.y * 3.0f, c.x - side.x * 3.0f, c.y - side.y * 3.0f,
                  c.x + dir.x * ( kGaugeRadius - 8.0f ), c.y + dir.y * ( kGaugeRadius - 8.0f ), Rgba( 220, 30, 30, 255 ) );
    PushArc( vertices, kGaugeX, kGaugeY, 0.0f, 7.0f, 0.0f, 2.0f * kPI, Rgba( 20, 20, 20, 255 ) );
    // Odometer box.
    PushQuad( vertices, { 1040.0f, 662.0f }, { 1165.0f, 662.0f }, { 1165.0f, 684.0f }, { 1040.0f, 684.0f },
              Rgba( 20, 20, 20, 200 ) );
    renderer.drawTriangles( nullptr, vertices.data(), static_cast<uint32_t>( vertices.size() ) );

    const Font *pFont = gpFlash->getTitleFont();
    char text[32];
    for ( int32_t k = 0; k <= static_cast<int32_t>( maxRpm / 1000.0f ); k++ ) {
        const float a = a0 + ( a1 - a0 ) * k * 1000.0f / maxRpm;
        snprintf( text, sizeof( text ), "%d", k );
        renderer.drawText( pFont, text, kGaugeX + std::cos( a ) * ( kGaugeRadius - 28.0f ),
                           kGaugeY + std::sin( a ) * ( kGaugeRadius - 28.0f ) - 8.0f, 15.0f, Rgba( 235, 235, 235 ),
                           eTextAlign::TA_Center );
    }
    snprintf( text, sizeof( text ), "%03d", static_cast<int32_t>( std::abs( state.SpeedKmh ) + 0.5f ) );
    renderer.drawText( pFont, text, kGaugeX + 34.0f, kGaugeY + 18.0f, 22.0f, Rgba( 255, 210, 40 ), eTextAlign::TA_Center );
    const char *pGear = state.Gear < 0 ? "R" : nullptr;
    char gearText[8];
    if ( pGear == nullptr ) {
        if ( state.Gear == 0 ) {
            pGear = "N";
        } else {
            snprintf( gearText, sizeof( gearText ), "%d", state.Gear );
            pGear = gearText;
        }
    }
    renderer.drawText( pFont, pGear, kGaugeX + 34.0f, kGaugeY + 40.0f, 24.0f, Rgba( 255, 210, 40 ), eTextAlign::TA_Center );
    snprintf( text, sizeof( text ), "%09d", static_cast<int32_t>( state.OdometerKm ) );
    renderer.drawText( pFont, text, 1102.0f, 663.0f, 19.0f, Rgba( 240, 240, 240 ), eTextAlign::TA_Center, 2.0f );
}

void FreeRideHud::drawMinimap( Renderer2D &renderer, const VehicleWorldCollision *pCollision ) {
    std::vector<Vertex2D> vertices;
    const float x0 = kMapX, y0 = kMapY, x1 = kMapX + kMapSize, y1 = kMapY + kMapSize;
    PushQuad( vertices, { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 }, Rgba( 0, 0, 0, 170 ) );
    const Vector2f center( 0.5f * ( x0 + x1 ), y0 + 0.62f * kMapSize );
    const Vector3f forward( state.HeadingX, 0.0f, state.HeadingZ );
    const Vector3f right( -forward.z, 0.0f, forward.x );
    auto toMap = [&]( const Vector3f &p ) {
        const Vector3f d( p.x - state.CarX, 0.0f, p.z - state.CarZ );
        return Vector2f( center.x + d.dot( right ) / kMapMetersPerPixel, center.y - d.dot( forward ) / kMapMetersPerPixel );
    };
    if ( pCollision != nullptr ) {
        // GamePC.cpr [HudMap]: AIRL_ROAD unlocked colour 0 255 255 (the
        // reference shows white roads at this stage [INF]).
        const uint32_t roadColor = Rgba( 235, 235, 235, 255 );
        pCollision->forEachRoadTriangle( Vector3f( state.CarX, 0.0f, state.CarZ ), kMapSize * kMapMetersPerPixel,
                                         [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
                                             const std::vector<Vector2f> polygon =
                                                 ClipToRect( { toMap( a ), toMap( b ), toMap( c ) }, x0, y0, x1, y1 );
                                             for ( size_t i = 2; i < polygon.size(); i++ ) {
                                                 PushTriangle( vertices, polygon[0].x, polygon[0].y, polygon[i - 1].x,
                                                               polygon[i - 1].y, polygon[i].x, polygon[i].y, roadColor );
                                             }
                                         } );
    }
    if ( bTarget ) {
        // Target marker, clamped on the map border [INF].
        Vector2f m = toMap( Vector3f( targetX, 0.0f, targetZ ) );
        const Vector2f d( m.x - center.x, m.y - center.y );
        float scale = 1.0f;
        const float margin = 8.0f;
        if ( d.x > 0.0f && m.x > x1 - margin ) {
            scale = Min( scale, ( x1 - margin - center.x ) / d.x );
        }
        if ( d.x < 0.0f && m.x < x0 + margin ) {
            scale = Min( scale, ( x0 + margin - center.x ) / d.x );
        }
        if ( d.y > 0.0f && m.y > y1 - margin ) {
            scale = Min( scale, ( y1 - margin - center.y ) / d.y );
        }
        if ( d.y < 0.0f && m.y < y0 + margin ) {
            scale = Min( scale, ( y0 + margin - center.y ) / d.y );
        }
        m = Vector2f( center.x + d.x * scale, center.y + d.y * scale );
        PushQuad( vertices, { m.x, m.y - 7.0f }, { m.x + 7.0f, m.y }, { m.x, m.y + 7.0f }, { m.x - 7.0f, m.y },
                  Rgba( 40, 40, 40, 255 ) );
        PushQuad( vertices, { m.x, m.y - 5.0f }, { m.x + 5.0f, m.y }, { m.x, m.y + 5.0f }, { m.x - 5.0f, m.y },
                  Rgba( 255, 200, 0, 255 ) );
    }
    // Player arrow.
    PushTriangle( vertices, center.x, center.y - 9.0f, center.x - 6.0f, center.y + 6.0f, center.x + 6.0f, center.y + 6.0f,
                  Rgba( 40, 40, 40, 255 ) );
    PushTriangle( vertices, center.x, center.y - 7.0f, center.x - 4.0f, center.y + 4.0f, center.x + 4.0f, center.y + 4.0f,
                  Rgba( 255, 255, 255, 255 ) );
    renderer.drawTriangles( nullptr, vertices.data(), static_cast<uint32_t>( vertices.size() ) );
    renderer.drawRectOutline( Rect2D( x0, y0, kMapSize, kMapSize ), 2.0f, Rgba( 230, 230, 230, 255 ) );
    if ( bTarget ) {
        const float dx = targetX - state.CarX, dz = targetZ - state.CarZ;
        char distance[32];
        snprintf( distance, sizeof( distance ), "%.1f km", std::sqrt( dx * dx + dz * dz ) / 1000.0f );
        renderer.drawText( gpFlash->getTitleFont(), distance, x0 + 0.5f * kMapSize, y1 + 4.0f, 16.0f,
                           Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    }
}

void FreeRideHud::drawZone( Renderer2D &renderer ) {
    const Font *pFont = gpFlash->getTitleFont();
    renderer.drawRect( Rect2D( 1124.0f, 52.0f, 54.0f, 54.0f ), Rgba( 40, 120, 200, 230 ) );
    renderer.drawRectOutline( Rect2D( 1124.0f, 52.0f, 54.0f, 54.0f ), 2.0f, Rgba( 230, 230, 230, 255 ) );
    renderer.drawText( pFont, "IBIZA", 1151.0f, 54.0f, 12.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    char number[8];
    snprintf( number, sizeof( number ), "%d", zoneIndex );
    renderer.drawText( pFont, number, 1151.0f, 66.0f, 32.0f, Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    renderer.drawRect( Rect2D( 1022.0f, 112.0f, 156.0f, 24.0f ), Rgba( 0, 0, 0, 140 ) );
    renderer.drawText( pFont, zoneName.c_str(), 1030.0f, 114.0f, 17.0f, Rgba( 255, 255, 255 ) );
}

// ---------------------------------------------------------------------------
// GAUGES (docs: /tmp/claude-0/spec_freeride.md 4b)

float FreeRideHud::NeedleTable::map( float value ) const {
    // FUN_00968fe0: first key i with value < key[i] (or the last), clamp to
    // [key[i-1], key[i]], linear angle.
    const size_t n = Min( Values.size(), Angles.size() );
    if ( n < 2 ) {
        return n == 1 ? Angles[0] : 0.0f;
    }
    size_t segment = n - 1;
    for ( size_t i = 1; i < n; i++ ) {
        if ( value < Values[i] || i == n - 1 ) {
            segment = i;
            break;
        }
    }
    const float lo = Values[segment - 1], hi = Values[segment];
    float v = Min( value, hi );
    v = Max( v, lo );
    const float t = hi != lo ? ( v - lo ) / ( hi - lo ) : 1.0f;
    return ( 1.0f - t ) * Angles[segment - 1] + t * Angles[segment];
}

bool FreeRideHud::loadGaugeBank( const std::string &hudFileName ) {
    // FUN_0096fe90: "%sFrontEnd/%s/Gauges/%s.bnk" (HiRes, _hud_file_name).
    const std::string path = gpFile->getBankFolder() + "frontend/hires/gauges/" + ToLower( hudFileName ) + ".bnk";
    if ( hudFileName.empty() || !gaugeBank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "FreeRideHud: gauge bank '%s' not found\n", path.c_str() );
        return false;
    }
    TextureCache &textures = gpRender->getTextureCache();
    textures.loadBankTextures( gaugeBank, gaugeBank.getPath().c_str() );
    // Exterior view: ext_rpm dial, needle.2db [INF: texture by file name;
    // the FUN_0096fa50 arguments are not recovered].
    pDialTexture = textures.find( gaugeBank.getPath().c_str(), "ext_rpm" );
    pNeedleTexture = textures.find( gaugeBank.getPath().c_str(), "needle" );
    // hud.ini [HUD]: Style "<label>"; AddHud "<name>" v0 v1 a0 a1 [v a]...
    // (FUN_0096f720 needs more than 4 tokens; FUN_0096f670: N = (tokens -
    // 1) / 2 points: (t1, t3), (t5, t6), (t7, t8), (t9, t10), last = (t2, t4)).
    const BankEntry *pIni = gaugeBank.findEntry( "hud.ini" );
    if ( pIni != nullptr ) {
        const std::string text( reinterpret_cast<const char *>( pIni->pData ), pIni->Size );
        size_t start = 0;
        while ( start < text.size() ) {
            size_t end = text.find( '\n', start );
            if ( end == std::string::npos ) {
                end = text.size();
            }
            std::string line = text.substr( start, end - start );
            start = end + 1;
            const size_t comment = line.find( "//" );
            if ( comment != std::string::npos ) {
                line.resize( comment );
            }
            std::vector<std::string> tokens;
            size_t i = 0;
            while ( i < line.size() ) {
                while ( i < line.size() && isspace( static_cast<unsigned char>( line[i] ) ) ) {
                    i++;
                }
                if ( i >= line.size() ) {
                    break;
                }
                size_t j = i;
                if ( line[i] == '"' ) {
                    j = line.find( '"', i + 1 );
                    j = j == std::string::npos ? line.size() : j;
                    tokens.push_back( line.substr( i + 1, j - i - 1 ) );
                    i = j + 1;
                    continue;
                }
                while ( j < line.size() && !isspace( static_cast<unsigned char>( line[j] ) ) ) {
                    j++;
                }
                tokens.push_back( line.substr( i, j - i ) );
                i = j;
            }
            if ( tokens.size() >= 2 && StringCmpI( tokens[0].c_str(), "Style" ) == 0 ) {
                gaugeStyle = tokens[1];
            } else if ( tokens.size() > 5 && StringCmpI( tokens[0].c_str(), "AddHud" ) == 0 ) {
                // tokens[1] = name: FUN_0096f720 counts the name as token 0.
                std::vector<float> t;
                for ( size_t k = 1; k < tokens.size(); k++ ) {
                    t.push_back( static_cast<float>( atof( tokens[k].c_str() ) ) ); // t[0] = name (0)
                }
                const int32_t count = static_cast<int32_t>( tokens.size() - 1 );
                const int32_t points = ( count - 1 ) / 2;
                auto token = [&]( int32_t k ) { return k < static_cast<int32_t>( t.size() ) ? t[k] : 0.0f; };
                NeedleTable table;
                table.Values.assign( points, 0.0f );
                table.Angles.assign( points, 0.0f );
                table.Values[0] = token( 1 );
                table.Angles[0] = token( 3 );
                for ( int32_t p = 1; p < points - 1 && p < 4; p++ ) {
                    table.Values[p] = token( 2 * p + 3 );
                    table.Angles[p] = token( 2 * p + 4 );
                }
                table.Values[points - 1] = token( 2 );
                table.Angles[points - 1] = token( 4 );
                needleTables[tokens[1]] = table;
            }
        }
    }
    // FUN_0096f8c0: compteur_ext / compteur_int go to the Style label.
    if ( !gaugeStyle.empty() ) {
        gauges.gotoLabel( "/HUD/compteur_ext", gaugeStyle, false );
        gauges.gotoLabel( "/HUD/compteur_int", gaugeStyle, false );
    }
    OTDU_LOG_INFO( "FreeRideHud: gauge bank '%s': style '%s', %zu needle tables, dial %s, needle %s\n", path.c_str(),
                   gaugeStyle.c_str(), needleTables.size(), pDialTexture != nullptr ? "ok" : "missing",
                   pNeedleTexture != nullptr ? "ok" : "missing" );
    return true;
}

void FreeRideHud::updateGauges() {
    // FUN_00966ed0: interior views 22 / 42 -> compteur_int, cockpit views
    // 23 / 43 -> speed_cokpit(_sh), otherwise compteur_ext.
    const bool bInterior = state.CameraMode == 0x16 || state.CameraMode == 0x2a;
    const bool bCockpit = state.CameraMode == 0x17 || state.CameraMode == 0x2b;
    gauges.setClipVisible( "/HUD/speed_cokpit", bCockpit );
    gauges.setClipVisible( "/HUD/speed_cokpit_sh", bCockpit );
    gauges.setClipVisible( "/HUD/compteur_ext", !bCockpit && !bInterior );
    gauges.setClipVisible( "/HUD/compteur_int", !bCockpit && bInterior );
    // The aiguille is drawn here with the bank needle (hidden in the movie).
    gauges.setClipVisible( "/HUD/compteur_ext/aiguille", false );
    // FUN_00964ef0 snapshot: gear char (car+0x250: 0 'R', 1 'N', 2.. '1'..),
    // speed x 3.6 (int), rpm = rad/s x 30 / pi.
    char gear[4] = { 'N', 0, 0, 0 };
    if ( state.Gear < 0 ) {
        gear[0] = 'R';
    } else if ( state.Gear > 0 && state.Gear <= 9 ) {
        gear[0] = static_cast<char>( '0' + state.Gear );
    }
    gauges.setVariable( "/:Gear", gear );
    // FUN_00968e20: Speed_u / _d / _c = digits of the speed clamped 0..999.
    int32_t speed = Clamp( static_cast<int32_t>( std::abs( state.SpeedKmh ) ), 0, 999 );
    for ( const char *pName : { "/:Speed_u", "/:Speed_d", "/:Speed_c" } ) {
        gauges.setVariable( pName, std::to_string( speed % 10 ) );
        speed /= 10;
    }
    // FUN_00968ef0: /:Km 0..99999999 (8 digits, zero padded), /:Centaine
    // 0..99 (1 digit) [INF: km and hundreds of metres of the odometer].
    char text[32];
    snprintf( text, sizeof( text ), "%08d", Clamp( static_cast<int32_t>( state.OdometerKm ), 0, 99999999 ) );
    gauges.setVariable( "/:Km", text );
    snprintf( text, sizeof( text ), "%d",
              Clamp( static_cast<int32_t>( ( state.OdometerKm - std::floor( state.OdometerKm ) ) * 10.0f ), 0, 9 ) );
    gauges.setVariable( "/:Centaine", text );
    // /:Rpm (int) in HUD and GAUGES.
    gauges.setVariableNumber( "/:Rpm", static_cast<int32_t>( state.Rpm ) );
    // Exterior needle: AddHud "exterior_rpm" (rpm -> degrees).
    auto it = needleTables.find( "exterior_rpm" );
    needleAngle = it != needleTables.end() ? it->second.map( state.Rpm ) : 0.0f;
}

bool FreeRideHud::clipMatrix( const std::string &path, FlashMatrix &outMatrix, uint16_t &outCharacter ) {
    FlashPlayer::Clip *pClip = gauges.findClip( path );
    if ( pClip == nullptr || pClip->pInstance == nullptr ) {
        return false;
    }
    outCharacter = pClip->pInstance->CharacterId;
    FlashMatrix matrix;
    for ( FlashPlayer::Clip *pCurrent = pClip; pCurrent != nullptr && pCurrent->pInstance != nullptr;
          pCurrent = pCurrent->pParent ) {
        matrix = pCurrent->pInstance->Matrix * matrix;
    }
    outMatrix = matrix;
    return true;
}

void FreeRideHud::beginGaugeStage( Renderer2D &renderer ) const {
    // Same destination rect as FlashPlayer::beginStage (FUN_00d7faa0).
    static constexpr float kRects[5][4] = {
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, -1.0f / 6.0f, 1.0f, 4.0f / 3.0f },
        { 0.0f, -0.125f, 1.0f, 1.25f },
        { 0.0f, -1.0f / 6.0f, 1.0f, 4.0f / 3.0f },
        { 1.0f / 3.0f, -1.0f / 6.0f, 1.0f / 3.0f, 4.0f / 3.0f },
    };
    const int32_t mode = gpConfig != nullptr ? gpConfig->getVideoSettings().ScreenRatio : 0;
    const float *pRect = kRects[mode >= 0 && mode < 5 ? mode : 0];
    const VMFFile &vmf = gauges.getMovie()->getVMF();
    renderer.beginRect( vmf.getStageWidth(), vmf.getStageHeight(), pRect[0], pRect[1], pRect[2], pRect[3] );
}

void FreeRideHud::drawGaugeTextures( Renderer2D &renderer, bool bDial ) {
    if ( !gauges.isLoaded() || ( bDial ? pDialTexture : pNeedleTexture ) == nullptr ) {
        return;
    }
    FlashPlayer::Clip *pExterior = gauges.findClip( "/HUD/compteur_ext" );
    if ( pExterior == nullptr || pExterior->pInstance == nullptr || !pExterior->pInstance->bVisible ) {
        return;
    }
    FlashMatrix matrix;
    uint16_t character = 0;
    if ( !clipMatrix( bDial ? "/HUD/compteur_ext/c_ext" : "/HUD/compteur_ext/aiguille", matrix, character ) ) {
        return;
    }
    FlashMatrix stageToCanvas;
    stageToCanvas.A = stageToCanvas.D = 1.0f / 20.0f;
    if ( !bDial ) {
        // _rotation of the aiguille (FUN_00968d60, vfunc +0x90) [INF:
        // degrees, clockwise, the placement scale kept].
        FlashPlayer::Clip *pNeedle = gauges.findClip( "/HUD/compteur_ext/aiguille" );
        const FlashMatrix &base = pNeedle->pInstance->Matrix;
        const float sx = std::sqrt( base.A * base.A + base.B * base.B );
        const float sy = std::sqrt( base.C * base.C + base.D * base.D );
        const float angle = needleAngle * kDegToRad;
        FlashMatrix rotated = base;
        rotated.A = sx * std::cos( angle );
        rotated.B = sx * std::sin( angle );
        rotated.C = -sy * std::sin( angle );
        rotated.D = sy * std::cos( angle );
        FlashMatrix parent;
        if ( !clipMatrix( "/HUD/compteur_ext", parent, character ) ) {
            return;
        }
        matrix = parent * rotated;
        character = pNeedle->pInstance->CharacterId;
    }
    // Shapes of the sprite (frame 0, nested sprites included).
    std::vector<Vertex2D> vertices;
    float boundsMin[2] = { 1e9f, 1e9f }, boundsMax[2] = { -1e9f, -1e9f };
    std::function<void( uint16_t, const FlashMatrix & )> visit = [&]( uint16_t id, const FlashMatrix &m ) {
        if ( const FlashShape *pShape = gaugeDefinitions.getShape( id ) ) {
            const FlashMatrix toCanvas = stageToCanvas * m;
            for ( const FlashShapeMesh &mesh : pShape->Meshes ) {
                const FlashFill &fill = pShape->Fills[mesh.FillIndex];
                for ( uint32_t index : mesh.Indices ) {
                    const float x = mesh.Positions[index * 2], y = mesh.Positions[index * 2 + 1];
                    Vertex2D vertex;
                    toCanvas.transform( x, y, vertex.X, vertex.Y );
                    boundsMin[0] = Min( boundsMin[0], vertex.X );
                    boundsMin[1] = Min( boundsMin[1], vertex.Y );
                    boundsMax[0] = Max( boundsMax[0], vertex.X );
                    boundsMax[1] = Max( boundsMax[1], vertex.Y );
                    float fx = 0.0f, fy = 0.0f;
                    fill.InverseMatrix.transform( x, y, fx, fy );
                    // The bank texture replaces the whole bitmap.
                    vertex.U = fx / Max( fill.BitmapWidth, 1.0f );
                    vertex.V = fy / Max( fill.BitmapHeight, 1.0f );
                    vertex.Color = 0xffffffffu;
                    if ( fill.Type == eFlashFillType::FF_Bitmap ) {
                        vertices.push_back( vertex );
                    }
                }
            }
            return;
        }
        if ( const FlashTimeline *pSprite = gaugeDefinitions.getSprite( id ) ) {
            if ( pSprite->Frames.empty() ) {
                return;
            }
            for ( const FlashControlTag &tag : pSprite->Frames[0] ) {
                if ( tag.Type == eFlashControlType::CT_Place && tag.Place.bHasCharacter ) {
                    visit( tag.Place.CharacterId, m * tag.Place.Matrix );
                }
            }
        }
    };
    visit( character, matrix );
    beginGaugeStage( renderer );
    if ( bDial ) {
        // FUN_009661b0: the dial texture on the c_ext rectangle, UV (0, 0)
        // - (1, 1) above 640 pixels wide (DAT_00f90bbc) [CODE].
        if ( boundsMax[0] > boundsMin[0] ) {
            renderer.drawQuad( pDialTexture, Rect2D( boundsMin[0], boundsMin[1], boundsMax[0] - boundsMin[0], boundsMax[1] - boundsMin[1] ),
                               Rect2D( 0.0f, 0.0f, 1.0f, 1.0f ), 0xffffffffu );
        }
    } else if ( !vertices.empty() ) {
        renderer.drawTriangles( pNeedleTexture, vertices.data(), static_cast<uint32_t>( vertices.size() ) );
    }
    renderer.end();
}
