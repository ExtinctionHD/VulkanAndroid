#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform Space
{
    mat4 view;
    mat4 proj;
};

layout(set = 1, binding = 1) uniform Tranformation
{
    mat4 transformation;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 outPos;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec3 outTangent;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{	
    outPos = vec3(transformation * vec4(inPos, 1.0f));
    outUV = inUV;
    
    outNormal = vec3(transformation * vec4(inNormal, 0.0f));
    outTangent = vec3(transformation * vec4(inTangent, 0.0f));

	gl_Position = proj * view * transformation * vec4(inPos, 1.0f);
}