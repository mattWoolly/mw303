# JUCE, pinned. Bumping the tag is a deliberate change: rebuild and re-run the validators.
# A checkout under .deps/ (see scripts/fetch-deps.sh) is used when present, so several
# build trees share one copy of the sources.
include(FetchContent)
if(EXISTS "${CMAKE_SOURCE_DIR}/.deps/JUCE/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_SOURCE_DIR}/.deps/JUCE" CACHE PATH "" FORCE)
endif()
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)
