#ifndef PDFIUM_WRAPPER_H
#define PDFIUM_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

// ============================================================
// COM-style C++ interface for the PDFium wrapper.
//
// Binary contract:
//   - Interfaces are pure abstract base classes (vtable-only).
//   - Lifetime managed via AddRef()/Release().
//   - The DLL exposes a small set of C factory symbols that the
//     client resolves at runtime (e.g. via QLibrary::resolve).
// ============================================================

#ifdef _WIN32
    #ifdef PDFWRAPPER_EXPORTS
        #define PDFWRAPPER_API __declspec(dllexport)
    #else
        #define PDFWRAPPER_API __declspec(dllimport)
    #endif
    #define PDFWRAPPER_CALL __stdcall
#else
    #define PDFWRAPPER_API __attribute__((visibility("default")))
    #define PDFWRAPPER_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Error codes
// ============================================================
typedef enum {
    PDF_OK = 0,
    PDF_ERR_UNKNOWN = -1,
    PDF_ERR_FILE_NOT_FOUND = -2,
    PDF_ERR_INVALID_PASSWORD = -3,
    PDF_ERR_INVALID_PARAM = -4,
    PDF_ERR_OUT_OF_MEMORY = -5,
    PDF_ERR_UNSUPPORTED = -6,
    PDF_ERR_FORMAT = -7,
} PDF_Error;

// ============================================================
// Value structs (marshalled across the DLL boundary by value)
// ============================================================

typedef struct {
    int page_count;
    double page_width;   // Points (1/72 inch)
    double page_height;  // Points (1/72 inch)
} PDF_PageSize;

typedef struct {
    int page_count;
    int object_count;
    int font_count;
    int image_count;
    int form_field_count;
    int annotation_count;
    int bookmark_count;
    size_t file_size;
    char producer[256];
    char creator[256];
    char creation_date[64];
    char mod_date[64];
} PDF_DocumentStructure;

typedef struct {
    int flags;
    int image_quality;
    int image_dpi_threshold;
    int min_image_dpi;
    int font_subset_threshold;
    int remove_annotations;
    int remove_forms;
    int remove_bookmarks;
    int remove_metadata;
} PDF_CompressOptions;

typedef struct {
    size_t original_size;
    size_t compressed_size;
    double compression_ratio;
    int objects_removed;
    int images_recompressed;
    int fonts_subsets;
} PDF_CompressStats;

// Element type
typedef enum {
    PDF_ELEMENT_UNKNOWN = 0,
    PDF_ELEMENT_TEXT = 1,
    PDF_ELEMENT_IMAGE = 2,
    PDF_ELEMENT_PATH = 3,
    PDF_ELEMENT_SHADING = 4,
    PDF_ELEMENT_FORM = 5,
    PDF_ELEMENT_GROUP = 6,
    PDF_ELEMENT_REFERENCE = 7,
} PDF_ElementType;

// Render flags
enum PDF_RenderFlags {
    PDF_RENDER_ANNOTATIONS = 1 << 0,
    PDF_RENDER_LCD_TEXT    = 1 << 1,
    PDF_RENDER_NO_NATIVE_TEXT = 1 << 2,
    PDF_RENDER_GRAYSCALE   = 1 << 3,
    PDF_RENDER_DEBUG       = 1 << 4,
    PDF_RENDER_LIMITED_COLOR = 1 << 5,
    PDF_RENDER_FORCE_HALFTONE = 1 << 6,
    PDF_RENDER_PRINTING    = 1 << 7,
};

// Search flags
enum PDF_SearchFlags {
    PDF_SEARCH_MATCH_CASE = 1 << 0,
    PDF_SEARCH_WHOLE_WORD = 1 << 1,
};

// Compression flags
enum PDF_CompressFlags {
    PDF_COMPRESS_NONE = 0,
    PDF_COMPRESS_FLATE = 1 << 0,
    PDF_COMPRESS_OBJECT_STREAMS = 1 << 1,
    PDF_COMPRESS_IMAGES = 1 << 2,
    PDF_COMPRESS_FONTS = 1 << 3,
    PDF_COMPRESS_REMOVE_UNUSED = 1 << 4,
    PDF_COMPRESS_LINEARIZE = 1 << 5,
    PDF_COMPRESS_LOSSLESS = PDF_COMPRESS_FLATE | PDF_COMPRESS_OBJECT_STREAMS | PDF_COMPRESS_REMOVE_UNUSED,
    PDF_COMPRESS_DEFAULT = PDF_COMPRESS_LOSSLESS | PDF_COMPRESS_LINEARIZE,
};

// ============================================================
// Forward declarations of interfaces
// ============================================================
struct IPdfDocument;
struct IPdfPage;
struct IPdfElement;

// ============================================================
// IPdfUnknown: base interface with ref-counting lifetime
// ============================================================
struct IPdfUnknown {
    virtual void PDFWRAPPER_CALL AddRef() = 0;
    virtual void PDFWRAPPER_CALL Release() = 0;
    virtual ~IPdfUnknown() {}
};

// ============================================================
// IPdfDocument
// ============================================================
struct IPdfDocument : public IPdfUnknown {
    // Page access
    virtual int GetPageCount() = 0;
    // Returns a new page object; caller owns a ref (must Release)
    virtual IPdfPage* GetPage(int page_index) = 0;

    // Metadata
    virtual void GetSize(int page_index, double* width, double* height) = 0;
    virtual PDF_DocumentStructure* GetDocumentStructure() = 0;
    // Returns nullptr if no metadata; otherwise a stable pointer valid until
    // the next metadata call or document release.
    virtual const char* GetMetaText(const char* key) = 0;

    // Compression / optimization
    virtual int Optimize(const PDF_CompressOptions* options, IPdfDocument** out_handle) = 0;
    virtual int SaveWithCompression(const char* file_path, const PDF_CompressOptions* options) = 0;
    virtual int GetLastCompressStats(PDF_CompressStats* stats) = 0;
};

// ============================================================
// IPdfPage
// ============================================================
struct IPdfPage : public IPdfUnknown {
    // Returns true on success
    virtual bool Render(int width, int height, int rotation, int flags,
                        void* buffer, int stride) = 0;
    virtual void GetSize(double* width, double* height) = 0;
    virtual int GetIndex() = 0;

    // Text
    // Returns allocated UTF-8 string; caller must free with FreeString.
    virtual const char* GetText(int* out_length) = 0;
    virtual int SearchText(const char* search_text, int flags, int start_index,
                           int max_results, double* out_bounds, int* out_count) = 0;

    // Elements
    virtual int CountPageElements() = 0;
    // Returns a new element object; caller owns a ref (must Release)
    virtual IPdfElement* GetPageElement(int index) = 0;
    virtual int FindElementsByType(PDF_ElementType type, IPdfElement** out_elements, int max_count) = 0;
};

// ============================================================
// IPdfElement
// ============================================================
struct IPdfElement : public IPdfUnknown {
    virtual PDF_ElementType GetType() = 0;
    virtual void GetBounds(double* bounds) = 0; // x1, y1, x2, y2

    // Text attrs (text elements). Returns 1 on success, 0 otherwise.
    virtual int GetTextAttributes(void* out_attr) = 0;
    virtual int GetImageAttributes(void* out_attr) = 0;
    virtual int GetPathAttributes(void* out_attr) = 0;

    // Text content (text elements). Returns allocated UTF-8 string;
    // caller must free with FreeString.
    virtual const char* GetElementText(int* out_length) = 0;

    // Raw content. Returns allocated buffer; caller must free with FreeElementData.
    virtual unsigned char* GetImageData(size_t* out_size) = 0;
    virtual unsigned char* GetPathData(size_t* out_size) = 0;
};

// ============================================================
// Helpers exposed by the DLL (resolvable via QLibrary::resolve)
// ============================================================
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_InitLibrary();
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_DestroyLibrary();

// Factory: create a document from a file. Returns a NEW ref (must Release).
// Returns nullptr on failure; set *out_error to receive the PDF_Error code.
PDFWRAPPER_API IPdfDocument* PDFWRAPPER_CALL PDF_CreateDocument(
    const char* file_path,
    const char* password,
    int* out_error
);

// Free helpers
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_FreeString(const char* str);
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_FreeElementData(void* data);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PDFIUM_WRAPPER_H
