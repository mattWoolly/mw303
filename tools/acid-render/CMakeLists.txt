# acid-render: offline renderer for golden tests, factory-content authoring and listening.
add_executable(acid-render main.cpp)
target_link_libraries(acid-render PRIVATE acid_engine)
target_include_directories(acid-render PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)   # Wav.h, header only
target_compile_options(acid-render PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion)
