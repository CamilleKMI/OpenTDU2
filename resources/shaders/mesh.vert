#version 450
// OpenTDU2 replacement shader: lit textured geometry (.3DG) until the
// original TD_* shader libraries (.sob) are wired to the material system.
layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec3 inNormal;
layout( location = 2 ) in vec2 inTexcoord;
layout( location = 3 ) in vec4 inColor;

layout( push_constant ) uniform PushConstants {
    mat4 ViewProjection;
    vec4 ModelRow0;
    vec4 ModelRow1;
    vec4 ModelRow2;
    vec4 MaterialColor; // rgb: tint, a: flags
} pc;

layout( location = 0 ) out vec3 outWorldNormal;
layout( location = 1 ) out vec2 outTexcoord;
layout( location = 2 ) out vec4 outColor;
layout( location = 3 ) out vec3 outWorldPosition;

void main() {
    mat4 model = transpose( mat4( pc.ModelRow0, pc.ModelRow1, pc.ModelRow2, vec4( 0, 0, 0, 1 ) ) );
    vec4 worldPosition = model * vec4( inPosition, 1.0 );
    outWorldNormal = normalize( mat3( model ) * inNormal );
    outTexcoord = inTexcoord;
    outColor = inColor;
    outWorldPosition = worldPosition.xyz;
    gl_Position = pc.ViewProjection * worldPosition;
}
