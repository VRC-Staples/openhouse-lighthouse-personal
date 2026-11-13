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
  if(TARGET glfw AND OpenGL_FOUND AND Python3_Interpreter_FOUND)
    foreach(mode IN ITEMS baseline hardened)
      add_executable(lh_viewer_startup_${mode} tests/personal/test_viewer_startup.cpp)
      target_include_directories(lh_viewer_startup_${mode} PRIVATE apps/openhouse-viewer)
      target_link_libraries(lh_viewer_startup_${mode} PRIVATE glfw OpenGL::GL)
    endforeach()
    target_compile_definitions(lh_viewer_startup_hardened PRIVATE LH_RESTRICT_DLL_SEARCH)
    if(MSVC)
      target_link_options(lh_viewer_startup_hardened PRIVATE /DEPENDENTLOADFLAG:0x800)
    endif()
    add_test(NAME viewer_startup COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/scripts/tests/check-viewer-startup.py
      $<TARGET_FILE:lh_viewer_startup_baseline> $<TARGET_FILE:lh_viewer_startup_hardened>)
  endif()
endif()
