#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <GLFW/glfw3.h>
#include "windows_dll_search.hpp"

int main() {
  assert(openhouse::viewer::RestrictDllSearch());
  assert(glfwInit());
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* window = glfwCreateWindow(64, 64, "openhouse hidden graphics test", nullptr, nullptr);
  assert(window != nullptr);
  glfwMakeContextCurrent(window);
  const auto* version = glGetString(GL_VERSION);
  assert(version != nullptr);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  assert(glGetError() == GL_NO_ERROR);
  std::printf("PASS: system-only DLL search with hidden GLFW/OpenGL context: %s\n",
              reinterpret_cast<const char*>(version));
  glfwDestroyWindow(window);
  glfwTerminate();
}
