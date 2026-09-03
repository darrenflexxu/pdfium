# PDF Reader - Qt6 + PDFium

A PDF viewer application built with Qt6 and Google's PDFium rendering engine (via a C++ wrapper DLL), with extended PDFium capabilities for compression and element extraction.

## Architecture

```
+------------------+     +------------------+     +------------------+
|   Qt6 Frontend   | <---> |  pdfium_wrapper  | <---> |     PDFium       |
|  (pdf_reader)    |  C-API|     (DLL)        |       |   (pdfium.dll)   |
+------------------+     +------------------+     +------------------+
```

- **pdfium_wrapper**: C++ DLL exposing a stable C API around PDFium
- **qt_app (pdf_reader)**: Qt6 application using the wrapper DLL
- **PDFium**: Google's PDF rendering engine (pre-built or built from source)

## Prerequisites

1. **Qt 6.x** (6.2 or later recommended)
   - Install via Qt Online Installer or package manager
   - Components: `qtbase`, `qttools` (for `lrelease`)

2. **CMake 3.16+**

3. **C++17 compatible compiler**
   - MSVC 2019/2022 (Windows)
   - GCC 9+ / Clang 10+ (Linux)

4. **Python 3.8+** (required for building PDFium from source)

5. **Git** (required for fetching PDFium source)

## Building

### Option 1: Using Pre-built PDFium Binaries (Quick Start)

Place PDFium binaries in `third_party/pdfium/`:
```
third_party/pdfium/
├── include/          # PDFium headers (fpdfview.h, etc.)
├── bin/              # pdfium.dll / libpdfium.dylib / libpdfium.so
└── lib/              # pdfium.lib / libpdfium.a
```

Then build normally:
```cmd
# Windows (MSVC)
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release

# Linux/macOS
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel
```

### Option 2: Build PDFium from Source (Required for Extensions)

This project includes automated scripts to build PDFium from source with custom extensions for **compression** and **element extraction**.

**Requirements for source build:**
- Windows: Visual Studio 2022 with "Desktop development with C++" workload
- macOS: Xcode Command Line Tools (`xcode-select --install`)
- Linux: build-essential, clang, pkg-config, ninja-build

**Build with automated pipeline:**
```cmd
# Windows
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DBUILD_PDFIUM_FROM_SOURCE=ON ..
cmake --build . --config Release --target pdfium_build

# Linux/macOS
mkdir build && cd build
cmake -DBUILD_PDFIUM_FROM_SOURCE=ON -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel --target pdfium_build
```

The build pipeline will:
1. Auto-install `depot_tools` (Google's build tools)
2. Fetch PDFium source via `gclient sync`
3. Apply custom extensions (compression, element extraction)
4. Build with GN/Ninja for target platform
5. Extract artifacts to `third_party/pdfium/`

**CMake Options for PDFium Build:**

| Option | Description | Default |
|--------|-------------|---------|
| `BUILD_PDFIUM_FROM_SOURCE` | Build PDFium from source | OFF |
| `PDFIUM_BUILD_DEBUG` | Build PDFium in debug mode | OFF |
| `PDFIUM_ENABLE_XFA` | Enable XFA support | ON |
| `PDFIUM_ENABLE_V8` | Enable V8 JavaScript engine | OFF |
| `PDFIUM_COMPONENT_BUILD` | Build as shared library components | OFF |
| `PDFIUM_TARGET_OS` | Target OS (win, mac, linux) | Host OS |
| `PDFIUM_TARGET_CPU` | Target CPU (x64, arm64, arm, x86) | Host CPU |

**Cross-compilation Examples:**
```bash
# Build Windows ARM64 from x64 Linux
cmake -DBUILD_PDFIUM_FROM_SOURCE=ON -DPDFIUM_TARGET_OS=win -DPDFIUM_TARGET_CPU=arm64 ..

# Build macOS ARM64 from x64
cmake -DBUILD_PDFIUM_FROM_SOURCE=ON -DPDFIUM_TARGET_OS=mac -DPDFIUM_TARGET_CPU=arm64 ..

# Build Linux ARM64 from x64
cmake -DBUILD_PDFIUM_FROM_SOURCE=ON -DPDFIUM_TARGET_OS=linux -DPDFIUM_TARGET_CPU=arm64 ..
```

### Development Targets

```bash
# List supported PDFium build configurations
cmake --build . --target pdfium_list_configs

# Clean PDFium build directories
cmake --build . --target pdfium_clean

# Bootstrap environment only (install depot_tools, sync source)
cmake --build . --target pdfium_bootstrap
```

## Project Structure

```
pdf-reader/
├── CMakeLists.txt                 # Root CMake (with PDFium build pipeline)
├── build_scripts/
│   ├── bootstrap.py              # Environment setup (depot_tools, source sync)
│   ├── build_pdfium.py           # GN/Ninja build orchestrator
│   └── apply_patches.py          # Apply custom PDFium extensions
├── pdfium_extensions/
│   ├── public/                   # Custom PDFium API headers
│   │   ├── fpdf_ext_compression.h   # PDF compression/optimization API
│   │   └── fpdf_ext_element.h       # Element extraction API
│   └── core/                     # Custom PDFium implementations
│       ├── fpdf_ext_compression.cpp
│       └── fpdf_ext_element.cpp
├── third_party/
│   └── pdfium/                   # PDFium SDK (pre-built or built from source)
│       ├── include/              # PDFium headers
│       ├── bin/                  # Runtime libraries
│       └── lib/                  # Import libraries
├── pdfium_wrapper/               # Wrapper DLL (C API)
│   ├── CMakeLists.txt
│   ├── include/pdfium_wrapper.h  # Public C API (extended)
│   └── src/pdfium_wrapper.cpp    # Implementation
└── qt_app/                       # Qt6 Application
    ├── CMakeLists.txt
    └── src/
        ├── main.cpp
        ├── mainwindow.cpp/h
        ├── pdfviewerwidget.cpp/h
        └── pdfdocument.cpp/h
```

## PDFium Extensions

This project extends PDFium with two major capabilities:

### 1. PDF Compression & Optimization (`fpdf_ext_compression.h`)

- **Flate compression** of streams (lossless)
- **Object stream consolidation** for smaller files
- **Image recompression** with quality control
- **Font subsetting** to remove unused glyphs
- **Unused object removal** 
- **Linearization** for fast web view
- **Selective content removal** (annotations, forms, bookmarks, metadata)

### 2. Element Extraction (`fpdf_ext_element.h`)

- **Page element iterator** for traversing all elements
- **Text elements** with font, color, positioning attributes
- **Image elements** with dimensions, color space, filter info
- **Path elements** with fill/stroke colors, line styles
- **Form field extraction** (names, values, types, bounds)
- **Annotation extraction** (types, contents, bounds)
- **Bookmark/outline extraction**
- **Document structure analysis**
- **Extended text search** with element context

## Running

After building, the executable and required DLLs will be in `build/bin/` (or `build/bin/Release/` on Windows):

```cmd
# Windows
cd build/bin/Release
pdf_reader.exe

# Linux/macOS
cd build/bin
./pdf_reader
```

## Features

### Core Viewer
- [x] Open PDF files
- [x] Page navigation (First/Prev/Next/Last)
- [x] Zoom (In/Out/Fit Page/Fit Width)
- [x] Rotation (90° increments)
- [x] Text selection and copy
- [x] Keyboard shortcuts
- [x] Full-screen mode
- [x] Page caching for smooth scrolling

### New Extended Features
- [x] **PDF Optimization/Compression** (Tools → Optimize PDF)
- [x] **Element Inspector** (Tools → Element Inspector) - view page elements
- [x] **Page Text Extraction** (Tools → Extract Page Text)
- [x] **Image Extraction** (Tools → Extract Images)
- [x] **Document Structure Viewer** (Tools → Document Structure)
- [ ] Text search (basic search exists)
- [ ] Bookmarks/Outline navigation
- [ ] Annotations editing
- [ ] Printing
- [ ] Thumbnail sidebar

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Ctrl+O` | Open file |
| `Ctrl+Shift+S` | Save Optimized |
| `Ctrl+P` | Print |
| `Ctrl+Q` | Quit |
| `Left/Right` | Previous/Next page |
| `Home/End` | First/Last page |
| `Space` | Next page |
| `Ctrl++` / `Ctrl+-` | Zoom in/out |
| `Ctrl+0` | Fit page |
| `Ctrl+9` | Fit width |
| `Ctrl+R` | Rotate clockwise |
| `Ctrl+Shift+R` | Rotate counterclockwise |
| `F11` | Full screen |
| `Ctrl+C` | Copy page text |
| `Ctrl+I` | Element Inspector |
| `Ctrl+Shift+T` | Extract Page Text |
| `Ctrl+Shift+I` | Extract Images |
| `Ctrl+D` | Document Structure |
| Middle mouse / Left drag | Pan |

## Troubleshooting

### "PDFium headers not found"
Ensure `third_party/pdfium/include/fpdfview.h` exists. Set `PDFIUM_ROOT` CMake variable if located elsewhere.

### "Failed to load pdfium_wrapper.dll"
- Ensure `pdfium.dll` is in the same directory as the executable (copied automatically by CMake post-build)
- Check that both DLLs are same architecture (x64)

### "PDF_InitLibrary failed"
- PDFium version mismatch between headers and DLL
- Missing Visual C++ Redistributable (Windows)

### Blank/black pages
- PDFium may fail to render certain PDFs (encrypted, corrupted, unsupported features)
- Check console output for PDFium error codes

### PDFium Build Failures
- **depot_tools not found**: Run `cmake --build . --target pdfium_bootstrap` first
- **GN gen failed**: Check `out/<config>/args.gn` for correct arguments
- **Ninja build failed**: Ensure sufficient RAM (8GB+ recommended), try `-j4` to limit parallelism
- **Cross-compile failures**: Requires proper toolchain setup (sysroot, clang)

## License

- PDFium: BSD-3-Clause (Google)
- PDFium Extensions: MIT License (this project)
- This wrapper/application: MIT License

## Contributing

1. Fork the repository
2. Create feature branch
3. Make changes
4. Submit pull request

## References

- [PDFium Source](https://pdfium.googlesource.com/pdfium/)
- [PDFium API Documentation](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/)
- [Chromium depot_tools](https://chromium.googlesource.com/chromium/tools/depot_tools.git)
- [Qt6 Documentation](https://doc.qt.io/qt-6/)