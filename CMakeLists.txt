cmake_minimum_required (VERSION 3.22)

# macOS: these must be set before project(). Universal binary (Apple Silicon + Intel), macOS 10.13+.
set (CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

project (TheSauceSuite VERSION 1.5.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    # Static runtime so the plugin does not need the VC++ redistributable installed
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option (SAUCE_BUILD_TESTS "Build the verification test runner" ON)
# The VST3 manifest step runs the built plugin, which is impossible when cross-compiling.
if (CMAKE_CROSSCOMPILING)
    set (SAUCE_VST3_MANIFEST FALSE)
else()
    set (SAUCE_VST3_MANIFEST TRUE)
endif()

# ---------------------------------------------------------------------------
# JUCE (pinned). Pass -DJUCE_DIR=/path/to/JUCE to use a local checkout,
# otherwise CMake downloads JUCE 8.0.15 from GitHub.
# ---------------------------------------------------------------------------
set (SAUCE_JUCE_TAG "8.0.15")
if (DEFINED JUCE_DIR)
    add_subdirectory ("${JUCE_DIR}" JUCE)
else()
    include (FetchContent)
    # Plain zip download: no Git needed on the build machine.
    FetchContent_Declare (JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/${SAUCE_JUCE_TAG}.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable (JUCE)
endif()

if (APPLE)
    set (SAUCE_FORMATS VST3 AU)
else()
    set (SAUCE_FORMATS VST3)
endif()

juce_add_plugin (TheSauceSuite
    COMPANY_NAME                "Drellmadeit"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "com.drellmadeit.thesaucesuite"
    PLUGIN_MANUFACTURER_CODE    Drel
    PLUGIN_CODE                 Sauc
    PRODUCT_NAME                "The Sauce Suite"
    VERSION                     ${PROJECT_VERSION}
    DESCRIPTION                 "Vocal production suite"
    FORMATS                     ${SAUCE_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Dynamics EQ Reverb
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          ${SAUCE_VST3_MANIFEST})

juce_add_binary_data (SauceAssets
    HEADER_NAME SauceAssets.h
    NAMESPACE   SauceAssets
    SOURCES
        Resources/fonts/Poppins-Regular.ttf
        Resources/fonts/Poppins-Medium.ttf
        Resources/fonts/Poppins-Bold.ttf
        Resources/fonts/Poppins-BoldItalic.ttf)

set (SAUCE_SOURCES
    Source/PluginProcessor.cpp
    Source/Parameters.cpp
    Source/PresetManager.cpp
    Source/FactoryPresets.cpp
    Source/AnalysisEngine.cpp
    Source/SauceAssistant.cpp
    Source/ui/AssistantPanel.cpp
    Source/PluginEditor.cpp
    Source/ui/SauceLookAndFeel.cpp
    Source/ui/Header.cpp
    Source/ui/EqGraph.cpp
    Source/ui/ModulePages.cpp
    Source/ui/SidePanels.cpp
    Source/ui/FxPanels.cpp)

target_sources (TheSauceSuite PRIVATE ${SAUCE_SOURCES})
target_include_directories (TheSauceSuite PRIVATE Source)

target_compile_definitions (TheSauceSuite
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries (TheSauceSuite
    PRIVATE
        SauceAssets
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Windows cross-build with llvm-mingw (see cmake/mingw-w64-clang.cmake): MinGW ignores the
# '#pragma comment(lib)' lines JUCE uses with MSVC, so list the Windows system libraries here.
if (MINGW)
    set (SAUCE_WIN_LIBS version shlwapi dbghelp ws2_32 winmm wininet imm32 ole32 oleaut32 uuid comdlg32 comctl32
                        shell32 user32 gdi32 rpcrt4 dwrite d2d1 d3d11 dxgi dxguid dcomp uiautomationcore advapi32 winhttp
                        setupapi usp10 crypt32 windowscodecs shcore dwmapi sapi)
    target_link_libraries (TheSauceSuite PRIVATE ${SAUCE_WIN_LIBS})
    # Embed the Common Controls 6 manifest in the plugin DLL (MSVC does this via #pragma; MinGW ignores it).
    enable_language (RC)
    target_sources (TheSauceSuite_VST3 PRIVATE Resources/win/plugin.rc)
    target_compile_definitions (TheSauceSuite PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
endif()

# ---------------------------------------------------------------------------
# Verification runner: drives the real AudioProcessor through processBlock,
# exactly like a DAW, and checks the behaviours listed in the spec.
# ---------------------------------------------------------------------------
if (SAUCE_BUILD_TESTS)
    juce_add_console_app (SauceTests PRODUCT_NAME "SauceTests")
    target_sources (SauceTests PRIVATE tests/SauceTests.cpp ${SAUCE_SOURCES})
    target_include_directories (SauceTests PRIVATE Source)
    target_compile_definitions (SauceTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="The Sauce Suite"
        JucePlugin_Manufacturer="Drellmadeit" JucePlugin_VersionString="1.5.0"
        SAUCE_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries (SauceTests PRIVATE
        SauceAssets
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    enable_testing()
    add_test (NAME SauceTests COMMAND SauceTests)
endif()

# Loads the built VST3 through JUCE's hosting code (like a DAW) and checks it.
if (SAUCE_BUILD_TESTS)
    juce_add_console_app (SauceHostCheck PRODUCT_NAME "SauceHostCheck")
    target_sources (SauceHostCheck PRIVATE tools/HostCheck.cpp)
    target_compile_definitions (SauceHostCheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries (SauceHostCheck PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
endif()

if (MINGW AND SAUCE_BUILD_TESTS)
    foreach (t SauceTests SauceHostCheck)
        target_link_libraries (${t} PRIVATE ${SAUCE_WIN_LIBS})
        target_compile_definitions (${t} PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
    endforeach()
endif()
