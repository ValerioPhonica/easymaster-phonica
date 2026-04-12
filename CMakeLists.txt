cmake_minimum_required(VERSION 3.22)
project(GrooveBoxPhonica VERSION 1.0.0 LANGUAGES CXX)

# --- JUCE ---
# Auto-download JUCE if not present locally
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        7.0.12
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    message(STATUS "Downloading JUCE 7.0.12 (solo la prima volta)...")
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(GrooveBoxPhonica
    PLUGIN_MANUFACTURER_CODE Phon
    PLUGIN_CODE GBPh
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "GrooveBox Phonica"
    COMPANY_NAME "Phonica School"
    BUNDLE_ID "com.phonicaschool.grooveboxphonica"
    PLUGIN_NAME "GrooveBox Phonica"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_compile_features(GrooveBoxPhonica PUBLIC cxx_std_17)

target_sources(GrooveBoxPhonica PRIVATE
    # Core
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp

    # Sequencer
    Source/Sequencer/SequencerEngine.cpp

    # Audio - Drums
    Source/Audio/DrumEngine/DrumVoice.cpp
    Source/Audio/DrumEngine/DrumSynth.cpp

    # Audio - Synth
    Source/Audio/SynthEngine/AnalogVoice.cpp

    # State
    Source/State/GrooveBoxState.cpp

    # UI
    Source/UI/LookAndFeel/GrooveBoxLookAndFeel.cpp
    Source/UI/Components/HeaderBar.cpp
    Source/UI/Components/StepButton.cpp
    Source/UI/Components/PianoKeyboard.cpp
    Source/UI/Components/StepGrid.cpp
    Source/UI/Components/DrumTrackRow.cpp
    Source/UI/Components/SynthTrackRow.cpp
    Source/UI/Components/KnobComponent.cpp
    Source/UI/Components/LEDMeter.cpp
    Source/UI/Components/RatchetRow.cpp
)

target_include_directories(GrooveBoxPhonica PRIVATE Source)

target_compile_definitions(GrooveBoxPhonica PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(GrooveBoxPhonica
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)
