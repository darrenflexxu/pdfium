cmake_minimum_required(VERSION 3.16)
project(pdfium_extensions LANGUAGES CXX)

# ------------------------------------------------------------
# pdfium_extensions - Static Library
# This library implements the extension core logic using PDFium's
# internal C++ APIs. It is intended to be linked into pdfium_wrapper.
# ------------------------------------------------------------

add_library(pdfium_extensions STATIC
    core/fpdf_ext_compression.cpp
    core/fpdf_ext_element.cpp
    public/fpdf_ext_compression.h
    public/fpdf_ext_element.h
)

# PDFium internal headers now use C++20 concepts and requires clauses
set_target_properties(pdfium_extensions PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# PDFium internal include paths (mapped from current source tree layout)
target_include_directories(pdfium_extensions
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/pdfium_extensions/public>
    PRIVATE
        # Base path for all internal core/fpdfapi/... includes
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/pdfium/pdfium>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/pdfium/pdfium/core/fxcrt>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/pdfium/pdfium/core/fxcrt/css>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/pdfium/pdfium/third_party/zlib>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/pdfium/pdfium/third_party/base>
        # FreeType headers
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/pdfium/pdfium/third_party/freetype/src/include>
        # SDK headers
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/pdfium/include>
)

# Link against the PDFium core implementation
target_link_libraries(pdfium_extensions PRIVATE pdfium)


