# AGENTS.md - PDFium Qt6 PDF Reader

## Project Structure
- `pdfium_extensions/`: Custom PDFium extensions (Compression, Element Extraction). Built into PDFium.
- `pdfium_wrapper/`: **COM-style C++ interface DLL** (vtable-only abstract interfaces + ref counting). Decouples Qt app from PDFium C++ ABI.
- `qt_app/`: Qt6 Application. Loads wrapper via `QLibrary::resolve()` (runtime binding).
- `third_party/pdfium/`: PDFium SDK binaries/headers. **Not in repo.**

## Build Commands
### Standard Build (Pre-built PDFium)
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Source Build (Required for Extensions)
```bash
mkdir build && cd build
cmake -DBUILD_PDFIUM_FROM_SOURCE=ON ..
cmake --build . --target pdfium_build
```

### PDFium Utility Targets
- `pdfium_bootstrap`: Install `depot_tools` and sync source.
- `pdfium_clean`: Remove PDFium build artifacts.
- `pdfium_list_configs`: Show supported OS/CPU configs.

## Architecture & Implementation
- **COM-style Interfaces**: `pdfium_wrapper.h` defines `IPdfUnknown` (with `AddRef()`/`Release()`), `IPdfDocument`, `IPdfPage`, `IPdfElement` as pure abstract (vtable-only) classes. Lifetime is managed via ref counting.
- **Runtime Loading**: `PdfDocument` resolves only a small set of C factory symbols (`PDF_CreateDocument`, `PDF_InitLibrary`, `PDF_DestroyLibrary`, `PDF_FreeString`, `PDF_FreeElementData`) at runtime via `QLibrary::resolve`; all other calls go through the interfaces. No link-time dependency on wrapper headers.
- **Factory Entry Point**: `PDF_CreateDocument(path, password, &err)` returns a new `IPdfDocument*` (caller owns a ref, must `Release()`).
- **Page/Elem Lifetime**: `IPdfDocument::GetPage()`, `IPdfPage::GetPageElement()`, and `FindElementsByType()` return new refs the caller must `Release()`. Never double-close cached `FPDF_PAGE`s (they belong to the doc impl).
- **Async Rendering**: `PdfDocument::requestRender()` uses `QtConcurrent`. Results via `renderFinished`.
- **Color Conversion**: PDFium outputs **BGRA** $\rightarrow$ `PdfDocument` converts to **ARGB**.
- **Caching**: `PdfViewerWidget` caches last 10 pages (key: page+size+rotation).
- **Text Selection**: `PdfDocument` builds a per-page `CharInfo` map (reading order = top-down line clustering by vertical overlap, then left-right) via wrapper `GetCharCount/GetCharUnicode/GetCharBox`. `findNearestCharIndex` resolves clicks (y-up page space). `PdfViewerWidget` stores `m_cursorIndex`/`m_selectionStart/End`; linear char highlight + 1px caret drawn via `mapRectFromPage`; no-text pages fall back to a box overlay. Double-click word, triple-click paragraph (`wordRange`/`paragraphRange`), `Ctrl+A`/`Ctrl+C`.
- **Cross-Compile**: Use `PDFIUM_TARGET_OS` and `PDFIUM_TARGET_CPU` (e.g., `win`, `mac`, `linux` | `x64`, `arm64`, `arm`).
- **Runtime lib name**: On macOS the DLL is `libpdfium_wrapper.dylib`, on Linux `libpdfium_wrapper.so`, on Windows `pdfium_wrapper.dll` (selected via `#ifdef` in `PdfDocument::Private::loadLibrary()`).

## Common Gotchas
- **Missing Binaries**: Ensure `third_party/pdfium/` contains the required `.dll`/`.dylib`/`.so` and `.lib`/`.a` files.
- **Extension Support**: Extension APIs return/behave unreliably (fields unsupported) if PDFium was not built with `BUILD_PDFIUM_FROM_SOURCE=ON`. The wrapper guards these with `#ifdef FPDF_CreateElementIterator` / `#ifdef FPDF_OptimizeDocument`.
- **Ref counting**: Every object returned with a new ref must be matched with a `Release()`, otherwise the impl object (and its underlying PDFium handles) leak.
- **Blank Pages**: Check console for `FPDF_GetLastError()` codes.
- **MSVC**: Warnings for PDFium headers are suppressed in root `CMakeLists.txt`.

## Entry Points
- **UI Flow**: `main.cpp` $\rightarrow$ `MainWindow` $\rightarrow$ `PdfViewerWidget` $\rightarrow$ `PdfDocument` $\rightarrow$ `pdfium_wrapper` DLL $\rightarrow$ PDFium SDK.
- **Build Flow**: `bootstrap.py` $\rightarrow$ `build_pdfium.py` $\rightarrow$ `apply_patches.py`.
