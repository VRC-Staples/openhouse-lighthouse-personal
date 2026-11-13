vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/madler/zlib/releases/download/v1.4/zlib-1.4.tar.gz"
    FILENAME "zlib-1.4.tar.gz"
    SHA512 13f02285da85956ff722d814695b96807aff7a511ebf4b6d98cf708c353920666093cc18b6653cd7992c8e75c103f7b4baf2de3a105c2db9dcd62bab2c940859
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DZLIB_BUILD_TESTING=OFF
        -DZLIB_BUILD_SHARED=OFF
        -DZLIB_BUILD_STATIC=ON
        -DZLIB_INSTALL=ON
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/zlib")
vcpkg_fixup_pkgconfig()

# MSVC 1.4 uses libzs.lib; MinGW keeps libzs.a.
if(VCPKG_TARGET_IS_WINDOWS)
    set(pc_library zs)
    if(NOT VCPKG_TARGET_IS_MINGW)
        set(pc_library libzs)
    endif()
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc"
        " -lz" " -l${pc_library}")
    if(NOT VCPKG_BUILD_TYPE)
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc"
            " -lz" " -l${pc_library}d")
    endif()
endif()
if(NOT VCPKG_BUILD_TYPE)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc"
        "/include" "/../include")
endif()
vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/zconf.h" "ifdef ZLIB_DLL" "if 0")
configure_file("${CMAKE_CURRENT_LIST_DIR}/ZLIBConfig.cmake"
    "${CURRENT_PACKAGES_DIR}/share/zlib/ZLIBConfig.cmake" COPYONLY)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/zlib")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
