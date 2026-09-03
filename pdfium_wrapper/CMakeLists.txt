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

# Link against PDFium - cross-platform
if(WIN32)
    target_link_libraries(pdfium_wrapper PRIVATE
        ${PDFIUM_ROOT}/lib/pdfium.lib
    )
    # Windows-specific: Export symbols
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
        ${PDFIUM_ROOT}/lib/libpdfium.dylib
    )
    set_target_properties(pdfium_wrapper PROPERTIES
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
        MACOSX_RPATH ON
        INSTALL_NAME_DIR "@rpath"
    )
else()
    target_link_libraries(pdfium_wrapper PRIVATE
        ${PDFIUM_ROOT}/lib/libpdfium.so
    )
    set_target_properties(pdfium_wrapper PROPERTIES
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Depend on PDFium build if building from source
if(BUILD_PDFIUM_FROM_SOURCE)
    add_dependencies(pdfium_wrapper pdfium_build)
endif()

# ------------------------------------------------------------
# Copy PDFium runtime library to build output
# ------------------------------------------------------------
if(WIN32)
    add_custom_command(TARGET pdfium_wrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PDFIUM_ROOT}/bin/pdfium.dll"
            $<TARGET_FILE_DIR:pdfium_wrapper>
        COMMENT "Copying pdfium.dll to wrapper output directory"
    )
elseif(APPLE)
    add_custom_command(TARGET pdfium_wrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PDFIUM_ROOT}/bin/libpdfium.dylib"
            $<TARGET_FILE_DIR:pdfium_wrapper>
        COMMENT "Copying libpdfium.dylib to wrapper output directory"
    )
else()
    add_custom_command(TARGET pdfium_wrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PDFIUM_ROOT}/bin/libpdfium.so"
            $<TARGET_FILE_DIR:pdfium_wrapper>
        COMMENT "Copying libpdfium.so to wrapper output directory"
    )
endif()

# ------------------------------------------------------------
# Install Headers
# ------------------------------------------------------------
install(TARGETS pdfium_wrapper
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
)
install(FILES include/pdfium_wrapper.h DESTINATION include)