#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inUvX;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inUvY;
layout(location = 4) in vec4 inColor;

layout(push_constant) uniform constants {
    mat4 worldMatrix;
} PushConstants;

layout(location = 0) out vec3 outColor;

void main() {
    gl_Position = PushConstants.worldMatrix * vec4(inPosition, 1.0);
    outColor = inColor.rgb;
}
