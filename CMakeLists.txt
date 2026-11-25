cmake_minimum_required(VERSION 3.22)
project(ColorForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the plugin loads without the VC++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CF_JUCE_DIR "JUCE7" CACHE STRING "JUCE checkout directory, relative to this file")
set(CF_AUTO_MANIFEST TRUE)
if(MINGW)
    # Self-contained binary: no libstdc++/libgcc/winpthread DLLs next to the plugin.
    add_link_options(-static -static-libgcc -static-libstdc++)
    set(CF_AUTO_MANIFEST FALSE)
endif()

add_subdirectory(${CF_JUCE_DIR})

juce_add_plugin(ColorForge
    COMPANY_NAME "ColorForge Audio"
    PLUGIN_MANUFACTURER_CODE Cfga
    PLUGIN_CODE Cfrg
    FORMATS VST3
    PRODUCT_NAME "ColorForge"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST ${CF_AUTO_MANIFEST}
    VST3_CATEGORIES Fx Filter Modulation)

# Processor + editor sources, shared by the plugin and the standalone player.
file(GLOB CF_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp")
if(CF_SOURCES)
    target_sources(ColorForge PRIVATE ${CF_SOURCES})
endif()

target_compile_definitions(ColorForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0)

target_link_libraries(ColorForge
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

# Standalone player: hosts the same processor + editor, plays dropped audio files through it.
juce_add_gui_app(ColorForgePlayer
    PRODUCT_NAME "ColorForge Player"
    COMPANY_NAME "ColorForge Audio"
    VERSION 1.0.0)

target_sources(ColorForgePlayer PRIVATE Player/PlayerMain.cpp ${CF_SOURCES})
target_include_directories(ColorForgePlayer PRIVATE Source)

target_compile_definitions(ColorForgePlayer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ColorForgePlayer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ColorForgePlayer,JUCE_VERSION>")

target_link_libraries(ColorForgePlayer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
