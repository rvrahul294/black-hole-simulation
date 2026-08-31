// ============================================================================
// 2D Black Hole Gravitational Lensing Simulation
// ============================================================================
//
// What this program does:
// This program simulates how light bends around a non-rotating black hole
// (gravitational lensing) based on Einstein's General Relativity.
//
// How gravitational lensing works here:
// A black hole's strong gravity curves the space around it. Light rays
// traveling past the black hole follow this curved space:
//   - Rays passing far away bend slightly.
//   - Rays passing close bend sharply around the black hole.
//   - Rays that get too close cross the event horizon and cannot escape.
//
// How the simulation works:
// 1. Sets up a black hole at the center (modeling Sagittarius A*).
// 2. Shoots a series of light rays from the left side toward the right.
// 3. Solves the physics equations of motion in polar coordinates (distance r,
// angle phi).
// 4. Steps the rays forward in time using Runge-Kutta 4th order (RK4)
// integration.
// 5. Draws the black hole and the fading paths (trails) of the light rays in
// real time.
// ============================================================================

#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;
using namespace glm;

// ----------------------------------------------------------------------------
// Physical Constants (SI Units)
// ----------------------------------------------------------------------------
// Gravitational constant G in m^3 / (kg * s^2)
const double G = 6.674e-11;

// Speed of light c in meters per second (m/s)
const double c = 299792458.0;

struct Tele {};

// ----------------------------------------------------------------------------
// Window & OpenGL Setup
// ----------------------------------------------------------------------------
// Manages window creation and sets up the 2D coordinate system in meters.
struct Engine {
  GLFWwindow *window;
  int WIDTH = 800;       // Window width in pixels
  int HEIGHT = 600;      // Window height in pixels
  float width = 1e11;    // Half-width of the view in meters (~100 million km)
  float height = 7.5e10; // Half-height of the view in meters (~75 million km)

  Engine() {
    if (!glfwInit()) {
      cerr << "Failed to initialize GLFW" << endl;
      exit(EXIT_FAILURE);
    }

    window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole sim", NULL, NULL);

    if (!window) {
      cerr << "Window failed to create, PANIC!" << endl;
      glfwTerminate();
      exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);
  }

  // Prepares each frame: clears the screen and sets up a 2D camera view
  // so we can draw directly using coordinates in meters.
  void run() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Reset projection matrix
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    // Set 2D visible bounds: x from -width to +width, y from -height to +height
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
// Black Hole
// ----------------------------------------------------------------------------
// Represents a non-rotating black hole using its mass and position.
struct BlackHole {
  vec2 position; // Center position (x, y) in meters
  double mass;   // Mass in kilograms (kg)
  double r_s;    // Event horizon radius (Schwarzschild radius) in meters

  BlackHole(vec2 pos, double m) : position(pos), mass(m) {
    // Schwarzschild radius: r_s = 2GM / c^2.
    // Inside this radius, gravity is so strong that even light cannot escape.
    r_s = (2.0 * G * mass) / (c * c);
  }

  // Draws the black hole's event horizon as a circle
  void draw() {
    glColor3f(1.0, 0.0, 0.0); // Red color for visibility
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

// Sagittarius A* (the supermassive black hole at the center of the Milky Way):
// Mass ~ 8.54e36 kg (~4.3 million times our Sun), placed at the center (0, 0).
BlackHole SagaA(vec2(0.0, 0.0), 8.54e36);

struct Ray;
void rk4Step(Ray &ray, double d_lambda, double rs);

// ----------------------------------------------------------------------------
// Light Ray
// ----------------------------------------------------------------------------
// Tracks a single light ray as it travels through curved space.
struct Ray {
  // Cartesian position in meters
  double x;
  double y;

  // Polar position relative to the black hole center:
  // r: distance to center in meters, phi: angle in radians
  double r;
  double phi;

  // Velocities: rate of change of radius and angle
  double dr;
  double dphi;
  double d2r;
  double d2phi;

  // Conserved quantities: Energy (E) and Angular Momentum (L)
  // In this spacetime, these values remain constant along the ray's path.
  double E, L;
  vec2 dir;           // Initial direction in Cartesian space
  vector<vec2> trail; // Past positions stored to draw the ray's path

  Ray(vec2 pos, vec2 dir) : x(pos.x), y(pos.y), dir(dir) {
    r = hypot(x, y);
    phi = atan2(y, x);

    // Spacetime factor f = 1 - r_s / r
    double f = 1.0 - SagaA.r_s / r;

    // Convert initial (x, y) velocity into polar velocity (radial and angular)
    dr = dir.x * cos(phi) + dir.y * sin(phi);          // m/s (radial speed)
    dphi = (-dir.x * sin(phi) + dir.y * cos(phi)) / r; // rad/s (angular speed)

    // Angular momentum L = r^2 * dphi stays constant
    L = r * r * dphi;
    d2r = 0.0;
    d2phi = 0.0;

    // Calculate time rate (dt/dlambda) for a light ray
    double dt_dlambda = sqrt((dr * dr) / (f * f) + (r * r * dphi * dphi) / f);

    // Conserved energy E
    E = f * dt_dlambda;
    trail.push_back({x, y});
  }

  // Draws the path of the ray as a fading line
  void draw() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

    size_t N = trail.size();
    if (N < 2)
      return;

    // Older points fade out, newer points stay bright
    glBegin(GL_LINE_STRIP);
    for (size_t i = 0; i < N; i++) {
      float alpha = float(i) / float(N - 1);
      glColor4f(1.0, 1.0, 1.0, std::max(0.5f, alpha));
      glVertex2f(trail[i].x, trail[i].y);
    }
    glEnd();
  }

  // Moves the ray forward by one time step (d_lambda)
  void step(double r_s, double d_lambda) {
    // If the ray entered the event horizon, it cannot escape, so stop moving it
    if (r <= r_s)
      return;

    // Update position and velocity using RK4 numerical integration
    rk4Step(*this, d_lambda, r_s);

    // Convert new polar position back to (x, y) and save to trail
    x = r * cos(phi);
    y = r * sin(phi);
    trail.push_back({float(x), float(y)});
  }
};
vector<Ray> rays;

// ----------------------------------------------------------------------------
// Equations of Motion (Geodesic)
// ----------------------------------------------------------------------------
// Computes the accelerations for a light ray near the black hole.
// rhs[0] = dr/dlambda   (change in distance)
// rhs[1] = dphi/dlambda (change in angle)
// rhs[2] = d2r          (radial acceleration from gravity)
// rhs[3] = d2phi        (angular acceleration)
void geodesic(Ray &ray, double rhs[4], double r_s) {
  double r = ray.r;
  double dr = ray.dr;
  double dphi = ray.dphi;
  double E = ray.E;
  double f = 1.0 - r_s / r;

  // Rates of change of position
  rhs[0] = dr;
  rhs[1] = dphi;

  // Rate of time advance
  double dt_dlambda = E / f;

  // Radial acceleration: how gravity pulls the light inward
  rhs[2] = -(r_s / (2.0 * r * r)) * f * dt_dlambda * dt_dlambda +
           (r_s / (2.0 * r * r * f)) * dr * dr + (r - r_s) * dphi * dphi;

  // Angular acceleration: ensures angular momentum (r^2 * dphi) stays constant
  rhs[3] = -2.0 * dr * dphi / r;
}

// Helper to calculate an intermediate step: out = y + factor * k
void addState(const double y[4], const double k[4], double factor,
              double out[4]) {
  for (int i = 0; i < 4; i++) {
    out[i] = y[i] + factor * k[i];
  }
}

// ----------------------------------------------------------------------------
// Runge-Kutta 4th Order (RK4) Integrator
// ----------------------------------------------------------------------------
// Takes 4 sample slopes (k1, k2, k3, k4) to accurately move the ray forward
// by step size d_lambda.
void rk4Step(Ray &ray, double d_lambda, double rs) {
  double y0[4] = {ray.r, ray.phi, ray.dr, ray.dphi};
  double k1[4], k2[4], k3[4], k4[4], temp[4];

  if (ray.r < rs)
    return;

  // Step 1: Initial slope
  geodesic(ray, k1, rs);

  // Step 2: Slope at midpoint using k1
  addState(y0, k1, d_lambda / 2.0, temp);
  Ray r2 = ray;
  r2.r = temp[0];
  r2.phi = temp[1];
  r2.dr = temp[2];
  r2.dphi = temp[3];
  geodesic(r2, k2, rs);

  // Step 3: Slope at midpoint using k2
  addState(y0, k2, d_lambda / 2.0, temp);
  Ray r3 = ray;
  r3.r = temp[0];
  r3.phi = temp[1];
  r3.dr = temp[2];
  r3.dphi = temp[3];
  geodesic(r3, k3, rs);

  // Step 4: Slope at end of interval using k3
  addState(y0, k3, d_lambda, temp);
  Ray r4 = ray;
  r4.r = temp[0];
  r4.phi = temp[1];
  r4.dr = temp[2];
  r4.dphi = temp[3];
  geodesic(r4, k4, rs);

  // Combine slopes with RK4 weights to update distance, angle, and velocities
  ray.r += (d_lambda / 6.0) * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]);
  ray.phi += (d_lambda / 6.0) * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]);
  ray.dr += (d_lambda / 6.0) * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]);
  ray.dphi += (d_lambda / 6.0) * (k1[3] + 2 * k2[3] + 2 * k3[3] + k4[3]);
}

// ----------------------------------------------------------------------------
// Main Simulation Loop
// ----------------------------------------------------------------------------
int main() {
  // Create light rays along the left side of the screen.
  // All rays initially travel to the right at the speed of light.
  // Different y positions let us see how close each ray passes to the black
  // hole.
  for (float y = -engine.height; y <= engine.height; y += 1.e10) {
    rays.push_back(Ray(vec2(-engine.width, y), vec2(c, 0.0f)));
  }

  // Main rendering and simulation loop
  while (!glfwWindowShouldClose(engine.window)) {
    // Clear screen and set up 2D camera
    engine.run();

    // Draw the black hole at center
    SagaA.draw();

    // Move each ray forward and draw its trail
    for (auto &ray : rays) {
      ray.step(SagaA.r_s, 1.0f);
      ray.draw();
    }

    // Swap buffers to display the new frame and handle window events
    glfwSwapBuffers(engine.window);
    glfwPollEvents();
  }

  glfwTerminate();
  return 0;
}