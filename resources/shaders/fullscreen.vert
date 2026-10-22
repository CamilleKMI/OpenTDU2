#version 450
// OpenTDU2 replacement shader: fullscreen triangle (post processing passes).
// Texcoords: (0, 0) top left, like D3D (render target row 0 = top).
layout( location = 0 ) out vec2 outTexcoord;

void main() {
    const vec2 position = vec2( ( gl_VertexIndex << 1 ) & 2, gl_VertexIndex & 2 );
    outTexcoord = position;
    gl_Position = vec4( position * 2.0 - 1.0, 0.0, 1.0 );
}
