#ifndef PDFIUM_WRAPPER_H
#define PDFIUM_WRAPPER_H

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

// Opaque handle types
typedef struct PDF_Document* PDF_DocHandle;
typedef struct PDF_Page* PDF_PageHandle;

// Error codes
typedef enum {
    PDF_OK = 0,
    PDF_ERR_UNKNOWN = -1,
    PDF_ERR_FILE_NOT_FOUND = -2,
    PDF_ERR_INVALID_PASSWORD = -3,
    PDF_ERR_INVALID_PARAM = -4,
    PDF_ERR_OUT_OF_MEMORY = -5,
    PDF_ERR_UNSUPPORTED = -6,
} PDF_Error;

// Document info structure
typedef struct {
    int page_count;
    double page_width;   // Points (1/72 inch)
    double page_height;  // Points (1/72 inch)
    const char* title;
    const char* author;
    const char* subject;
    const char* keywords;
    const char* creator;
    const char* producer;
    const char* creation_date;
    const char* modification_date;
} PDF_DocInfo;

// Initialize the PDFium library (must be called once before any other function)
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_InitLibrary();

// Destroy the PDFium library (must be called at application exit)
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_DestroyLibrary();

// Load a PDF document from file
// Returns PDF_OK on success, error code on failure.
// password can be NULL if no password required.
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_LoadDocument(
    const char* file_path,
    const char* password,
    PDF_DocHandle* out_handle
);

// Load a PDF document from memory buffer
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_LoadDocumentFromMemory(
    const void* data,
    size_t size,
    const char* password,
    PDF_DocHandle* out_handle
);

// Close a document and release resources
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_CloseDocument(PDF_DocHandle handle);

// Get document metadata and page count
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetDocumentInfo(PDF_DocHandle handle, PDF_DocInfo* out_info);

// Get number of pages in document
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetPageCount(PDF_DocHandle handle);

// Load a specific page (page_index is 0-based)
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_LoadPage(
    PDF_DocHandle handle,
    int page_index,
    PDF_PageHandle* out_page
);

// Close a page
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_ClosePage(PDF_PageHandle page);

// Get page dimensions in points (1/72 inch)
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_GetPageSize(
    PDF_PageHandle page,
    double* width,
    double* height
);

// Render a page to a memory buffer
// buffer: Pre-allocated buffer of size width * height * 4 (BGRA format)
// width/height: Target pixel dimensions
// Returns PDF_OK on success
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_RenderPage(
    PDF_PageHandle page,
    int width,
    int height,
    int rotation,          // 0, 90, 180, 270
    void* buffer,          // Must be at least width * height * 4 bytes
    int stride             // Bytes per row (0 = auto = width * 4)
);

// Render a page to a memory buffer with options
// flags: Bitfield of render flags (see PDF_RenderFlags below)
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_RenderPageEx(
    PDF_PageHandle page,
    int width,
    int height,
    int rotation,
    int flags,
    void* buffer,
    int stride
);

// Render flags for PDF_RenderPageEx
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

// Search text on a page
// Returns number of matches found, or negative error code
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_SearchText(
    PDF_PageHandle page,
    const char* search_text,
    int flags,             // Case sensitive, whole word, etc.
    int start_index,       // Start from this match index
    int max_results,       // Maximum results to return
    double* out_bounds,    // Array of [x1, y1, x2, y2] in page coordinates (points)
    int* out_count         // Actual number of results returned
);

// Text search flags
enum PDF_SearchFlags {
    PDF_SEARCH_MATCH_CASE = 1 << 0,
    PDF_SEARCH_WHOLE_WORD = 1 << 1,
};

// Get text from a page (UTF-8 encoded)
// Returns allocated string that must be freed with PDF_FreeString
PDFWRAPPER_API const char* PDFWRAPPER_CALL PDF_GetPageText(
    PDF_PageHandle page,
    int* out_length
);

// Free string returned by PDF_GetPageText or other functions
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_FreeString(const char* str);

// Convert page coordinates (points) to device coordinates (pixels)
// rotation: 0, 90, 180, 270
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_PageToDevice(
    PDF_PageHandle page,
    int page_width,
    int page_height,
    int rotation,
    double page_x,
    double page_y,
    int* device_x,
    int* device_y
);

// Convert device coordinates (pixels) to page coordinates (points)
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_DeviceToPage(
    PDF_PageHandle page,
    int page_width,
    int page_height,
    int rotation,
    int device_x,
    int device_y,
    double* page_x,
    double* page_y
);

#ifdef __cplusplus
}
#endif

#endif // PDFIUM_WRAPPER_H