#version 450

// One triangle over the whole target; Blur.frag does the rest per pixel. The parts of the
// triangle outside the target are clipped away by the GPU.

vec2 positions[3] = vec2[](
	vec2(-1.0, -1.0),
	vec2( 3.0, -1.0),
	vec2(-1.0,  3.0)
);

void main()
{
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
