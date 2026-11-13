# Hardware-free viewer policy and rendering checks. Included from the root test block.
add_executable(lh_viewer_math_tests tests/personal/test_viewer_math.cpp
  apps/openhouse-viewer/log_shape.cpp)
target_include_directories(lh_viewer_math_tests PRIVATE
  include src apps/openhouse-viewer third_party/imgui)
add_test(NAME viewer_math COMMAND lh_viewer_math_tests)
set_tests_properties(viewer_math PROPERTIES TIMEOUT 10)

if(WIN32)
  add_library(openhouse-loader-probe SHARED tests/personal/dll_probe.c)
  set_target_properties(openhouse-loader-probe PROPERTIES PREFIX "")
  add_executable(lh_dll_search_tests tests/personal/test_dll_search.cpp)
  target_include_directories(lh_dll_search_tests PRIVATE src apps/openhouse-viewer)
  add_dependencies(lh_dll_search_tests openhouse-loader-probe)
  add_test(NAME viewer_dll_search
    COMMAND lh_dll_search_tests $<TARGET_FILE:openhouse-loader-probe>)
  if(TARGET glfw AND OpenGL_FOUND)
    add_executable(lh_viewer_startup_tests tests/personal/test_viewer_startup.cpp)
    target_include_directories(lh_viewer_startup_tests PRIVATE apps/openhouse-viewer)
    target_link_libraries(lh_viewer_startup_tests PRIVATE glfw OpenGL::GL)
    if(MSVC)
      target_link_options(lh_viewer_startup_tests PRIVATE /DEPENDENTLOADFLAG:0x800)
    endif()
    add_test(NAME viewer_startup COMMAND lh_viewer_startup_tests)
  endif()
endif()
