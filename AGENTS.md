# AGENTS.md - PDFium Qt6 PDF Reader

## Project Structure (Multi-target CMake)
```
pdfium/                          # Root
├── CMakeLists.txt               # Orchestrates pdfium_wrapper + qt_app + PDFium build pipeline
├── build_scripts/               # Build automation
│   ├── bootstrap.py             # depot_tools install + PDFium source sync
│   ├── build_pdfium.py          # GN/Ninja build orchestrator (cross-platform ARM/X64)
│   └── apply_patches.py         # Apply custom PDFium extensions
├── pdfium_extensions/           # Custom PDFium extensions (built into PDFium)
│   ├── public/                  # Extension headers
│   │   ├── fpdf_ext_compression.h   # PDF compression/optimization API
│   │   └── fpdf_ext_element.h       # Element extraction API
│   └── core/                    # Extension implementations
│       ├── fpdf_ext_compression.cpp
│       └── fpdf_ext_element.cpp
├── pdfium_wrapper/              # C++ DLL (C API around PDFium)
│   ├── include/pdfium_wrapper.h # Stable C API - opaque handles, error codes + extensions
│   └── src/pdfium_wrapper.cpp   # Implementation
├── qt_app/                      # Qt6 Application
│   └── src/                     # main, mainwindow, pdfviewerwidget, pdfdocument
└── third_party/pdfium/          # PDFium SDK (built from source or user provides)
    ├── include/                 # fpdfview.h, fpdf_text.h, etc. + extension headers
    ├── bin/                     # pdfium.dll / libpdfium.dylib / libpdfium.so
    └── lib/                     # pdfium.lib / libpdfium.a
```

## Build Commands

### Using Pre-built PDFium
```bash
# Windows (MSVC) - from repo root
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
# Output: build/bin/Release/pdf_reader.exe + pdfium_wrapper.dll + pdfium.dll
```

### Building PDFium from Source (Required for Extensions)
```bash
# Windows
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DBUILD_PDFIUM_FROM_SOURCE=ON ..
cmake --build . --config Release --target pdfium_build

# Linux/macOS
mkdir build && cd build
cmake -DBUILD_PDFIUM_FROM_SOURCE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel --target pdfium_build
```

### PDFium Build Targets
```bash
cmake --build . --target pdfium_list_configs  # List supported configurations
cmake --build . --target pdfium_bootstrap     # Setup depot_tools + sync source
cmake --build . --target pdfium_clean         # Clean PDFium build dirs
```

## Key Architecture Facts

- **DLL Boundary**: `pdfium_wrapper` exposes **C API only** (opaque handles, `extern "C"`, `__stdcall`). Qt app loads via `QLibrary` at runtime - no C++ ABI coupling.
- **Async Rendering**: `PdfDocument::requestRender()` runs on `QtConcurrent` thread pool, emits `renderFinished(pageIndex, QImage, bool)`.
- **Page Caching**: `PdfViewerWidget` caches last 10 rendered pages (`m_renderCache`) keyed by page+size+rotation.
- **PDFium Loading**: `PdfDocument` resolves DLL symbols via `QLibrary::resolve()` in `Private::loadLibrary()` - no link-time dependency on wrapper headers.
- **Color Format**: PDFium outputs **BGRA**; `PdfDocument` swaps to ARGB in render callback (see `pdfdocument.cpp:110-120`).
- **PDFium Extensions**: Built into PDFium at compile time, exposed via wrapper DLL. Requires `BUILD_PDFIUM_FROM_SOURCE=ON`.

## Required External Dependency

**PDFium binaries NOT in repo**. Place in `third_party/pdfium/` (or build from source):
- `include/` - all public headers + `fpdf_ext_compression.h`, `fpdf_ext_element.h`
- `bin/pdfium.dll` - runtime (Windows) / `libpdfium.dylib` (macOS) / `libpdfium.so` (Linux)
- `lib/pdfium.lib` - import library (MSVC) / `libpdfium.a` (Unix)

## Common Gotchas

- **CMake copies DLLs** via `POST_BUILD` commands - check `build/bin/` if missing at runtime
- **PDF_InitLibrary** fails if `pdfium.dll` version mismatches headers
- **Blank pages** = PDFium render error (check console for `FPDF_GetLastError()` codes)
- **MSVC warnings** suppressed in root `CMakeLists.txt:18-20` for PDFium headers
- **PDFium source build** requires `depot_tools`, Python 3.8+, and platform toolchain (VS2022, Xcode, or GCC/Clang)
- **Extension APIs return UNSUPPORTED** if PDFium was built without extensions

## Extension APIs

### Compression (`PDF_CompressOptions`, `PDF_OptimizeDocument`, `PDF_SaveWithCompression`)
- Flags: `FLATE`, `OBJECT_STREAMS`, `IMAGES`, `FONTS`, `REMOVE_UNUSED`, `LINEARIZE`
- Quality settings for lossy image compression
- Returns compression stats (ratio, objects removed, images recompressed)

### Element Extraction (`PDF_CreateElementIterator`, `PDF_GetElementType`, etc.)
- Iterate all page elements (text, images, paths, forms, shading)
- Get detailed attributes per element type
- Extract text, image data, path data per element
- Extract form fields, annotations, bookmarks, document structure
- Extended text search with element context

## Entry Points

- App: `qt_app/src/main.cpp` → `MainWindow` → `PdfViewerWidget` → `PdfDocument` → DLL
- DLL: `pdfium_wrapper/src/pdfium_wrapper.cpp` → PDFium C API + Extensions
- PDFium Build: `build_scripts/bootstrap.py` → `build_scripts/build_pdfium.py` → `build_scripts/apply_patches.py`

## Cross-Platform ARM Support

The build pipeline supports:
- **Windows**: x64, arm64 (native and cross-compile)
- **macOS**: x64, arm64 (Apple Silicon, universal binaries possible)
- **Linux**: x64, arm64, arm (cross-compile requires toolchain)

Use CMake variables: `PDFIUM_TARGET_OS`, `PDFIUM_TARGET_CPU`