cmake_minimum_required(VERSION 3.25)

# The product name lives here and nowhere else. Plugin name, bundle/CLAP ids and
# the LV2 URI all derive from it; source code uses the neutral `acid` namespace.
set(MW_PRODUCT_NAME "mw12F")
set(MW_PRODUCT_ID   "mw12f")
set(MW_PLUGIN_CODE  "mw12")

project(${MW_PRODUCT_NAME} VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(MW_BUILD_PLUGIN "Build the JUCE plugin (VST3, CLAP, LV2, Standalone)" ON)
option(MW_BUILD_TESTS  "Build the host-free test suite" ON)

option(MW_LTO "Link-time optimisation for the plugin (slow to link; use for release builds)" OFF)

add_subdirectory(libs/acid-core)

if(MW_BUILD_TESTS)
    include(cmake/FetchCatch2.cmake)
    enable_testing()
    add_subdirectory(tests)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()

if(MW_BUILD_PLUGIN)
    include(cmake/FetchJUCE.cmake)
    include(cmake/FetchClapExtensions.cmake)
    add_subdirectory(plugin)
endif()
