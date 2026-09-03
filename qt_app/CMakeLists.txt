# qt_app - Qt6 PDF Reader Application
cmake_minimum_required(VERSION 3.16)

# ------------------------------------------------------------
# Find Qt6
# ------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# ------------------------------------------------------------
# Target: pdf_reader (Executable)
# ------------------------------------------------------------
add_executable(pdf_reader
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/pdfviewerwidget.cpp
    src/pdfviewerwidget.h
    src/pdfdocument.cpp
    src/pdfdocument.h
)

# Qt6 auto-moc, auto-uic, auto-rcc
set_target_properties(pdf_reader PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries(pdf_reader PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    pdfium_wrapper
)

target_include_directories(pdf_reader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/pdfium_wrapper/include
)

# ------------------------------------------------------------
# Copy DLLs to executable directory for debugging
# ------------------------------------------------------------
# Cross-platform copy of runtime libraries
if(WIN32)
    add_custom_command(TARGET pdf_reader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium_wrapper>"
            "$<TARGET_FILE_DIR:pdf_reader>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/pdfium/bin/pdfium.dll"
            "$<TARGET_FILE_DIR:pdf_reader>"
        COMMENT "Copying runtime DLLs to executable directory"
    )
elseif(APPLE)
    add_custom_command(TARGET pdf_reader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium_wrapper>"
            "$<TARGET_FILE_DIR:pdf_reader>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/pdfium/bin/libpdfium.dylib"
            "$<TARGET_FILE_DIR:pdf_reader>"
        COMMENT "Copying runtime libraries to executable directory"
    )
else()
    add_custom_command(TARGET pdf_reader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pdfium_wrapper>"
            "$<TARGET_FILE_DIR:pdf_reader>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/pdfium/bin/libpdfium.so"
            "$<TARGET_FILE_DIR:pdf_reader>"
        COMMENT "Copying runtime libraries to executable directory"
    )
endif()

# ------------------------------------------------------------
# Windows: Set subsystem to windows (no console)
# ------------------------------------------------------------
if(WIN32)
    set_target_properties(pdf_reader PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

# ------------------------------------------------------------
# Install
# ------------------------------------------------------------
install(TARGETS pdf_reader
    RUNTIME DESTINATION bin
)