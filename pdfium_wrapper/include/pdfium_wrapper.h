#ifndef PDFIUM_WRAPPER_H
#define PDFIUM_WRAPPER_H

#include <stddef.h>

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
    PDF_ERR_FORMAT = -7,
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

// ============================================================
// PDF Compression / Optimization API
// ============================================================

// Compression flags
typedef enum {
    PDF_COMPRESS_NONE = 0,
    PDF_COMPRESS_FLATE = 1 << 0,
    PDF_COMPRESS_OBJECT_STREAMS = 1 << 1,
    PDF_COMPRESS_IMAGES = 1 << 2,
    PDF_COMPRESS_FONTS = 1 << 3,
    PDF_COMPRESS_REMOVE_UNUSED = 1 << 4,
    PDF_COMPRESS_LINEARIZE = 1 << 5,
    PDF_COMPRESS_LOSSLESS = PDF_COMPRESS_FLATE | PDF_COMPRESS_OBJECT_STREAMS | PDF_COMPRESS_REMOVE_UNUSED,
    PDF_COMPRESS_DEFAULT = PDF_COMPRESS_LOSSLESS | PDF_COMPRESS_LINEARIZE,
} PDF_CompressFlags;

// Image quality for lossy compression
typedef enum {
    PDF_IMAGE_QUALITY_LOW = 50,
    PDF_IMAGE_QUALITY_MEDIUM = 75,
    PDF_IMAGE_QUALITY_HIGH = 90,
    PDF_IMAGE_QUALITY_LOSSLESS = 100,
} PDF_ImageQuality;

// Compression options
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

// Initialize compression options with defaults
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_CompressOptionsInit(PDF_CompressOptions* options);

// Optimize/compress a document
// Returns new document handle that must be closed with PDF_CloseDocument
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_OptimizeDocument(
    PDF_DocHandle handle,
    const PDF_CompressOptions* options,
    PDF_DocHandle* out_handle
);

// Save document with compression
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_SaveWithCompression(
    PDF_DocHandle handle,
    const char* file_path,
    const PDF_CompressOptions* options
);

// Compression statistics
typedef struct {
    size_t original_size;
    size_t compressed_size;
    double compression_ratio;
    int objects_removed;
    int images_recompressed;
    int fonts_subsets;
} PDF_CompressStats;

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetLastCompressStats(PDF_CompressStats* stats);

// ============================================================
// Element Extraction API
// ============================================================

// Element types
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

// Opaque element handle
typedef struct PDF_PageElement* PDF_ElementHandle;
typedef struct PDF_ElementIterator* PDF_ElementIteratorHandle;

// Text element attributes
typedef struct {
    double font_size;
    double char_spacing;
    double word_spacing;
    double horizontal_scaling;
    double leading;
    unsigned int font_flags;
    unsigned int color_rgb;
    unsigned int color_alpha;
    char font_name[256];
    char font_family[128];
    int writing_mode;
    double text_matrix[6];
} PDF_TextAttributes;

// Image element attributes
typedef struct {
    int width;
    int height;
    int bits_per_component;
    int color_space;
    int filter;
    size_t data_size;
    double matrix[6];
    int has_mask;
    int is_inline;
} PDF_ImageAttributes;

// Path element attributes
typedef struct {
    int fill_color_rgb;
    int fill_color_alpha;
    int stroke_color_rgb;
    int stroke_color_alpha;
    double line_width;
    int line_cap;
    int line_join;
    double miter_limit;
    int fill_rule;
    double dash_pattern[16];
    int dash_count;
    double dash_phase;
    double matrix[6];
} PDF_PathAttributes;

// Create element iterator for a page
PDFWRAPPER_API PDF_ElementIteratorHandle PDFWRAPPER_CALL PDF_CreateElementIterator(PDF_PageHandle page);

// Get next element (returns NULL at end)
PDFWRAPPER_API PDF_ElementHandle PDFWRAPPER_CALL PDF_GetNextElement(PDF_ElementIteratorHandle iterator);

// Destroy iterator
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_DestroyElementIterator(PDF_ElementIteratorHandle iterator);

// Get element type
PDFWRAPPER_API PDF_ElementType PDFWRAPPER_CALL PDF_GetElementType(PDF_ElementHandle element);

// Get element bounds (x1, y1, x2, y2 in page coordinates)
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_GetElementBounds(PDF_ElementHandle element, double* bounds);

// Get text attributes (for text elements)
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetElementTextAttributes(PDF_ElementHandle element, PDF_TextAttributes* out_attr);

// Get image attributes (for image elements)
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetElementImageAttributes(PDF_ElementHandle element, PDF_ImageAttributes* out_attr);

// Get path attributes (for path elements)
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetElementPathAttributes(PDF_ElementHandle element, PDF_PathAttributes* out_attr);

// Extract text from element
PDFWRAPPER_API const char* PDFWRAPPER_CALL PDF_GetElementText(PDF_ElementHandle element, int* out_length);

// Extract image data from element
// Returns allocated buffer that must be freed with PDF_FreeElementData
PDFWRAPPER_API unsigned char* PDFWRAPPER_CALL PDF_GetElementImageData(PDF_ElementHandle element, size_t* out_size);

// Extract path data from element
PDFWRAPPER_API unsigned char* PDFWRAPPER_CALL PDF_GetElementPathData(PDF_ElementHandle element, size_t* out_size);

// Free data returned by extraction functions
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_FreeElementData(void* data);

// Get element count on page
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_CountPageElements(PDF_PageHandle page);

// Get element by index
PDFWRAPPER_API PDF_ElementHandle PDFWRAPPER_CALL PDF_GetPageElement(PDF_PageHandle page, int index);

// Find elements by type
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_FindElementsByType(
    PDF_PageHandle page,
    PDF_ElementType type,
    PDF_ElementHandle* out_elements,
    int max_count
);

// Extended text search with element context
typedef struct {
    double bounds[4];
    int char_index;
    int element_index;
    PDF_ElementHandle element;
} PDF_TextMatchEx;

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_SearchTextEx(
    PDF_PageHandle page,
    const char* search_text,
    int flags,
    int start_index,
    int max_results,
    PDF_TextMatchEx* out_matches,
    int* out_count
);

// Form field extraction
typedef struct {
    char name[256];
    char value[1024];
    int field_type;
    double bounds[4];
    int flags;
    PDF_ElementHandle element;
} PDF_FormFieldInfo;

typedef enum {
    PDF_FIELD_UNKNOWN = 0,
    PDF_FIELD_BUTTON = 1,
    PDF_FIELD_CHECKBOX = 2,
    PDF_FIELD_RADIOBUTTON = 3,
    PDF_FIELD_TEXT = 4,
    PDF_FIELD_CHOICE = 5,
    PDF_FIELD_SIGNATURE = 6,
} PDF_FormFieldType;

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_ExtractFormFields(
    PDF_DocHandle handle,
    PDF_FormFieldInfo* out_fields,
    int max_fields
);

// Annotation extraction
typedef struct {
    int type;
    char contents[2048];
    double bounds[4];
    int flags;
    PDF_ElementHandle element;
} PDF_AnnotInfo;

typedef enum {
    PDF_ANNOT_UNKNOWN = 0,
    PDF_ANNOT_TEXT = 1,
    PDF_ANNOT_LINK = 2,
    PDF_ANNOT_FREETEXT = 3,
    PDF_ANNOT_LINE = 4,
    PDF_ANNOT_SQUARE = 5,
    PDF_ANNOT_CIRCLE = 6,
    PDF_ANNOT_POLYGON = 7,
    PDF_ANNOT_POLYLINE = 8,
    PDF_ANNOT_HIGHLIGHT = 9,
    PDF_ANNOT_UNDERLINE = 10,
    PDF_ANNOT_SQUIGGLY = 11,
    PDF_ANNOT_STRIKEOUT = 12,
    PDF_ANNOT_STAMP = 13,
    PDF_ANNOT_INK = 14,
    PDF_ANNOT_FILEATTACHMENT = 15,
    PDF_ANNOT_SOUND = 16,
    PDF_ANNOT_MOVIE = 17,
    PDF_ANNOT_WIDGET = 18,
    PDF_ANNOT_SCREEN = 19,
    PDF_ANNOT_PRINTERMARK = 20,
    PDF_ANNOT_TRAPNET = 21,
    PDF_ANNOT_WATERMARK = 22,
    PDF_ANNOT_3D = 23,
} PDF_AnnotType;

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_ExtractAnnotations(
    PDF_PageHandle page,
    PDF_AnnotInfo* out_annots,
    int max_annots
);

// Bookmark/outline extraction
typedef struct {
    char title[512];
    int page_index;
    double dest_x, dest_y;
    double zoom;
    int level;
    int child_count;
} PDF_BookmarkInfo;

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_ExtractBookmarks(
    PDF_DocHandle handle,
    PDF_BookmarkInfo* out_bookmarks,
    int max_bookmarks
);

// Document structure info
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

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetDocumentStructure(
    PDF_DocHandle handle,
    PDF_DocumentStructure* out_info
);

#ifdef __cplusplus
}
#endif

#endif // PDFIUM_WRAPPER_H