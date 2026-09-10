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
option(BUILD_PDFIUM_FROM_SOURCE "Build PDFium from source using GN/Ninja" OFF)
# Deprecated: pdfium_build_debug/pdfium_build_release/pdfium_build targets now
# drive debug vs release; this option is retained only for cache compatibility.
option(PDFIUM_BUILD_DEBUG "Build PDFium in debug mode" OFF)
option(PDFIUM_ENABLE_XFA "Enable XFA support in PDFium" ON)
option(PDFIUM_ENABLE_V8 "Enable V8 JavaScript engine in PDFium" OFF)
option(PDFIUM_COMPONENT_BUILD "Build PDFium as shared library components" OFF)
option(PDFIUM_ENABLE_PYTHON "Build Python bindings (pybind11)" ON)

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
# PDFium Dependency: INTERFACE Library (Lazy/Forward Declaration)
# This target exists at configure time but gets populated at build time
# ------------------------------------------------------------
add_library(pdfium INTERFACE)

# Common include directory (will exist after pdfium_build)
target_include_directories(pdfium
    SYSTEM INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/pdfium/include>
)

# PDFium library paths for different configurations
if(WIN32)
    set(PDFIUM_LIB_RELEASE "${CMAKE_SOURCE_DIR}/third_party/pdfium/lib/pdfium.lib" CACHE FILEPATH "Path to release PDFium library")
    set(PDFIUM_LIB_DEBUG "${CMAKE_SOURCE_DIR}/third_party/pdfium/lib/pdfium_debug.lib" CACHE FILEPATH "Path to debug PDFium library")
elseif(APPLE)
    set(PDFIUM_LIB_RELEASE "${CMAKE_SOURCE_DIR}/third_party/pdfium/lib/libpdfium.a" CACHE FILEPATH "Path to release PDFium library")
    set(PDFIUM_LIB_DEBUG "${CMAKE_SOURCE_DIR}/third_party/pdfium/lib/libpdfium_debug.a" CACHE FILEPATH "Path to debug PDFium library")
else()
    set(PDFIUM_LIB_RELEASE "${CMAKE_SOURCE_DIR}/third_party/pdfium/lib/libpdfium.so" CACHE FILEPATH "Path to release PDFium library")
    set(PDFIUM_LIB_DEBUG "${CMAKE_SOURCE_DIR}/third_party/pdfium/lib/libpdfium_debug.so" CACHE FILEPATH "Path to debug PDFium library")
endif()

# Platform-specific link libraries (resolved at build time)
target_link_libraries(pdfium INTERFACE
    $<$<CONFIG:Debug>:${PDFIUM_LIB_DEBUG}>
    $<$<NOT:$<CONFIG:Debug>>:${PDFIUM_LIB_RELEASE}>
)

# ------------------------------------------------------------
# PDFium External Project (build from source)
# ------------------------------------------------------------
if(BUILD_PDFIUM_FROM_SOURCE)
    message(STATUS "PDFium: Building from source (${PDFIUM_TARGET_OS}/${PDFIUM_TARGET_CPU})")
    
    # Use Homebrew Python 3.11+ (system Python 3.9 lacks StrEnum)
    find_program(Python3_EXECUTABLE NAMES python3.14 python3.13 python3.12 python3.11 python3
        PATHS /opt/homebrew/bin /usr/local/bin
        NO_DEFAULT_PATH)
    if(NOT Python3_EXECUTABLE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
    endif()
    message(STATUS "Using Python: ${Python3_EXECUTABLE}")
    
    # Construct build arguments based on options
    set(PDFIUM_BUILD_ARGS "--target-os" ${PDFIUM_TARGET_OS} "--target-cpu" ${PDFIUM_TARGET_CPU})
    if(NOT PDFIUM_ENABLE_XFA)
        list(APPEND PDFIUM_BUILD_ARGS "--no-xfa")
    endif()
    if(PDFIUM_ENABLE_V8)
        list(APPEND PDFIUM_BUILD_ARGS "--enable-v8")
    endif()
    if(PDFIUM_COMPONENT_BUILD)
        list(APPEND PDFIUM_BUILD_ARGS "--component")
    endif()

    # Custom target to bootstrap environment (run once)
    add_custom_target(pdfium_bootstrap
        COMMAND ${Python3_EXECUTABLE} ${PDFIUM_BOOTSTRAP_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Bootstrapping PDFium build environment (depot_tools, source sync)"
        VERBATIM
    )
    
    # Debug / Release PDFium library builds. They extract to library names with
    # a '_debug' suffix (libpdfium_debug.a) vs the plain release names, so both
    # variants can coexist in third_party/pdfium/lib.
    add_custom_target(pdfium_build_debug
        COMMAND ${Python3_EXECUTABLE} ${PDFIUM_BUILD_SCRIPT} ${PDFIUM_BUILD_ARGS} "--debug"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building debug PDFium for ${PDFIUM_TARGET_OS}/${PDFIUM_TARGET_CPU}"
        DEPENDS pdfium_bootstrap
        VERBATIM
    )

    add_custom_target(pdfium_build_release
        COMMAND ${Python3_EXECUTABLE} ${PDFIUM_BUILD_SCRIPT} ${PDFIUM_BUILD_ARGS} "--release"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building release PDFium for ${PDFIUM_TARGET_OS}/${PDFIUM_TARGET_CPU}"
        DEPENDS pdfium_bootstrap
        VERBATIM
    )

    # Meta target: build both variants in one go.
    add_custom_target(pdfium_build
        DEPENDS pdfium_build_debug pdfium_build_release
        COMMENT "Building both debug and release PDFium"
    )
    
    # The pdfium INTERFACE library depends on pdfium_build being complete
    # This ensures headers/libs exist before anything links against pdfium
    add_dependencies(pdfium pdfium_build)
    
else()
    message(STATUS "PDFium: Using pre-built binaries from ${PDFIUM_SOURCE_DIR}")
    # If using pre-built, warn if not found but don't fail configure
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/pdfium/include/fpdfview.h")
        message(WARNING "Pre-built PDFium headers not found at third_party/pdfium/include/. Build may fail.")
    endif()
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
add_subdirectory(pdfium_extensions)
add_subdirectory(pdfium_wrapper)
add_subdirectory(qt_app)

if(PDFIUM_ENABLE_PYTHON)
    add_subdirectory(pdfium_python)
endif()

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