# qt_app - Qt6 PDF Reader Application
cmake_minimum_required(VERSION 3.16)

# ------------------------------------------------------------
# Find Qt6
# ------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Concurrent OpenGL OpenGLWidgets)

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
    src/skiapdfviewerwidget.cpp
    src/skiapdfviewerwidget.h
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
    Qt6::PrintSupport
    Qt6::Concurrent
    pdfium_wrapper
)

# ------------------------------------------------------------------
# Optional Skia GPU path (see Section 8 of PLAN.md). Configure with
#   cmake -DPDFIUM_ENABLE_SKIA=ON -DSKIA_DIR=<skia root>
# The Skia root must contain the public headers (include/... plus GPU
# headers under include/gpu/ganesh) and a static library at lib/libskia.a,
# which matches third_party/skia_lib shipped with this repository.
# ------------------------------------------------------------------
if(PDFIUM_ENABLE_SKIA)
    set(_skia_include "${SKIA_DIR}/include")
    if(NOT EXISTS "${_skia_include}/core/SkCanvas.h")
        message(FATAL_ERROR "PDFIUM_ENABLE_SKIA=ON requires Skia headers at ${_skia_include}")
    endif()

    # -I<SKIA_DIR> lets the sources use Skia's canonical "include/..." paths.
    target_include_directories(pdf_reader PRIVATE
        ${SKIA_DIR}
        ${SKIA_DIR}/include
        ${SKIA_DIR}/include/config
    )
    target_compile_definitions(pdf_reader PRIVATE SKIA_AVAILABLE=1)

    # Qt's OpenGL modules (QOpenGLWidget / QOpenGLContext) back the Skia
    # drawing surface and must be linked whenever the GPU path is rebuilt.
    target_link_libraries(pdf_reader PRIVATE Qt6::OpenGL Qt6::OpenGLWidgets)

    if(APPLE)
        target_link_libraries(pdf_reader PRIVATE
            ${SKIA_DIR}/lib/libskia.a
            "-framework OpenGL"
            "-framework QuartzCore"
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework CoreText"
            "-framework ApplicationServices"
            "-framework ImageIO"
            "-framework Foundation"
        )
    elseif(ANDROID)
        target_link_libraries(pdf_reader PRIVATE
            EGL
            GLESv2
            ${SKIA_DIR}/lib/libskia.a
        )
    elseif(IOS)
        target_link_libraries(pdf_reader PRIVATE
            ${SKIA_DIR}/lib/libskia.a
            "-framework Metal"
            "-framework QuartzCore"
        )
    else()
        target_link_libraries(pdf_reader PRIVATE ${SKIA_DIR}/lib/libskia.a)
    endif()

    unset(_skia_include)
endif()

target_include_directories(pdf_reader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/pdfium_wrapper/include
)

# ------------------------------------------------------------
# Skia GL harness: verifies the PDFium -> Skia -> GPU pipeline without a
# windowing session (offscreen GL context, readback compare vs CPU raster).
# ------------------------------------------------------------
if(PDFIUM_ENABLE_SKIA AND NOT IOS AND NOT ANDROID)
    add_executable(skia_gl_harness
        src/skia_gl_harness_main.cpp
        src/pdfdocument.cpp
        src/pdfdocument.h
    )
    set_target_properties(skia_gl_harness PROPERTIES
        AUTOMOC ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    target_include_directories(skia_gl_harness PRIVATE
        ${SKIA_DIR}
        ${SKIA_DIR}/include
        ${SKIA_DIR}/include/config
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/pdfium_wrapper/include
    )
    target_compile_definitions(skia_gl_harness PRIVATE SKIA_AVAILABLE=1)
    target_link_libraries(skia_gl_harness PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        pdfium_wrapper
        ${SKIA_DIR}/lib/libskia.a
    )
    if(APPLE)
        target_link_libraries(skia_gl_harness PRIVATE
            "-framework OpenGL"
            "-framework QuartzCore"
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework CoreText"
            "-framework ApplicationServices"
            "-framework ImageIO"
            "-framework Foundation"
        )
    endif()
endif()

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