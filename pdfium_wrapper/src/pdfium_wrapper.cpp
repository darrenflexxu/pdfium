#include "pdfium_wrapper.h"
#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_text.h>
#include <fpdf_progressive.h>
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <mutex>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <codecvt>
    #include <locale>
#endif

// ============================================================
// Extension entry points (provided by the pdfium_extensions static library)
// ============================================================
#if defined(PDFIUM_HAS_EXTENSIONS)
extern "C" {
    void FPDF_CompressOptionsInit(void* o);
    void* FPDF_OptimizeDocument(void* d, const void* o,
                                void (*cb)(int, const char*, void*),
                                void* user, void** out_d);
    int   FPDF_SaveWithCompression(void* d, const char* p, const void* o,
                                   void (*cb)(int, const char*, void*),
                                   void* user);
    int   FPDF_GetLastCompressStats(void* s);
}
#endif

// ============================================================
// Value structs for element attributes (by-value marshalling)
// ============================================================
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

namespace {

// Global library state
bool g_pdfium_initialized = false;

// PDFium is not thread-safe: the document, page, and syntax-parser state is
// shared and mutated lazily, so concurrent access from the GUI thread (outline
// walks, search) and the async render thread corrupts the parser. A single
// global mutex serializes every FPDF_* call made through the wrapper.
std::mutex g_pdfium_mutex;
#define PDFIUM_SCOPE_LOCK std::lock_guard<std::mutex> _pdfium_lock(g_pdfium_mutex)

// Initializes the PDFium library. The caller must already hold g_pdfium_mutex
// (never call PDF_InitLibrary() from inside a locked wrapper method).
static void ensure_library_initialized() {
    if (g_pdfium_initialized) return;

    FPDF_LIBRARY_CONFIG config = {0};
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);

    g_pdfium_initialized = true;
}

// Reference-counting base
class RefCounted {
public:
    RefCounted() : m_refs(1) {}
    virtual ~RefCounted() {}

    void AddRef() { m_refs.fetch_add(1, std::memory_order_relaxed); }

    void Release() {
        if (m_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

private:
    std::atomic<int> m_refs;
};

// Store raw string + length for MetaText results
static char* str_dup(const std::string& str) {
    char* result = static_cast<char*>(std::malloc(str.size() + 1));
    if (result) {
        std::memcpy(result, str.c_str(), str.size() + 1);
    }
    return result;
}

// Convert UTF-16 to UTF-8
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

// Convert UTF-8 to UTF-16 code units (unsigned short), null-terminated.
static std::vector<unsigned short> utf8_to_utf16(const char* str) {
    std::vector<unsigned short> result;
    if (!str) return result;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if (wlen <= 0) return result;
    result.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, reinterpret_cast<wchar_t*>(result.data()), wlen);
#else
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wstr = converter.from_bytes(str);
        for (wchar_t wc : wstr) {
            result.push_back(static_cast<unsigned short>(wc));
        }
        result.push_back(0);
    } catch (...) {
        result.clear();
    }
#endif
    return result;
}

// Forward decls
struct PdfPageImpl;
struct PdfElementImpl;
struct PdfOutlineImpl;

// ============================================================
// PdfDocumentImpl
// ============================================================
struct PdfDocumentImpl : public IPdfDocument, public RefCounted {
    FPDF_DOCUMENT doc = nullptr;
    std::map<int, FPDF_PAGE> page_cache;
    PDF_DocumentStructure structure;
    std::string producer, creator, creation_date, mod_date;
    std::map<std::string, std::string> metadata;

    PdfDocumentImpl() {
        std::memset(&structure, 0, sizeof(structure));
    }

    // IPdfUnknown
    void AddRef() override { RefCounted::AddRef(); }
    void Release() override { RefCounted::Release(); }

    // IPdfDocument
    int GetPageCount() override {
        PDFIUM_SCOPE_LOCK;
        return doc ? FPDF_GetPageCount(doc) : 0;
    }

    IPdfOutline* GetOutlineRoot() override;

    IPdfPage* GetPage(int page_index) override;
    void GetSize(int page_index, double* width, double* height) override;
    PDF_DocumentStructure* GetDocumentStructure() override;
    const char* GetMetaText(const char* key) override;
    int Optimize(const PDF_CompressOptions* options,
                 PDF_CompressProgressCallback progress_cb,
                 void* user_data,
                 IPdfDocument** out_handle) override;
    int SaveWithCompression(const char* file_path,
                            const PDF_CompressOptions* options,
                            PDF_CompressProgressCallback progress_cb,
                            void* user_data) override;
    int GetLastCompressStats(PDF_CompressStats* stats) override;

    ~PdfDocumentImpl() override {
        PDFIUM_SCOPE_LOCK;
        for (auto& pair : page_cache) {
            FPDF_ClosePage(pair.second);
        }
        page_cache.clear();
        if (doc) FPDF_CloseDocument(doc);
    }

    FPDF_PAGE getPageRef(int index) {
        auto it = page_cache.find(index);
        if (it != page_cache.end()) return it->second;
        FPDF_PAGE p = FPDF_LoadPage(doc, index);
        if (p) page_cache[index] = p;
        return p;
    }
};

// ============================================================
// PdfOutlineImpl
// ============================================================
struct PdfOutlineImpl : public IPdfOutline, public RefCounted {
    PdfDocumentImpl* document = nullptr;
    FPDF_BOOKMARK bookmark = nullptr;

    PdfOutlineImpl(PdfDocumentImpl* doc, FPDF_BOOKMARK bm) : document(doc), bookmark(bm) {
        if (document) document->AddRef();
    }

    void AddRef() override { RefCounted::AddRef(); }
    void Release() override { RefCounted::Release(); }

    const char* GetTitle(int* out_length) override {
        PDFIUM_SCOPE_LOCK;
        if (out_length) *out_length = 0;
        if (!bookmark) return str_dup("");

        // First query the required byte count (including the UTF16 NUL).
        unsigned long total = FPDFBookmark_GetTitle(bookmark, nullptr, 0);
        if (total < 2) return str_dup("");

        // FPDFBookmark_GetTitle always emits UTF-16LE, independent of host
        // endianness. Read the raw code units and widen them so the shared
        // utf16_to_utf8 converter can handle surrogate pairs correctly.
        std::vector<uint16_t> raw(total / sizeof(uint16_t) + 1);
        if (FPDFBookmark_GetTitle(bookmark, raw.data(), total) == 0) return str_dup("");

        std::vector<wchar_t> wbuf;
        wbuf.reserve(raw.size());
        for (uint16_t u : raw) {
            if (u == 0) break;
            wbuf.push_back(static_cast<wchar_t>(u));
        }
        std::string utf8 = utf16_to_utf8(wbuf.data(), static_cast<int>(wbuf.size()));
        if (out_length) *out_length = static_cast<int>(utf8.size());
        return str_dup(utf8);
    }

    IPdfOutline* GetFirstChild() override {
        PDFIUM_SCOPE_LOCK;
        if (!document || !document->doc) return nullptr;
        FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(document->doc, bookmark);
        if (!child) return nullptr;
        return new PdfOutlineImpl(document, child);
    }

    IPdfOutline* GetNextSibling() override {
        PDFIUM_SCOPE_LOCK;
        if (!document || !document->doc || !bookmark) return nullptr;
        FPDF_BOOKMARK next = FPDFBookmark_GetNextSibling(document->doc, bookmark);
        if (!next) return nullptr;
        return new PdfOutlineImpl(document, next);
    }

    int GetDestinationPage() override {
        PDFIUM_SCOPE_LOCK;
        if (!document || !document->doc || !bookmark) return -1;
        FPDF_DEST dest = FPDFBookmark_GetDest(document->doc, bookmark);
        if (!dest) return -1;
        return FPDFDest_GetDestPageIndex(document->doc, dest);
    }

    ~PdfOutlineImpl() override {
        if (document) document->Release();
    }
};

// ============================================================
// PdfElementImpl
// ============================================================
struct PdfElementImpl : public IPdfElement, public RefCounted {
    PdfPageImpl* owner = nullptr;
    int index = -1;
    PDF_ElementType type = PDF_ELEMENT_UNKNOWN;
    double bounds[4] = {0, 0, 0, 0};
    PDF_TextAttributes text;
    PDF_ImageAttributes image;
    PDF_PathAttributes path;

    // These hold the decoder-side handles from the extensions, if enabled.
    void* ext_element = nullptr;

    PdfElementImpl() {
        std::memset(&text, 0, sizeof(text));
        std::memset(&image, 0, sizeof(image));
        std::memset(&path, 0, sizeof(path));
    }

    void AddRef() override { RefCounted::AddRef(); }
    void Release() override { RefCounted::Release(); }

    PDF_ElementType GetType() override { return type; }
    void GetBounds(double* out_bounds) override {
        if (out_bounds) std::memcpy(out_bounds, bounds, sizeof(bounds));
    }
    int GetTextAttributes(void* out_attr) override {
        if (!out_attr) return 0;
        std::memcpy(out_attr, &text, sizeof(text));
        return 1;
    }
    int GetImageAttributes(void* out_attr) override {
        if (!out_attr) return 0;
        std::memcpy(out_attr, &image, sizeof(image));
        return 1;
    }
    int GetPathAttributes(void* out_attr) override {
        if (!out_attr) return 0;
        std::memcpy(out_attr, &path, sizeof(path));
        return 1;
    }
    const char* GetElementText(int* out_length) override;
    unsigned char* GetImageData(size_t* out_size) override;
    unsigned char* GetPathData(size_t* out_size) override;

    ~PdfElementImpl() override {
        // owner is released separately by the page's caller
    }
};

// ============================================================
// PdfPageImpl
// ============================================================
struct PdfPageImpl : public IPdfPage, public RefCounted {
    PdfDocumentImpl* document = nullptr;
    int index = -1;
    FPDF_PAGE page = nullptr;
    FPDF_TEXTPAGE text_page = nullptr;

    PdfPageImpl(PdfDocumentImpl* doc, int idx, FPDF_PAGE p) {
        document = doc;
        index = idx;
        page = p;
        if (document) document->AddRef();
        text_page = FPDFText_LoadPage(p);
    }

    void AddRef() override { RefCounted::AddRef(); }
    void Release() override { RefCounted::Release(); }

    bool Render(int width, int height, int rotation, int flags,
                void* buffer, int stride) override;
    void GetSize(double* width, double* height) override {
        PDFIUM_SCOPE_LOCK;
        double w = 0, h = 0;
        if (document && document->doc) {
            FPDF_GetPageSizeByIndex(document->doc, index, &w, &h);
        }
        if (width) *width = w;
        if (height) *height = h;
    }
    int GetIndex() override { return index; }
    void PageToDevice(int start_x, int start_y, int size_x, int size_y,
                      int rotation, double page_x, double page_y,
                      int* device_x, int* device_y) override;
    void DeviceToPage(int start_x, int start_y, int size_x, int size_y,
                      int rotation, int device_x, int device_y,
                      double* page_x, double* page_y) override;
    const char* GetText(int* out_length) override;
    int SearchText(const char* search_text, int flags, int start_index,
                   int max_results, double* out_bounds, int* out_count) override;
    int GetCharCount() override;
    int GetCharUnicode(int index) override;
    void GetCharBox(int index, double* left, double* top,
                    double* right, double* bottom) override;
    int CountPageElements() override;
    IPdfElement* GetPageElement(int index) override;
    int FindElementsByType(PDF_ElementType type, IPdfElement** out_elements, int max_count) override;

    ~PdfPageImpl() override {
        PDFIUM_SCOPE_LOCK;
        if (text_page) FPDFText_ClosePage(text_page);
        // Do NOT close the FPDF_PAGE here; it's cached in the document.
        if (document) document->Release();
    }
};

// ============================================================
// IPdfDocument implementations
// ============================================================
IPdfPage* PdfDocumentImpl::GetPage(int page_index) {
    PDFIUM_SCOPE_LOCK;
    if (!doc) return nullptr;
    FPDF_PAGE p = getPageRef(page_index);
    if (!p) return nullptr;
    PdfPageImpl* page = new PdfPageImpl(this, page_index, p);
    return page;
}

IPdfOutline* PdfDocumentImpl::GetOutlineRoot() {
    PDFIUM_SCOPE_LOCK;
    if (!doc) return nullptr;
    FPDF_BOOKMARK bm = FPDFBookmark_GetFirstChild(doc, nullptr);
    if (!bm) return nullptr;
    return new PdfOutlineImpl(this, bm);
}

void PdfDocumentImpl::GetSize(int page_index, double* width, double* height) {
    PDFIUM_SCOPE_LOCK;
    double w = 0, h = 0;
    if (doc) FPDF_GetPageSizeByIndex(doc, page_index, &w, &h);
    if (width) *width = w;
    if (height) *height = h;
}

PDF_DocumentStructure* PdfDocumentImpl::GetDocumentStructure() {
    PDFIUM_SCOPE_LOCK;
    std::memset(&structure, 0, sizeof(structure));
    if (!doc) return nullptr;

    structure.page_count = FPDF_GetPageCount(doc);

    // Metadata strings
    structure.producer[0] = 0;
    structure.creator[0] = 0;
    structure.creation_date[0] = 0;
    structure.mod_date[0] = 0;

#ifdef FPDF_GetMetaText
    #define COPY_META(key, dest, cap) \
        do { \
            unsigned long len = FPDF_GetMetaText(doc, key, nullptr, 0); \
            if (len > 0 && len < cap) { \
                std::vector<wchar_t> buf(len); \
                FPDF_GetMetaText(doc, key, buf.data(), len * sizeof(wchar_t)); \
                std::string s = utf16_to_utf8(buf.data(), (int)len); \
                std::memcpy(dest, s.c_str(), s.size()); \
                dest[s.size()] = 0; \
            } \
        } while (0)
    COPY_META("Producer", structure.producer, 256);
    COPY_META("Creator", structure.creator, 256);
    COPY_META("CreationDate", structure.creation_date, 64);
    COPY_META("ModDate", structure.mod_date, 64);
    #undef COPY_META
#endif

    return &structure;
}

const char* PdfDocumentImpl::GetMetaText(const char* key) {
    PDFIUM_SCOPE_LOCK;
    if (!doc || !key) return nullptr;

#ifdef FPDF_GetMetaText
    unsigned long len = FPDF_GetMetaText(doc, key, nullptr, 0);
    if (len > 0) {
        std::vector<wchar_t> buf(len);
        FPDF_GetMetaText(doc, key, buf.data(), len * sizeof(wchar_t));
        std::string utf8 = utf16_to_utf8(buf.data(), (int)len);
        metadata[key] = utf8;
        return metadata[key].c_str();
    }
#endif
    return nullptr;
}

int PdfDocumentImpl::Optimize(const PDF_CompressOptions* options,
                             PDF_CompressProgressCallback progress_cb,
                             void* user_data,
                             IPdfDocument** out_handle) {
    PDFIUM_SCOPE_LOCK;
    if (!out_handle) return PDF_ERR_INVALID_PARAM;
    *out_handle = nullptr;

#ifdef PDFIUM_HAS_EXTENSIONS
    // The extension optimizes the document IN PLACE (returns the input
    // handle). No new handle is produced, so the caller keeps ownership of
    // its existing IPdfDocument. Progress events are forwarded as-is.
    if (FPDF_OptimizeDocument(doc, options, progress_cb, user_data, nullptr)) {
        *out_handle = nullptr;
        return PDF_OK;
    }
    return PDF_ERR_UNKNOWN;
#else
    (void)options;
    (void)progress_cb;
    (void)user_data;
    return PDF_ERR_UNSUPPORTED;
#endif
}

int PdfDocumentImpl::SaveWithCompression(const char* file_path,
                                        const PDF_CompressOptions* options,
                                        PDF_CompressProgressCallback progress_cb,
                                        void* user_data) {
    PDFIUM_SCOPE_LOCK;
    if (!file_path) return PDF_ERR_INVALID_PARAM;

#ifdef PDFIUM_HAS_EXTENSIONS
    return FPDF_SaveWithCompression(doc, file_path, options, progress_cb,
                                    user_data)
               ? PDF_OK
               : PDF_ERR_UNKNOWN;
#else
    (void)options;
    (void)progress_cb;
    (void)user_data;
    return PDF_ERR_UNSUPPORTED;
#endif
}

int PdfDocumentImpl::GetLastCompressStats(PDF_CompressStats* stats) {
    PDFIUM_SCOPE_LOCK;
    if (!stats) return PDF_ERR_INVALID_PARAM;
    std::memset(stats, 0, sizeof(*stats));

#ifdef PDFIUM_HAS_EXTENSIONS
    return FPDF_GetLastCompressStats(stats) ? PDF_OK : PDF_ERR_UNKNOWN;
#else
    return PDF_ERR_UNSUPPORTED;
#endif
}

// ============================================================
// IPdfPage implementations
// ============================================================
static int render_flags_to_pdfium(int flags) {
    int pdfium_flags = FPDF_RENDER_NO_SMOOTHTEXT;
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

bool PdfPageImpl::Render(int width, int height, int rotation, int flags,
                         void* buffer, int stride) {
    PDFIUM_SCOPE_LOCK;
    if (!page || !buffer || width <= 0 || height <= 0) return false;
    if (stride == 0) stride = width * 4;

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRA, buffer, stride);
    if (!bitmap) return false;

    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);

    int pdfium_rotation = 0;
    switch (rotation) {
        case 90: pdfium_rotation = 1; break;
        case 180: pdfium_rotation = 2; break;
        case 270: pdfium_rotation = 3; break;
    }

    int pdfium_flags = render_flags_to_pdfium(flags);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, pdfium_rotation, pdfium_flags);

    FPDFBitmap_Destroy(bitmap);
    return true;
}

void PdfPageImpl::PageToDevice(int start_x, int start_y, int size_x, int size_y,
                               int rotation, double page_x, double page_y,
                               int* device_x, int* device_y) {
    PDFIUM_SCOPE_LOCK;
    if (!page || !device_x || !device_y) return;
    FPDF_PageToDevice(page, start_x, start_y, size_x, size_y, rotation / 90,
                      page_x, page_y, device_x, device_y);
}

void PdfPageImpl::DeviceToPage(int start_x, int start_y, int size_x, int size_y,
                               int rotation, int device_x, int device_y,
                               double* page_x, double* page_y) {
    PDFIUM_SCOPE_LOCK;
    if (!page || !page_x || !page_y) return;
    FPDF_DeviceToPage(page, start_x, start_y, size_x, size_y, rotation / 90,
                      device_x, device_y, page_x, page_y);
}

const char* PdfPageImpl::GetText(int* out_length) {
    PDFIUM_SCOPE_LOCK;
    if (out_length) *out_length = 0;
    if (!text_page) return str_dup("");

    int len = FPDFText_CountChars(text_page);
    if (len <= 0) return str_dup("");

    std::vector<unsigned short> wbuffer(len + 1);
    int actual = FPDFText_GetText(text_page, 0, len, wbuffer.data());
    wbuffer[actual] = 0;

    std::string utf8;
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wstr;
        wstr.reserve(actual);
        for (int i = 0; i < actual; ++i) {
            unsigned short us = wbuffer[i];
            if (us == 0) break;
            wstr.push_back(static_cast<wchar_t>(us));
        }
        utf8 = converter.to_bytes(wstr);
    } catch (...) {
        utf8 = "";
    }
    if (out_length) *out_length = static_cast<int>(utf8.size());
    return str_dup(utf8);
}

int PdfPageImpl::GetCharCount() {
    PDFIUM_SCOPE_LOCK;
    if (!text_page) return 0;
    return FPDFText_CountChars(text_page);
}

int PdfPageImpl::GetCharUnicode(int index) {
    PDFIUM_SCOPE_LOCK;
    if (!text_page || index < 0) return 0;
    int count = FPDFText_CountChars(text_page);
    if (index >= count) return 0;
    return static_cast<int>(FPDFText_GetUnicode(text_page, index));
}

void PdfPageImpl::GetCharBox(int index, double* left, double* top,
                             double* right, double* bottom) {
    PDFIUM_SCOPE_LOCK;
    if (left) *left = 0;
    if (top) *top = 0;
    if (right) *right = 0;
    if (bottom) *bottom = 0;
    if (!text_page || index < 0) return;
    int count = FPDFText_CountChars(text_page);
    if (index >= count) return;
    // PDFium's GetCharBox reports (left, right, bottom, top).
    double l = 0, r = 0, b = 0, t = 0;
    if (!FPDFText_GetCharBox(text_page, index, &l, &r, &b, &t)) return;
    if (left) *left = l;
    if (top) *top = t;
    if (right) *right = r;
    if (bottom) *bottom = b;
}

// Collects the bounding box of the current search result.
// Uses FPDFText_GetSchResultRect when the API is present; otherwise falls back
// to merging per-character boxes via FPDFText_GetCharBox.
static bool get_search_rect(FPDF_SCHHANDLE handle, FPDF_TEXTPAGE text_page, double* out) {
#ifdef FPDFText_GetSchResultRect
    (void)text_page;
    FPDFText_GetSchResultRect(handle, out, out + 1, out + 2, out + 3);
    return true;
#else
    int idx = FPDFText_GetSchResultIndex(handle);
    int count = FPDFText_GetSchCount(handle);
    bool first = true;
    double l = 0, t = 0, r = 0, b = 0;
    for (int i = 0; i < count; ++i) {
        double x1, y1, x2, y2;
        if (!FPDFText_GetCharBox(text_page, idx + i, &x1, &x2, &y1, &y2)) continue;
        if (first) {
            l = x1; t = y1; r = x2; b = y2;
            first = false;
        } else {
            l = std::min(l, x1);
            r = std::max(r, x2);
            t = std::min(t, y1);
            b = std::max(b, y2);
        }
    }
    if (first) return false;
    out[0] = l; out[1] = t; out[2] = r; out[3] = b;
    return true;
#endif
}

int PdfPageImpl::SearchText(const char* search_text, int flags, int start_index,
                            int max_results, double* out_bounds, int* out_count) {
    PDFIUM_SCOPE_LOCK;
    if (!search_text || !out_bounds || !out_count) return PDF_ERR_INVALID_PARAM;
    *out_count = 0;
    if (!text_page) return PDF_ERR_UNKNOWN;

    int search_flags = 0;
    if (flags & PDF_SEARCH_MATCH_CASE) search_flags |= FPDF_MATCHCASE;
    if (flags & PDF_SEARCH_WHOLE_WORD) search_flags |= FPDF_MATCHWHOLEWORD;

    // Convert search text to UTF-16 code units (FPDF_WIDESTRING)
    std::vector<unsigned short> wsearch = utf8_to_utf16(search_text);
    if (wsearch.empty()) return PDF_ERR_UNKNOWN;

    FPDF_SCHHANDLE handle = FPDFText_FindStart(text_page, wsearch.data(), search_flags, start_index);
    if (!handle) {
        return PDF_OK;
    }

    int found = 0;
    while (found < max_results && FPDFText_FindNext(handle)) {
        int index = FPDFText_GetSchResultIndex(handle);
        if (index < start_index) continue;

        double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        double coords[4];
        if (get_search_rect(handle, text_page, coords)) {
            x1 = coords[0]; y1 = coords[1]; x2 = coords[2]; y2 = coords[3];
        }
        int base = found * 4;
        out_bounds[base] = x1;
        out_bounds[base + 1] = y1;
        out_bounds[base + 2] = x2;
        out_bounds[base + 3] = y2;
        found++;
    }

    FPDFText_FindClose(handle);
    *out_count = found;
    return PDF_OK;
}

int PdfPageImpl::CountPageElements() {
    PDFIUM_SCOPE_LOCK;
#ifdef FPDF_CreateElementIterator
    extern "C" { int FPDF_CountPageElements(void* page); }
    return FPDF_CountPageElements(page);
#else
    return 0;
#endif
}

IPdfElement* PdfPageImpl::GetPageElement(int index) {
    PDFIUM_SCOPE_LOCK;
#ifdef FPDF_CreateElementIterator
    extern "C" { void* FPDF_GetPageElement(void* page, int index); }
    void* ext = FPDF_GetPageElement(page, index);
    if (!ext) return nullptr;

    PdfElementImpl* element = new PdfElementImpl();
    element->owner = this;
    element->index = index;
    element->ext_element = ext;

    // Extract type + bounds from the extension element.
    extern "C" {
        int  FPDF_GetElementType(void* e);
        void FPDF_GetElementBounds(void* e, double* b);
        int  FPDF_GetElementTextAttributes(void* e, void* a);
        int  FPDF_GetElementImageAttributes(void* e, void* a);
        int  FPDF_GetElementPathAttributes(void* e, void* a);
    }
    element->type = static_cast<PDF_ElementType>(FPDF_GetElementType(ext));
    FPDF_GetElementBounds(ext, element->bounds);
    // Note: our PDF_* structs are layout-compatible with FPDF_* attrs.
    FPDF_GetElementTextAttributes(ext, &element->text);
    FPDF_GetElementImageAttributes(ext, &element->image);
    FPDF_GetElementPathAttributes(ext, &element->path);
    return element;
#else
    (void)index;
    return nullptr;
#endif
}

int PdfPageImpl::FindElementsByType(PDF_ElementType type, IPdfElement** out_elements, int max_count) {
    PDFIUM_SCOPE_LOCK;
    if (!out_elements || max_count <= 0) return 0;

#ifdef FPDF_CreateElementIterator
    extern "C" { int FPDF_FindElementsByType(void* page, int type, void** out, int max); }
    std::vector<void*> raw(max_count);
    int count = FPDF_FindElementsByType(page, static_cast<int>(type),
                                        raw.data(), max_count);
    for (int i = 0; i < count; ++i) {
        if (raw[i]) {
            PdfElementImpl* element = new PdfElementImpl();
            element->owner = this;
            element->index = -1;
            element->ext_element = raw[i];
            extern "C" {
                int  FPDF_GetElementType(void* e);
                void FPDF_GetElementBounds(void* e, double* b);
                int  FPDF_GetElementTextAttributes(void* e, void* a);
                int  FPDF_GetElementImageAttributes(void* e, void* a);
                int  FPDF_GetElementPathAttributes(void* e, void* a);
            }
            element->type = static_cast<PDF_ElementType>(FPDF_GetElementType(raw[i]));
            FPDF_GetElementBounds(raw[i], element->bounds);
            FPDF_GetElementTextAttributes(raw[i], &element->text);
            FPDF_GetElementImageAttributes(raw[i], &element->image);
            FPDF_GetElementPathAttributes(raw[i], &element->path);
            out_elements[i] = element;
        } else {
            out_elements[i] = nullptr;
        }
    }
    return count;
#else
    (void)type;
    return 0;
#endif
}

// ============================================================
// IPdfElement implementations
// ============================================================
const char* PdfElementImpl::GetElementText(int* out_length) {
    PDFIUM_SCOPE_LOCK;
    if (out_length) *out_length = 0;
#ifdef FPDF_CreateElementIterator
    if (type == PDF_ELEMENT_TEXT) {
        extern "C" { const char* FPDF_GetElementText(void* e, int* out_len); }
        const char* ext = FPDF_GetElementText(ext_element, out_length);
        if (ext) return ext;
    }
#endif
    return nullptr;
}

unsigned char* PdfElementImpl::GetImageData(size_t* out_size) {
    PDFIUM_SCOPE_LOCK;
    if (out_size) *out_size = 0;
#ifdef FPDF_CreateElementIterator
    if (type == PDF_ELEMENT_IMAGE) {
        extern "C" { unsigned char* FPDF_GetElementImageData(void* e, size_t* out_sz); }
        return FPDF_GetElementImageData(ext_element, out_size);
    }
#endif
    return nullptr;
}

unsigned char* PdfElementImpl::GetPathData(size_t* out_size) {
    PDFIUM_SCOPE_LOCK;
    if (out_size) *out_size = 0;
#ifdef FPDF_CreateElementIterator
    if (type == PDF_ELEMENT_PATH) {
        extern "C" { unsigned char* FPDF_GetElementPathData(void* e, size_t* out_sz); }
        return FPDF_GetElementPathData(ext_element, out_size);
    }
#endif
    return nullptr;
}

} // namespace

// ============================================================
// Exported C API
// ============================================================
PDFWRAPPER_API int PDFWRAPPER_CALL PDF_InitLibrary() {
    PDFIUM_SCOPE_LOCK;
    ensure_library_initialized();
    return PDF_OK;
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_DestroyLibrary() {
    PDFIUM_SCOPE_LOCK;
    if (!g_pdfium_initialized) return;
    FPDF_DestroyLibrary();
    g_pdfium_initialized = false;
}

PDFWRAPPER_API IPdfDocument* PDFWRAPPER_CALL PDF_CreateDocument(
    const char* file_path,
    const char* password,
    int* out_error
) {
    PDFIUM_SCOPE_LOCK;
    if (!file_path) {
        if (out_error) *out_error = PDF_ERR_INVALID_PARAM;
        return nullptr;
    }
    if (!g_pdfium_initialized) ensure_library_initialized();

    FPDF_DOCUMENT doc = FPDF_LoadDocument(file_path, password);
    if (!doc) {
        unsigned long err = FPDF_GetLastError();
        int code = PDF_ERR_FILE_NOT_FOUND;
        switch (err) {
            case FPDF_ERR_PASSWORD: code = PDF_ERR_INVALID_PASSWORD; break;
            case FPDF_ERR_FORMAT: code = PDF_ERR_FORMAT; break;
            case FPDF_ERR_SECURITY: code = PDF_ERR_FORMAT; break;
            case FPDF_ERR_FILE: code = PDF_ERR_FILE_NOT_FOUND; break;
            default: code = PDF_ERR_UNKNOWN; break;
        }
        if (out_error) *out_error = code;
        return nullptr;
    }

    PdfDocumentImpl* wrapper = new PdfDocumentImpl();
    wrapper->doc = doc;
    if (out_error) *out_error = PDF_OK;
    return wrapper;
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_FreeString(const char* str) {
    if (str) std::free(const_cast<char*>(str));
}

PDFWRAPPER_API void PDFWRAPPER_CALL PDF_FreeElementData(void* data) {
    if (!data) return;
    PDFIUM_SCOPE_LOCK;
#ifdef FPDF_CreateElementIterator
    extern "C" { void FPDF_FreeElementData(void* data); }
    FPDF_FreeElementData(data);
#endif
}
