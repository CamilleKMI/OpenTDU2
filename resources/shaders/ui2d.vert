#version 450
// OpenTDU2 replacement shader: 2D batches (frontend, HUD, debug UI).
layout( location = 0 ) in vec2 inPosition;
layout( location = 1 ) in vec2 inTexcoord;
layout( location = 2 ) in vec4 inColor;

layout( push_constant ) uniform PushConstants {
    mat4 Projection;
    vec4 Params; // x: texture mode (0: rgba, 1: alpha only), y: saturation
} pc;

layout( location = 0 ) out vec2 outTexcoord;
layout( location = 1 ) out vec4 outColor;

void main() {
    outTexcoord = inTexcoord;
    outColor = inColor;
    gl_Position = pc.Projection * vec4( inPosition, 0.0, 1.0 );
}
