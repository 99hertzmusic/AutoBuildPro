cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

project(AutoBuildPro VERSION 1.0.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(AUTOBUILDPRO_COPY_PLUGIN_AFTER_BUILD "Copy built plugins into local plug-in folders" ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.13
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(AutoBuildPro
    COMPANY_NAME "99Hertz"
    BUNDLE_ID "com.99hertz.autobuildpro"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${AUTOBUILDPRO_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Hz99
    PLUGIN_CODE ABpr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "AutoBuild Pro"
)

juce_add_binary_data(AutoBuildProData SOURCES
    Resources/Fonts/BarlowCondensed-Bold.ttf
    Resources/Fonts/BarlowCondensed-SemiBold.ttf
    Resources/Fonts/BarlowCondensed-Medium.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)
set_target_properties(AutoBuildProData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(AUTOBUILDPRO_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Licence/Licence.cpp
    Source/Licence/tweetnacl.c)

target_sources(AutoBuildPro PRIVATE ${AUTOBUILDPRO_SOURCES})

target_compile_definitions(AutoBuildPro PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_features(AutoBuildPro PUBLIC cxx_std_17)

target_link_libraries(AutoBuildPro
    PRIVATE AutoBuildProData juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Offline test: build timing vs host, clicks, state, Lite gating, levels, licence keys, sound examples, UI snapshots (not shipped)
option(AUTOBUILDPRO_BUILD_TEST "Build the offline test app" OFF)
if(AUTOBUILDPRO_BUILD_TEST)
    juce_add_console_app(AutoBuildProTest PRODUCT_NAME "AutoBuildProTest")
    target_sources(AutoBuildProTest PRIVATE Test/Main.cpp ${AUTOBUILDPRO_SOURCES})
    target_compile_definitions(AutoBuildProTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        AUTOBUILDPRO_TEST=1 JucePlugin_VersionString="1.0.0" JUCE_PLUGINHOST_VST3=1)
    target_compile_features(AutoBuildProTest PRIVATE cxx_std_17)
    target_link_libraries(AutoBuildProTest PRIVATE AutoBuildProData juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
endif()
