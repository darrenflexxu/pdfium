cmake_minimum_required(VERSION 3.16)
project(PdfReader VERSION 1.0.0 LANGUAGES CXX)

# ------------------------------------------------------------
# Global Settings
# ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC specific warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
    # Disable specific warnings common in PDFium headers
    add_compile_options(/wd4251 /wd4275 /wd4800 /wd4244 /wd4267 /wd4305)
endif()

# ------------------------------------------------------------
# PDFium Build Pipeline Integration
# ------------------------------------------------------------
# Option to build PDFium from source
option(BUILD_PDFIUM_FROM_SOURCE "Build PDFium from source using GN/Ninja" OFF)
option(PDFIUM_BUILD_DEBUG "Build PDFium in debug mode" OFF)
option(PDFIUM_ENABLE_XFA "Enable XFA support in PDFium" ON)
option(PDFIUM_ENABLE_V8 "Enable V8 JavaScript engine in PDFium" OFF)
option(PDFIUM_COMPONENT_BUILD "Build PDFium as shared library components" OFF)

# Cross-compilation targets
set(PDFIUM_TARGET_OS "" CACHE STRING "Target OS for PDFium build (win, mac, linux)")
set(PDFIUM_TARGET_CPU "" CACHE STRING "Target CPU for PDFium build (x64, arm64, arm, x86)")

# Paths
set(PDFIUM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/pdfium")
set(PDFIUM_DEPOT_TOOLS_DIR "${CMAKE_SOURCE_DIR}/depot_tools")
set(PDFIUM_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/build_scripts/build_pdfium.py")
set(PDFIUM_BOOTSTRAP_SCRIPT "${CMAKE_SOURCE_DIR}/build_scripts/bootstrap.py")

# Detect host platform for default configuration
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PDFIUM_HOST_OS "win")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
        set(PDFIUM_HOST_CPU "x64")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
        set(PDFIUM_HOST_CPU "arm64")
    else()
        set(PDFIUM_HOST_CPU "x64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PDFIUM_HOST_OS "mac")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(PDFIUM_HOST_CPU "arm64")
    else()
        set(PDFIUM_HOST_CPU "x64")
    endif()
else()
    set(PDFIUM_HOST_OS "linux")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(PDFIUM_HOST_CPU "x64")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(PDFIUM_HOST_CPU "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
        set(PDFIUM_HOST_CPU "arm")
    else()
        set(PDFIUM_HOST_CPU "x64")
    endif()
endif()

# Use host as default target if not specified
if(PDFIUM_TARGET_OS STREQUAL "")
    set(PDFIUM_TARGET_OS ${PDFIUM_HOST_OS})
endif()
if(PDFIUM_TARGET_CPU STREQUAL "")
    set(PDFIUM_TARGET_CPU ${PDFIUM_HOST_CPU})
endif()

# ------------------------------------------------------------
# PDFium External Project (build from source)
# ------------------------------------------------------------
if(BUILD_PDFIUM_FROM_SOURCE)
    message(STATUS "PDFium: Building from source (${PDFIUM_TARGET_OS}/${PDFIUM_TARGET_CPU})")
    
    # Find Python interpreter
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    
    # Custom target to bootstrap environment (run once)
    add_custom_target(pdfium_bootstrap
        COMMAND ${Python3_EXECUTABLE} ${PDFIUM_BOOTSTRAP_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Bootstrapping PDFium build environment (depot_tools, source sync)"
        VERBATIM
    )
    
    # Custom target to build PDFium
    add_custom_target(pdfium_build
        COMMAND ${Python3_EXECUTABLE} ${PDFIUM_BUILD_SCRIPT}
            --target-os ${PDFIUM_TARGET_OS}
            --target-cpu ${PDFIUM_TARGET_CPU}
            ${PDFIUM_BUILD_DEBUG} --debug
            ${PDFIUM_ENABLE_XFA} --no-xfa
            ${PDFIUM_ENABLE_V8} --enable-v8
            ${PDFIUM_COMPONENT_BUILD} --component
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building PDFium for ${PDFIUM_TARGET_OS}/${PDFIUM_TARGET_CPU}"
        DEPENDS pdfium_bootstrap
        VERBATIM
    )
    
    # Make pdfium_wrapper depend on pdfium_build
    set(PDFIUM_LIB_DEPENDS pdfium_build)
else()
    message(STATUS "PDFium: Using pre-built binaries from ${PDFIUM_SOURCE_DIR}")
    set(PDFIUM_LIB_DEPENDS)
endif()

# ------------------------------------------------------------
# PDFium Dependency Configuration
# ------------------------------------------------------------
# Expects the following structure in third_party/pdfium/:
#   include/     -> fpdfview.h, fpdf_edit.h, etc.
#   bin/         -> pdfium.dll / libpdfium.dylib / libpdfium.so
#   lib/         -> pdfium.lib / libpdfium.a
set(PDFIUM_ROOT "${CMAKE_SOURCE_DIR}/third_party/pdfium" CACHE PATH "Path to PDFium SDK")

# Verify PDFium installation
if(NOT EXISTS "${PDFIUM_ROOT}/include/fpdfview.h")
    if(BUILD_PDFIUM_FROM_SOURCE)
        message(WARNING "PDFium headers not found. Run 'cmake --build . --target pdfium_build' first.")
    else()
        message(WARNING "PDFium headers not found at ${PDFIUM_ROOT}/include/. Build will fail.")
    endif()
endif()

# Find PDFium library
if(WIN32)
    set(PDFIUM_LIBRARY_NAME "pdfium")
    set(PDFIUM_LIBRARY_PATH "${PDFIUM_ROOT}/lib/pdfium.lib")
    set(PDFIUM_DLL_PATH "${PDFIUM_ROOT}/bin/pdfium.dll")
elseif(APPLE)
    set(PDFIUM_LIBRARY_NAME "pdfium")
    set(PDFIUM_LIBRARY_PATH "${PDFIUM_ROOT}/lib/libpdfium.dylib")
    set(PDFIUM_DLL_PATH "${PDFIUM_ROOT}/bin/libpdfium.dylib")
else()
    set(PDFIUM_LIBRARY_NAME "pdfium")
    set(PDFIUM_LIBRARY_PATH "${PDFIUM_ROOT}/lib/libpdfium.so")
    set(PDFIUM_DLL_PATH "${PDFIUM_ROOT}/bin/libpdfium.so")
endif()

# ------------------------------------------------------------
# Output Directories
# ------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------
add_subdirectory(pdfium_wrapper)
add_subdirectory(qt_app)

# ------------------------------------------------------------
# Install (Optional)
# ------------------------------------------------------------
# install(TARGETS pdfium_wrapper pdf_reader
#         RUNTIME DESTINATION bin
#         LIBRARY DESTINATION lib
#         ARCHIVE DESTINATION lib)

# ------------------------------------------------------------
# Custom Targets for PDFium Development
# ------------------------------------------------------------
add_custom_target(pdfium_clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${PDFIUM_SOURCE_DIR}/out
    COMMENT "Cleaning PDFium build directories"
)

add_custom_target(pdfium_list_configs
    COMMAND ${Python3_EXECUTABLE} ${PDFIUM_BUILD_SCRIPT} --list-configs
    COMMENT "Listing supported PDFium build configurations"
)