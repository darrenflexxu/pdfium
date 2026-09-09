#include "fpdf_ext_compression.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/page/cpdf_image.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fxge/dib/cfx_dibbase.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/span.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "fpdfview.h"
#include "zlib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

namespace {

// Global compression stats.
FPDF_CompressStats g_last_compress_stats = {0};

// Clamp and forward a progress notification to the callback, if any.
void ReportProgress(FPDF_CompressProgressCallback cb, void* user_data,
                    int progress, const char* status) {
    if (!cb) return;
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    cb(progress, status, user_data);
}

// Stage status strings (UTF-8) shown in the progress report.
constexpr const char* kStageStart = "\xE5\xBC\x80\xE5\xA7\x8B\xE4\xBC\x98\xE5\x8C\x96...";   // 开始优化...
constexpr const char* kStageImages = "\xE5\xA4\x84\xE7\x90\x86\xE5\x9B\xBE\xE5\x83\x8F...";  // 处理图像...
constexpr const char* kStageTrim = "\xE5\x89\x94\xE9\x99\xA4\xE5\x86\x97\xE4\xBD\x99...";    // 剔除冗余...
constexpr const char* kStageSave = "\xE4\xBF\x9D\xE5\xAD\x98\xE6\x96\x87\xE4\xBB\xB6...";    // 保存文件...
constexpr const char* kStageDone = "\xE4\xBF\x9D\xE5\xAD\x98\xE5\xAE\x8C\xE6\x88\x90";      // 保存完成

// Flate-compress a byte span. Returns an empty vector on failure.
std::vector<uint8_t> FlateCompress(pdfium::span<const uint8_t> data) {
    if (data.empty()) return {};
    std::vector<uint8_t> out(compressBound(data.size()));
    uLongf dest_len = out.size();
    int ret = compress2(out.data(), &dest_len, data.data(), data.size(),
                        Z_BEST_COMPRESSION);
    if (ret != Z_OK) return {};
    out.resize(dest_len);
    return out;
}

bool StreamHasFilter(const CPDF_Stream* stream) {
    if (!stream) return false;
    const CPDF_Dictionary* dict = stream->GetDict().Get();
    if (!dict) return false;
    return !!dict->GetObjectFor("Filter");
}

// Read the unfiltered-on-disk bytes of a stream.
std::vector<uint8_t> StreamRawBytes(const CPDF_Stream* stream) {
    if (!stream) return {};
    if (stream->IsMemoryBased()) {
        pdfium::span<const uint8_t> span = stream->GetInMemoryRawData();
        return std::vector<uint8_t>(span.begin(), span.end());
    }
    fxcrt::DataVector<uint8_t> raw = stream->ReadAllRawData();
    return std::vector<uint8_t>(raw.begin(), raw.end());
}

// Compress an in-place stream with FlateDecode. Returns the number of bytes
// saved, or 0 when nothing was applied.
size_t FlateStream(CPDF_Stream* stream) {
    if (!stream || StreamHasFilter(stream)) return 0;

    std::vector<uint8_t> raw = StreamRawBytes(stream);
    if (raw.empty()) return 0;

    std::vector<uint8_t> comp = FlateCompress(pdfium::span(raw));
    if (comp.empty() || comp.size() >= raw.size()) return 0;

    stream->SetData(pdfium::span(comp));

    CPDF_Dictionary* dict =
        const_cast<CPDF_Dictionary*>(stream->GetDict().Get());
    if (dict) {
        dict->RemoveFor("DecodeParms");
        dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
    }
    return raw.size() - comp.size();
}

// ---------------------------------------------------------------------------
// Image processing
// ---------------------------------------------------------------------------

// Pixel accessors for the supported DIB layouts. This build uses BGR(A)
// byte order for 24/32 bpp bitmaps (see CFX_DIBitmap).
bool SampleRgbPixel(const CFX_DIBBase* dib, int x, int y, uint8_t out[3]) {
    if (!dib || x < 0 || y < 0 || x >= dib->GetWidth() || y >= dib->GetHeight())
        return false;

    const int bpp = dib->GetBPP();
    pdfium::span<const uint8_t> line = dib->GetScanline(y);
    if (bpp == 8) {
        out[0] = out[1] = out[2] = line[x];
        return true;
    }
    if (bpp == 24) {
        size_t off = static_cast<size_t>(x) * 3;
        if (off + 3 > line.size()) return false;
        out[0] = line[off + 2];  // R
        out[1] = line[off + 1];  // G
        out[2] = line[off + 0];  // B
        return true;
    }
    if (bpp == 32) {
        size_t off = static_cast<size_t>(x) * 4;
        if (off + 4 > line.size()) return false;
        out[0] = line[off + 2];  // R
        out[1] = line[off + 1];  // G
        out[2] = line[off + 0];  // B
        return true;
    }
    // 1bpp / masks / other formats.
    return false;
}

void SampleRgbBilinear(const CFX_DIBBase* dib, float fx, float fy,
                       uint8_t out[3]) {
    const int w = dib->GetWidth();
    const int h = dib->GetHeight();
    int x0 = std::floor(fx);
    int y0 = std::floor(fy);
    const float dx = fx - x0;
    const float dy = fy - y0;
    if (x0 >= w - 1) x0 = w - 2;
    if (y0 >= h - 1) y0 = h - 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;

    uint8_t c00[3], c10[3], c01[3], c11[3];
    if (!SampleRgbPixel(dib, x0, y0, c00) ||
        !SampleRgbPixel(dib, x0 + 1, y0, c10) ||
        !SampleRgbPixel(dib, x0, y0 + 1, c01) ||
        !SampleRgbPixel(dib, x0 + 1, y0 + 1, c11)) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    for (int c = 0; c < 3; ++c) {
        const float top = c00[c] * (1.0f - dx) + c10[c] * dx;
        const float bot = c01[c] * (1.0f - dx) + c11[c] * dx;
        out[c] = static_cast<uint8_t>(top * (1.0f - dy) + bot * dy + 0.5f);
    }
}

// Downsample + re-encode a single image stream in place. Returns true when
// the stream was rewritten.
bool DownsampleImage(CPDF_Image* image, CPDF_Stream* stream,
                     const FPDF_CompressOptions* options, float display_points) {
    const CPDF_Dictionary* dict = stream->GetDict().Get();
    if (!dict) return false;

    // Skip images that depend on data we cannot safely reproduce.
    static const char* kBlockingKeys[] = {"SMask",      "Mask",
                                          "ImageMask",  "Decode",
                                          "Alternates", "DecodeParms"};
    for (const char* key : kBlockingKeys) {
        if (dict->GetObjectFor(key)) return false;
    }

    int width = dict->GetIntegerFor("Width");
    int height = dict->GetIntegerFor("Height");
    if (width <= 0 || height <= 0 || display_points <= 0) return false;

    const double dpi = width / (display_points / 72.0);
    if (dpi <= options->image_dpi_threshold) return false;

    const float scale = static_cast<float>(options->min_image_dpi) / dpi;
    if (scale >= 1.0f) return false;

    int new_width = std::max(1, static_cast<int>(std::lround(width * scale)));
    int new_height = std::max(1, static_cast<int>(std::lround(height * scale)));
    if (new_width >= width || new_height >= height) return false;

    RetainPtr<CFX_DIBBase> dib = image->LoadDIBBase();
    const int bpp = dib ? dib->GetBPP() : 0;
    if (bpp != 8 && bpp != 24 && bpp != 32) return false;

    const size_t dest_size = static_cast<size_t>(new_width) * new_height * 3;
    std::vector<uint8_t> dest(dest_size);
    const float sx = static_cast<float>(width) / new_width;
    const float sy = static_cast<float>(height) / new_height;
    for (int ty = 0; ty < new_height; ++ty) {
        const float fy = (ty + 0.5f) * sy - 0.5f;
        uint8_t* row = dest.data() + static_cast<size_t>(ty) * new_width * 3;
        for (int tx = 0; tx < new_width; ++tx) {
            const float fx = (tx + 0.5f) * sx - 0.5f;
            SampleRgbBilinear(dib.Get(), fx, fy, row + static_cast<size_t>(tx) * 3);
        }
    }

    std::vector<uint8_t> comp = FlateCompress(pdfium::span(dest));
    if (comp.empty()) return false;

    CPDF_Dictionary* mdict = const_cast<CPDF_Dictionary*>(dict);
    mdict->RemoveFor("Filter");
    mdict->RemoveFor("DecodeParms");
    mdict->SetNewFor<CPDF_Number>("Width", new_width);
    mdict->SetNewFor<CPDF_Number>("Height", new_height);
    mdict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceRGB");
    mdict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);
    mdict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");

    stream->SetData(pdfium::span(comp));
    return true;
}

// Process all images of all pages: recompress in place, and down-sample any
// image above the configured DPI threshold. Reports 1-60% progress after
// each page (image stage of the compress pipeline).
int ProcessImages(CPDF_Document* doc, const FPDF_CompressOptions* options,
                  FPDF_CompressProgressCallback progress_cb,
                  void* user_data) {
    int processed = 0;
    const int total_pages = doc->GetPageCount();
    for (int i = 0; i < total_pages; ++i) {
        const int pct = (i + 1) * 60 / total_pages;
        ReportProgress(progress_cb, user_data, pct, kStageImages);
        FPDF_PAGE fpage = FPDF_LoadPage(reinterpret_cast<FPDF_DOCUMENT>(doc), i);
        if (!fpage) continue;
        CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
        if (!page) { FPDF_ClosePage(fpage); continue; }

        for (int j = 0; j < page->GetPageObjectCount(); ++j) {
            CPDF_PageObject* obj = page->GetPageObjectByIndex(j);
            if (!obj || obj->GetType() != CPDF_PageObject::Type::kImage)
                continue;

            CPDF_ImageObject* img_obj = obj->AsImage();
            CPDF_Image* image = img_obj->GetImage();
            if (!image) continue;

            RetainPtr<const CPDF_Stream> stream = image->GetStream();
            if (!stream) continue;

            const CFX_Matrix& matrix = img_obj->matrix();
            const float display_points =
                std::max(std::fabs(matrix.a), std::fabs(matrix.d));

            CPDF_Stream* mutable_stream =
                const_cast<CPDF_Stream*>(stream.Get());
            bool downsampled =
                options->image_dpi_threshold > 0 &&
                DownsampleImage(image, mutable_stream, options,
                                display_points);
            if (!downsampled &&
                FlateStream(mutable_stream) > 0) {
                ++g_last_compress_stats.images_recompressed;
                ++processed;
            } else if (downsampled) {
                ++g_last_compress_stats.images_recompressed;
                ++processed;
            }
        }
        FPDF_ClosePage(fpage);
    }
    return processed;
}

// ---------------------------------------------------------------------------
// Document component removal (annotations / forms / bookmarks / metadata)
// ---------------------------------------------------------------------------

int RemoveDocumentComponents(CPDF_Document* doc,
                             const FPDF_CompressOptions* options) {
    int removed = 0;

    if (options->remove_annotations) {
        for (int i = 0; i < doc->GetPageCount(); ++i) {
            RetainPtr<CPDF_Dictionary> page =
                doc->GetMutablePageDictionary(i);
            if (!page) continue;
            if (page->RemoveFor("Annots")) ++removed;
        }
    }

    RetainPtr<CPDF_Dictionary> root = doc->GetMutableRoot();
    if (root) {
        if (options->remove_forms && root->RemoveFor("AcroForm"))
            ++removed;
        if (options->remove_bookmarks && root->RemoveFor("Outlines"))
            ++removed;
        if (options->remove_metadata && root->RemoveFor("Metadata"))
            ++removed;
    }

    if (options->remove_metadata) {
        CPDF_Dictionary* trailer = doc->GetParser()->GetMutableTrailerForTesting();
        if (trailer && trailer->RemoveFor("Info")) ++removed;
    }
    return removed;
}

// ---------------------------------------------------------------------------
// Unreachable object deletion
// ---------------------------------------------------------------------------

void ExpandChildren(const CPDF_Object* cur, std::vector<const CPDF_Object*>* stack) {
    if (cur->IsDictionary()) {
        const CPDF_Dictionary* dict = cur->AsDictionary();
        for (const ByteString& key : dict->GetKeys())
            stack->push_back(dict->GetObjectFor(key.AsStringView()));
    } else if (cur->IsArray()) {
        const CPDF_Array* array = cur->AsArray();
        for (size_t i = 0; i < array->size(); ++i)
            stack->push_back(array->GetObjectAt(i));
    } else if (cur->IsStream()) {
        stack->push_back(cur->AsStream()->GetDict().Get());
    }
}

// Visit an object graph and collect every indirect object reachable from it.
// `expanded` records indirect objects whose children have been visited, so
// shared references are traversed once; following a reference only pushes the
// direct object and never marks it itself. Objects whose objnum was reset to 0
// (detached copies) are still expanded, guarded by their pointer address.
void CollectReachable(const CPDF_Object* obj,
                      std::set<uint32_t>* expanded,
                      std::vector<const CPDF_Object*>* stack) {
    std::set<const CPDF_Object*> expanded_copies;
    if (obj) stack->push_back(obj);

    while (!stack->empty()) {
        const CPDF_Object* cur = stack->back();
        stack->pop_back();
        if (!cur) continue;

        if (cur->IsReference()) {
            const CPDF_Reference* ref = cur->AsReference();
            const uint32_t num = ref->GetRefObjNum();
            if (num && !expanded->count(num))
                stack->push_back(ref->GetDirect());
            continue;
        }

        const uint32_t num = cur->GetObjNum();
        if (num != 0) {
            if (expanded->count(num)) continue;
            expanded->insert(num);
        } else {
            if (expanded_copies.count(cur)) continue;
            expanded_copies.insert(cur);
        }
        ExpandChildren(cur, stack);
    }
}

// Delete every indirect object that is not reachable from the document root
// (plus trailer entries such as /Info and /Encrypt, which are regenerated on
// save). Returns the number of deleted objects.
int RemoveUnusedObjects(CPDF_Document* doc) {
    std::set<uint32_t> reachable;
    std::vector<const CPDF_Object*> seed;

    // Trailer references (e.g. /Root) and the document root start the walk;
    // expansion happens on the first pop, so the root being pre-listed does
    // not suppress its subtree. /Info and /Encrypt were already removed by
    // RemoveDocumentComponents when the respective options are set.
    const CPDF_Dictionary* trailer = doc->GetParser()->GetTrailer();
    if (trailer) {
        for (const ByteString& key : trailer->GetKeys()) {
            const CPDF_Object* value = trailer->GetObjectFor(key.AsStringView());
            if (value && value->IsReference())
                seed.push_back(value);
        }
    }

    CollectReachable(doc->GetRoot(), &reachable, &seed);

    // The creator already omits unreachable objects from the output, so this
    // pass only REPORTS how many in-pool objects will be dropped. Objects must
    // NOT be deleted from the pool: the save path re-parses missing pool
    // objects from the original file, which would resurrect the very objects
    // we mutated (e.g. pages stripped of /Annots).
    int removed = 0;
    const uint32_t last = doc->GetLastObjNum();
    for (uint32_t n = 1; n <= last; ++n) {
        if (reachable.count(n)) continue;
        RetainPtr<CPDF_Object> obj = doc->GetOrParseIndirectObject(n);
        if (obj) ++removed;
    }
    return removed;
}

// Compress every in-memory indirect stream that does not already carry a
// filter (content streams, font data, uncompressed stream dicts, ...).
int CompressObjectStreams(CPDF_Document* doc) {
    int compressed = 0;
    const uint32_t last = doc->GetLastObjNum();
    for (uint32_t n = 1; n <= last; ++n) {
        RetainPtr<CPDF_Object> obj = doc->GetOrParseIndirectObject(n);
        if (!obj || !obj->IsStream()) continue;
        if (FlateStream(const_cast<CPDF_Stream*>(obj->AsStream())) > 0) ++compressed;
    }
    return compressed;
}

}  // namespace

void FPDF_CompressOptionsInit(FPDF_CompressOptions* options) {
    if (!options) return;
    options->flags = FPDF_COMPRESS_DEFAULT;
    options->image_quality = FPDF_IMAGE_QUALITY_HIGH;
    options->image_dpi_threshold = 300;
    options->min_image_dpi = 150;
    options->font_subset_threshold = 80;
    options->remove_annotations = 0;
    options->remove_forms = 0;
    options->remove_bookmarks = 0;
    options->remove_metadata = 0;
}

FPDF_DOCUMENT FPDF_OptimizeDocument(
    FPDF_DOCUMENT document,
    const FPDF_CompressOptions* options,
    FPDF_CompressProgressCallback progress_cb,
    void* user_data,
    FPDF_FILEWRITE* file_write) {
    memset(&g_last_compress_stats, 0, sizeof(g_last_compress_stats));

    ReportProgress(progress_cb, user_data, 0, kStageStart);

    CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
    if (!doc) return nullptr;

    FPDF_CompressOptions opts;
    if (options) {
        opts = *options;
    } else {
        FPDF_CompressOptionsInit(&opts);
    }

    if (opts.flags & FPDF_COMPRESS_IMAGES)
        ProcessImages(doc, &opts, progress_cb, user_data);

    if (opts.flags & FPDF_COMPRESS_FLATE)
        CompressObjectStreams(doc);

    RemoveDocumentComponents(doc, &opts);

    if (opts.flags & FPDF_COMPRESS_REMOVE_UNUSED)
        g_last_compress_stats.objects_removed = RemoveUnusedObjects(doc);

    ReportProgress(progress_cb, user_data, 70, kStageTrim);

    // Original size for the ratio reported after saving. SaveWithCompression
    // fills in compressed_size when the file is written.
    CPDF_Parser* parser = doc->GetParser();
    if (parser && parser->GetDocumentSize() > 0)
        g_last_compress_stats.original_size =
            static_cast<size_t>(parser->GetDocumentSize());

    // NOTE: The document is optimized in place. `document` is returned as-is
    // for convenience; callers must not wrap it in a new handle.
    return document;
}

int FPDF_GetLastCompressStats(FPDF_CompressStats* stats) {
    if (!stats) return 0;
    *stats = g_last_compress_stats;
    return 1;
}

namespace {

// File-write relay for FPDF_SaveAsCopy.
struct PDFSaveContext {
    FPDF_FILEWRITE file_write;
    std::ofstream file;
    size_t bytes_written = 0;
    bool ok = true;
};

}  // namespace

int PDFWriteBlock(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
    auto* ctx = reinterpret_cast<PDFSaveContext*>(self);
    if (!ctx->file.write(static_cast<const char*>(data), size)) {
        ctx->ok = false;
        return 0;
    }
    ctx->bytes_written += size;
    return 1;
}

int FPDF_SaveWithCompression(
    FPDF_DOCUMENT document,
    const char* file_path,
    const FPDF_CompressOptions* options,
    FPDF_CompressProgressCallback progress_cb,
    void* user_data) {
    if (!document || !file_path) return 0;

    if (!FPDF_OptimizeDocument(document, options, progress_cb, user_data,
                               nullptr))
        return 0;

    PDFSaveContext ctx;
    ctx.file.open(file_path, std::ios::binary | std::ios::trunc);
    if (!ctx.file.is_open()) return 0;

    ctx.file_write.version = 1;
    ctx.file_write.WriteBlock = PDFWriteBlock;

    ReportProgress(progress_cb, user_data, 90, kStageSave);

    // FPDF_FILEWRITE is the first member of PDFSaveContext, so the callback
    // can re-interpret `self` back to the full context.
    const FPDF_BOOL ok = FPDF_SaveAsCopy(document, &ctx.file_write,
                                         FPDF_NO_INCREMENTAL);
    ctx.file.close();

    if (ok && ctx.ok)
        g_last_compress_stats.compressed_size = ctx.bytes_written;
    if (g_last_compress_stats.original_size > 0)
        g_last_compress_stats.compression_ratio =
            static_cast<double>(g_last_compress_stats.compressed_size) /
            static_cast<double>(g_last_compress_stats.original_size);

    ReportProgress(progress_cb, user_data, 100, kStageDone);

    return (ok && ctx.ok) ? 1 : 0;
}