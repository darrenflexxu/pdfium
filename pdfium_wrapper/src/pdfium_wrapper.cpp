#include "pdfium_wrapper.h"
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fpdf_progressive.h>
#include <string>
#include <vector>
#include <map>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <codecvt>
    #include <locale>
#endif


// Internal structures
struct PDF_Document {
    FPDF_DOCUMENT doc = nullptr;
    std::string last_error;
    std::map<int, FPDF_PAGE> page_cache;
};

struct PDF_Page {
    FPDF_PAGE page = nullptr;
    FPDF_TEXTPAGE text_page = nullptr;
    PDF_Document* document = nullptr;
    int index = -1;
};

// Global initialization flag
static bool g_pdfium_initialized = false;

// Helper: Get last error string
static const char* get_error_string(FPDF_DOCUMENT doc) {
    unsigned long err = FPDF_GetLastError();
    static thread_local char buf[256];
    switch (err) {
        case FPDF_ERR_SUCCESS: return "Success";
        case FPDF_ERR_UNKNOWN: return "Unknown error";
        case FPDF_ERR_FILE: return "File not found or could not be opened";
        case FPDF_ERR_FORMAT: return "Invalid PDF format";
        case FPDF_ERR_PASSWORD: return "Incorrect password";
        case FPDF_ERR_SECURITY: return "Unsupported security scheme";
        case FPDF_ERR_PAGE: return "Page not found or content error";
        default:
            snprintf(buf, sizeof(buf), "PDFium error code: %lu", err);
            return buf;
    }
}

// Helper: Convert UTF-16 to UTF-8
static std::string utf16_to_utf8(const wchar_t* wstr, int len) {
    if (!wstr || len <= 0) return "";
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, len, nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, len, &result[0], size, nullptr, nullptr);
    return result;
#else
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.to_bytes(wstr, wstr + len);
    } catch (...) {
        return "";
    }
#endif
}

// Helper: Copy string to heap (caller must free with PDF_FreeString)
static char* str_dup(const std::string& str) {

    char* result = (char*)malloc(str.size() + 1);
    if (result) {
        memcpy(result, str.c_str(), str.size() + 1);
    }
    return result;
}

// ============================================================
// Library Init/Destroy
// ============================================================
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_InitLibrary() {
    if (g_pdfium_initialized) return PDF_OK;
    
    // Initialize PDFium
    FPDF_LIBRARY_CONFIG config = {0};
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);
    
    g_pdfium_initialized = true;
    return PDF_OK;
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_DestroyLibrary() {
    if (!g_pdfium_initialized) return;
    FPDF_DestroyLibrary();
    g_pdfium_initialized = false;
}

// ============================================================
// Document Management
// ============================================================
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_LoadDocument(
    const char* file_path,
    const char* password,
    PDF_DocHandle* out_handle
) {
    if (!out_handle || !file_path) return PDF_ERR_INVALID_PARAM;
    if (!g_pdfium_initialized) PDF_InitLibrary();

    FPDF_DOCUMENT doc = FPDF_LoadDocument(file_path, password);
    if (!doc) {
        return PDF_ERR_FILE_NOT_FOUND;
    }

    PDF_Document* wrapper = new PDF_Document();
    wrapper->doc = doc;
    *out_handle = wrapper;
    return PDF_OK;
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_LoadDocumentFromMemory(
    const void* data,
    size_t size,
    const char* password,
    PDF_DocHandle* out_handle
) {
    if (!out_handle || !data || size == 0) return PDF_ERR_INVALID_PARAM;
    if (!g_pdfium_initialized) PDF_InitLibrary();

    // PDFium doesn't have direct memory load, need to use file access interface
    // For simplicity, we'll use a temporary file approach or FPDF_LoadCustomDocument
    // Here we implement a simple file access wrapper
    
    // Actually, PDFium supports FPDF_LoadMemDocument in newer versions
    // Check if available, otherwise fallback
    FPDF_DOCUMENT doc = nullptr;
    #ifdef FPDF_LoadMemDocument
    doc = FPDF_LoadMemDocument(data, (int)size, password);
    #else
    // Fallback: Not implemented for older PDFium
    return PDF_ERR_UNSUPPORTED;
    #endif
    
    if (!doc) {
        return PDF_ERR_FILE_NOT_FOUND;
    }

    PDF_Document* wrapper = new PDF_Document();
    wrapper->doc = doc;
    *out_handle = wrapper;
    return PDF_OK;
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_CloseDocument(PDF_DocHandle handle) {
    if (!handle) return;
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    
    // Close cached pages
    for (auto& pair : doc->page_cache) {
        FPDF_ClosePage(pair.second);
    }
    doc->page_cache.clear();
    
    if (doc->doc) {
        FPDF_CloseDocument(doc->doc);
    }
    delete doc;
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetDocumentInfo(PDF_DocHandle handle, PDF_DocInfo* out_info) {
    if (!handle || !out_info) return PDF_ERR_INVALID_PARAM;
    
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    FPDF_DOCUMENT pdf = doc->doc;
    
    int count = FPDF_GetPageCount(pdf);
    out_info->page_count = count;
    
    if (count > 0) {
        FPDF_PAGE page = FPDF_LoadPage(pdf, 0);
        if (page) {
            double w, h;
            FPDF_GetPageSizeByIndex(pdf, 0, &w, &h);
            out_info->page_width = w;
            out_info->page_height = h;
            FPDF_ClosePage(page);
        }
    }
    
    // Metadata extraction (simplified)
    #ifdef FPDF_GetMetaText
    #define GET_META(key, field) \
        do { \
            unsigned long len = FPDF_GetMetaText(pdf, key, nullptr, 0); \
            if (len > 0) { \
                std::vector<wchar_t> buf(len); \
                FPDF_GetMetaText(pdf, key, buf.data(), len * sizeof(wchar_t)); \
                std::string utf8 = utf16_to_utf8(buf.data(), len); \
                static thread_local std::string storage; \
                storage = utf8; \
                out_info->field = storage.c_str(); \
            } else { \
                out_info->field = nullptr; \
            } \
        } while(0)
    #else
    #define GET_META(key, field) do { out_info->field = nullptr; } while(0)
    #endif
    
    GET_META("Title", title);
    GET_META("Author", author);
    GET_META("Subject", subject);
    GET_META("Keywords", keywords);
    GET_META("Creator", creator);
    GET_META("Producer", producer);
    GET_META("CreationDate", creation_date);
    GET_META("ModDate", modification_date);
    
    return PDF_OK;
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetPageCount(PDF_DocHandle handle) {
    if (!handle) return PDF_ERR_INVALID_PARAM;
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    return FPDF_GetPageCount(doc->doc);
}

// ============================================================
// Page Management
// ============================================================
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_LoadPage(
    PDF_DocHandle handle,
    int page_index,
    PDF_PageHandle* out_page
) {
    if (!handle || !out_page) return PDF_ERR_INVALID_PARAM;
    
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    int count = FPDF_GetPageCount(doc->doc);
    
    if (page_index < 0 || page_index >= count) {
        return PDF_ERR_INVALID_PARAM;
    }
    
    // Check cache first
    auto it = doc->page_cache.find(page_index);
    FPDF_PAGE page;
    
    if (it != doc->page_cache.end()) {
        page = it->second;
    } else {
        page = FPDF_LoadPage(doc->doc, page_index);
        if (!page) return PDF_ERR_UNKNOWN;
        doc->page_cache[page_index] = page;
    }
    
    PDF_Page* wrapper = new PDF_Page();
    wrapper->page = page;
    wrapper->document = doc;
    wrapper->index = page_index;
    wrapper->text_page = FPDFText_LoadPage(page);
    
    *out_page = wrapper;
    return PDF_OK;
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_ClosePage(PDF_PageHandle page) {
    if (!page) return;
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    
    if (wrapper->text_page) {
        FPDFText_ClosePage(wrapper->text_page);
    }
    // Note: We don't close the FPDF_PAGE here as it's cached in the document
    delete wrapper;
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_GetPageSize(
    PDF_PageHandle page,
    double* width,
    double* height
) {
    if (!page || !width || !height) return;
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    FPDF_GetPageSizeByIndex(wrapper->document->doc, wrapper->index, width, height);
}

// ============================================================
// Rendering
// ============================================================
static int render_flags_to_pdfium(int flags) {
    int pdfium_flags = FPDF_RENDER_NO_SMOOTHTEXT; // Default: no smoothing for speed
    if (flags & PDF_RENDER_ANNOTATIONS) pdfium_flags |= FPDF_ANNOT;
    if (flags & PDF_RENDER_LCD_TEXT) pdfium_flags |= FPDF_LCD_TEXT;
    if (flags & PDF_RENDER_NO_NATIVE_TEXT) pdfium_flags |= FPDF_NO_NATIVETEXT;
    if (flags & PDF_RENDER_GRAYSCALE) pdfium_flags |= FPDF_GRAYSCALE;
    if (flags & PDF_RENDER_DEBUG) pdfium_flags |= FPDF_DEBUG_INFO;
    #ifdef FPDF_LIMITEDCOLOR
    if (flags & PDF_RENDER_LIMITED_COLOR) pdfium_flags |= FPDF_LIMITEDCOLOR;
    #endif
    #ifdef FPDF_FORCEHALFTONE
    if (flags & PDF_RENDER_FORCE_HALFTONE) pdfium_flags |= FPDF_FORCEHALFTONE;
    #endif
    if (flags & PDF_RENDER_PRINTING) pdfium_flags |= FPDF_PRINTING;
    return pdfium_flags;
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_RenderPage(
    PDF_PageHandle page,
    int width,
    int height,
    int rotation,
    void* buffer,
    int stride
) {
    return PDF_RenderPageEx(page, width, height, rotation, 0, buffer, stride);
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_RenderPageEx(
    PDF_PageHandle page,
    int width,
    int height,
    int rotation,
    int flags,
    void* buffer,
    int stride
) {
    if (!page || !buffer || width <= 0 || height <= 0) {
        return PDF_ERR_INVALID_PARAM;
    }
    
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    FPDF_PAGE pdf_page = wrapper->page;
    
    if (stride == 0) stride = width * 4;
    
    // Create bitmap
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRA, buffer, stride);
    if (!bitmap) return PDF_ERR_OUT_OF_MEMORY;
    
    // Fill with white background
    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    
    // Render
    int pdfium_flags = render_flags_to_pdfium(flags);
    int pdfium_rotation = 0;
    switch (rotation) {
        case 90: pdfium_rotation = 1; break;
        case 180: pdfium_rotation = 2; break;
        case 270: pdfium_rotation = 3; break;
    }
    
    FPDF_RenderPageBitmap(bitmap, pdf_page, 0, 0, width, height, pdfium_rotation, pdfium_flags);
    
    FPDFBitmap_Destroy(bitmap);
    return PDF_OK;
}

// ============================================================
// Text Search & Extraction
// ============================================================
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_SearchText(
    PDF_PageHandle page,
    const char* search_text,
    int flags,
    int start_index,
    int max_results,
    double* out_bounds,
    int* out_count
) {
    if (!page || !search_text || !out_bounds || !out_count) {
        return PDF_ERR_INVALID_PARAM;
    }
    
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    if (!wrapper->text_page) {
        return PDF_ERR_UNKNOWN;
    }
    
    // Convert search text to UTF-16
    #ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, search_text, -1, nullptr, 0);
    std::vector<wchar_t> wsearch(wlen);
    MultiByteToWideChar(CP_UTF8, 0, search_text, -1, wsearch.data(), wlen);
    #else
    std::vector<unsigned short> wsearch;
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wsearch_str = converter.from_bytes(search_text);
        
        wsearch.reserve(wsearch_str.size() + 1);
        for(wchar_t wc : wsearch_str) {
            wsearch.push_back(static_cast<unsigned short>(wc));
        }
        wsearch.push_back(0); // Ensure null termination
    } catch (...) {
        return PDF_ERR_UNKNOWN;
    }
    #endif
    
    int search_flags = 0;
    if (flags & PDF_SEARCH_MATCH_CASE) search_flags |= FPDF_MATCHCASE;
    if (flags & PDF_SEARCH_WHOLE_WORD) search_flags |= FPDF_MATCHWHOLEWORD;
    
    FPDF_SCHHANDLE handle = FPDFText_FindStart(wrapper->text_page, wsearch.data(), search_flags, start_index);
    if (!handle) {
        *out_count = 0;
        return PDF_OK;
    }
    
    int found = 0;
    int current_index = start_index;
    
    while (found < max_results) {
        if (!FPDFText_FindNext(handle)) break;
        
        int index = FPDFText_GetSchResultIndex(handle);
        if (index < start_index) continue;
        
        double x1, y1, x2, y2;
        // FPDFText_GetSchResultRect might be missing in some versions, 
        // in that case we just set bounds to 0
        #ifdef FPDFText_GetSchResultRect
        FPDFText_GetSchResultRect(handle, &x1, &y1, &x2, &y2);
        #else
        x1 = y1 = x2 = y2 = 0;
        #endif
        
        // Store bounds: [x1, y1, x2, y2]
        int base = found * 4;
        out_bounds[base] = x1;
        out_bounds[base + 1] = y1;
        out_bounds[base + 2] = x2;
        out_bounds[base + 3] = y2;
        
        found++;
        current_index = index + 1;
    }
    
    FPDFText_FindClose(handle);
    *out_count = found;
    return found >= 0 ? PDF_OK : PDF_ERR_UNKNOWN;
}

PDFWRAPPER_API const char* PDFWRAPPER_CALL PDF_GetPageText(
    PDF_PageHandle page,
    int* out_length
) {
    if (!page) return nullptr;
    
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    if (!wrapper->text_page) return nullptr;
    
    int len = FPDFText_CountChars(wrapper->text_page);
    if (len <= 0) {
        if (out_length) *out_length = 0;
        return str_dup("");
    }
    
    // Get text in UTF-16
    int buffer_size = (len + 1) * sizeof(wchar_t);
    std::vector<unsigned short> wbuffer(len + 1);
    int actual = FPDFText_GetText(wrapper->text_page, 0, len, wbuffer.data());
    wbuffer[actual] = 0;
    
    // Convert from unsigned short* (UTF-16) to std::string (UTF-8)
    std::string utf8;
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wstr;
        wstr.reserve(actual);
        for(unsigned short us : wbuffer) {
            if (us == 0) break;
            wstr.push_back(static_cast<wchar_t>(us));
        }
        utf8 = converter.to_bytes(wstr);
    } catch (...) {
        utf8 = "";
    }
    if (out_length) *out_length = utf8.size();
    
    return str_dup(utf8);
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_FreeString(const char* str) {
    if (str) {
        free(const_cast<char*>(str));
    }
}

// ============================================================
// Coordinate Transformation
// ============================================================
PDFWRAPPER_API void PDFWRAPPER_CALL PDF_PageToDevice(
    PDF_PageHandle page,
    int page_width,
    int page_height,
    int rotation,
    double page_x,
    double page_y,
    int* device_x,
    int* device_y
) {
    if (!page || !device_x || !device_y) return;
    
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    int dev_x, dev_y;
    FPDF_PageToDevice(wrapper->page, 0, 0, page_width, page_height, rotation, page_x, page_y, &dev_x, &dev_y);
    *device_x = dev_x;
    *device_y = dev_y;
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_DeviceToPage(
    PDF_PageHandle page,
    int page_width,
    int page_height,
    int rotation,
    int device_x,
    int device_y,
    double* page_x,
    double* page_y
) {
    if (!page || !page_x || !page_y) return;
    
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    double pg_x, pg_y;
    FPDF_DeviceToPage(wrapper->page, 0, 0, page_width, page_height, rotation, device_x, device_y, &pg_x, &pg_y);
    *page_x = pg_x;
    *page_y = pg_y;
}

// ============================================================
// PDF Compression / Optimization API
// ============================================================

// Forward declarations for extension functions
// These would be available if PDFium was built with our extensions
#ifdef FPDF_OptimizeDocument
extern "C" {
    void FPDF_CompressOptionsInit(void* options);
    int FPDF_OptimizeDocument(void* doc, const void* options, void** out_doc);
    int FPDF_SaveWithCompression(void* doc, const char* path, const void* options);
    int FPDF_GetLastCompressStats(void* stats);
}
#endif

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_CompressOptionsInit(PDF_CompressOptions* options) {
    if (!options) return;
    #ifdef FPDF_OptimizeDocument
    FPDF_CompressOptionsInit(options);
    #else
    memset(options, 0, sizeof(PDF_CompressOptions));
    options->flags = PDF_COMPRESS_DEFAULT;
    options->image_quality = PDF_IMAGE_QUALITY_HIGH;
    options->image_dpi_threshold = 300;
    options->min_image_dpi = 150;
    options->font_subset_threshold = 80;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_OptimizeDocument(
    PDF_DocHandle handle,
    const PDF_CompressOptions* options,
    PDF_DocHandle* out_handle
) {
    if (!handle || !out_handle) return PDF_ERR_INVALID_PARAM;
    
    #ifdef FPDF_OptimizeDocument
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    FPDF_DOCUMENT new_doc = nullptr;
    int result = FPDF_OptimizeDocument(doc->doc, options, &new_doc);
    if (result && new_doc) {
        PDF_Document* wrapper = new PDF_Document();
        wrapper->doc = new_doc;
        *out_handle = wrapper;
        return PDF_OK;
    }
    return PDF_ERR_UNSUPPORTED;
    #else
    (void)options;
    return PDF_ERR_UNSUPPORTED;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_SaveWithCompression(
    PDF_DocHandle handle,
    const char* file_path,
    const PDF_CompressOptions* options
) {
    if (!handle || !file_path) return PDF_ERR_INVALID_PARAM;
    
    #ifdef FPDF_OptimizeDocument
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    return FPDF_SaveWithCompression(doc->doc, file_path, options) ? PDF_OK : PDF_ERR_UNKNOWN;
    #else
    (void)options;
    return PDF_ERR_UNSUPPORTED;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetLastCompressStats(PDF_CompressStats* stats) {
    if (!stats) return PDF_ERR_INVALID_PARAM;
    
    #ifdef FPDF_OptimizeDocument
    return FPDF_GetLastCompressStats(stats) ? PDF_OK : PDF_ERR_UNKNOWN;
    #else
    memset(stats, 0, sizeof(PDF_CompressStats));
    return PDF_ERR_UNSUPPORTED;
    #endif
}

// ============================================================
// Element Extraction API
// ============================================================

// Forward declarations for extension functions
#ifdef FPDF_CreateElementIterator
extern "C" {
    void* FPDF_CreateElementIterator(void* page);
    void* FPDF_GetNextElement(void* iterator);
    void FPDF_DestroyElementIterator(void* iterator);
    int FPDF_GetElementType(void* element);
    void FPDF_GetElementBounds(void* element, double* bounds);
    int FPDF_GetElementTextAttributes(void* element, void* attrs);
    int FPDF_GetElementImageAttributes(void* element, void* attrs);
    int FPDF_GetElementPathAttributes(void* element, void* attrs);
    const char* FPDF_GetElementText(void* element, int* out_len);
    unsigned char* FPDF_GetElementImageData(void* element, size_t* out_size);
    unsigned char* FPDF_GetElementPathData(void* element, size_t* out_size);
    void FPDF_FreeElementData(void* data);
    int FPDF_CountPageElements(void* page);
    void* FPDF_GetPageElement(void* page, int index);
    int FPDF_FindElementsByType(void* page, int type, void** out_elements, int max_count);
    int FPDF_SearchTextEx(void* page, const char* text, int flags, int start, int max, void* matches, int* count);
    int FPDF_ExtractFormFields(void* doc, void* fields, int max);
    int FPDF_ExtractAnnotations(void* page, void* annots, int max);
    int FPDF_ExtractBookmarks(void* doc, void* bookmarks, int max);
    int FPDF_GetDocumentStructure(void* doc, void* info);
}
#endif

PDFWRAPPER_API PDF_ElementIteratorHandle PDFWRAPPER_CALL PDF_CreateElementIterator(PDF_PageHandle page) {
    if (!page) return nullptr;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    return FPDF_CreateElementIterator(wrapper->page);
    #else
    return nullptr;
    #endif
}

PDFWRAPPER_API PDF_ElementHandle PDFWRAPPER_CALL PDF_GetNextElement(PDF_ElementIteratorHandle iterator) {
    if (!iterator) return nullptr;
    
    #ifdef FPDF_CreateElementIterator
    return FPDF_GetNextElement(iterator);
    #else
    return nullptr;
    #endif
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_DestroyElementIterator(PDF_ElementIteratorHandle iterator) {
    if (!iterator) return;
    
    #ifdef FPDF_CreateElementIterator
    FPDF_DestroyElementIterator(iterator);
    #endif
}

PDFWRAPPER_API PDF_ElementType PDFWRAPPER_CALL PDF_GetElementType(PDF_ElementHandle element) {
    if (!element) return PDF_ELEMENT_UNKNOWN;
    
    #ifdef FPDF_CreateElementIterator
    return static_cast<PDF_ElementType>(FPDF_GetElementType(element));
    #else
    return PDF_ELEMENT_UNKNOWN;
    #endif
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_GetElementBounds(PDF_ElementHandle element, double* bounds) {
    if (!element || !bounds) return;
    
    #ifdef FPDF_CreateElementIterator
    FPDF_GetElementBounds(element, bounds);
    #else
    bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetElementTextAttributes(PDF_ElementHandle element, PDF_TextAttributes* out_attr) {
    if (!element || !out_attr) return 0;
    
    #ifdef FPDF_CreateElementIterator
    return FPDF_GetElementTextAttributes(element, out_attr);
    #else
    return 0;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetElementImageAttributes(PDF_ElementHandle element, PDF_ImageAttributes* out_attr) {
    if (!element || !out_attr) return 0;
    
    #ifdef FPDF_CreateElementIterator
    return FPDF_GetElementImageAttributes(element, out_attr);
    #else
    return 0;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetElementPathAttributes(PDF_ElementHandle element, PDF_PathAttributes* out_attr) {
    if (!element || !out_attr) return 0;
    
    #ifdef FPDF_CreateElementIterator
    return FPDF_GetElementPathAttributes(element, out_attr);
    #else
    return 0;
    #endif
}

PDFWRAPPER_API const char* PDFWRAPPER_CALL PDF_GetElementText(PDF_ElementHandle element, int* out_length) {
    if (!element) {
        if (out_length) *out_length = 0;
        return nullptr;
    }
    
    #ifdef FPDF_CreateElementIterator
    return FPDF_GetElementText(element, out_length);
    #else
    if (out_length) *out_length = 0;
    return nullptr;
    #endif
}

PDFWRAPPER_API unsigned char* PDFWRAPPER_CALL PDF_GetElementImageData(PDF_ElementHandle element, size_t* out_size) {
    if (!element || !out_size) return nullptr;
    
    #ifdef FPDF_CreateElementIterator
    return FPDF_GetElementImageData(element, out_size);
    #else
    *out_size = 0;
    return nullptr;
    #endif
}

PDFWRAPPER_API unsigned char* PDFWRAPPER_CALL PDF_GetElementPathData(PDF_ElementHandle element, size_t* out_size) {
    if (!element || !out_size) return nullptr;
    
    #ifdef FPDF_CreateElementIterator
    return FPDF_GetElementPathData(element, out_size);
    #else
    *out_size = 0;
    return nullptr;
    #endif
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_FreeElementData(void* data) {
    if (!data) return;
    
    #ifdef FPDF_CreateElementIterator
    FPDF_FreeElementData(data);
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_CountPageElements(PDF_PageHandle page) {
    if (!page) return 0;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    return FPDF_CountPageElements(wrapper->page);
    #else
    return 0;
    #endif
}

PDFWRAPPER_API PDF_ElementHandle PDFWRAPPER_CALL PDF_GetPageElement(PDF_PageHandle page, int index) {
    if (!page || index < 0) return nullptr;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    return FPDF_GetPageElement(wrapper->page, index);
    #else
    return nullptr;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_FindElementsByType(
    PDF_PageHandle page,
    PDF_ElementType type,
    PDF_ElementHandle* out_elements,
    int max_count
) {
    if (!page || !out_elements || max_count <= 0) return 0;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    return FPDF_FindElementsByType(wrapper->page, static_cast<int>(type), 
                                    reinterpret_cast<void**>(out_elements), max_count);
    #else
    return 0;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_SearchTextEx(
    PDF_PageHandle page,
    const char* search_text,
    int flags,
    int start_index,
    int max_results,
    PDF_TextMatchEx* out_matches,
    int* out_count
) {
    if (!page || !search_text || !out_matches || !out_count) return 0;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    return FPDF_SearchTextEx(wrapper->page, search_text, flags, start_index, max_results,
                             reinterpret_cast<void*>(out_matches), out_count);
    #else
    *out_count = 0;
    return 0;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_ExtractFormFields(
    PDF_DocHandle handle,
    PDF_FormFieldInfo* out_fields,
    int max_fields
) {
    if (!handle || !out_fields || max_fields <= 0) return 0;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    return FPDF_ExtractFormFields(doc->doc, out_fields, max_fields);
    #else
    return 0;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_ExtractAnnotations(
    PDF_PageHandle page,
    PDF_AnnotInfo* out_annots,
    int max_annots
) {
    if (!page || !out_annots || max_annots <= 0) return 0;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Page* wrapper = static_cast<PDF_Page*>(page);
    return FPDF_ExtractAnnotations(wrapper->page, out_annots, max_annots);
    #else
    return 0;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_ExtractBookmarks(
    PDF_DocHandle handle,
    PDF_BookmarkInfo* out_bookmarks,
    int max_bookmarks
) {
    if (!handle || !out_bookmarks || max_bookmarks <= 0) return 0;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    return FPDF_ExtractBookmarks(doc->doc, out_bookmarks, max_bookmarks);
    #else
    return 0;
    #endif
}

PDFWRAPPER_API int PDFWRAPPER_CALL PDF_GetDocumentStructure(
    PDF_DocHandle handle,
    PDF_DocumentStructure* out_info
) {
    if (!handle || !out_info) return 0;
    
    #ifdef FPDF_CreateElementIterator
    PDF_Document* doc = static_cast<PDF_Document*>(handle);
    return FPDF_GetDocumentStructure(doc->doc, out_info);
    #else
    return 0;
    #endif
}