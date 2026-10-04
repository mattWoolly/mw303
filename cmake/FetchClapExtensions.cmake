# CLAP export for JUCE plugins, pinned to a commit verified against JUCE 8.0.15 and gcc 13
# (CLAP 1.2.7). The 0.26.0 tag does not build against JUCE 8.
include(FetchContent)
if(EXISTS "${CMAKE_SOURCE_DIR}/.deps/clap-juce-extensions/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS "${CMAKE_SOURCE_DIR}/.deps/clap-juce-extensions" CACHE PATH "" FORCE)
endif()
FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        55525c9858d4b25687be7759a5e0f70eccef218e
)
FetchContent_MakeAvailable(clap-juce-extensions)
