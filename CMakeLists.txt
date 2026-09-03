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
# PDFium Dependency Configuration
# ------------------------------------------------------------
# Expects the following structure in third_party/pdfium/:
#   include/     -> fpdfview.h, fpdf_edit.h, etc.
#   bin/         -> pdfium.dll
#   lib/         -> pdfium.lib (import library)
set(PDFIUM_ROOT "${CMAKE_SOURCE_DIR}/third_party/pdfium" CACHE PATH "Path to PDFium SDK")

if(NOT EXISTS "${PDFIUM_ROOT}/include/fpdfview.h")
    message(WARNING "PDFium headers not found at ${PDFIUM_ROOT}/include/. Build will fail.")
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