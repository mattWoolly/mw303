include(FetchContent)
if(EXISTS "${CMAKE_SOURCE_DIR}/.deps/Catch2/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_CATCH2 "${CMAKE_SOURCE_DIR}/.deps/Catch2" CACHE PATH "" FORCE)
endif()
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
