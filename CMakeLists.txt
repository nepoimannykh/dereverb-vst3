cmake_minimum_required(VERSION 3.22)

# Resolve is commonly run natively on Apple Silicon, while Intel hosts still remain in
# use. Build both slices by default so the same VST3 bundle loads in either process.
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(ClearRoom VERSION 0.4.1 LANGUAGES C CXX)

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
    BUNDLE_ID "audio.jenya.dereverb"
    PLUGIN_MANUFACTURER_CODE Jnya
    PLUGIN_CODE JDrv
    FORMATS VST3
    PRODUCT_NAME "JenyaDereverb"
    VST3_CATEGORIES Fx Reverb)

juce_generate_juce_header(ClearRoom)

target_sources(ClearRoom PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/OnlineWpe.cpp
    Source/OnlineWpe.h
    Source/NeuralEnhancer.cpp
    Source/NeuralEnhancer.h)

juce_add_binary_data(JenyaDereverbModel SOURCES
    Assets/Models/dpdfnet2_48khz_hr.onnx)

set(ORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/onnxruntime-osx-universal2-1.23.2")
set(ORT_DYLIB "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libonnxruntime.dylib")
target_include_directories(ClearRoom SYSTEM PUBLIC "${ORT_ROOT}/include")

target_compile_definitions(ClearRoom PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ClearRoom PRIVATE
    JenyaDereverbModel
    "${ORT_DYLIB}"
    "-framework Accelerate"
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# JUCE's VST3 helper writes moduleinfo.json after the link step. Seal the completed
# bundle afterward so strict hosts such as Resolve do not reject it at instantiation.
if(APPLE)
    set_target_properties(ClearRoom_VST3 PROPERTIES
        BUILD_RPATH "@loader_path/../Frameworks")
    add_custom_command(TARGET ClearRoom_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:ClearRoom_VST3>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DYLIB}"
                "$<TARGET_BUNDLE_DIR:ClearRoom_VST3>/Contents/Frameworks/libonnxruntime.dylib"
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:ClearRoom_VST3>/Contents/Frameworks/libonnxruntime.dylib"
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ClearRoom_VST3>"
        COMMENT "Ad-hoc signing completed JenyaDereverb VST3 bundle")
endif()

option(CLEARROOM_BUILD_TESTS "Build ClearRoom DSP regression tests" ON)
if(CLEARROOM_BUILD_TESTS)
    juce_add_console_app(ClearRoomTests PRODUCT_NAME "ClearRoom DSP Tests")
    juce_generate_juce_header(ClearRoomTests)
    target_sources(ClearRoomTests PRIVATE Tests/DspTests.cpp)
    target_link_libraries(ClearRoomTests PRIVATE ClearRoom JenyaDereverbModel juce::juce_audio_utils)
    set_target_properties(ClearRoomTests PROPERTIES BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
    target_compile_definitions(ClearRoomTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
endif()
