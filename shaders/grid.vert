// ============================================================================
// Spacetime Grid - Vertex Shader
// ============================================================================
//
// Role in the simulation:
// This vertex shader transforms the 3D warped spacetime grid mesh from world
// space into clip/screen space so it can be rendered on top of the ray-traced scene.
//
// How it works:
// - aPos: The 3D position of each grid vertex generated on the CPU. The Y-coordinate
//   has already been warped downward near massive objects to visualize gravity.
// - viewProj: Combined camera view and perspective projection matrix.
// ============================================================================

#version 330 core
layout(location = 0) in vec3 aPos;

// Camera view-projection matrix from CPU
uniform mat4 viewProj;

void main() {
    // Transform the 3D world-space grid vertex into 2D screen coordinates
    gl_Position = viewProj * vec4(aPos, 1.0);
}