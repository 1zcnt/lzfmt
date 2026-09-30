file(GLOB LZFMT_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

foreach (src IN LISTS LZFMT_TEST_SOURCES)
    get_filename_component(name ${src} NAME_WE)

    add_executable(lzfmt_${name} ${src})
    target_link_libraries(lzfmt_${name} PRIVATE lzfmt::lzfmt)
    target_compile_options(lzfmt_${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

    add_test(NAME ${name} COMMAND lzfmt_${name})
endforeach ()
