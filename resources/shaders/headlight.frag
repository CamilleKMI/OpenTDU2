#version 450
// OpenTDU2 replacement for the HEADLIGHTS pass (docs/re/car_lights_spec.md 8):
// one projected spot light per car into HeadLightsRT (R16F, additive), the
// math of edrender_pfx_lightness ps_1 / ps_2 [SHADER]:
//   P = view position of the pixel (Zpass depth), L = P - Spot_Position,
//   d = |L|, c = dot(L / d, Spot_Direction),
//   value = (sat(c * att.w + att.z) * sat(att.x - d * att.y))^2 * Spot_Color.x
//           * gProjTexture(ProjMatrix * P).y * vars.z * near drop-off,
//   drop-off = 0 below DropOffNear - 0.5, 2 (d - (DropOffNear - 0.5)) up to
//   DropOffNear, 1 beyond; out = min(value, 32).
// with the _sat result modifiers of ps_1 on c, the angular and the distance
// terms. Computed here in the spot frame (origin = spot, +Z = direction,
// rigid: same d and c). The original also restricts the pixels with the
// stencil of the spot cone geometry (HL_ConeGeom / HL_Stencil, FUN_00922d60
// [CODE], geometry not decoded): outside the cone and beyond far the
// saturated terms are 0 anyway.
layout( set = 0, binding = 0 ) uniform sampler2D uViewPos;   // gViewPos: B/A = view depth
layout( set = 0, binding = 1 ) uniform sampler2D uProjTexture; // projectivespotlighttexture
layout( push_constant ) uniform PushConstants {
    vec4 Recon;       // x: 1 / P00, y: 1 / P11 (camera frame: x right, y up, z forward)
    vec4 ToSpot[3];   // rows: camera frame -> spot frame (rigid)
    vec4 Attenuation; // Spot_Attenuation (x, y, z, w)
    vec4 Params;      // x: DropOffNear, y: Spot_Color.x * vars.z, z: tan(fov / 2), w: far
    vec4 Cone;        // x: cos(outer half angle), y: debug output (-hldebug: 1 distance / 10, 2 cos, 3 uv)
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

void main() {
    const vec4 viewPos = texelFetch( uViewPos, ivec2( gl_FragCoord.xy ), 0 ); // encoded depth: not filtered
    const float b = floor( viewPos.b * 255.0 + 0.5 );
    const float a = floor( viewPos.a * 255.0 + 0.5 );
    const float exponent = floor( b / 8.0 );
    const float viewZ = exp2( exponent - 15.0 ) * ( 1.0 + ( b - exponent * 8.0 ) / 8.0 + a / 2040.0 );
    const vec2 ndc = vec2( inTexcoord.x * 2.0 - 1.0, 1.0 - inTexcoord.y * 2.0 );
    const vec4 camera = vec4( ndc.x * viewZ * pc.Recon.x, ndc.y * viewZ * pc.Recon.y, viewZ, 1.0 );
    const vec3 p = vec3( dot( pc.ToSpot[0], camera ), dot( pc.ToSpot[1], camera ), dot( pc.ToSpot[2], camera ) );
    const float d = length( p );
    float value = 0.0;
    if ( d > 0.0 ) {
        // dp3_sat, mad_sat x 2 (result modifiers of ps_1) [SHADER].
        const float c = clamp( p.z / d, 0.0, 1.0 );
        // ProjMatrix + texldp (ps_1 texld control 1 = projective): perspective
        // of the spot (fov slot +0x368) -> D3D texture coordinates.
        const float w = max( p.z, 1e-6 );
        const vec2 uv = vec2( 0.5 + 0.5 * p.x / ( w * pc.Params.z ), 0.5 - 0.5 * p.y / ( w * pc.Params.z ) );
        const float projected = texture( uProjTexture, uv ).g;
        const float angular = clamp( c * pc.Attenuation.w + pc.Attenuation.z, 0.0, 1.0 );
        const float distance = clamp( pc.Attenuation.x - d * pc.Attenuation.y, 0.0, 1.0 );
        float term = angular * distance;
        term = term * term * pc.Params.y * projected;
        const float nearStart = pc.Params.x - 0.5;
        const float drop = d >= pc.Params.x ? 1.0 : ( d >= nearStart ? 2.0 * ( d - nearStart ) : 0.0 );
        value = min( term * drop, 32.0 );
    }
    if ( pc.Cone.y > 0.5 ) {
        const vec2 uv = vec2( 0.5 + 0.5 * p.x / ( p.z * pc.Params.z ), 0.5 - 0.5 * p.y / ( p.z * pc.Params.z ) );
        value = pc.Cone.y < 1.5 ? d / 10.0 : ( pc.Cone.y < 2.5 ? p.z / max( d, 0.001 ) : ( pc.Cone.y < 3.5 ? fract( uv.y ) : ( pc.Cone.y < 4.5 ? camera.y * 0.1 + 0.5 : p.y * 0.1 + 0.5 ) ) );
    }
    outColor = vec4( value, 0.0, 0.0, 1.0 );
}
