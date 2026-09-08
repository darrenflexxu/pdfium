# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run Commands

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
- Bootstrap environment: `cmake --build . --target pdfium_bootstrap`
- Clean PDFium artifacts: `cmake --build . --target pdfium_clean`
- List configs: `cmake --build . --target pdfium_list_configs`

### Running the App
- Windows: `build/bin/Release/pdf_reader.exe`
- Linux/macOS: `build/bin/pdf_reader`

## High-Level Architecture

### System Flow
`Qt6 Frontend (qt_app)` $\rightarrow$ `COM-style Wrapper DLL (pdfium_wrapper)` $\rightarrow$ `PDFium SDK (third_party/pdfium)`

### Key Components
- **`pdfium_wrapper`**: A C++ DLL exposing stable, vtable-only abstract interfaces (e.g., `IPdfDocument`, `IPdfPage`) with reference counting (`AddRef`/`Release`). This decouples the Qt application from the PDFium C++ ABI.
- **`qt_app`**: A Qt6 application that loads the wrapper at runtime. `PdfDocument` uses `QLibrary::resolve()` to bind only a few factory symbols (e.g., `PDF_CreateDocument`), while all subsequent operations occur through the interfaces.
- **`pdfium_extensions`**: Custom C++ implementations built directly into the PDFium SDK for advanced features like compression and element extraction.

### Critical Implementation Details
- **Lifetime Management**: Any object returned from the wrapper must be matched with a `Release()` call to avoid memory leaks of underlying PDFium handles.
- **Coordinate Transformation**: All PDF coordinates must be converted to view coordinates using the PDFium native `FPDF_PageToDevice` and `FPDF_DeviceToPage` APIs via the wrapper to account for rotation and zoom.
- **Async Rendering**: Page rendering is handled via `QtConcurrent` in `PdfDocument` to keep the UI responsive.
- **Color Space**: PDFium BGRA output is converted to ARGB by `PdfDocument` for Qt compatibility.
