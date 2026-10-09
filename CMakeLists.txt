cmake_minimum_required(VERSION 4.3)
project(lzfmt
        VERSION 0.1.0
        DESCRIPTION "header-only c++26 serialization library"
        HOMEPAGE_URL "https://github.com/1zcnt/lzfmt"
        LANGUAGES CXX)

include(GNUInstallDirs)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/refl.cmake)

option(LZFMT_BUILD_TESTS "Build lzfmt tests" ${PROJECT_IS_TOP_LEVEL})
option(LZFMT_BUILD_EXAMPLES "Build lzfmt examples" ${PROJECT_IS_TOP_LEVEL})

add_library(lzfmt INTERFACE)
add_library(lzfmt::lzfmt ALIAS lzfmt)

target_include_directories(lzfmt INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(lzfmt INTERFACE cxx_std_26)
target_compile_options(lzfmt INTERFACE ${REFLECTION_FLAG})

target_sources(lzfmt INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/lzfmt/lzfmt.hpp
        include/lzfmt/binfmt.hpp
        include/lzfmt/util.hpp)

# --- Install / find_package(lzfmt) -----------------------------------------
install(TARGETS lzfmt EXPORT lzfmtTargets FILE_SET HEADERS)
install(EXPORT lzfmtTargets
    FILE lzfmtConfig.cmake
    NAMESPACE lzfmt::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lzfmt)

if (LZFMT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (LZFMT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()
