# pdfium_wrapper - C API wrapper around PDFium
cmake_minimum_required(VERSION 3.16)

# ------------------------------------------------------------
# Target: pdfium_wrapper (Shared Library)
# ------------------------------------------------------------
add_library(pdfium_wrapper SHARED
    include/pdfium_wrapper.h
    src/pdfium_wrapper.cpp
)

target_include_directories(pdfium_wrapper
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # PDFium headers are brought in via the pdfium INTERFACE target
)

target_compile_definitions(pdfium_wrapper
    PRIVATE
        PDFWRAPPER_EXPORTS
        PDF_ENABLE_XFA=0
        PDF_ENABLE_V8=0
        # The pdfium_extensions static library is always built & linked here,
        # so the extension entry points (compression, element iteration) are
        # available. This enables the #ifdef PDFIUM_HAS_EXTENSIONS blocks in
        # the wrapper implementation.
        PDFIUM_HAS_EXTENSIONS
)

# Link against PDFium and PDFium Extensions
target_link_libraries(pdfium_wrapper PRIVATE pdfium pdfium_extensions)

# Platform-specific link options
if(WIN32)
    target_link_libraries(pdfium_wrapper PRIVATE
        delayimp
    )
    set_target_properties(pdfium_wrapper PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
    )
elseif(APPLE)
    target_link_libraries(pdfium_wrapper PRIVATE
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework Security"
        "-framework Foundation"
    )
    set_target_properties(pdfium_wrapper PROPERTIES
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
        MACOSX_RPATH ON
        INSTALL_NAME_DIR "@rpath"
    )
else()
    set_target_properties(pdfium_wrapper PROPERTIES
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# The pdfium INTERFACE target already depends on pdfium_build (when BUILD_PDFIUM_FROM_SOURCE=ON)
# so pdfium_wrapper automatically gets the correct build order.

# ------------------------------------------------------------
# Copy PDFium runtime library to build output
# ------------------------------------------------------------
# When linking statically, the PDFium code is already bundled into the wrapper.
# We only copy runtime DLLs if they are explicitly needed for shared build.
# For now, since we use static linking on macOS, these are removed or conditional.
# ------------------------------------------------------------

# ------------------------------------------------------------
# Install Headers
# ------------------------------------------------------------
install(TARGETS pdfium_wrapper
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
)
install(FILES include/pdfium_wrapper.h DESTINATION include)