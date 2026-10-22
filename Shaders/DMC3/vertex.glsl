#version 450 core

layout (location = 0) in vec3 position;
layout (location = 2) in vec2 uv;
layout (location = 3) in vec4 color;
layout (location = 4) in uvec4 boneIndices;
layout (location = 5) in vec4 boneWeights;

out vec2 vertexUv;
out vec4 vertexColor;

layout(std140, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
};

uniform mat4 model;
/* Locations and size follow HasSkinLocation, BonesLocation and MaxBones in Shader.h. */
layout(location = 2) uniform bool hasSkin;
layout(location = 3) uniform mat4 bones[64];

void main() {
    vec4 local = vec4(position, 1.0);
    if (hasSkin) {
        mat4 skin = boneWeights.x * bones[boneIndices.x] + boneWeights.y * bones[boneIndices.y] + boneWeights.z * bones[boneIndices.z] + boneWeights.w * bones[boneIndices.w];
        local = skin * local;
    }
    gl_Position = projection * view * model * local;
    vertexUv = uv;
    vertexColor = color;
}
