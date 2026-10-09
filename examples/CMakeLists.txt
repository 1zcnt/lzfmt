file(GLOB LZFMT_EXAMPLE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

foreach (src IN LISTS LZFMT_EXAMPLE_SOURCES)
    get_filename_component(name ${src} NAME_WE)

    add_executable(lzfmt_example_${name} ${src})
    target_link_libraries(lzfmt_example_${name} PRIVATE lzfmt::lzfmt)
    target_compile_options(lzfmt_example_${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
endforeach ()
