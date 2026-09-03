# pdfium_wrapper - C API wrapper around PDFium
cmake_minimum_required(VERSION 3.16)

# ------------------------------------------------------------
# Target: pdfium_wrapper (Shared Library)
# ------------------------------------------------------------
add_library(pdfium_wrapper SHARED
    src/pdfium_wrapper.cpp
)

target_include_directories(pdfium_wrapper
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PDFIUM_ROOT}/include
)

target_compile_definitions(pdfium_wrapper
    PRIVATE
        PDFWRAPPER_EXPORTS
        PDF_ENABLE_XFA=0
        PDF_ENABLE_V8=0
)

# Link against PDFium
target_link_libraries(pdfium_wrapper PRIVATE
    ${PDFIUM_ROOT}/bin/pdfium.lib
)

# Windows-specific: Export symbols
if(WIN32)
    target_link_libraries(pdfium_wrapper PRIVATE
        delayimp
    )
    set_target_properties(pdfium_wrapper PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
    )
endif()

# ------------------------------------------------------------
# Copy PDFium DLL to build output for runtime
# ------------------------------------------------------------
add_custom_command(TARGET pdfium_wrapper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PDFIUM_ROOT}/bin/pdfium.dll"
        $<TARGET_FILE_DIR:pdfium_wrapper>
    COMMENT "Copying pdfium.dll to wrapper output directory"
)

# ------------------------------------------------------------
# Install Headers
# ------------------------------------------------------------
install(TARGETS pdfium_wrapper
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
)
install(FILES include/pdfium_wrapper.h DESTINATION include)