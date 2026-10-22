#version 450
// OpenTDU2 replacement shader: builds the screen space "gViewPos" buffer read
// by TD_ShadowBox (s1) and the scene shaders (gTexSamplerShadowRealTime, s15)
// from the scene depth (D3D projection, depth in [0, 1]):
//   R: sun visibility (real time shadows: 1 = lit)
//   B, A: linear view depth, value = 2^(E - 15) * (1 + (B & 7) / 8 + A / 2040)
//         with E = B >> 3 (decoded in the original shaders as
//         exp2(floor(b * 31.875) - 15) * ((b * 255 - 8 floor(b * 31.875)) / 8 + a / 8 + 1)).
layout( set = 0, binding = 0 ) uniform sampler2D uDepth;
layout( push_constant ) uniform PushConstants {
    vec4 Params; // x: near, y: far
} pc;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

void main() {
    const float depth = texture( uDepth, inTexcoord ).r;
    const float zNear = pc.Params.x;
    const float zFar = pc.Params.y;
    // Sky (dome pushed to the far plane): the dome's z prepass writes its own
    // w, the view depth of a unit direction (~1): the haze leaves it alone.
    const float viewZ = depth >= 0.9999999 ? 1.0 : zNear * zFar / ( zFar - depth * ( zFar - zNear ) );
    const float value = clamp( viewZ, exp2( -15.0 ), 65535.0 );
    float exponent = floor( log2( value ) );
    float mantissa = value / exp2( exponent ) - 1.0; // [0, 1)
    exponent = clamp( exponent + 15.0, 0.0, 31.0 );
    const float high = floor( mantissa * 8.0 );
    const float low = clamp( floor( ( mantissa * 8.0 - high ) * 255.0 + 0.5 ), 0.0, 255.0 );
    const float byteB = exponent * 8.0 + high;
    outColor = vec4( 1.0, 0.0, byteB / 255.0, low / 255.0 );
}
