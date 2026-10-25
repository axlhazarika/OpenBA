// src/render/vulkan/present/present.vert -- the pre-rotated present pass's vertex shader (NOT RETAIL).
// OpenBA's own shader, not game content. One oversized triangle over the swap-chain image; `st` is the
// swap-chain image's normalised position (0,0 top left), and the source (the landscape frame) is read
// at uv = A * st + b, the inverse of the surface's currentTransform (present.cpp).
// Regenerate present_spirv.h with tools/vulkan/gen_present_spirv.sh after an edit.
#version 450

layout(push_constant) uniform Transform {
    vec4 a;   // A row-wise: u = a.x * s + a.y * t, v = a.z * s + a.w * t
    vec2 b;
} pc;

layout(location = 0) out vec2 uv;

void main() {
    const vec2 st = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(st * 2.0 - 1.0, 0.0, 1.0);
    uv = vec2(dot(pc.a.xy, st), dot(pc.a.zw, st)) + pc.b;
}
