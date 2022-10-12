#version 320 es

/*in vec3 vWorldPos;
in vec3 vWorldNormal;*/
//in vec2 vTexcoord0;

uniform vec3 eyePos;

layout (points) in;
layout (line_strip, max_vertices = 4) out;
uniform int u_bStar;
//--------------------------------------------------------------------------------------
void main()
//--------------------------------------------------------------------------------------
{
//    gl_Position = gl_in[0].gl_Position;

       gl_Position = gl_in[0].gl_Position + vec4( 0.0, 0.1, 0.0, 0.0);
        EmitVertex();

        gl_Position = gl_in[0].gl_Position + vec4( 0.0, -0.1, 0.0, 0.0);
        EmitVertex();

        EndPrimitive();

        gl_Position = gl_in[0].gl_Position + vec4(-0.1, 0.0, 0.0, 0.0);
        EmitVertex();
        gl_Position = gl_in[0].gl_Position + vec4( 0.1, 0.0, 0.0, 0.0);
        EmitVertex();

        EndPrimitive();

}

