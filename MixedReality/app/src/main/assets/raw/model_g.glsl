#version 320 es

in vec3 vWorldPos;
in vec3 vWorldNormal;
//in vec2 vTexcoord0;

uniform vec3 eyePos;

layout (points) in;
layout (line_strip, max_vertices = 2) out;
uniform int u_bStar;
//--------------------------------------------------------------------------------------
void main()
//--------------------------------------------------------------------------------------
{
    gl_Position = gl_in[0].gl_Position;

    /*if (u_bStar == 1){
       gl_Position = gl_in[0].gl_Position + vec4( 0.0, 0.1, 0.0, 0.0);
        EmitVertex();

        gl_Position = gl_in[0].gl_Position + vec4( 0.0, -0.1, 0.0, 0.0);
        EmitVertex();

        EndPrimitive();
    }
    else{
        gl_Position = gl_in[0].gl_Position + vec4(-0.02, 0.0, 0.0, 0.0);
        EmitVertex();
        gl_Position = gl_in[0].gl_Position + vec4( 0.02, 0.0, 0.0, 0.0);
        EmitVertex();

        EndPrimitive();
    }*/
}

