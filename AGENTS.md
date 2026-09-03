# AGENTS.md - PDFium Qt6 PDF Reader

## Project Structure (Multi-target CMake)
```
pdfium/                          # Root
├── CMakeLists.txt               # Orchestrates pdfium_wrapper + qt_app
├── pdfium_wrapper/              # C++ DLL (C API around PDFium)
│   ├── include/pdfium_wrapper.h # Stable C API - opaque handles, error codes
│   └── src/pdfium_wrapper.cpp   # Implementation
├── qt_app/                      # Qt6 Application
│   └── src/                     # main, mainwindow, pdfviewerwidget, pdfdocument
└── third_party/pdfium/          # PDFium SDK (NOT IN REPO - user provides)
    ├── include/                 # fpdfview.h, fpdf_text.h, etc.
    ├── bin/pdfium.dll
    └── lib/pdfium.lib
```

## Build Commands
```bash
# Windows (MSVC) - from repo root
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release

# Output: build/bin/Release/pdf_reader.exe + pdfium_wrapper.dll + pdfium.dll
```

## Key Architecture Facts
- **DLL Boundary**: `pdfium_wrapper` exposes **C API only** (opaque handles, `extern "C"`, `__stdcall`). Qt app loads via `QLibrary` at runtime - no C++ ABI coupling.
- **Async Rendering**: `PdfDocument::requestRender()` runs on `QtConcurrent` thread pool, emits `renderFinished(pageIndex, QImage, bool)`.
- **Page Caching**: `PdfViewerWidget` caches last 10 rendered pages (`m_renderCache`) keyed by page+size+rotation.
- **PDFium Loading**: `PdfDocument` resolves DLL symbols via `QLibrary::resolve()` in `Private::loadLibrary()` - no link-time dependency on wrapper headers.
- **Color Format**: PDFium outputs **BGRA**; `PdfDocument` swaps to ARGB in render callback (see `pdfdocument.cpp:110-120`).

## Required External Dependency
**PDFium binaries NOT in repo**. Place in `third_party/pdfium/`:
- `include/` - all public headers (`fpdfview.h`, `fpdf_text.h`, `fpdf_edit.h`, `fpdf_progressive.h`)
- `bin/pdfium.dll` - runtime
- `lib/pdfium.lib` - import library (MSVC)

## Common Gotchas
- **CMake copies DLLs** via `POST_BUILD` commands - check `build/bin/` if missing at runtime
- **PDF_InitLibrary** fails if `pdfium.dll` version mismatches headers
- **Blank pages** = PDFium render error (check console for `FPDF_GetLastError()` codes)
- **MSVC warnings** suppressed in root `CMakeLists.txt:18-20` for PDFium headers

## Entry Points
- App: `qt_app/src/main.cpp` → `MainWindow` → `PdfViewerWidget` → `PdfDocument` → DLL
- DLL: `pdfium_wrapper/src/pdfium_wrapper.cpp` → PDFium C API