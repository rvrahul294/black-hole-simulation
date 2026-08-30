#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <numbers>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;
using namespace glm;
const double G = 6.674e-11;
const double c = 299792458.0;

struct Tele {};

struct Engine {
  GLFWwindow *window;
  int WIDTH = 800;
  int HEIGHT = 600;
  float width = 1e11;
  float height = 7.5e10;

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

struct BlackHole {
  vec2 position;
  double mass;
  double r_s; /*Event horizon*/

  BlackHole(vec2 pos, double m) : position(pos), mass(m) {
    r_s = (2.0 * G * mass) / (c * c);
  }

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
BlackHole SagaA(vec2(0.0, 0.0), 8.54e36);
struct Ray;
void rk4Step(Ray &ray, double d_lambda, double rs);
struct Ray {
  // cartesian coordinates //
  double x;
  double y;

  // polar coordinates
  double r;
  double phi;

  // velocity for polar position //
  double dr;
  double dphi;
  double d2r;
  double d2phi;
  double E, L;
  vec2 dir;
  vector<vec2> trail;

  Ray(vec2 pos, vec2 dir) : x(pos.x), y(pos.y), dir(dir) {
    r = hypot(x, y);
    phi = atan2(y, x);
    double f = 1.0 - SagaA.r_s / r;

    dr = dir.x * cos(phi) + dir.y * sin(phi);          // m/s
    dphi = (-dir.x * sin(phi) + dir.y * cos(phi)) / r; // rad/s

    L = r * r * dphi;
    d2r = 0.0;
    d2phi = 0.0;
    double dt_dlambda = sqrt((dr * dr) / (f * f) + (r * r * dphi * dphi) / f);

    E = f * dt_dlambda;
    trail.push_back({x, y});
  }
  void draw() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

    size_t N = trail.size();
    if (N < 2)
      return;

    glBegin(GL_LINE_STRIP);
    for (size_t i = 0; i < N; i++) {
      float alpha = float(i) / float(N - 1);
      glColor4f(1.0, 1.0, 1.0, std::max(0.5f, alpha));
      glVertex2f(trail[i].x, trail[i].y);
    }
    glEnd();
  }

  void step(double r_s, double d_lambda) {
    if (r <= r_s)
      return; // inside blackhole, ray cannot escape //
    rk4Step(*this, d_lambda, r_s);

    x = r * cos(phi);
    y = r * sin(phi);
    trail.push_back({float(x), float(y)});
  }
};
vector<Ray> rays;

void geodesic(Ray &ray, double rhs[4], double r_s) {
  double r = ray.r;
  double dr = ray.dr;
  double dphi = ray.dphi;
  double E = ray.E;
  double f = 1.0 - r_s / r;
  rhs[0] = dr;
  rhs[1] = dphi;

  double dt_dlambda = E / f;

  rhs[2] = -(r_s / (2.0 * r * r)) * f * dt_dlambda * dt_dlambda +
           (r_s / (2.0 * r * r * f)) * dr * dr + (r - r_s) * dphi * dphi;
  rhs[3] = -2.0 * dr * dphi /
           r; // Angular acceleration: conservation of angular momentum //
}
void addState(const double y[4], const double k[4], double factor,
              double out[4]) {
  for (int i = 0; i < 4; i++) {
    out[i] = y[i] + factor * k[i];
  }
}
void rk4Step(Ray &ray, double d_lambda, double rs) {
  double y0[4] = {ray.r, ray.phi, ray.dr, ray.dphi};
  double k1[4], k2[4], k3[4], k4[4], temp[4];

  if (ray.r < rs)
    return;
  // k1
  geodesic(ray, k1, rs);

  // k2
  addState(y0, k1, d_lambda / 2.0, temp);
  Ray r2 = ray;
  r2.r = temp[0];
  r2.phi = temp[1];
  r2.dr = temp[2];
  r2.dphi = temp[3];

  geodesic(r2, k2, rs);

  // k3
  addState(y0, k2, d_lambda / 2.0, temp);
  Ray r3 = ray;
  r3.r = temp[0];
  r3.phi = temp[1];
  r3.dr = temp[2];
  r3.dphi = temp[3];

  geodesic(r3, k3, rs);

  // k4
  addState(y0, k3, d_lambda, temp);
  Ray r4 = ray;
  r4.r = temp[0];
  r4.phi = temp[1];
  r4.dr = temp[2];
  r4.dphi = temp[3];

  geodesic(r4, k4, rs);

  // final step
  ray.r += (d_lambda / 6.0) * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]);
  ray.phi += (d_lambda / 6.0) * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]);
  ray.dr += (d_lambda / 6.0) * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]);
  ray.dphi += (d_lambda / 6.0) * (k1[3] + 2 * k2[3] + 2 * k3[3] + k4[3]);
}

int main() {
  for (float y = -engine.height; y <= engine.height; y += 1.e10) {
    rays.push_back(Ray(vec2(-engine.width, y), vec2(c, 0.0f)));
  }
  while (!glfwWindowShouldClose(engine.window)) {
    engine.run();
    SagaA.draw();

    for (auto &ray : rays) {
      ray.step(SagaA.r_s, 1.0f);
      ray.draw();
    }

    glfwSwapBuffers(engine.window);
    glfwPollEvents();
  }
  glfwTerminate();
  return 0;
}