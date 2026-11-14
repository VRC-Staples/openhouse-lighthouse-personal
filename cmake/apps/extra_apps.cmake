if(NOT LH_BUILD_EXTRA_APPS)
  return()
endif()

if(NOT TARGET lh_imgui OR NOT TARGET lh_usb OR NOT OPENVR_INCLUDE_DIR)
  message(FATAL_ERROR
    "LH_BUILD_EXTRA_APPS requires USB, viewers, GLFW/OpenGL, and OpenVR headers.")
endif()

add_executable(openhouse-check WIN32 apps/openhouse-check/main.cpp)
target_include_directories(openhouse-check PRIVATE include)
if(WIN32)
  target_link_libraries(openhouse-check PRIVATE winhttp)
endif()
if(MSVC)
  target_link_options(openhouse-check PRIVATE /ENTRY:mainCRTStartup)
endif()

add_executable(openhouse-compare
  apps/openhouse-compare/main.cpp apps/openhouse-compare/stock_host.cpp)
target_include_directories(openhouse-compare PRIVATE src ${OPENVR_INCLUDE_DIR})
target_link_libraries(openhouse-compare PRIVATE
  lh lh_common lh_imgui Eigen3::Eigen nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})

add_executable(example_app apps/example_app/main.cpp)
target_link_libraries(example_app PRIVATE lh::lh)
