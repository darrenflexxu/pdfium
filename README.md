# PDF Reader - Qt6 + PDFium

A PDF viewer application built with Qt6 and Google's PDFium rendering engine (via a C++ wrapper DLL).

## Architecture

```
+------------------+     +------------------+     +------------------+
|   Qt6 Frontend   | <---> |  pdfium_wrapper  | <---> |     PDFium       |
|  (pdf_reader)    |  C-API|     (DLL)        |       |   (pdfium.dll)   |
+------------------+     +------------------+     +------------------+
```

- **pdfium_wrapper**: C++ DLL exposing a stable C API around PDFium
- **qt_app (pdf_reader)**: Qt6 application using the wrapper DLL
- **PDFium**: Google's PDF rendering engine (provided as pre-built binaries)

## Prerequisites

1. **Qt 6.x** (6.2 or later recommended)
   - Install via Qt Online Installer or package manager
   - Components: `qtbase`, `qttools` (for `lrelease`)

2. **CMake 3.16+**

3. **C++17 compatible compiler**
   - MSVC 2019/2022 (Windows)
   - GCC 9+ / Clang 10+ (Linux)

4. **PDFium Pre-built Binaries**
   - Download from [PDFium releases](https://pdfium.googlesource.com/pdfium/) or build from source
   - Required files:
     - `third_party/pdfium/include/` - Headers (`fpdfview.h`, etc.)
     - `third_party/pdfium/bin/pdfium.dll` - Runtime DLL
     - `third_party/pdfium/lib/pdfium.lib` - Import library (Windows)

## Building

### Windows (MSVC)

```cmd
# Setup environment (run in Developer Command Prompt for VS)
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### Linux/macOS

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel
```

### CMake Options

| Option | Description | Default |
|--------|-------------|---------|
| `PDFIUM_ROOT` | Path to PDFium SDK | `../third_party/pdfium` |
| `CMAKE_BUILD_TYPE` | Build type (Release/Debug) | Release |

## Project Structure

```
pdf-reader/
├── CMakeLists.txt              # Root CMake
├── third_party/
│   └── pdfium/                 # PDFium SDK (user provided)
│       ├── include/            # PDFium headers
│       ├── bin/                # pdfium.dll
│       └── lib/                # pdfium.lib
├── pdfium_wrapper/             # Wrapper DLL
│   ├── CMakeLists.txt
│   ├── include/pdfium_wrapper.h # Public C API
│   └── src/pdfium_wrapper.cpp   # Implementation
└── qt_app/                     # Qt6 Application
    ├── CMakeLists.txt
    └── src/
        ├── main.cpp
        ├── mainwindow.cpp/h
        ├── pdfviewerwidget.cpp/h
        └── pdfdocument.cpp/h
```

## PDFium Setup

Place PDFium binaries in `third_party/pdfium/`:

```
third_party/pdfium/
├── include/
│   ├── fpdfview.h
│   ├── fpdf_edit.h
│   ├── fpdf_text.h
│   └── ... (all public headers)
├── bin/
│   └── pdfium.dll
└── lib/
    └── pdfium.lib
```

**Note**: PDFium doesn't provide official pre-built packages. You can:
1. Build from source (complex, requires GN/Ninja)
2. Use third-party builds (e.g., `pdfium-binaries` on GitHub)
3. Extract from Chrome/Chromium installation

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

- [x] Open PDF files
- [x] Page navigation (First/Prev/Next/Last)
- [x] Zoom (In/Out/Fit Page/Fit Width)
- [x] Rotation (90° increments)
- [x] Text selection and copy
- [x] Keyboard shortcuts
- [x] Full-screen mode
- [x] Page caching for smooth scrolling
- [ ] Text search
- [ ] Bookmarks/Outline
- [ ] Annotations
- [ ] Printing
- [ ] Thumbnail sidebar

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Ctrl+O` | Open file |
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
| `Ctrl+C` | Copy page text (when text selection enabled) |
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

## License

- PDFium: BSD-3-Clause (Google)
- This wrapper/application: MIT License

## Contributing

1. Fork the repository
2. Create feature branch
3. Make changes
4. Submit pull request

## References

- [PDFium Source](https://pdfium.googlesource.com/pdfium/)
- [PDFium API Documentation](https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/)
- [Qt6 Documentation](https://doc.qt.io/qt-6/)