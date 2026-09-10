# ------------------------------------------------------------
# PDFium Python bindings (pybind11)
# ------------------------------------------------------------
# Prefer an installed pybind11 (set pybind11_DIR). If absent, fall back to
# FetchContent so the default `cmake .` flow still works online.
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
    message(STATUS "PDFium Python bindings: pybind11 not found, fetching via FetchContent")
    include(FetchContent)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v3.1.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(pybind11)
endif()

if(NOT pybind11_FOUND)
    message(STATUS "PDFium Python bindings disabled: pybind11 not found")
    return()
endif()

message(STATUS "PDFium Python bindings: pybind11 ${pybind11_VERSION}")

pybind11_add_module(pdfium_python bindings.cpp)

target_include_directories(pdfium_python PRIVATE
    ${CMAKE_SOURCE_DIR}/pdfium_wrapper/include
)

target_link_libraries(pdfium_python PRIVATE pdfium_wrapper)

# Importable module name (`import pdfium`); the target itself is named
# pdfium_python to avoid clashing with the `pdfium` INTERFACE library target.
set_target_properties(pdfium_python PROPERTIES OUTPUT_NAME pdfium)