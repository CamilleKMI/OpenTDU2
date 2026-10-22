#version 450
layout( set = 0, binding = 0 ) uniform sampler2D uDiffuse;

layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 ModelRow0;
    vec4 ModelRow1;
    vec4 ModelRow2;
    vec4 MaterialColor;
} pc;

layout( location = 0 ) in vec3 inWorldNormal;
layout( location = 1 ) in vec2 inTexcoord;
layout( location = 2 ) in vec4 inColor;
layout( location = 3 ) in vec3 inWorldPosition;
layout( location = 0 ) out vec4 outColor;

const vec3 kSunDirection = normalize( vec3( 0.35, 0.8, 0.45 ) );
const vec3 kSunColor = vec3( 1.0, 0.95, 0.85 );
const vec3 kSkyColor = vec3( 0.45, 0.55, 0.7 );
const vec3 kGroundColor = vec3( 0.25, 0.22, 0.2 );

void main() {
    // MaterialColor.a flags: 1 = use vertex color, 2 = paint (glossy), 4 = alpha test,
    // 8 = unlit, 16 = glass
    int flags = int( pc.MaterialColor.a + 0.5 );
    vec4 albedo = texture( uDiffuse, inTexcoord );
    if ( ( flags & 4 ) != 0 && albedo.a < 0.4 ) {
        discard;
    }
    albedo.rgb *= pc.MaterialColor.rgb;
    if ( ( flags & 1 ) != 0 ) {
        albedo *= inColor;
    }
    if ( ( flags & 8 ) != 0 ) {
        outColor = albedo;
        return;
    }

    vec3 n = normalize( inWorldNormal );
    if ( !gl_FrontFacing ) {
        n = -n;
    }
    float ndotl = max( dot( n, kSunDirection ), 0.0 );
    vec3 ambient = mix( kGroundColor, kSkyColor, n.y * 0.5 + 0.5 );
    vec3 color = albedo.rgb * ( ambient * 0.9 + kSunColor * ndotl * 0.85 );

    if ( ( flags & 2 ) != 0 ) {
        // Approximated clear coat: fresnel-ish sky reflection + sun highlight.
        vec3 viewDir = normalize( vec3( 0.0, 0.35, 1.0 ) );
        vec3 h = normalize( kSunDirection + viewDir );
        float spec = pow( max( dot( n, h ), 0.0 ), 96.0 );
        float fresnel = pow( 1.0 - max( dot( n, viewDir ), 0.0 ), 3.0 );
        color += kSunColor * spec * 1.2 + kSkyColor * fresnel * 0.5;
    }
    float alpha = albedo.a;
    if ( ( flags & 16 ) != 0 ) {
        alpha = max( alpha, 0.35 );
    }
    outColor = vec4( color, alpha );
}
