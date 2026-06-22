cmake_minimum_required(VERSION 3.22)

# Projektname und Sprache definieren
project(SpectralStoryteller VERSION 0.1.0 LANGUAGES CXX)

# C++ Standard auf C++17 setzen
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

# Exportiere die JUCE binary_dir für späteren Zugriff
get_target_property(JUCE_INCLUDE_DIR juce_core INTERFACE_INCLUDE_DIRECTORIES)

# Hier aktivieren wir die Standard-Audio-Formate für Windows
juce_add_plugin(SpectralStoryteller
    COMPANY_NAME "SignalFlow"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Mnam
    PLUGIN_CODE Spec
    FORMATS VST3 Standalone
)

# NEU: Kopiert die fertige VST3 nach dem Build automatisch in deinen System-Ordner
set_target_properties(SpectralStoryteller PROPERTIES
    JUCE_COPY_PLUGIN_AFTER_BUILD ON
)

# JUCE-Module mit PUBLIC-Scope verlinken (damit includes propagiert werden)
target_link_libraries(SpectralStoryteller 
    PUBLIC
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
)

# Include Verzeichnis für lokale Source-Dateien
target_include_directories(SpectralStoryteller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# Die Quellcodedateien (C++ Quelltexte und Header)
target_sources(SpectralStoryteller PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)