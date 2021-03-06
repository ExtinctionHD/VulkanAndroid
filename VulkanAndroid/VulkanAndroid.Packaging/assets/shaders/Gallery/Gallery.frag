#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 1) uniform Lighting
{
    vec3 direction;
    float directedIntensity;
    vec3 cameraPos;
    float transitionFactor;
    float ambientIntensity;
};

layout(set = 1, binding = 0) uniform sampler2DArray photo;

layout(set = 1, binding = 2) uniform Parameters
{
    float index;
    float opacity;
};

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = vec4(texture(photo, vec3(inUV, index)).rgb, opacity);
}