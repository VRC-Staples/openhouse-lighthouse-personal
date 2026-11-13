# Headless viewer regressions: no window/GPU backend or real system clipboard.
set(viewer_test_root "${CMAKE_CURRENT_LIST_DIR}/..")
set(viewer_src "${viewer_test_root}/apps/openhouse-viewer")
set(viewer_tests "${viewer_test_root}/tests/viewer")
set(viewer_imgui "${viewer_test_root}/third_party/imgui")

add_library(lh_viewer_test_client STATIC
  "${viewer_src}/telemetry_client.cpp" "${viewer_test_root}/src/runtime/log_level.cpp")
target_compile_features(lh_viewer_test_client PUBLIC cxx_std_20)
target_include_directories(lh_viewer_test_client PUBLIC
  "${viewer_src}" "${viewer_test_root}/include" "${viewer_test_root}/src")
target_link_libraries(lh_viewer_test_client PUBLIC nlohmann_json::nlohmann_json)

add_library(lh_viewer_test_imgui STATIC
  "${viewer_imgui}/imgui.cpp" "${viewer_imgui}/imgui_draw.cpp"
  "${viewer_imgui}/imgui_tables.cpp" "${viewer_imgui}/imgui_widgets.cpp")
target_include_directories(lh_viewer_test_imgui PUBLIC "${viewer_imgui}")
target_compile_definitions(lh_viewer_test_imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE
  IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS IMGUI_DISABLE_DEFAULT_CLIPBOARD_FUNCTIONS
  IMGUI_DISABLE_DEFAULT_IME_FUNCTIONS)

add_executable(lh_viewer_connection_tests
  "${viewer_tests}/test_connection.cpp" "${viewer_tests}/fake_net.cpp")
add_executable(lh_viewer_log_tests "${viewer_tests}/test_log_view.cpp"
  "${viewer_tests}/gui_fixture.cpp" "${viewer_tests}/fake_net.cpp"
  "${viewer_src}/log_view.cpp" "${viewer_src}/log_shape.cpp")
target_link_libraries(lh_viewer_log_tests PRIVATE lh_viewer_test_imgui)
add_executable(lh_viewer_network_tests "${viewer_tests}/test_net_connect.cpp"
  "${viewer_test_root}/src/common/net.cpp"
  "${viewer_test_root}/tests/openvr/test_viewer_guards.cpp")
target_link_libraries(lh_viewer_network_tests PRIVATE ZLIB::ZLIB)
if(WIN32)
  target_link_libraries(lh_viewer_network_tests PRIVATE ws2_32 iphlpapi advapi32)
endif()

foreach(kind IN ITEMS connection log network)
  target_link_libraries(lh_viewer_${kind}_tests PRIVATE
    lh_viewer_test_client GTest::gtest_main)
  add_test(NAME viewer_${kind} COMMAND lh_viewer_${kind}_tests)
  set_tests_properties(viewer_${kind} PROPERTIES TIMEOUT 30)
endforeach()
