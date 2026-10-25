// src/render/vulkan/present/present.frag -- the pre-rotated present pass's fragment shader (NOT RETAIL).
// OpenBA's own shader, not game content: copies the landscape frame into the rotated swap-chain image.
// Regenerate present_spirv.h with tools/vulkan/gen_present_spirv.sh after an edit.
#version 450

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    color = texture(source, uv);
}
