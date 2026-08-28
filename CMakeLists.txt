cmake_minimum_required(VERSION 3.22)

project(SpectralFix VERSION 1.0 LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "SpectralFix is a 32-bit Windows Ashita plugin.")
endif()

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "SpectralFix must be configured for Win32 (-A Win32).")
endif()

set(ASHITA_SDK_PATH "" CACHE PATH "Path to Ashita's plugins/sdk directory")
if (NOT EXISTS "${ASHITA_SDK_PATH}/Ashita.h")
    message(FATAL_ERROR "Set ASHITA_SDK_PATH to a complete Ashita plugins/sdk directory.")
endif()

configure_file(src/version.hpp.in generated/version.hpp @ONLY)

add_library(spectralfix MODULE
    src/spectralfix.cpp
    src/exports.def
)
target_include_directories(spectralfix PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ASHITA_SDK_PATH}"
)
target_compile_features(spectralfix PRIVATE cxx_std_20)
target_compile_definitions(spectralfix PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(spectralfix PROPERTIES
    PREFIX ""
    OUTPUT_NAME "spectralfix"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

function(spectralfix_msvc_defaults target)
    if (MSVC)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    endif()
endfunction()

spectralfix_msvc_defaults(spectralfix)
if (MSVC)
    target_compile_options(spectralfix PRIVATE /EHa)
    target_link_options(spectralfix PRIVATE /SAFESEH:NO)
endif()

enable_testing()

add_executable(spectralfix_export_smoke tests/export_smoke.cpp)
target_include_directories(spectralfix_export_smoke PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ASHITA_SDK_PATH}"
)
target_compile_features(spectralfix_export_smoke PRIVATE cxx_std_20)
set_target_properties(spectralfix_export_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
spectralfix_msvc_defaults(spectralfix_export_smoke)
if (MSVC)
    target_compile_options(spectralfix_export_smoke PRIVATE /EHa)
endif()
add_test(NAME spectralfix_exports
    COMMAND spectralfix_export_smoke "$<TARGET_FILE:spectralfix>")

add_executable(spectralfix_geometry_validation tests/geometry_validation.cpp)
target_include_directories(spectralfix_geometry_validation PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
set_target_properties(spectralfix_geometry_validation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
spectralfix_msvc_defaults(spectralfix_geometry_validation)
add_test(NAME spectralfix_geometry COMMAND spectralfix_geometry_validation)

add_executable(spectralfix_selector_policy_validation tests/selector_policy_validation.cpp)
target_include_directories(spectralfix_selector_policy_validation PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
set_target_properties(spectralfix_selector_policy_validation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
spectralfix_msvc_defaults(spectralfix_selector_policy_validation)
add_test(NAME spectralfix_selector_policy COMMAND spectralfix_selector_policy_validation)

add_executable(spectralfix_wrapper_smoke tests/wrapper_smoke.cpp)
target_include_directories(spectralfix_wrapper_smoke PRIVATE "${ASHITA_SDK_PATH}")
target_compile_features(spectralfix_wrapper_smoke PRIVATE cxx_std_20)
set_target_properties(spectralfix_wrapper_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
spectralfix_msvc_defaults(spectralfix_wrapper_smoke)
if (MSVC)
    target_compile_options(spectralfix_wrapper_smoke PRIVATE /EHa)
endif()
