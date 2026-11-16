cmake_minimum_required(VERSION 3.22)
project(Filth VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FILTH_COPY_PLUGIN_AFTER_BUILD "Copy built plug-ins into the local plug-in folders" ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY      https://github.com/juce-framework/JUCE.git
    GIT_TAG             9.0.5
    GIT_SHALLOW         TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Filth
    COMPANY_NAME                "FilthAudio"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     ${FILTH_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE    FlAu
    PLUGIN_CODE                 Flth
    LV2URI                      "https://filthaudio.example.com/plugins/filth"
    FORMATS                     VST3 LV2 Standalone
    PRODUCT_NAME                "Filth"
)

target_sources(Filth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

juce_add_binary_data(FilthArtwork SOURCES Assets/FilthPedal.jpg Assets/FilthKnob.png)

target_compile_definitions(Filth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_XINPUT=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Filth
    PRIVATE
        FilthArtwork
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
