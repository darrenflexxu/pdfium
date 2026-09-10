// pdfium_python/bindings.cpp - pybind11 bindings for pdfium_wrapper.
//
// Maps the COM-style interfaces (IPdfDocument/IPdfPage/IPdfElement/IPdfOutline)
// onto Python classes. Lifetime is managed with std::shared_ptr using a
// custom deleter that calls Release(), so PDFium handles are freed exactly
// when the last Python reference disappears. Strings returned in allocated
// UTF-8 buffers (PDF_FreeString) are copied into std::string/PyBytes and
// released immediately.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "pdfium_wrapper.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace py = pybind11;

namespace {

// Wrap a fresh COM reference in shared_ptr; the deleter releases the handle.
template <typename IFace>
std::shared_ptr<IFace> AdoptRef(IFace* p) {
    if (!p) return nullptr;
    return std::shared_ptr<IFace>(p, [](IFace* q) { q->Release(); });
}

// ---------------------------------------------------------------------------
// Python-facing wrapper structs (own the shared_ptr refs)
// ---------------------------------------------------------------------------

struct PyDocument {
    std::shared_ptr<IPdfDocument> p;
};

struct PyPage {
    std::shared_ptr<IPdfPage> p;
};

struct PyElement {
    std::shared_ptr<IPdfElement> p;
};

struct PyOutline {
    std::shared_ptr<IPdfOutline> p;
};

// ---------------------------------------------------------------------------
// PDF_CompressOptions: convert field flags into the C struct.
// ---------------------------------------------------------------------------

PDF_CompressOptions MakeDefaultOptions() {
    // Mirrors FPDF_CompressOptionsInit defaults in the extension.
    PDF_CompressOptions o;
    std::memset(&o, 0, sizeof(o));
    o.flags = PDF_COMPRESS_FLATE | PDF_COMPRESS_OBJECT_STREAMS |
              PDF_COMPRESS_REMOVE_UNUSED | PDF_COMPRESS_LINEARIZE;
    o.image_quality = 90;
    o.image_dpi_threshold = 300;
    o.min_image_dpi = 150;
    o.font_subset_threshold = 80;
    return o;
}

// ---------------------------------------------------------------------------
// PDF_FreeString helpers (memory safety: always release allocated buffers).
// ---------------------------------------------------------------------------

std::string TakeString(const char* text) {
    std::string out = text ? std::string(text) : std::string();
    if (text) PDF_FreeString(text);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// PyDocument
// ---------------------------------------------------------------------------

static std::shared_ptr<PyDocument> PyDocumentCreate(const std::string& path,
                                                    const std::string& password) {
    int err = 0;
    IPdfDocument* raw = PDF_CreateDocument(
        path.c_str(), password.empty() ? nullptr : password.c_str(), &err);
    if (!raw)
        throw std::runtime_error("cannot open PDF (error " +
                                 std::to_string(err) + ")");
    auto doc = std::make_shared<PyDocument>();
    doc->p = AdoptRef(raw);
    return doc;
}

static int PyDocumentPageCount(std::shared_ptr<PyDocument> self) {
    return self->p->GetPageCount();
}

static std::shared_ptr<PyPage> PyDocumentGetPage(std::shared_ptr<PyDocument> self,
                                                 int index) {
    IPdfPage* raw = self->p->GetPage(index);
    if (!raw) throw std::runtime_error("invalid page index");
    auto page = std::make_shared<PyPage>();
    page->p = AdoptRef(raw);
    return page;
}

static py::dict PyDocumentGetDocumentStructure(std::shared_ptr<PyDocument> self) {
    PDF_DocumentStructure* ds = self->p->GetDocumentStructure();
    py::dict d;
    if (!ds) return d;
    d["page_count"] = ds->page_count;
    d["object_count"] = ds->object_count;
    d["font_count"] = ds->font_count;
    d["image_count"] = ds->image_count;
    d["form_field_count"] = ds->form_field_count;
    d["annotation_count"] = ds->annotation_count;
    d["bookmark_count"] = ds->bookmark_count;
    d["file_size"] = ds->file_size;
    d["creator"] = std::string(ds->creator);
    d["producer"] = std::string(ds->producer);
    d["creation_date"] = std::string(ds->creation_date);
    d["mod_date"] = std::string(ds->mod_date);
    return d;
}

static std::shared_ptr<PyOutline> PyDocumentGetOutlineRoot(
    std::shared_ptr<PyDocument> self) {
    IPdfOutline* raw = self->p->GetOutlineRoot();
    if (!raw) return nullptr;
    auto out = std::make_shared<PyOutline>();
    out->p = AdoptRef(raw);
    return out;
}

static bool PyDocumentOptimize(std::shared_ptr<PyDocument> self,
                               const PDF_CompressOptions& opts) {
    IPdfDocument* out = nullptr;
    return self->p->Optimize(&opts, nullptr, nullptr, &out) == PDF_OK;
}

static bool PyDocumentSaveWithCompression(std::shared_ptr<PyDocument> self,
                                          const std::string& file_path,
                                          const PDF_CompressOptions& opts) {
    return self->p->SaveWithCompression(file_path.c_str(), &opts, nullptr,
                                        nullptr) == PDF_OK;
}

static py::dict PyDocumentGetLastCompressStats(std::shared_ptr<PyDocument> self) {
    PDF_CompressStats stats;
    std::memset(&stats, 0, sizeof(stats));
    if (self->p->GetLastCompressStats(&stats) != PDF_OK) return py::dict();
    py::dict d;
    d["original_size"] = stats.original_size;
    d["compressed_size"] = stats.compressed_size;
    d["compression_ratio"] = stats.compression_ratio;
    d["objects_removed"] = stats.objects_removed;
    d["images_recompressed"] = stats.images_recompressed;
    d["fonts_subsets"] = stats.fonts_subsets;
    return d;
}

// ---------------------------------------------------------------------------
// PyPage
// ---------------------------------------------------------------------------

static py::bytes PyPageRender(std::shared_ptr<PyPage> self, int width,
                              int height, int rotation, int flags) {
    if (width <= 0 || height <= 0) throw std::runtime_error("bad render size");
    std::vector<unsigned char> buf(static_cast<size_t>(width) * height * 4);
    if (!self->p->Render(width, height, rotation, flags, buf.data(), width * 4))
        throw std::runtime_error("render failed");
    return py::bytes(reinterpret_cast<const char*>(buf.data()), buf.size());
}

static py::tuple PyPageGetSize(std::shared_ptr<PyPage> self) {
    double w = 0.0, h = 0.0;
    self->p->GetSize(&w, &h);
    return py::make_tuple(w, h);
}

static std::string PyPageGetText(std::shared_ptr<PyPage> self) {
    int len = 0;
    const char* text = self->p->GetText(&len);
    std::string out = text ? std::string(text, static_cast<size_t>(len))
                           : std::string();
    if (text) PDF_FreeString(text);
    return out;
}

static py::list PyPageSearchText(std::shared_ptr<PyPage> self,
                                 const std::string& query, int flags,
                                 int start_index, int max_results) {
    std::vector<double> bounds(std::max(0, max_results) * 4);
    int count = 0;
    int rc = self->p->SearchText(query.c_str(), flags, start_index,
                                 max_results, bounds.data(), &count);
    if (rc != PDF_OK) throw std::runtime_error("search failed");
    py::list out;
    for (int i = 0; i < count; ++i)
        out.append(py::make_tuple(bounds[i * 4], bounds[i * 4 + 1],
                                  bounds[i * 4 + 2], bounds[i * 4 + 3]));
    return out;
}

static int PyPageGetCharCount(std::shared_ptr<PyPage> self) {
    return self->p->GetCharCount();
}

static int PyPageGetCharUnicode(std::shared_ptr<PyPage> self, int index) {
    return self->p->GetCharUnicode(index);
}

static py::tuple PyPageGetCharBox(std::shared_ptr<PyPage> self, int index) {
    double l = 0, t = 0, r = 0, b = 0;
    self->p->GetCharBox(index, &l, &t, &r, &b);
    return py::make_tuple(l, t, r, b);
}

static int PyPageCountPageElements(std::shared_ptr<PyPage> self) {
    return self->p->CountPageElements();
}

static std::shared_ptr<PyElement> PyPageGetPageElement(
    std::shared_ptr<PyPage> self, int index) {
    IPdfElement* raw = self->p->GetPageElement(index);
    if (!raw) throw std::runtime_error("invalid element index");
    auto el = std::make_shared<PyElement>();
    el->p = AdoptRef(raw);
    return el;
}

static py::list PyPageFindElementsByType(std::shared_ptr<PyPage> self,
                                         int type_value, int max_count) {
    std::vector<IPdfElement*> got(std::max(0, max_count), nullptr);
    int n = self->p->FindElementsByType(
        static_cast<PDF_ElementType>(type_value), got.data(), max_count);
    py::list out;
    for (int i = 0; i < n; ++i) {
        auto el = std::make_shared<PyElement>();
        el->p = AdoptRef(got[i]);
        out.append(el);
    }
    return out;
}

// ---------------------------------------------------------------------------
// PyElement
// ---------------------------------------------------------------------------

static int PyElementGetType(std::shared_ptr<PyElement> self) {
    return static_cast<int>(self->p->GetType());
}

static py::tuple PyElementGetBounds(std::shared_ptr<PyElement> self) {
    double b[4] = {0, 0, 0, 0};
    self->p->GetBounds(b);
    return py::make_tuple(b[0], b[1], b[2], b[3]);
}

static std::string PyElementGetText(std::shared_ptr<PyElement> self) {
    int len = 0;
    const char* text = self->p->GetElementText(&len);
    return TakeString(text);
}

// ---------------------------------------------------------------------------
// PyOutline
// ---------------------------------------------------------------------------

static std::string PyOutlineGetTitle(std::shared_ptr<PyOutline> self) {
    int len = 0;
    const char* title = self->p->GetTitle(&len);
    return TakeString(title);
}

static std::shared_ptr<PyOutline> PyOutlineGetFirstChild(
    std::shared_ptr<PyOutline> self) {
    IPdfOutline* raw = self->p->GetFirstChild();
    if (!raw) return nullptr;
    auto out = std::make_shared<PyOutline>();
    out->p = AdoptRef(raw);
    return out;
}

static std::shared_ptr<PyOutline> PyOutlineGetNextSibling(
    std::shared_ptr<PyOutline> self) {
    IPdfOutline* raw = self->p->GetNextSibling();
    if (!raw) return nullptr;
    auto out = std::make_shared<PyOutline>();
    out->p = AdoptRef(raw);
    return out;
}

static int PyOutlineGetDestinationPage(std::shared_ptr<PyOutline> self) {
    return self->p->GetDestinationPage();
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

static void FinalizePdfium() { PDF_DestroyLibrary(); }

PYBIND11_MODULE(pdfium, m) {
    m.doc() =
        "PDFium wrapper bindings: open, render, inspect and compress PDFs.";

    if (PDF_InitLibrary() != PDF_OK)
        throw std::runtime_error("PDF_InitLibrary failed");
    Py_AtExit(&FinalizePdfium);

    // Compression / render / search flags.
    m.attr("FLATE") = py::int_(static_cast<int>(PDF_COMPRESS_FLATE));
    m.attr("OBJECT_STREAMS") = py::int_(static_cast<int>(PDF_COMPRESS_OBJECT_STREAMS));
    m.attr("IMAGES") = py::int_(static_cast<int>(PDF_COMPRESS_IMAGES));
    m.attr("FONTS") = py::int_(static_cast<int>(PDF_COMPRESS_FONTS));
    m.attr("REMOVE_UNUSED") = py::int_(static_cast<int>(PDF_COMPRESS_REMOVE_UNUSED));
    m.attr("LINEARIZE") = py::int_(static_cast<int>(PDF_COMPRESS_LINEARIZE));
    m.attr("RENDER_ANNOTATIONS") = py::int_(static_cast<int>(PDF_RENDER_ANNOTATIONS));
    m.attr("RENDER_GRAYSCALE") = py::int_(static_cast<int>(PDF_RENDER_GRAYSCALE));
    m.attr("SEARCH_MATCH_CASE") = py::int_(static_cast<int>(PDF_SEARCH_MATCH_CASE));
    m.attr("SEARCH_WHOLE_WORD") = py::int_(static_cast<int>(PDF_SEARCH_WHOLE_WORD));
    m.attr("ELEMENT_TEXT") = py::int_(static_cast<int>(PDF_ELEMENT_TEXT));
    m.attr("ELEMENT_IMAGE") = py::int_(static_cast<int>(PDF_ELEMENT_IMAGE));
    m.attr("ELEMENT_PATH") = py::int_(static_cast<int>(PDF_ELEMENT_PATH));

    py::class_<PDF_CompressOptions>(m, "CompressOptions",
                                    "Compression settings mirroring "
                                    "FPDF_CompressOptions with defaults.")
        .def(py::init(&MakeDefaultOptions))
        .def_readwrite("flags", &PDF_CompressOptions::flags)
        .def_readwrite("image_quality", &PDF_CompressOptions::image_quality)
        .def_readwrite("image_dpi_threshold",
                       &PDF_CompressOptions::image_dpi_threshold)
        .def_readwrite("min_image_dpi", &PDF_CompressOptions::min_image_dpi)
        .def_readwrite("font_subset_threshold",
                       &PDF_CompressOptions::font_subset_threshold)
        .def_readwrite("remove_annotations",
                       &PDF_CompressOptions::remove_annotations)
        .def_readwrite("remove_forms", &PDF_CompressOptions::remove_forms)
        .def_readwrite("remove_bookmarks",
                       &PDF_CompressOptions::remove_bookmarks)
        .def_readwrite("remove_metadata",
                       &PDF_CompressOptions::remove_metadata);

    py::class_<PyPage, std::shared_ptr<PyPage>>(m, "PdfPage",
                                                "A single PDF page.")
        .def("render", &PyPageRender, py::arg("width"), py::arg("height"),
             py::arg("rotation") = 0, py::arg("flags") = 0,
             "Render the page; returns raw BGRA bytes (w*h*4).")
        .def("get_size", &PyPageGetSize, "Return (width, height) in points.")
        .def("get_text", &PyPageGetText, "Extract all text on the page.")
        .def("search_text", &PyPageSearchText, py::arg("query"),
             py::arg("flags") = 0, py::arg("start_index") = 0,
             py::arg("max_results") = 10,
             "Search text; returns list of (left, top, right, bottom).")
        .def("get_char_count", &PyPageGetCharCount)
        .def("get_char_unicode", &PyPageGetCharUnicode, py::arg("index"))
        .def("get_char_box", &PyPageGetCharBox, py::arg("index"),
             "Return (left, top, right, bottom) for a character.")
        .def("count_page_elements", &PyPageCountPageElements)
        .def("get_page_element", &PyPageGetPageElement, py::arg("index"))
        .def("find_elements_by_type", &PyPageFindElementsByType,
             py::arg("element_type"), py::arg("max_count") = 32);

    py::class_<PyElement, std::shared_ptr<PyElement>>(m, "PdfElement",
                                                      "A page element.")
        .def("get_type", &PyElementGetType)
        .def("get_bounds", &PyElementGetBounds,
             "Return (x1, y1, x2, y2) in page coordinates.")
        .def("get_text", &PyElementGetText,
             "Text content for text elements, else empty string.");

    py::class_<PyOutline, std::shared_ptr<PyOutline>>(m, "PdfOutline",
                                                      "A bookmark node.")
        .def("get_title", &PyOutlineGetTitle)
        .def("get_first_child", &PyOutlineGetFirstChild)
        .def("get_next_sibling", &PyOutlineGetNextSibling)
        .def("get_destination_page", &PyOutlineGetDestinationPage);

    py::class_<PyDocument, std::shared_ptr<PyDocument>>(m, "PdfDocument",
                                                        "An open PDF file.")
        .def_static("create", &PyDocumentCreate, py::arg("path"),
                    py::arg("password") = std::string(),
                    "Open a PDF file; raises on failure.")
        .def("get_page_count", &PyDocumentPageCount)
        .def("get_page", &PyDocumentGetPage, py::arg("index"))
        .def("get_document_structure", &PyDocumentGetDocumentStructure)
        .def("get_outline_root", &PyDocumentGetOutlineRoot)
        .def("optimize", &PyDocumentOptimize, py::arg("options"),
             "Optimize the open document in place; returns True on success.")
        .def("save_with_compression", &PyDocumentSaveWithCompression,
             py::arg("file_path"), py::arg("options"),
             "Save an optimized copy; returns True on success.")
        .def("get_last_compress_stats", &PyDocumentGetLastCompressStats);
}