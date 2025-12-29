cmake_minimum_required(VERSION 3.16)
project(SDKDumperProject LANGUAGES CXX)

add_library(SDKDumper SHARED
    src/SDKDumper.cpp
)

target_include_directories(SDKDumper PRIVATE include)
target_compile_features(SDKDumper PRIVATE cxx_std_17)

if (WIN32)
    target_link_libraries(SDKDumper PRIVATE psapi dbghelp)
    target_compile_definitions(SDKDumper PRIVATE "_WINDOWS")
endif()

set_target_properties(SDKDumper PROPERTIES
    OUTPUT_NAME "SDKDumper"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
