# One Catch2 executable per core module (acid_<module>_tests), globbed from
# tests/<module>/*.cpp. A module with no test sources yet gets no executable. tests/support/ holds shared helpers (allocation guard, WAV I/O).
include(Catch)

add_library(acid_test_support STATIC support/AllocGuard.cpp)
target_include_directories(acid_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(acid_test_support PUBLIC acid_common Catch2::Catch2)

function(acid_tests name)
    file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp)
    if(srcs)
        add_executable(acid_${name}_tests ${srcs})
        target_link_libraries(acid_${name}_tests PRIVATE acid_${name} acid_test_support Catch2::Catch2WithMain)
        target_compile_options(acid_${name}_tests PRIVATE -Wall -Wextra)
        target_compile_definitions(acid_${name}_tests PRIVATE
            ACID_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-output")
        catch_discover_tests(acid_${name}_tests)
    endif()
endfunction()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-output)
foreach(module filter osc dsp dist inserts fx seq player engine)
    acid_tests(${module})
endforeach()
