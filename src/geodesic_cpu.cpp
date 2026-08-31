// ============================================================================
// CPU-Based 3D Black Hole Geodesic Ray Tracer (Reference Prototype)
// ============================================================================
//
// What this file is:
// This is a standalone CPU implementation of 3D null-geodesic ray tracing
// around a Schwarzschild black hole (Sagittarius A*).
//
// Why this CPU version exists:
// Calculating curved light paths in general relativity requires numerical
// integration of nonlinear differential equations. This file served as the
// CPU reference prototype used to verify the geodesic physics and math before
// implementing the high-performance GPU compute shader (geodesic.comp) used
// in the main 3D simulation.
//
// How it works:
// 1. Camera & Rays: For every pixel on screen, a light ray is cast from the
//    camera position into the scene.
// 2. Geodesic Equations: Light follows "geodesics" (the straightest possible
//    paths through curved spacetime). In the presence of a black hole, spacetime
//    is curved, causing light rays to bend.
// 3. RK4 Integration: The Runge-Kutta 4th Order (RK4) method steps the ray
//    forward in small increments, calculating how gravity bends its trajectory.
// 4. Multithreading (OpenMP): CPU ray tracing is computationally heavy, so
//    OpenMP distributes pixel ray tracing across all available CPU cores.
// 5. Texture Display: The CPU pixel buffer is uploaded to an OpenGL texture
//    and rendered onto a fullscreen quad.
//
// Note: This is an independent educational/reference program and is not part
// of the main CMake 3D simulation executable.
// ============================================================================

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Clock = std::chrono::high_resolution_clock;
using namespace std;
using namespace glm;

// ----------------------------------------------------------------------------
// Physical Constants (SI Units)
// ----------------------------------------------------------------------------
// Universal gravitational constant G (m^3 / kg / s^2)
const double G = 6.674e-11;

// Speed of light in vacuum c (meters per second)
const double c = 299792458.0;

int frameCount = 0;
double lastPrintTime = 0.0;

// Toggle between straight-line ray tracing and curved geodesic ray tracing (press 'G')
bool useGeodesics = false;

// ----------------------------------------------------------------------------
// Orbital Camera
// ----------------------------------------------------------------------------
// Spherical camera orbiting around the origin (0, 0, 0) where the black hole sits.
// - azimuth: horizontal angle around the Y-axis
// - elevation: vertical angle from the top pole
// - radius: distance from target in meters
struct Camera {
  vec3 pos;
  vec3 target;
  float fovY;
  float azimuth, elevation, radius;
  float minRadius = 1e12f;
  float maxRadius = 1e20f;
  bool dragging = false;
  bool panning = false;
  double lastX = 0, lastY = 0;

  // Camera movement speeds
  float orbitSpeed = 0.080f;
  float panSpeed = 0.001f;
  float zoomSpeed = 1.08f;

  Camera()
      : azimuth(0), elevation(M_PI / 2.0), radius(6.34194e10), fovY(60.0f) {
    target = vec3(0, 0, 0);
    updateVectors();
  }

  // Converts spherical coordinates (radius, elevation, azimuth) into Cartesian 3D position (x, y, z)
  void updateVectors() {
    pos.x = target.x + radius * sin(elevation) * cos(azimuth);
    pos.y = target.y + radius * cos(elevation);
    pos.z = target.z + radius * sin(elevation) * sin(azimuth);
  }

  // Handles mouse drag for orbiting and panning
  void processMouse(GLFWwindow *window, double xpos, double ypos) {
    float dx = float(xpos - lastX);
    float dy = float(ypos - lastY);

    if (dragging && !panning) {
      azimuth -= dx * orbitSpeed;
      elevation -= dy * orbitSpeed;
      elevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
    } else if (panning) {
      vec3 forward = normalize(target - pos);
      vec3 right = normalize(cross(forward, vec3(0, 1, 0)));
      vec3 up = normalize(cross(right, forward));
      target += -right * dx * panSpeed * radius + up * dy * panSpeed * radius;
    }
    updateVectors();
    lastX = xpos;
    lastY = ypos;
  }

  // Handles mouse scroll for zooming
  void processScroll(double yoffset) {
    // zoom
    if (yoffset >= 0) {
      radius /= pow(zoomSpeed, yoffset);
    } else {
      radius *= pow(zoomSpeed, -yoffset);
    }
    radius = glm::clamp(radius, minRadius, maxRadius);
    updateVectors();
  }
  static void mouseCallback(GLFWwindow *window, int button, int action,
                            int mods) {
    Camera *camera = (Camera *)glfwGetWindowUserPointer(window);
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (action == GLFW_PRESS) {
        camera->dragging = true;
        camera->panning = (mods & GLFW_MOD_SHIFT);
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        camera->lastX = x;
        camera->lastY = y;
      } else if (action == GLFW_RELEASE) {
        camera->dragging = false;
        camera->panning = false;
      }
    }
  }

  static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos) {
    Camera *camera = (Camera *)glfwGetWindowUserPointer(window);
    camera->processMouse(window, xpos, ypos);
  }

  static void scrollCallback(GLFWwindow *window, double xoffset,
                             double yoffset) {
    Camera *camera = (Camera *)glfwGetWindowUserPointer(window);
    camera->processScroll(yoffset);
  }
};
Camera camera;

// ----------------------------------------------------------------------------
// OpenGL Display Engine
// ----------------------------------------------------------------------------
// Manages window creation, texture generation, and fullscreen quad rendering.
struct Engine {
  GLFWwindow *window;
  GLuint quadVAO;
  GLuint texture;
  GLuint shaderProgram;
  int WIDTH = 800;
  int HEIGHT = 600;
  float width = 1e11;
  float height = 7.5e10;

  Engine() {
    if (!glfwInit()) {
      cerr << "Failed to initialize GLFW" << endl;
      exit(EXIT_FAILURE);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "3D Black Hole sim", NULL, NULL);

    if (!window) {
      cerr << "Window failed to create" << endl;
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
    this->shaderProgram = CreateShaderProgram();

    auto result = QuadVAO();
    this->quadVAO = result[0];
    this->texture = result[1];
  }
  // Compiles basic shader program to display the CPU ray-traced texture on a 2D quad
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
  // Creates VAO/VBO for a fullscreen rectangular quad (two triangles)
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
    vector<GLuint> VAOtexture = {VAO, texture};
    return VAOtexture;
  }
  // Uploads ray-traced CPU pixel data to GPU texture and renders to screen
  void renderScene(const vector<unsigned char> &pixels, int texWidth,
                   int texHeight) {
    // update texture w/ ray-tracing results
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texWidth, texHeight, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, pixels.data());

    // clear screen and draw textured quad
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(shaderProgram);

    GLint textureLocation =
        glGetUniformLocation(shaderProgram, "screenTexture");
    glUniform1i(textureLocation, 0);

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glfwSwapBuffers(window);
    glfwPollEvents();
  };

  // Keyboard callback: press 'G' to toggle geodesic mode on/off
  static void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                          int mods) {
    if (action == GLFW_PRESS) {
      if (key == GLFW_KEY_G) {
        useGeodesics = !useGeodesics;
        cout << "Geodesics: " << (useGeodesics ? "ON\n" : "OFF\n");
      }
    }
  }

  void run() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Reset projection matrix
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    double left = -width;
    double right = width;
    double bottom = -height;
    double top = height;

    glOrtho(left, right, bottom, top, -1, 1);

    // Reset model matrix
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
  }
};
Engine engine;

// ----------------------------------------------------------------------------
// Black Hole Representation
// ----------------------------------------------------------------------------
// Represents a non-rotating (Schwarzschild) black hole using mass and position.
struct BlackHole {
  vec3 position;
  double mass;
  double radius;
  double r_s; /*Event horizon*/

  BlackHole(vec3 pos, float m) : position(pos), mass(m) {
    // Schwarzschild radius (event horizon): distance where escape velocity equals speed of light
    // r_s = 2 * G * M / c^2
    r_s = 2.0 * G * mass / (c * c);
  }
  // Checks if a 3D position is inside the event horizon
  bool Intercept(float px, float py, float pz) const {
    float dx = px - position.x;
    float dy = py - position.y;
    float dz = pz - position.z;
    float dist2 = dx * dx + dy * dy + dz * dz;
    return dist2 < r_s * r_s;
  }

  // 2D disk preview drawing
  void draw() {
    glColor3f(1.0, 0.0, 0.0);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(position.x, position.y);
    for (int i = 0; i <= 100; i++) {
      float angle = 2.0f * M_PI * i / 100;
      float x = r_s * cos(angle) + position.x;
      float y = r_s * sin(angle) + position.y;
      glVertex2f(x, y);
    }
    glEnd();
  }
};
// Sagittarius A* supermassive black hole (~8.54 x 10^36 kg)
BlackHole SagaA(vec3(0.0, 0.0, 0.0), 8.54e36);

struct Ray;
void rk4Step(Ray &ray, double d_lambda, double rs);

// ----------------------------------------------------------------------------
// Light Ray Representation
// ----------------------------------------------------------------------------
// Stores position and velocity in spherical coordinates (r, theta, phi)
// and Cartesian coordinates (x, y, z), along with conserved quantities.
struct Ray {
  // cartesian coordinates //
  double x;
  double y;
  double z;
  // polar coordinates
  double r;
  double phi;
  double theta;

  // velocity for polar position //
  double dr;
  double dphi;
  double dtheta;
  double E, L; // Conserved Energy and Angular Momentum
  Ray(vec3 pos, vec3 dir) : x(pos.x), y(pos.y), z(pos.z) {
    // Step 1: get spherical coords (r, theta, phi)
    r = sqrt(x * x + y * y + z * z);
    theta = acos(z / r);
    phi = atan2(y, x);

    // Step 2: seed velocities (dr, dtheta, dphi)
    // Convert direction to spherical basis
    double dx = dir.x, dy = dir.y, dz = dir.z;
    dr = sin(theta) * cos(phi) * dx + sin(theta) * sin(phi) * dy +
         cos(theta) * dz;
    dtheta = cos(theta) * cos(phi) * dx + cos(theta) * sin(phi) * dy -
             sin(theta) * dz;
    dtheta /= r;
    dphi = -sin(phi) * dx + cos(phi) * dy;
    dphi /= (r * sin(theta));

    // Step 3: store conserved quantities
    L = r * r * sin(theta) * dphi;
    double f = 1.0 - SagaA.r_s / r;
    double dt_dλ = sqrt((dr * dr) / f + r * r * dtheta * dtheta +
                        r * r * sin(theta) * sin(theta) * dphi * dphi);
    E = f * dt_dλ;
  }
  // Steps the ray forward in affine parameter space using RK4
  void step(double dλ, double rs) {
    if (r <= rs)
      return;
    rk4Step(*this, dλ, rs);
    // convert back to cartesian
    this->x = r * sin(theta) * cos(phi);
    this->y = r * sin(theta) * sin(phi);
    this->z = r * cos(theta);
  }
};
vector<Ray> rays;

// ----------------------------------------------------------------------------
// CPU Ray Tracing Pipeline
// ----------------------------------------------------------------------------
// Casts light rays through each screen pixel. Uses OpenMP to distribute the
// workload across multiple CPU cores in parallel.
void raytrace(vector<unsigned char> &pixels, int W, int H) {
  pixels.resize(W * H * 3);

  // build camera basis (forward, right, up)
  vec3 forward = normalize(camera.target - camera.pos);
  vec3 right = normalize(cross(forward, vec3(0, 1, 0)));
  vec3 up = cross(right, forward);
  float aspect = float(W) / float(H);
  float tanHalfFov = tan(radians(camera.fovY) * 0.5f);

  // OpenMP: parallelize pixel rendering across CPU threads
#pragma omp parallel for schedule(dynamic, 4)
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      // NDC → screen space in [−1,1]
      float u = (2.0f * (x + 0.5f) / float(W) - 1.0f) * aspect * tanHalfFov;
      float v = (1.0f - 2.0f * (y + 0.5f) / float(H)) * tanHalfFov;
      vec3 dir = normalize(u * right + v * up + forward);

      // construct your Ray
      Ray ray(camera.pos, dir);

      const int MAX_STEPS = 10000;
      const double D_LAMBDA = 1e7;
      const double ESCAPE_R = 1e14;

      // 2) march the ray forward in λ
      vec3 color(0.0f);
      if (!useGeodesics) {
        // Simple Euclidean mode: straight-line ray-sphere intersection check
        double b = 2.0 * dot(camera.pos, dir);
        double c0 = dot(camera.pos, camera.pos) - SagaA.r_s * SagaA.r_s;
        double disc = b * b - 4.0 * c0;
        if (disc > 0.0) {
          double t1 = (-b - sqrt(disc)) * 0.5;
          double t2 = (-b + sqrt(disc)) * 0.5;
          if (t1 > 0.0 || t2 > 0.0)
            color = vec3(1.0f, 0.0f, 0.0f);
        }
      } else {
        // Full null-geodesic marching in curved spacetime
        Ray ray(camera.pos, dir);
        for (int i = 0; i < MAX_STEPS; ++i) {
          // Check if ray fell into the event horizon
          if (SagaA.Intercept(ray.x, ray.y, ray.z)) {
            color = vec3(1.0f, 0.0f, 0.0f);
            break;
          }
          ray.step(D_LAMBDA, SagaA.r_s);
          if (ray.r > ESCAPE_R) {
            // escaped to infinity → remains black
            break;
          }
        }
      }

      int idx = (y * W + x) * 3;
      pixels[idx + 0] = (unsigned char)(color.r * 255);
      pixels[idx + 1] = (unsigned char)(color.g * 255);
      pixels[idx + 2] = (unsigned char)(color.b * 255);
    }
  }
}

// ----------------------------------------------------------------------------
// Geodesic Differential Equations (Right-Hand Side)
// ----------------------------------------------------------------------------
// Evaluates the derivatives for null geodesics in Schwarzschild spacetime.
// State vector: y = [r, theta, phi, dr/dlambda, dtheta/dlambda, dphi/dlambda]
// Computes: dy/dlambda = [dr, dtheta, dphi, d^2r, d^2theta, d^2phi]
void geodesicRHS(const Ray &ray, double rhs[6], double rs) {
  double r = ray.r;
  double theta = ray.theta;
  double dr = ray.dr;
  double dtheta = ray.dtheta;
  double dphi = ray.dphi;
  double E = ray.E;

  double f = 1.0 - rs / r;
  double dt_dlambda = E / f;

  // First derivatives (velocities)
  rhs[0] = dr;
  rhs[1] = dtheta;
  rhs[2] = dphi;

  // Second derivatives (accelerations from 3D Schwarzschild null geodesic equations):
  rhs[3] = -(rs / (2 * r * r)) * f * dt_dlambda * dt_dlambda +
           (rs / (2 * r * r * f)) * dr * dr +
           r * (dtheta * dtheta + sin(theta) * sin(theta) * dphi * dphi);

  rhs[4] = -(2.0 / r) * dr * dtheta + sin(theta) * cos(theta) * dphi * dphi;

  rhs[5] =
      -(2.0 / r) * dr * dphi - 2.0 * cos(theta) / sin(theta) * dtheta * dphi;
}

// Helper to calculate intermediate RK4 state: out = y + factor * k
void addState(const double y[6], const double k[6], double factor,
              double out[6]) {
  for (int i = 0; i < 6; i++) {
    out[i] = y[i] + factor * k[i];
  }
}

// ----------------------------------------------------------------------------
// Runge-Kutta 4th Order (RK4) Numerical Integrator
// ----------------------------------------------------------------------------
// Integrates the 6 geodesic equations across step size d_lambda.
// RK4 computes four slope estimates (k1, k2, k3, k4) to achieve high numerical
// accuracy and prevent integration drift near the strong gravity of the black hole.
void rk4Step(Ray &ray, double d_lambda, double rs) {
  double y0[6] = {ray.r, ray.theta, ray.phi, ray.dr, ray.dtheta, ray.dphi};
  double k1[6], k2[6], k3[6], k4[6], temp[6];

  if (ray.r < rs)
    return;
  // k1: initial slope
  geodesicRHS(ray, k1, rs);

  // k2: slope at midpoint using k1
  addState(y0, k1, d_lambda / 2.0, temp);
  Ray r2 = ray;
  r2.r = temp[0];
  r2.theta = temp[1];
  r2.phi = temp[2];
  r2.dr = temp[3];
  r2.dtheta = temp[4];
  r2.dphi = temp[5];

  geodesicRHS(r2, k2, rs);

  // k3: second slope at midpoint using k2
  addState(y0, k2, d_lambda / 2.0, temp);
  Ray r3 = ray;
  r3.r = temp[0];
  r3.theta = temp[1];
  r3.phi = temp[2];
  r3.dr = temp[3];
  r3.dtheta = temp[4];
  r3.dphi = temp[5];

  geodesicRHS(r3, k3, rs);

  // k4: slope at end of step using k3
  addState(y0, k3, d_lambda, temp);
  Ray r4 = ray;
  r4.r = temp[0];
  r4.theta = temp[1];
  r4.phi = temp[2];
  r4.dr = temp[3];
  r4.dtheta = temp[4];
  r4.dphi = temp[5];

  geodesicRHS(r4, k4, rs);

  // Combine weighted slopes to take the final RK4 step
  ray.r += (d_lambda / 6.0) * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]);
  ray.theta += (d_lambda / 6.0) * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]);
  ray.phi += (d_lambda / 6.0) * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]);
  ray.dr += (d_lambda / 6.0) * (k1[3] + 2 * k2[3] + 2 * k3[3] + k4[3]);
  ray.dtheta += (d_lambda / 6.0) * (k1[4] + 2 * k2[4] + 2 * k3[4] + k4[4]);
  ray.dphi += (d_lambda / 6.0) * (k1[5] + 2 * k2[5] + 2 * k3[5] + k4[5]);
}

// ----------------------------------------------------------------------------
// Window & Input Callbacks
// ----------------------------------------------------------------------------
void setupCameraCallbacks(GLFWwindow *window) {
  glfwSetWindowUserPointer(window, &camera);
  glfwSetMouseButtonCallback(window, Camera::mouseCallback);
  glfwSetCursorPosCallback(window, Camera::cursorPosCallback);
  glfwSetScrollCallback(window, Camera::scrollCallback);
  glfwSetKeyCallback(window, Engine::keyCallback);
}

// ----------------------------------------------------------------------------
// Main Loop
// ----------------------------------------------------------------------------
int main() {
  setupCameraCallbacks(engine.window);
  vector<unsigned char> pixels(engine.WIDTH * engine.HEIGHT * 3);

  auto t0 = Clock::now();
  lastPrintTime = std::chrono::duration<double>(t0.time_since_epoch()).count();

  while (!glfwWindowShouldClose(engine.window)) {
    // 1) Ray trace on CPU and draw to OpenGL quad
    raytrace(pixels, engine.WIDTH, engine.HEIGHT);
    engine.renderScene(pixels, engine.WIDTH, engine.HEIGHT);

    // 2) FPS counting
    frameCount++;
    auto t1 = Clock::now();
    double now = std::chrono::duration<double>(t1.time_since_epoch()).count();
    if (now - lastPrintTime >= 1.0) {
      cout << "FPS: " << frameCount / (now - lastPrintTime) << "\n";
      frameCount = 0;
      lastPrintTime = now;
    }
  }

  glfwDestroyWindow(engine.window);
  glfwTerminate();
  return 0;
}
