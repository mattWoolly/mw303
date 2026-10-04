# Plugin tests: JUCE console apps with Catch2, compiled against the processor sources directly
# (plugin/src, minus the plugin entry point), so no plugin format needs to be built to run them.
#
#   mwacid_plugin_tests      state, parameters, processing, MIDI, presets, MIDI learn, threads
#   mwacid_plugin_rt_tests   the allocation spy (--wrap=malloc) over 30 s of processBlock
#
# Added from plugin/CMakeLists.txt so JUCE is available. For the ThreadSanitizer run configure a
# separate tree with -fsanitize=thread (see the report) and run under 'setarch -R'.
include(Catch)

set(MW_PLUGIN_TEST_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/plugin/src/Parameters.cpp
    ${CMAKE_SOURCE_DIR}/plugin/src/Model.cpp
    ${CMAKE_SOURCE_DIR}/plugin/src/PresetManager.cpp
    ${CMAKE_SOURCE_DIR}/plugin/src/Controller.cpp
    ${CMAKE_SOURCE_DIR}/plugin/src/PluginProcessor.cpp)

function(mw_plugin_test_app name)
    juce_add_console_app(${name} PRODUCT_NAME "${name}")
    target_sources(${name} PRIVATE ${ARGN} ${MW_PLUGIN_TEST_COMMON_SOURCES})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/plugin/src
        ${CMAKE_SOURCE_DIR}/tests/support
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        MW_PRODUCT_NAME="${MW_PRODUCT_NAME}"
        MW_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        MW_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-output/plugin")
    target_link_libraries(${name}
        PRIVATE
            juce::juce_audio_utils
            acid_engine
            mwacid_factory
            Catch2::Catch2
        PUBLIC
            juce::juce_recommended_config_flags)
    # Tests compare floats exactly on purpose, so JUCE's warning set (-Wfloat-equal) is not used here.
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wshadow)
endfunction()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-output/plugin)

mw_plugin_test_app(mwacid_plugin_tests
    TestMain.cpp
    ParamIdTests.cpp
    StateTests.cpp
    ProcessTests.cpp
    MidiTests.cpp
    DistModelTests.cpp
    PresetTests.cpp
    MidiLearnTests.cpp
    ControllerTests.cpp
    FactoryStateTests.cpp
    ThreadTests.cpp
    HostIntegrationTests.cpp)
add_test(NAME mwacid_plugin_tests COMMAND mwacid_plugin_tests)

# The real-time binary wraps the C allocator so every allocation and free made while the
# "audio context" flag is set on the calling thread is counted, JUCE's HeapBlock (which calls
# malloc directly) included. libstdc++ is linked statically so its own calls are wrapped too.
mw_plugin_test_app(mwacid_plugin_rt_tests
    RealtimeMain.cpp
    RealtimeTests.cpp)
target_link_options(mwacid_plugin_rt_tests PRIVATE
    -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc
    -Wl,--wrap=posix_memalign -Wl,--wrap=aligned_alloc -Wl,--wrap=memalign
    -static-libstdc++ -static-libgcc)
add_test(NAME mwacid_plugin_rt_tests COMMAND mwacid_plugin_rt_tests)
set_tests_properties(mwacid_plugin_tests mwacid_plugin_rt_tests PROPERTIES LABELS "plugin")

# ---- Independent review tests (review_*.cpp) ----------------------------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/review_PluginTests.cpp)
    mw_plugin_test_app(mwacid_plugin_review_tests TestMain.cpp review_PluginTests.cpp)
    mw_plugin_test_app(mwacid_plugin_review_rt_tests RealtimeMain.cpp review_RealtimeTests.cpp)
    target_link_options(mwacid_plugin_review_rt_tests PRIVATE
        -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc
        -Wl,--wrap=posix_memalign -Wl,--wrap=aligned_alloc -Wl,--wrap=memalign
        -static-libstdc++ -static-libgcc)
    # The findings these pin are fixed; they run with the rest. The litmus case runs 4M
    # iterations and is kept out of the default ctest run (run it by its tag).
    add_test(NAME mwacid_plugin_review_tests COMMAND mwacid_plugin_review_tests "~[litmus]")
    add_test(NAME mwacid_plugin_review_rt_tests COMMAND mwacid_plugin_review_rt_tests)
    set_tests_properties(mwacid_plugin_review_tests mwacid_plugin_review_rt_tests PROPERTIES LABELS "plugin")
endif()

# Independent editor review tests: the processor sources plus the editor (MW_UI_SOURCES, globbed
# in plugin/CMakeLists.txt) and the embedded fonts. Headless; failing cases are review findings.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/review_EditorTests.cpp AND MW_UI_SOURCES AND TARGET mwacid_assets)
    mw_plugin_test_app(mwacid_plugin_review_editor_tests TestMain.cpp review_EditorTests.cpp ${MW_UI_SOURCES})
    target_include_directories(mwacid_plugin_review_editor_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin/ui)
    target_link_libraries(mwacid_plugin_review_editor_tests PRIVATE mwacid_assets)
    add_test(NAME mwacid_plugin_review_editor_tests COMMAND mwacid_plugin_review_editor_tests "~[budget]")
    set_tests_properties(mwacid_plugin_review_editor_tests PROPERTIES LABELS "review-editor")
endif()

# ---- QA review tests (review_qa_*.cpp) ----------------------------------------------------------
# Deterministic reproductions from the five-lens QA pass. Processor-only cases build without the
# editor; the editor-side cases (UX, parity, editor robustness) need the UI sources and fonts.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/review_qa_HostRenderTests.cpp)
    mw_plugin_test_app(mwacid_plugin_review_qa_tests
        TestMain.cpp review_qa_HostRenderTests.cpp review_qa_RobustnessTests.cpp)
    add_test(NAME mwacid_plugin_review_qa_tests COMMAND mwacid_plugin_review_qa_tests)
    set_tests_properties(mwacid_plugin_review_qa_tests PROPERTIES LABELS "plugin")
endif()

if(MW_UI_SOURCES AND TARGET mwacid_assets)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/review_qa_UxTests.cpp)
        mw_plugin_test_app(mwacid_plugin_review_qa_ux_tests TestMain.cpp review_qa_UxTests.cpp ${MW_UI_SOURCES})
        target_include_directories(mwacid_plugin_review_qa_ux_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin/ui)
        target_link_libraries(mwacid_plugin_review_qa_ux_tests PRIVATE mwacid_assets)
        add_test(NAME mwacid_plugin_review_qa_ux_tests COMMAND mwacid_plugin_review_qa_ux_tests)
        set_tests_properties(mwacid_plugin_review_qa_ux_tests PROPERTIES LABELS "review-editor")
    endif()
    set(MW_QA_EDITOR_SOURCES)
    foreach(f review_qa_EditorRobustnessTests.cpp review_qa_parity_editor.cpp)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${f})
            list(APPEND MW_QA_EDITOR_SOURCES ${f})
        endif()
    endforeach()
    if(MW_QA_EDITOR_SOURCES)
        mw_plugin_test_app(mwacid_plugin_review_qa_editor_tests TestMain.cpp ${MW_QA_EDITOR_SOURCES} ${MW_UI_SOURCES})
        target_include_directories(mwacid_plugin_review_qa_editor_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin/ui)
        target_link_libraries(mwacid_plugin_review_qa_editor_tests PRIVATE mwacid_assets)
        add_test(NAME mwacid_plugin_review_qa_editor_tests COMMAND mwacid_plugin_review_qa_editor_tests)
        set_tests_properties(mwacid_plugin_review_qa_editor_tests PROPERTIES LABELS "review-editor")
    endif()
endif()
