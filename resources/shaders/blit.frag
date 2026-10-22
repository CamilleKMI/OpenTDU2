#version 450
// OpenTDU2 replacement shader: texture copy (composites the 3D scene under
// the frontend).
layout( set = 0, binding = 0 ) uniform sampler2D uSource;
layout( location = 0 ) in vec2 inTexcoord;
layout( location = 0 ) out vec4 outColor;

void main() {
    outColor = vec4( texture( uSource, inTexcoord ).rgb, 1.0 );
}
