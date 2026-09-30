include(CheckCXXSourceCompiles)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(REFLECTION_FLAG -freflection)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(REFLECTION_FLAG -freflection-latest)
endif ()

set(CMAKE_REQUIRED_FLAGS "-std=c++26 ${REFLECTION_FLAG}")
check_cxx_source_compiles([[
    #include <meta>
    struct S { int x; };
    int main() { constexpr auto r = ^^S; return std::meta::is_class_type(r) ? 0 : 1; }
]] HAS_REFLECTION)
unset(CMAKE_REQUIRED_FLAGS)

if (NOT HAS_REFLECTION)
    message(FATAL_ERROR
        "${PROJECT_NAME} requires a C++26 compiler with support for compile-time reflection (P2996), "
        "e.g. GCC 16+ or clang-p2996. Current: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif ()
