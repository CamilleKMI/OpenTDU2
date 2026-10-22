#version 450
// OpenTDU2 replacement for the stencil restriction of the RT SHADOW 2D slice
// passes (FSShadowMatStencil, rain_shadow_spec B.5): every slice pass (the
// original shader, MIN-blended into its own buffer) only counts for the pixels
// whose view depth lies in that slice ([d(i-1), d(i)]); beyond the last split
// the pixel receives no sun shadow.
layout( set = 0, binding = 0 ) uniform sampler2D uViewPos;
layout( set = 0, binding = 1 ) uniform sampler2D uSlice0;
layout( set = 0, binding = 2 ) uniform sampler2D uSlice1;
layout( set = 0, binding = 3 ) uniform sampler2D uSlice2;
layout( push_constant ) uniform PushConstants {
    vec4 Splits; // xyz: split distances
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

void main() {
    const vec4 viewPos = texture( uViewPos, inTexcoord );
    const float b = floor( viewPos.b * 255.0 + 0.5 );
    const float a = floor( viewPos.a * 255.0 + 0.5 );
    const float exponent = floor( b / 8.0 );
    const float viewZ = exp2( exponent - 15.0 ) * ( 1.0 + ( b - exponent * 8.0 ) / 8.0 + a / 2040.0 );
    float lit = 1.0;
    if ( pc.Splits.w > 0.5 ) {
        // Debug: one slice (w - 1) everywhere.
        const int slice = int( pc.Splits.w + 0.5 ) - 1;
        lit = slice == 0 ? texture( uSlice0, inTexcoord ).r
            : slice == 1 ? texture( uSlice1, inTexcoord ).r : texture( uSlice2, inTexcoord ).r;
    } else if ( viewZ < pc.Splits.x ) {
        lit = texture( uSlice0, inTexcoord ).r;
    } else if ( viewZ < pc.Splits.y ) {
        lit = texture( uSlice1, inTexcoord ).r;
    } else if ( viewZ < pc.Splits.z ) {
        lit = texture( uSlice2, inTexcoord ).r;
    }
    outColor = vec4( lit, 0.0, 0.0, 1.0 );
}
