// ============================================================================
// Spacetime Grid - Fragment Shader
// ============================================================================
//
// Role in the simulation:
// Outputs the color and transparency for the wireframe lines of the 3D spacetime grid.
//
// How it works:
// - The grid is drawn using alpha blending (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
//   so the lines appear semi-transparent and overlay smoothly over the black hole
//   and ray-traced background.
// ============================================================================

#version 330 core
out vec4 FragColor;

void main() {
    // Output color with 70% alpha transparency for smooth overlay blending
    FragColor = vec4(0.5, 0.5, 0.5, 0.7); // translucent blue lines
}