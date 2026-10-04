# The editor sources and the gallery tool.
#
# This file is included from plugin/CMakeLists.txt after the plugin target exists, so it is
# also where the editor is wired into the plugin: the UI sources (MW_UI_SOURCES, globbed by
# the parent) are compiled into mwacid_plugin, which gets the ui include root and the embedded
# fonts. The processor's own CMake could absorb those three lines; they live here so the
# editor workstream owns them.
target_sources(mwacid_plugin PRIVATE ${MW_UI_SOURCES})
target_include_directories(mwacid_plugin PRIVATE ${CMAKE_SOURCE_DIR}/plugin/ui)
target_link_libraries(mwacid_plugin PRIVATE mwacid_assets)

# The processor reaches the editor through a weak reference to mw12FCreateEditor (so the test
# executables link without the UI). The format binaries link the shared code as a static
# archive, and a weak reference does not pull an archive member in, so each format is told
# the symbol is needed. The option is INTERFACE: it applies to the targets linking the shared
# code, not to the archive itself.
target_link_options(mwacid_plugin INTERFACE "LINKER:--undefined=mw12FCreateEditor")

# ---- Gallery -----------------------------------------------------------------------------------
# Renders the kit, the step editors and the real editor (over a real processor) to PNG files in
# both themes, times the editor's paint, and drives a few interactions through the controls.
# Headless: run it as
#   xvfb-run -a build-plugin/plugin/ui-gallery/mwacid_ui_gallery_artefacts/Release/mwacid_ui_gallery [out dir]
juce_add_console_app(mwacid_ui_gallery PRODUCT_NAME "mwacid_ui_gallery")

set(MW_GALLERY_PLUGIN_SOURCES
    ${CMAKE_SOURCE_DIR}/plugin/src/Parameters.cpp
    ${CMAKE_SOURCE_DIR}/plugin/src/Model.cpp
    ${CMAKE_SOURCE_DIR}/plugin/src/PresetManager.cpp
    ${CMAKE_SOURCE_DIR}/plugin/src/Controller.cpp
    ${CMAKE_SOURCE_DIR}/plugin/src/PluginProcessor.cpp)

target_sources(mwacid_ui_gallery PRIVATE Main.cpp ${MW_UI_SOURCES} ${MW_GALLERY_PLUGIN_SOURCES})

target_include_directories(mwacid_ui_gallery PRIVATE ${CMAKE_SOURCE_DIR}/plugin/ui ${CMAKE_SOURCE_DIR}/plugin/src)

target_compile_definitions(mwacid_ui_gallery
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        MW_PRODUCT_NAME="${MW_PRODUCT_NAME}"
        MW_GALLERY_DEFAULT_OUT="${CMAKE_BINARY_DIR}/gallery"
)

target_link_libraries(mwacid_ui_gallery
    PRIVATE
        juce::juce_audio_utils
        mwacid_assets
        mwacid_factory
        acid_engine
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---- Editor tests (ctest) ----------------------------------------------------------------------
# Catch2 cases over a real editor on a real processor, headless: the paint budget (a full
# repaint at 1x and 2x, a single knob repaint), what a parameter change may and may not
# re-render, and the editor behaviours the review pinned (browser audition against a host state
# load, held overrides, zoom and host resizes, double-click defaults, the file dialogs' owner,
# drag-out file paths). Fails above the budget: that is the point of the timing cases.
if(MW_BUILD_TESTS AND TARGET Catch2::Catch2)
    juce_add_console_app(mwacid_ui_tests PRODUCT_NAME "mwacid_ui_tests")
    target_sources(mwacid_ui_tests PRIVATE EditorTests.cpp ${MW_UI_SOURCES} ${MW_GALLERY_PLUGIN_SOURCES})
    target_include_directories(mwacid_ui_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/plugin/ui
        ${CMAKE_SOURCE_DIR}/plugin/src
        ${CMAKE_SOURCE_DIR}/tests/plugin)
    target_compile_definitions(mwacid_ui_tests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=0
            MW_PRODUCT_NAME="${MW_PRODUCT_NAME}"
            MW_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-output/ui")
    target_link_libraries(mwacid_ui_tests
        PRIVATE
            juce::juce_audio_utils
            mwacid_assets
            mwacid_factory
            acid_engine
            Catch2::Catch2
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-output/ui)
    add_test(NAME mwacid_ui_tests COMMAND mwacid_ui_tests)
    set_tests_properties(mwacid_ui_tests PROPERTIES LABELS "ui")

    # The QA pass's editor-area review tests (tests/plugin/review_qa_*): the UTF-8 robustness
    # cases (run them under ASan as well) and the parity cases for the editor. Same shape as
    # mwacid_ui_tests, with the tests' own Catch2 main.
    foreach(_qa IN ITEMS robustness:review_qa_EditorRobustnessTests parity:review_qa_parity_editor)
        string(REPLACE ":" ";" _qa_pair "${_qa}")
        list(GET _qa_pair 0 _qa_kind)
        list(GET _qa_pair 1 _qa_file)
        set(_qa_target mwacid_qa_editor_${_qa_kind}_tests)

        if(EXISTS ${CMAKE_SOURCE_DIR}/tests/plugin/${_qa_file}.cpp)
            juce_add_console_app(${_qa_target} PRODUCT_NAME "${_qa_target}")
            target_sources(${_qa_target} PRIVATE ${CMAKE_SOURCE_DIR}/tests/plugin/TestMain.cpp
                ${CMAKE_SOURCE_DIR}/tests/plugin/${_qa_file}.cpp ${MW_UI_SOURCES} ${MW_GALLERY_PLUGIN_SOURCES})
            target_include_directories(${_qa_target} PRIVATE
                ${CMAKE_SOURCE_DIR}/plugin/ui
                ${CMAKE_SOURCE_DIR}/plugin/src
                ${CMAKE_SOURCE_DIR}/tests/plugin
                ${CMAKE_SOURCE_DIR}/tests/support)
            target_compile_definitions(${_qa_target}
                PRIVATE
                    JUCE_WEB_BROWSER=0
                    JUCE_USE_CURL=0
                    JUCE_MODAL_LOOPS_PERMITTED=0
                    MW_PRODUCT_NAME="${MW_PRODUCT_NAME}"
                    MW_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}/tests/plugin"
                    MW_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-output/ui")
            target_link_libraries(${_qa_target}
                PRIVATE
                    juce::juce_audio_utils
                    mwacid_assets
                    mwacid_factory
                    acid_engine
                    Catch2::Catch2
                PUBLIC
                    juce::juce_recommended_config_flags)
            # Tests compare floats exactly on purpose, so JUCE's warning set (-Wfloat-equal) is not used here.
            target_compile_options(${_qa_target} PRIVATE -Wall -Wextra -Wshadow)
            add_test(NAME ${_qa_target} COMMAND ${_qa_target})
            set_tests_properties(${_qa_target} PROPERTIES LABELS "ui;review-qa")
        endif()
    endforeach()
endif()
