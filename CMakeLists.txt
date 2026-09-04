cmake_minimum_required(VERSION 3.22)

# Resolve is commonly run natively on Apple Silicon, while Intel hosts still remain in
# use. Build both slices by default so the same VST3 bundle loads in either process.
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(ClearRoom VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ClearRoom
    COMPANY_NAME "Jenya Audio"
    BUNDLE_ID "audio.jenya.dereverb2"
    PLUGIN_MANUFACTURER_CODE Jnya
    PLUGIN_CODE JDv2
    # The Mac App Store build of DaVinci Resolve is sandboxed and loads Audio Units only;
    # it will not scan VST3 at all. Ship both so either Resolve distribution can host this.
    FORMATS VST3 AU
    PRODUCT_NAME "JenyaDereverb2"
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect)

juce_generate_juce_header(ClearRoom)

target_sources(ClearRoom PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/NeuralEnhancer.cpp
    Source/NeuralEnhancer.h)

juce_add_binary_data(JenyaDereverb2Model SOURCES
    Assets/Models/dpdfnet2_48khz_hr.onnx)

set(ORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/onnxruntime-osx-universal2-1.23.2")
set(ORT_DYLIB "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libonnxruntime.dylib")
target_include_directories(ClearRoom SYSTEM PUBLIC "${ORT_ROOT}/include")

target_compile_definitions(ClearRoom PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ClearRoom PRIVATE
    JenyaDereverb2Model
    "${ORT_DYLIB}"
    "-framework Accelerate"
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Each plug-in bundle carries its own copy of the ONNX Runtime dylib and is sealed after
# the link step (JUCE's VST3 helper writes moduleinfo.json late), so that strict hosts such
# as Resolve do not reject it at instantiation.
if(APPLE)
    foreach(format IN ITEMS VST3 AU)
        if(TARGET ClearRoom_${format})
            set_target_properties(ClearRoom_${format} PROPERTIES
                BUILD_RPATH "@loader_path/../Frameworks")
            add_custom_command(TARGET ClearRoom_${format} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_BUNDLE_DIR:ClearRoom_${format}>/Contents/Frameworks"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DYLIB}"
                        "$<TARGET_BUNDLE_DIR:ClearRoom_${format}>/Contents/Frameworks/libonnxruntime.dylib"
                COMMAND codesign --force --sign -
                        "$<TARGET_BUNDLE_DIR:ClearRoom_${format}>/Contents/Frameworks/libonnxruntime.dylib"
                COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ClearRoom_${format}>"
                COMMENT "Ad-hoc signing completed JenyaDereverb2 ${format} bundle")
        endif()
    endforeach()
endif()

option(CLEARROOM_BUILD_TESTS "Build ClearRoom DSP regression tests" ON)
if(CLEARROOM_BUILD_TESTS)
    juce_add_console_app(ClearRoomTests PRODUCT_NAME "ClearRoom DSP Tests")
    juce_generate_juce_header(ClearRoomTests)
    target_sources(ClearRoomTests PRIVATE Tests/DspTests.cpp)
    target_link_libraries(ClearRoomTests PRIVATE ClearRoom JenyaDereverb2Model juce::juce_audio_utils)
    set_target_properties(ClearRoomTests PROPERTIES BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
    target_compile_definitions(ClearRoomTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

    # Loads the finished .vst3 through JUCE's VST3 host, which is the same scan/instantiate
    # path a DAW uses. Catches bundle and factory faults that the DSP tests cannot see.
    juce_add_console_app(ClearRoomHostCheck PRODUCT_NAME "ClearRoom Host Check")
    juce_generate_juce_header(ClearRoomHostCheck)
    target_sources(ClearRoomHostCheck PRIVATE Tests/HostCheck.cpp)
    target_link_libraries(ClearRoomHostCheck PRIVATE juce::juce_audio_utils juce::juce_audio_processors)
    target_compile_definitions(ClearRoomHostCheck PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
endif()
