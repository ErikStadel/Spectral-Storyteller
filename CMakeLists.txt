cmake_minimum_required(VERSION 3.22)

# Projektname und Sprache definieren
project(SpectralStoryteller VERSION 0.1.0 LANGUAGES CXX)

# C++ Standard auf C++17 oder C++20 setzen
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE automatisch von GitHub herunterladen
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12 # Eine stabile JUCE-Version
)
FetchContent_MakeAvailable(JUCE)

# Hier aktivieren wir die Standard-Audio-Formate
# Wichtig: "Standalone" erzeugt eine eigene App – perfekt zum Testen ohne DAW!
juce_add_plugin(SpectralStoryteller
    COMPANY_NAME "SignalFlow"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Mnam
    PLUGIN_CODE Spec
    FORMATS VST3 AU Standalone 
)

target_sources(SpectralStoryteller PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(SpectralStoryteller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)