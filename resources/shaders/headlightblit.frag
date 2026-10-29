#version 450
// OpenTDU2 replacement for HEADLIGHTS BLIT (docs/re/car_lights_spec.md 8):
// HeadLightsRT (light amount L, R16F) written into G / B of the s15 buffer
// (added: the combine pass leaves G = B = 0; BM_Additive = SRC_ALPHA / ONE on
// colour, alpha kept: out (0, y, z, 1) leaves R / A untouched)
// with edrender_pfx_lightness ps_0 [SHADER]:
//   t = 0.15625 (1 - 2^(-0.28853902 L)), t * 32768 + 0.01 = 2^E (1 + f),
//   y = (E * 8 + floor(8 f)) / 255, z = frac(8 f).
// The scene shaders decode G / B (depth style) and add x 32 to the lighting.
layout( set = 0, binding = 0 ) uniform sampler2D uHeadLights;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

void main() {
    const float light = texelFetch( uHeadLights, ivec2( gl_FragCoord.xy ), 0 ).r;
    const float t = ( 1.0 - exp2( light * -0.28853902 ) ) * 0.15625;
    const float e = floor( log2( t * 32768.0 + 0.01 ) );
    const float m = ( t * 32768.0 * exp2( -e ) - 1.0 ) * 8.0;
    const float low = fract( m );
    outColor = vec4( 0.0, ( e * 8.0 + ( m - low ) ) * 0.003921568859, low, 1.0 );
}
