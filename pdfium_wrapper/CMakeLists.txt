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
        # PDFium headers are brought in via the pdfium INTERFACE target
)

target_compile_definitions(pdfium_wrapper
    PRIVATE
        PDFWRAPPER_EXPORTS
        PDF_ENABLE_XFA=0
        PDF_ENABLE_V8=0
)

# Link against PDFium INTERFACE library
# This pulls in include directories and link libraries from the pdfium target
target_link_libraries(pdfium_wrapper PRIVATE pdfium)

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
# The runtime DLLs are copied by the pdfium_build target's post-processing
# But we also ensure they're copied to the wrapper output directory
if(WIN32)
    add_custom_command(TARGET pdfium_wrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/pdfium/bin/pdfium.dll"
            $<TARGET_FILE_DIR:pdfium_wrapper>
        COMMENT "Copying pdfium.dll to wrapper output directory"
    )
elseif(APPLE)
    add_custom_command(TARGET pdfium_wrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/pdfium/bin/libpdfium.dylib"
            $<TARGET_FILE_DIR:pdfium_wrapper>
        COMMENT "Copying libpdfium.dylib to wrapper output directory"
    )
else()
    add_custom_command(TARGET pdfium_wrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/pdfium/bin/libpdfium.so"
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