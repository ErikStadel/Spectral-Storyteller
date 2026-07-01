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

# Add post-build command to copy VST3 to custom location
add_custom_command(TARGET SpectralStoryteller POST_BUILD
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "& {"
        "$src='${CMAKE_CURRENT_BINARY_DIR}/SpectralStoryteller_artefacts/Release/VST3';"
        "$dst='D:/VST/VST/VST3';"
        "New-Item -ItemType Directory -Force -Path $dst | Out-Null;"
        "if (Test-Path $src) { Copy-Item -Path (Join-Path $src '*') -Destination $dst -Recurse -Force -ErrorAction Continue }"
        "exit 0"
        "}"
    COMMENT "Copying VST3 plugin to D:/VST/VST/VST3 (non-fatal on copy errors)"
)

# JUCE-Module verlinken
target_link_libraries(SpectralStoryteller 
    PRIVATE
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

# Explizite Include-Verzeichnisse für JUCE
target_include_directories(SpectralStoryteller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${JUCE_SOURCE_DIR}/modules
)

# Disable VST2 compatibility code in JUCE VST3 client
target_compile_definitions(SpectralStoryteller PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# Linux: add GTK/WebKit include paths needed by juce_gui_extra
if(UNIX AND NOT APPLE)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK3 gtk+-3.0)
        if(GTK3_FOUND)
            target_include_directories(SpectralStoryteller PRIVATE ${GTK3_INCLUDE_DIRS})
            target_link_libraries(SpectralStoryteller PRIVATE ${GTK3_LIBRARIES})
        endif()
        pkg_check_modules(WEBKIT webkit2gtk-4.0)
        if(WEBKIT_FOUND)
            target_include_directories(SpectralStoryteller PRIVATE ${WEBKIT_INCLUDE_DIRS})
            target_link_libraries(SpectralStoryteller PRIVATE ${WEBKIT_LIBRARIES})
        endif()
    endif()
endif()

# Die Quellcodedateien (C++ Quelltexte und Header)
target_sources(SpectralStoryteller PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DSP/SpectralFrameBuffer.cpp
    Source/DSP/SpectralFrameBuffer.h
    Source/DSP/ObjectDatabase.cpp
    Source/DSP/ObjectDatabase.h
    Source/DSP/ModulationMatrix.cpp
    Source/DSP/ModulationMatrix.h
    Source/DSP/TimelineData.cpp
    Source/DSP/TimelineData.h
    Source/UI/SpectralView.cpp
    Source/UI/SpectralView.h
    Source/UI/SpectralSelector.cpp
    Source/UI/SpectralSelector.h
    Source/UI/ObjectSidebar.cpp
    Source/UI/ObjectSidebar.h
    Source/UI/ModulationPanel.cpp
    Source/UI/ModulationPanel.h
    Source/UI/StoryTimelineComponent.cpp
    Source/UI/StoryTimelineComponent.h
    Source/UI/FxRackPanel.cpp
    Source/UI/FxRackPanel.h
)