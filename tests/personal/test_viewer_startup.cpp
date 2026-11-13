#include <cstdio>
#include <GLFW/glfw3.h>
#include "windows_dll_search.hpp"

int Failure(const char* operation, int exitCode) {
  const char* description = nullptr;
  const int code = glfwGetError(&description);
  std::fprintf(stderr, "%s:%d:%s\n", operation, code, description ? description : "no description");
  glfwTerminate();
  return exitCode;
}

int main() {
#ifdef LH_RESTRICT_DLL_SEARCH
  if (!openhouse::viewer::RestrictDllSearch()) {
    std::fputs("DLL search restriction failed\n", stderr);
    return 1;
  }
#endif
  if (!glfwInit()) return Failure("GLFW_INIT_FAILED", 1);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* native = glfwCreateWindow(64, 64, "openhouse hidden window test", nullptr, nullptr);
  if (!native) return Failure("GLFW_NATIVE_WINDOW_FAILED", 1);
  glfwDestroyWindow(native);

  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
  GLFWwindow* window = glfwCreateWindow(64, 64, "openhouse hidden graphics test", nullptr, nullptr);
  if (!window) return Failure("GLFW_CONTEXT_UNAVAILABLE", 77);
  glfwMakeContextCurrent(window);
  const auto* version = glGetString(GL_VERSION);
  if (!version) return Failure("GL_VERSION_FAILED", 1);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  if (glGetError() != GL_NO_ERROR) return Failure("GL_RENDER_FAILED", 1);
  std::printf("GL_VERSION=%s\n", reinterpret_cast<const char*>(version));
  glfwDestroyWindow(window);
  glfwTerminate();
}
