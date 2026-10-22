#version 450
layout( set = 0, binding = 0 ) uniform sampler2D uTexture;

layout( push_constant ) uniform PushConstants {
    mat4 Projection;
    vec4 Params;
} pc;

layout( location = 0 ) in vec2 inTexcoord;
layout( location = 1 ) in vec4 inColor;
layout( location = 0 ) out vec4 outColor;

void main() {
    vec4 texel = texture( uTexture, inTexcoord );
    if ( pc.Params.x > 0.5 ) {
        texel = vec4( 1.0, 1.0, 1.0, texel.r );
    }
    vec4 color = texel * inColor;
    float luma = dot( color.rgb, vec3( 0.299, 0.587, 0.114 ) );
    color.rgb = mix( vec3( luma ), color.rgb, pc.Params.y );
    outColor = color;
}
