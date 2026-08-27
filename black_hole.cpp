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
BlackHole SagaA(vec2(engine.width / 2.0, 0.0), 8.54e36);

struct Ray {
  double x;
  double y;
  vec2 dir;
  vector<vec2> trail;

  Ray(vec2(pos), vec2(dir)) : x(pos.x), y(pos.y), dir(dir) {}
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

  void step() {
    x += dir.x * c;
    y += dir.y * c;
    trail.push_back({x, y});
  }
};
vector<Ray> rays;

int main() {
  for (float y = -engine.height; y < engine.height; y += 1.5e10) {
    rays.push_back(Ray(vec2(-engine.width, y), vec2(1.0, 0.0)));
  }
  rays.push_back(Ray(vec2(-engine.width, 0.0), vec2(1.0, 0.0)));
  while (!glfwWindowShouldClose(engine.window)) {
    engine.run();
    SagaA.draw();

    for (auto &ray : rays) {
      ray.draw();
      ray.step();
    }

    glfwSwapBuffers(engine.window);
    glfwPollEvents();
  }
  glfwTerminate();
  return 0;
}