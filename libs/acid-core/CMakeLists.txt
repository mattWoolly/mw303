# JUCE-free core. One static library per module so each can be built and tested on its
# own. Sources are globbed from src/<module>/: adding a file needs no CMake edit.
#
#   acid_filter   diode ladder filter           acid_dist     distortion and dynamics
#   acid_osc      oscillator, decimator         acid_inserts  EQ, delay, reverb, chorus, limiter
#   acid_dsp      the voice (uses both)         acid_fx       the effects chain (uses both)
#   acid_seq      patterns, edits, generator    acid_player   playback, arpeggiator, MIDI notes
#   acid_engine   everything, wired together for the plugin

add_library(acid_common INTERFACE)
target_include_directories(acid_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(acid_common INTERFACE cxx_std_20)

set(ACID_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion)
set(ACID_FP_FLAGS -fno-math-errno -fno-trapping-math)

function(acid_module name)
    file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/*.cpp)
    if(srcs)
        add_library(acid_${name} STATIC ${srcs})
        target_link_libraries(acid_${name} PUBLIC acid_common ${ARGN})
        target_compile_options(acid_${name} PRIVATE ${ACID_WARNINGS} ${ACID_FP_FLAGS})
    else()
        add_library(acid_${name} INTERFACE)
        target_link_libraries(acid_${name} INTERFACE acid_common ${ARGN})
    endif()
endfunction()

acid_module(filter)
acid_module(osc)
acid_module(dsp acid_filter acid_osc)
acid_module(dist)
acid_module(inserts)
acid_module(fx acid_dist acid_inserts)
acid_module(seq)
acid_module(player)
acid_module(engine acid_dsp acid_fx acid_seq acid_player)
