// ============================================================================
// 3D Black Hole Simulation & Gravitational Lensing
// ============================================================================
//
// Overview:
// This program is an interactive 3D simulation of a supermassive black hole
// (Sagittarius A* at the center of our galaxy). It visualizes how extreme
// gravity bends light rays in 3D space, shows an accretion disk around the
// black hole, simulates gravitational attraction between celestial bodies,
// and renders a warped 3D grid representing curved spacetime.
//
// CPU and GPU Collaboration:
// - GPU Compute Shader (geodesic.comp):
//   Calculating how light bends around a black hole for every single pixel on
//   the screen requires millions of numerical integration steps. The GPU handles
//   these expensive ray-tracing calculations in parallel across thousands of threads.
// - CPU Simulation & Grid Generation:
//   The CPU manages user input, updates the orbital camera, computes N-body
//   Newtonian gravity between objects, and builds the 3D warped spacetime grid mesh.
// - Uniform Buffer Objects (UBOs):
//   The CPU packages camera state, object data, and accretion disk settings into
//   structured buffers (UBOs) and sends them to GPU memory so the compute shader
//   can access all scene parameters efficiently.
// - Display Pipeline:
//   The compute shader writes its calculated pixel colors directly into an OpenGL
//   texture. The CPU then renders this texture to the screen on a fullscreen quad
//   and overlays the 3D spacetime grid wireframe.
//
// Main Stages of the Program:
// 1. Initialization: Set up OpenGL 4.3 (required for compute shaders), create
//    UBOs, compile shader programs, and prepare rendering buffers.
// 2. Input & Camera: Orbit and zoom smoothly around the black hole based on mouse input.
// 3. Physics Update: Compute N-body gravitational forces between objects (when enabled).
// 4. Spacetime Grid Generation: Build a 3D wireframe grid that dips downward near massive bodies.
// 5. GPU Ray Tracing: Dispatch the compute shader to trace curved light rays per pixel.
// 6. Blending & Display: Render the ray-traced background and overlay the spacetime grid.
// ============================================================================

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

#define _USE_MATH_DEFINES
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846

#endif
using namespace glm;
using namespace std;
using Clock = std::chrono::high_resolution_clock;

// ----------------------------------------------------------------------------
// Global Variables & Physical Constants
// ----------------------------------------------------------------------------
double lastPrintTime = 0.0;
int framesCount = 0;

// Speed of light in vacuum (meters per second, ~3.0 x 10^8 m/s)
double c = 299792458.0;

// Universal Gravitational Constant G (m^3 / kg / s^2)
// Controls the strength of gravitational attraction in physics calculations
double G = 6.67430e-11;

struct Ray;

// Toggle for N-body gravity simulation (press 'G' key or right-click to toggle)
bool Gravity = false;

// ----------------------------------------------------------------------------
// Orbital Camera System
// ----------------------------------------------------------------------------
// An orbital camera that revolves around the black hole at the origin (0, 0, 0).
// It uses spherical coordinates to make rotating and zooming intuitive:
// - azimuth: horizontal angle around the Y-axis
// - elevation: vertical angle measured from the top pole
// - radius: distance from the center (in meters)
struct Camera {
  // Center of camera orbit (fixed on the black hole at origin)
  vec3 target = vec3(0.0f, 0.0f, 0.0f); // Always look at the black hole center
  float radius = 6.34194e10f;           // Distance from black hole in meters
  float minRadius = 1e10f, maxRadius = 1e12f; // Zoom limits to prevent clipping

  float azimuth = 0.0f;          // Horizontal angle around the black hole
  float elevation = M_PI / 2.0f; // Vertical angle (pi/2 puts the camera on the equator)

  float orbitSpeed = 0.01f;      // Sensitivity for mouse drag rotation
  float panSpeed = 0.01f;
  double zoomSpeed = 25e9f;      // Distance change in meters per scroll step

  bool dragging = false;         // True while left mouse button is held down
  bool panning = false;
  bool moving = false;           // True when the user is actively moving the camera
  double lastX = 0.0, lastY = 0.0;

  // Calculates the 3D Cartesian position (x, y, z) of the camera in meters
  vec3 position() const {
    // Clamp elevation slightly away from 0 and PI to avoid gimbal lock
    // and prevent the camera from flipping upside down at the poles
    float clampedElevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
    // Convert spherical coordinates (radius, elevation, azimuth) to Cartesian 3D
    return vec3(radius * sin(clampedElevation) * cos(azimuth),
                radius * cos(clampedElevation),
                radius * sin(clampedElevation) * sin(azimuth));
  }
  void update() {
    // Always keep target at black hole center
    target = vec3(0.0f, 0.0f, 0.0f);
    if (dragging | panning) {
      moving = true;
    } else {
      moving = false;
    }
  }

  // Handles mouse drag to orbit smoothly around the black hole
  void processMouseMove(double x, double y) {
    float dx = float(x - lastX);
    float dy = float(y - lastY);

    if (dragging && panning) {
      // Pan: Shift + Left or Middle Mouse
      // Disable panning to keep camera centered on black hole
    } else if (dragging && !panning) {
      // Orbit: Left mouse only
      azimuth += dx * orbitSpeed;
      elevation -= dy * orbitSpeed;
      elevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
    }

    lastX = x;
    lastY = y;
    update();
  }

  // Handles mouse click events (left-click to rotate, right-click to toggle gravity)
  void processMouseButton(int button, int action, int mods, GLFWwindow *win) {
    if (button == GLFW_MOUSE_BUTTON_LEFT ||
        button == GLFW_MOUSE_BUTTON_MIDDLE) {
      if (action == GLFW_PRESS) {
        dragging = true;
        // Disable panning so camera always orbits center
        panning = false;
        glfwGetCursorPos(win, &lastX, &lastY);
      } else if (action == GLFW_RELEASE) {
        dragging = false;
        panning = false;
      }
    }
    // Right click toggles gravity on/off
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
      if (action == GLFW_PRESS) {
        Gravity = true;
      } else if (action == GLFW_RELEASE) {
        Gravity = false;
      }
    }
  }

  // Handles mouse scroll wheel to zoom closer to or farther from the black hole
  void processScroll(double xoffset, double yoffset) {
    radius -= yoffset * zoomSpeed;
    radius = glm::clamp(radius, minRadius, maxRadius);
    update();
  }

  // Handles keyboard shortcuts (press 'G' to toggle gravity simulation)
  void processKey(int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS && key == GLFW_KEY_G) {
      Gravity = !Gravity;
      cout << "[INFO] Gravity turned " << (Gravity ? "ON" : "OFF") << endl;
    }
  }
};
Camera camera;

// ----------------------------------------------------------------------------
// Black Hole Representation
// ----------------------------------------------------------------------------
// Represents a non-rotating (Schwarzschild) black hole using its mass and position.
struct BlackHole {
  vec3 position;  // Center position in 3D space (meters)
  double mass;    // Mass in kilograms
  double radius;
  double r_s;     // Schwarzschild radius (Event Horizon) in meters

  BlackHole(vec3 pos, float m) : position(pos), mass(m) {
    // The Schwarzschild radius is the distance from the center of the black hole
    // where the escape velocity reaches the speed of light.
    // Inside this radius, gravity is so strong that even light cannot escape.
    // Formula: r_s = 2 * G * M / c^2
    r_s = 2.0 * G * mass / (c * c);
  }

  // Checks whether a given 3D point is inside the event horizon
  bool Intercept(float px, float py, float pz) const {
    double dx = double(px) - double(position.x);
    double dy = double(py) - double(position.y);
    double dz = double(pz) - double(position.z);
    double dist2 = dx * dx + dy * dy + dz * dz;
    return dist2 < r_s * r_s;
  }
};

// Sagittarius A* supermassive black hole at the center of the Milky Way galaxy
// Mass is approximately 4.3 million solar masses (~8.54 x 10^36 kg)
BlackHole SagA(vec3(0.0f, 0.0f, 0.0f), 8.54e36); // Sagittarius A black hole

// ----------------------------------------------------------------------------
// Scene Objects (Celestial Bodies & Stars)
// ----------------------------------------------------------------------------
// Holds physical properties and visual appearance for objects in the scene.
// These are sent to the GPU so the compute shader can render them, and used
// on the CPU for gravitational interactions.
struct ObjectData {
  vec4 posRadius; // xyz = position in space (meters), w = visual radius (meters)
  vec4 color;     // rgb = surface color (0.0 to 1.0), a = unused
  float mass;     // Mass in kilograms (used for gravitational attraction)
  vec3 velocity = vec3(0.0f, 0.0f, 0.0f); // Velocity in meters per second
};

// List of celestial objects in the simulation (stars and the central black hole)
vector<ObjectData> objects = {
    {vec4(4e11f, 0.0f, 0.0f, 4e10f), vec4(1, 1, 0, 1), 1.98892e30}, // Yellow star
    {vec4(0.0f, 0.0f, 4e11f, 4e10f), vec4(1, 0, 0, 1), 1.98892e30}, // Red star
    {vec4(0.0f, 0.0f, 0.0f, SagA.r_s), vec4(0, 0, 0, 1),
     static_cast<float>(SagA.mass)}, // Central black hole event horizon
    //{ vec4(6e10f, 0.0f, 0.0f, 5e10f), vec4(0,1,0,1) }
};

// ----------------------------------------------------------------------------
// Main Simulation Engine
// ----------------------------------------------------------------------------
// Manages window creation, shader compilation, GPU buffer allocations,
// Uniform Buffer Objects (UBOs), compute shader dispatching, and rendering passes.
struct Engine {
  GLuint gridShaderProgram;
  // -- Quad & Texture render -- //
  GLFWwindow *window;
  GLuint quadVAO;
  GLuint texture;
  GLuint shaderProgram;
  GLuint computeProgram = 0;

  // -- Uniform Buffer Objects (UBOs) -- //
  // UBOs allow the CPU to upload structured data once into GPU memory,
  // where all shader threads in parallel can read from it:
  GLuint cameraUBO = 0;   // Sends camera position, orientation, aspect ratio, and FOV
  GLuint diskUBO = 0;     // Sends accretion disk inner/outer radii and thickness
  GLuint objectsUBO = 0;  // Sends celestial bodies (positions, radii, colors, masses)

  // -- Spacetime Grid Mesh Buffers -- //
  GLuint gridVAO = 0;
  GLuint gridVBO = 0;
  GLuint gridEBO = 0;
  int gridIndexCount = 0;

  int WIDTH = 800;               // Window display width in pixels
  int HEIGHT = 600;              // Window display height in pixels
  int COMPUTE_WIDTH = 200;       // Compute resolution width (lower resolution for speed during movement)
  int COMPUTE_HEIGHT = 150;      // Compute resolution height
  float width = 100000000000.0f; // Viewport spatial width in meters
  float height = 75000000000.0f; // Viewport spatial height in meters

  Engine() {
    if (!glfwInit()) {
      cerr << "GLFW init failed\n";
      exit(EXIT_FAILURE);
    }
    // OpenGL 4.3 Core Profile is required for compute shaders (glDispatchCompute)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole", nullptr, nullptr);
    if (!window) {
      cerr << "Failed to create GLFW window\n";
      glfwTerminate();
      exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
      cerr << "Failed to initialize GLEW: "
           << (const char *)glewGetErrorString(glewErr) << "\n";
      glfwTerminate();
      exit(EXIT_FAILURE);
    }
    cout << "OpenGL " << glGetString(GL_VERSION) << "\n";

    // 1) Shader to draw the full-screen texture produced by the compute shader
    this->shaderProgram = CreateShaderProgram();

    // 2) Shader for rendering the warped 3D spacetime grid lines
    gridShaderProgram =
        CreateShaderProgram("shaders/grid.vert", "shaders/grid.frag");

    // 3) Compute shader for GPU-accelerated ray tracing and light bending
    computeProgram = CreateComputeProgram("shaders/geodesic.comp");

    // Allocate camera UBO buffer (~128 bytes) and link to binding point 1
    glGenBuffers(1, &cameraUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferData(GL_UNIFORM_BUFFER, 128, nullptr,
                 GL_DYNAMIC_DRAW); // alloc ~128 bytes
    glBindBufferBase(GL_UNIFORM_BUFFER, 1,
                     cameraUBO); // binding = 1 matches shader

    // Allocate accretion disk UBO buffer and link to binding point 2
    glGenBuffers(1, &diskUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 4, nullptr,
                 GL_DYNAMIC_DRAW); // 3 values + 1 padding
    glBindBufferBase(GL_UNIFORM_BUFFER, 2,
                     diskUBO); // binding = 2 matches compute shader

    // Allocate objects UBO buffer and link to binding point 3
    glGenBuffers(1, &objectsUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
    // allocate space for 16 objects:
    // sizeof(int) + padding + 16×(vec4 posRadius + vec4 color)
    GLsizeiptr objUBOSize = sizeof(int) + 3 * sizeof(float) +
                            16 * (sizeof(vec4) + sizeof(vec4)) +
                            16 * sizeof(float); // 16 floats for mass
    glBufferData(GL_UNIFORM_BUFFER, objUBOSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3,
                     objectsUBO); // binding = 3 matches shader

    // Fullscreen quad for displaying the compute shader output texture
    auto result = QuadVAO();
    this->quadVAO = result[0];
    this->texture = result[1];
  }

  // --------------------------------------------------------------------------
  // Spacetime Grid Generation (CPU)
  // --------------------------------------------------------------------------
  // Creates a 3D wireframe mesh on the X-Z plane and bends it downward along the
  // Y-axis near massive objects.
  // In Einstein's General Relativity, mass curves spacetime like a heavy ball placed
  // on a rubber sheet. This function calculates that curvature (Flamm's paraboloid)
  // to give an intuitive visual representation of the gravitational "funnel".
  void generateGrid(const vector<ObjectData> &objects) {
    const int gridSize = 25;     // Number of grid lines in each direction
    const float spacing = 1e10f; // Distance between grid lines in meters

    vector<vec3> vertices;
    vector<GLuint> indices;

    // Generate grid vertices and calculate depth (y) based on gravitational curvature
    for (int z = 0; z <= gridSize; ++z) {
      for (int x = 0; x <= gridSize; ++x) {
        float worldX = (x - gridSize / 2.0f) * spacing;
        float worldZ = (z - gridSize / 2.0f) * spacing;

        float y = 0.0f;

        // Calculate downward dip for each massive object using Schwarzschild geometry
        for (const auto &obj : objects) {
          vec3 objPos = vec3(obj.posRadius);
          double mass = obj.mass;
          double radius = obj.posRadius.w;

          double r_s = 2.0 * G * mass / (c * c);
          double dx = worldX - objPos.x;
          double dz = worldZ - objPos.z;
          double dist = sqrt(dx * dx + dz * dz);

          // Outside the event horizon: calculate curved spacetime height (embedding funnel)
          // Based on Flamm's paraboloid: z(r) = 2 * sqrt(r_s * (r - r_s))
          if (dist > r_s) {
            double deltaY = 2.0 * sqrt(r_s * (dist - r_s));
            y += static_cast<float>(deltaY) - 3e10f;
          } else {
            // Inside or at event horizon: make it dip down sharply into the center
            y += 2.0f * static_cast<float>(sqrt(r_s * r_s)) -
                 3e10f; // or add a deep pit
          }
        }

        vertices.emplace_back(worldX, y, worldZ);
      }
    }

    // Connect grid vertices with line indices (GL_LINES format)
    for (int z = 0; z < gridSize; ++z) {
      for (int x = 0; x < gridSize; ++x) {
        int i = z * (gridSize + 1) + x;
        // Horizontal line segment
        indices.push_back(i);
        indices.push_back(i + 1);

        // Vertical line segment
        indices.push_back(i);
        indices.push_back(i + gridSize + 1);
      }
    }

    // Upload generated grid vertices and indices to GPU buffers
    if (gridVAO == 0)
      glGenVertexArrays(1, &gridVAO);
    if (gridVBO == 0)
      glGenBuffers(1, &gridVBO);
    if (gridEBO == 0)
      glGenBuffers(1, &gridEBO);

    glBindVertexArray(gridVAO);

    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vec3),
                 vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void *)0);

    gridIndexCount = indices.size();

    glBindVertexArray(0);
  }

  // Renders the 3D spacetime grid using line primitives and alpha blending
  void drawGrid(const mat4 &viewProj) {
    glUseProgram(gridShaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "viewProj"), 1,
                       GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(gridVAO);

    // Disable depth testing and enable blending so the grid lines can be
    // seen clearly and overlay gracefully on the background
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawElements(GL_LINES, gridIndexCount, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
  }

  // Draws a full-screen quad textured with the compute shader's ray-traced output
  void drawFullScreenQuad() {
    glUseProgram(shaderProgram); // fragment + vertex shader
    glBindVertexArray(quadVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);

    glDisable(GL_DEPTH_TEST);              // draw as background
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 6); // 2 triangles
    glEnable(GL_DEPTH_TEST);
  }

  // Compiles the basic shader program used to display the rendered texture on the screen
  GLuint CreateShaderProgram() {
    const char *vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;  // Changed to vec2
        layout (location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);  // Explicit z=0
            TexCoord = aTexCoord;
        })";

    const char *fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D screenTexture;
        void main() {
            FragColor = texture(screenTexture, TexCoord);
        })";

    // vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    // fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
  };

  // Helper to find a shader file in the working directory or parent directory (for Code Runner compatibility)
  static string resolveShaderPath(const char *path) {
    ifstream in(path);
    if (in.is_open()) {
      return string(path);
    }
    string fallback = string("../") + path;
    ifstream inFallback(fallback);
    if (inFallback.is_open()) {
      return fallback;
    }
    return string(path);
  }

  // Helper to load, compile, and link vertex and fragment shaders from external files
  GLuint CreateShaderProgram(const char *vertPath, const char *fragPath) {
    auto loadShader = [](const char *path, GLenum type) -> GLuint {
      std::string resolved = resolveShaderPath(path);
      std::ifstream in(resolved);
      if (!in.is_open()) {
        std::cerr << "Failed to open shader: " << path << "\n";
        exit(EXIT_FAILURE);
      }
      std::stringstream ss;
      ss << in.rdbuf();
      std::string srcStr = ss.str();
      const char *src = srcStr.c_str();

      GLuint shader = glCreateShader(type);
      glShaderSource(shader, 1, &src, nullptr);
      glCompileShader(shader);

      GLint success;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
      if (!success) {
        GLint logLen;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen);
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::cerr << "Shader compile error (" << path << "):\n"
                  << log.data() << "\n";
        exit(EXIT_FAILURE);
      }
      return shader;
    };

    GLuint vertShader = loadShader(vertPath, GL_VERTEX_SHADER);
    GLuint fragShader = loadShader(fragPath, GL_FRAGMENT_SHADER);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    GLint linkSuccess;
    glGetProgramiv(program, GL_LINK_STATUS, &linkSuccess);
    if (!linkSuccess) {
      GLint logLen;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
      std::vector<char> log(logLen);
      glGetProgramInfoLog(program, logLen, nullptr, log.data());
      std::cerr << "Shader link error:\n" << log.data() << "\n";
      exit(EXIT_FAILURE);
    }

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    return program;
  }

  // Compiles and links the GLSL Compute Shader from file
  GLuint CreateComputeProgram(const char *path) {
    // 1) read GLSL source
    std::string resolved = resolveShaderPath(path);
    std::ifstream in(resolved);
    if (!in.is_open()) {
      std::cerr << "Failed to open compute shader: " << path << "\n";
      exit(EXIT_FAILURE);
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string srcStr = ss.str();
    const char *src = srcStr.c_str();

    // 2) compile
    GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(cs, 1, &src, nullptr);
    glCompileShader(cs);
    GLint ok;
    glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      GLint logLen;
      glGetShaderiv(cs, GL_INFO_LOG_LENGTH, &logLen);
      std::vector<char> log(logLen);
      glGetShaderInfoLog(cs, logLen, nullptr, log.data());
      std::cerr << "Compute shader compile error:\n" << log.data() << "\n";
      exit(EXIT_FAILURE);
    }

    // 3) link
    GLuint prog = glCreateProgram();
    glAttachShader(prog, cs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
      GLint logLen;
      glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
      std::vector<char> log(logLen);
      glGetProgramInfoLog(prog, logLen, nullptr, log.data());
      std::cerr << "Compute shader link error:\n" << log.data() << "\n";
      exit(EXIT_FAILURE);
    }

    glDeleteShader(cs);
    return prog;
  }

  // --------------------------------------------------------------------------
  // GPU Compute Shader Dispatch (Ray Tracing)
  // --------------------------------------------------------------------------
  // Dispatches the compute shader across a 2D grid of GPU work groups.
  // Each GPU thread calculates the path of a light ray from the camera through
  // one pixel, bending the ray through curved spacetime, and writes the final
  // pixel color directly into an OpenGL 2D texture.
  void dispatchCompute(const Camera &cam) {
    // Dynamic resolution: use lower resolution when camera is moving to maintain high frame rate
    int cw = cam.moving ? COMPUTE_WIDTH : 200;
    int ch = cam.moving ? COMPUTE_HEIGHT : 150;

    // 1) Reallocate the texture dimensions if resolution changed
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,        // mip
                 GL_RGBA8, // internal format
                 cw,       // width
                 ch,       // height
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // 2) Bind compute program & upload latest UBO data from CPU to GPU
    glUseProgram(computeProgram);
    uploadCameraUBO(cam);
    uploadDiskUBO();
    uploadObjectsUBO(objects);

    // 3) Bind texture as an image unit so compute shader threads can write to it
    glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    // 4) Dispatch GPU thread groups (each work group contains 16x16 threads)
    GLuint groupsX = (GLuint)std::ceil(cw / 16.0f);
    GLuint groupsY = (GLuint)std::ceil(ch / 16.0f);
    glDispatchCompute(groupsX, groupsY, 1);

    // 5) Memory barrier: ensures GPU finishes writing all texture pixels before we draw
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  }

  // Uploads camera position, direction basis vectors, and FOV to the camera UBO
  void uploadCameraUBO(const Camera &cam) {
    // std140 layout requires 16-byte alignment for vec3 variables, so we add padding floats
    struct UBOData {
      vec3 pos;
      float _pad0;
      vec3 right;
      float _pad1;
      vec3 up;
      float _pad2;
      vec3 forward;
      float _pad3;
      float tanHalfFov;
      float aspect;
      bool moving;
      int _pad4;
    } data;
    // Build orthonormal camera coordinate frame (forward, right, up)
    vec3 fwd = normalize(cam.target - cam.position());
    vec3 up = vec3(0, 1, 0); // Y-axis is up; accretion disk sits in X-Z plane
    vec3 right = normalize(cross(fwd, up));
    up = cross(right, fwd);

    data.pos = cam.position();
    data.right = right;
    data.up = up;
    data.forward = fwd;
    data.tanHalfFov = tan(radians(60.0f * 0.5f));
    data.aspect = float(WIDTH) / float(HEIGHT);
    data.moving = cam.dragging || cam.panning;

    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(UBOData), &data);
  }

  // Uploads scene objects (positions, radii, colors, masses) to the objects UBO
  void uploadObjectsUBO(const vector<ObjectData> &objs) {
    // Memory layout matching std140 uniform block in GLSL compute shader
    struct UBOData {
      int numObjects;
      float _pad0, _pad1, _pad2; // <-- pad out to 16 bytes
      vec4 posRadius[16];
      vec4 color[16];
      float mass[16];
    } data;

    size_t count = std::min(objs.size(), size_t(16));
    data.numObjects = static_cast<int>(count);

    for (size_t i = 0; i < count; ++i) {
      data.posRadius[i] = objs[i].posRadius;
      data.color[i] = objs[i].color;
      data.mass[i] = objs[i].mass;
    }

    // Upload to GPU buffer
    glBindBuffer(GL_UNIFORM_BUFFER, objectsUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
  }

  // Uploads accretion disk dimensions to the disk UBO
  void uploadDiskUBO() {
    // Accretion disk dimensions relative to the black hole's Schwarzschild radius
    float r1 = SagA.r_s * 2.2f; // Inner radius just outside the event horizon
    float r2 = SagA.r_s * 5.2f; // Outer boundary radius of the disk
    float num = 2.0;            // Number of rays / disk density parameter
    float thickness = 1e9f;     // Disk vertical thickness in meters
    float diskData[4] = {r1, r2, num, thickness};

    glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(diskData), diskData);
  }

  // Sets up the 2D quad geometry and texture used to display the compute output
  vector<GLuint> QuadVAO() {
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f, 1.0f,  0.0f, 1.0f, // top left
        -1.0f, -1.0f, 0.0f, 0.0f, // bottom left
        1.0f,  -1.0f, 1.0f, 0.0f, // bottom right

        -1.0f, 1.0f,  0.0f, 1.0f, // top left
        1.0f,  -1.0f, 1.0f, 0.0f, // bottom right
        1.0f,  1.0f,  1.0f, 1.0f  // top right
    };

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices,
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,        // mip
                 GL_RGBA8, // internal format
                 COMPUTE_WIDTH, COMPUTE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    vector<GLuint> VAOtexture = {VAO, texture};
    return VAOtexture;
  }
  void renderScene() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(shaderProgram);
    glBindVertexArray(quadVAO);
    // make sure your fragment shader samples from texture unit 0:
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glfwSwapBuffers(window);
    glfwPollEvents();
  };
};
Engine engine;

// ----------------------------------------------------------------------------
// GLFW Callbacks
// ----------------------------------------------------------------------------
// Connects user mouse and keyboard input to camera and simulation functions.
void setupCameraCallbacks(GLFWwindow *window) {
  glfwSetWindowUserPointer(window, &camera);

  // Mouse clicks
  glfwSetMouseButtonCallback(
      window, [](GLFWwindow *win, int button, int action, int mods) {
        Camera *cam = (Camera *)glfwGetWindowUserPointer(win);
        cam->processMouseButton(button, action, mods, win);
      });

  // Mouse movement for orbiting
  glfwSetCursorPosCallback(window, [](GLFWwindow *win, double x, double y) {
    Camera *cam = (Camera *)glfwGetWindowUserPointer(win);
    cam->processMouseMove(x, y);
  });

  // Scroll wheel for zooming
  glfwSetScrollCallback(window,
                        [](GLFWwindow *win, double xoffset, double yoffset) {
                          Camera *cam = (Camera *)glfwGetWindowUserPointer(win);
                          cam->processScroll(xoffset, yoffset);
                        });

  // Keyboard keys (e.g. 'G' key for gravity)
  glfwSetKeyCallback(
      window, [](GLFWwindow *win, int key, int scancode, int action, int mods) {
        Camera *cam = (Camera *)glfwGetWindowUserPointer(win);
        cam->processKey(key, scancode, action, mods);
      });
}

// ----------------------------------------------------------------------------
// Main Function & Simulation Loop
// ----------------------------------------------------------------------------
int main() {
  setupCameraCallbacks(engine.window);
  vector<unsigned char> pixels(engine.WIDTH * engine.HEIGHT * 3);

  auto t0 = Clock::now();
  lastPrintTime = chrono::duration<double>(t0.time_since_epoch()).count();

  double lastTime = glfwGetTime();
  int renderW = 800, renderH = 600, numSteps = 80000;
  while (!glfwWindowShouldClose(engine.window)) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // optional, but good practice
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    double now = glfwGetTime();
    double dt = now - lastTime; // seconds since last frame
    lastTime = now;

    // ------------------------------------------------------------------------
    // N-Body Gravity Update
    // ------------------------------------------------------------------------
    // Calculates Newtonian gravitational attraction between every pair of objects:
    // F = G * m1 * m2 / distance^2
    // If gravity is enabled, updates velocity and position of each object.
    for (auto &obj : objects) {
      for (auto &obj2 : objects) {
        if (&obj == &obj2)
          continue; // skip self-interaction
        float dx = obj2.posRadius.x - obj.posRadius.x;
        float dy = obj2.posRadius.y - obj.posRadius.y;
        float dz = obj2.posRadius.z - obj.posRadius.z;
        float distance = sqrt(dx * dx + dy * dy + dz * dz);
        if (distance > 0) {
          vector<double> direction = {dx / distance, dy / distance,
                                      dz / distance};
          // distance *= 1000;
          double Gforce = (G * obj.mass * obj2.mass) / (distance * distance);

          double acc1 = Gforce / obj.mass;
          std::vector<double> acc = {direction[0] * acc1, direction[1] * acc1,
                                     direction[2] * acc1};
          if (Gravity) {
            obj.velocity.x += acc[0];
            obj.velocity.y += acc[1];
            obj.velocity.z += acc[2];

            obj.posRadius.x += obj.velocity.x;
            obj.posRadius.y += obj.velocity.y;
            obj.posRadius.z += obj.velocity.z;
            cout << "velocity: " << obj.velocity.x << ", " << obj.velocity.y
                 << ", " << obj.velocity.z << endl;
          }
        }
      }
    }

    // ------------------------------------------------------------------------
    // 1) Generate & Draw Warped Spacetime Grid
    // ------------------------------------------------------------------------
    // Rebuild the warped grid based on current object positions
    engine.generateGrid(objects);

    // Compute view and projection matrices to render the 3D grid with the camera
    mat4 view = lookAt(camera.position(), camera.target, vec3(0, 1, 0));
    mat4 proj = perspective(radians(60.0f),
                            float(engine.COMPUTE_WIDTH) / engine.COMPUTE_HEIGHT,
                            1e9f, 1e14f);
    mat4 viewProj = proj * view;
    engine.drawGrid(viewProj);

    // ------------------------------------------------------------------------
    // 2) GPU Ray Tracing (Light Bending)
    // ------------------------------------------------------------------------
    // Set full viewport, run the compute shader to trace curved light rays,
    // and draw the resulting texture to the screen.
    glViewport(0, 0, engine.WIDTH, engine.HEIGHT);
    engine.dispatchCompute(camera);
    engine.drawFullScreenQuad();

    // ------------------------------------------------------------------------
    // 3) Display Frame & Handle Events
    // ------------------------------------------------------------------------
    glfwSwapBuffers(engine.window);
    glfwPollEvents();
  }

  glfwDestroyWindow(engine.window);
  glfwTerminate();
  return 0;
}