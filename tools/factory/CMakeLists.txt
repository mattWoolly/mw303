# factory-build: writes the factory content library (assets/factory/) from the recipes in
# this directory, through the JUCE-free StateCodec so every file is canonical, and checks
# existing files by round trip (--verify). qa.py renders and measures the result.
add_executable(factory-build
    main.cpp
    Common.cpp
    Sounds.cpp
    Effects.cpp
    Lines.cpp
    Patterns.cpp
    Scenes.cpp
    Templates.cpp)
target_link_libraries(factory-build PRIVATE acid_engine acid_seq)
target_compile_definitions(factory-build PRIVATE FACTORY_NAMES_FILE="${CMAKE_CURRENT_SOURCE_DIR}/forbidden_names.txt")
target_compile_options(factory-build PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion)
