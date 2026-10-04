# The JUCE plugin: mw12FProcessor around acid::Engine, built as VST3, CLAP, LV2 and Standalone.
# Product identity comes from the top-level CMakeLists.txt and nowhere else. The settings follow
# docs/research/07-linux-plugin-engineering.md section 2.1.

# ---- Factory content -------------------------------------------------------------------------
# Every assets/factory/**/*.mw12f is embedded through one generated table keyed by the file's
# path under assets/factory, so names may repeat across types. The glob is re-evaluated at
# build time (CONFIGURE_DEPENDS) and the table is regenerated whenever a file changes.
add_executable(mwacid_embed src/embed/EmbedFiles.cpp)
set_target_properties(mwacid_embed PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(mwacid_embed PRIVATE -Wall -Wextra)

set(MW_FACTORY_DIR ${CMAKE_SOURCE_DIR}/assets/factory)
file(GLOB_RECURSE MW_FACTORY_FILES CONFIGURE_DEPENDS ${MW_FACTORY_DIR}/*.mw12f)
list(SORT MW_FACTORY_FILES)
set(MW_FACTORY_LIST ${CMAKE_CURRENT_BINARY_DIR}/factory-files.txt)
string(REPLACE ";" "\n" _mw_factory_list_text "${MW_FACTORY_FILES}")
set(_mw_factory_list_text "${_mw_factory_list_text}\n")
set(_mw_factory_list_old "")
if(EXISTS ${MW_FACTORY_LIST})
    file(READ ${MW_FACTORY_LIST} _mw_factory_list_old)
endif()
if(NOT _mw_factory_list_old STREQUAL _mw_factory_list_text)
    file(WRITE ${MW_FACTORY_LIST} "${_mw_factory_list_text}")
endif()

set(MW_FACTORY_CPP ${CMAKE_CURRENT_BINARY_DIR}/FactoryContent.cpp)
add_custom_command(
    OUTPUT ${MW_FACTORY_CPP}
    COMMAND mwacid_embed ${MW_FACTORY_CPP} ${MW_FACTORY_DIR} ${MW_FACTORY_LIST}
    DEPENDS mwacid_embed ${MW_FACTORY_FILES} ${MW_FACTORY_LIST}
    COMMENT "Embedding factory content (${CMAKE_SOURCE_DIR}/assets/factory)"
    VERBATIM)
add_library(mwacid_factory STATIC ${MW_FACTORY_CPP})
target_include_directories(mwacid_factory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(mwacid_factory PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- The plugin --------------------------------------------------------------------------------
set(MW_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.cpp)

juce_add_plugin(mwacid_plugin
    PRODUCT_NAME                "${MW_PRODUCT_NAME}"
    VERSION                     ${PROJECT_VERSION}
    COMPANY_NAME                "mwAudio"
    COMPANY_WEBSITE             "https://github.com/mattWoolly/${MW_PRODUCT_NAME}"
    BUNDLE_ID                   "com.mwaudio.${MW_PRODUCT_ID}"
    PLUGIN_MANUFACTURER_CODE    mwAu
    PLUGIN_CODE                 ${MW_PLUGIN_CODE}
    DESCRIPTION                 "Classic bass line synthesizer"
    FORMATS                     VST3 LV2 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Synth
    LV2URI                      "https://github.com/mattWoolly/${MW_PRODUCT_NAME}"
    LV2_SHARED_LIBRARY_NAME     ${MW_PRODUCT_NAME}
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

target_sources(mwacid_plugin PRIVATE ${MW_PLUGIN_SOURCES} src/PluginEntry.cpp)

# Fonts and other embedded assets. Re-run CMake after adding files.
file(GLOB MW_ASSET_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/assets/fonts/*.ttf ${CMAKE_SOURCE_DIR}/assets/fonts/*.otf)
if(MW_ASSET_FILES)
    juce_add_binary_data(mwacid_assets HEADER_NAME "mwAssets.h" NAMESPACE mwAssets SOURCES ${MW_ASSET_FILES})
    set_target_properties(mwacid_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Editor sources are globbed so the UI kit, the editor and the gallery tool share one list.
file(GLOB_RECURSE MW_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui-gallery/CMakeLists.txt")
    add_subdirectory(ui-gallery)
endif()
target_include_directories(mwacid_plugin PRIVATE src)

target_compile_definitions(mwacid_plugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_JACK=1
        JUCE_JACK_CLIENT_NAME="${MW_PRODUCT_NAME}"
        MW_PRODUCT_NAME="${MW_PRODUCT_NAME}"
)

target_link_libraries(mwacid_plugin
    PRIVATE
        juce::juce_audio_utils
        acid_engine
        mwacid_factory
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

clap_juce_extensions_plugin(
    TARGET           mwacid_plugin
    CLAP_ID          "com.mwaudio.${MW_PRODUCT_ID}"
    CLAP_FEATURES    instrument synthesizer stereo
    CLAP_SUPPORT_URL "https://github.com/mattWoolly/${MW_PRODUCT_NAME}/issues"
    CLAP_MANUAL_URL  "https://github.com/mattWoolly/${MW_PRODUCT_NAME}"
)

if(MW_LTO)
    target_link_libraries(mwacid_plugin PUBLIC juce::juce_recommended_lto_flags)
endif()

# ---- Plugin tests (JUCE console apps with Catch2) ----------------------------------------------
if(MW_BUILD_TESTS AND TARGET Catch2::Catch2 AND EXISTS "${CMAKE_SOURCE_DIR}/tests/plugin/CMakeLists.txt")
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests/plugin ${CMAKE_BINARY_DIR}/tests/plugin)
endif()
