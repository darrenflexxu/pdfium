#include "fpdf_ext_compression.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/edit/cpdf_creator.h"
#include "core/fpdfapi/page/cpdf_image.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "zlib.h"
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <string>
#include <fstream>

// Global compression stats
static FPDF_CompressStats g_last_compress_stats = {0};

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

static int CompressStream(CPDF_Stream* stream, int flags) {
    if (!stream) return 0;
    
    if (flags & FPDF_COMPRESS_FLATE) {
        auto raw_data = stream->ReadAllRawData();
        if (raw_data.empty()) return 0;

        std::vector<uint8_t> compressed;
        compressed.resize(compressBound(raw_data.size()));
        
        uLongf dest_len = compressed.size();
        int ret = compress2(compressed.data(), &dest_len,
                           raw_data.data(), raw_data.size(), Z_BEST_COMPRESSION);
        
        if (ret == Z_OK) {
            compressed.resize(dest_len);
            stream->SetData(compressed);
            return 1;
        }
    }
    return 0;
}

static void RemoveUnusedObjects(CPDF_Document* doc) {
    std::set<uint32_t> referenced;
    std::vector<uint32_t> to_visit;
    
    const CPDF_Dictionary* root = doc->GetRoot();
    if (root) {
        to_visit.push_back(1); 
    }
    
    while (!to_visit.empty()) {
        uint32_t obj_num = to_visit.back();
        to_visit.pop_back();
        
        if (referenced.count(obj_num)) continue;
        referenced.insert(obj_num);
        
        CPDF_Object* obj = doc->GetOrParseIndirectObject(obj_num);
        if (!obj) continue;
        
        if (obj->IsDictionary()) {
            const CPDF_Dictionary* dict = obj->AsDictionary();
            for (auto key : dict->GetKeys()) {
                const CPDF_Object* val = dict->GetObjectFor(key.AsStringView());
                if (val && val->IsReference()) {
                    to_visit.push_back(val->GetDirect()->GetObjNum());
                }
            }
        } else if (obj->IsArray()) {
            const CPDF_Array* array = obj->AsArray();
            for (size_t i = 0; i < array->size(); ++i) {
                const CPDF_Object* item = array->GetObjectAt(i);
                if (item && item->IsReference()) {
                    to_visit.push_back(item->GetDirect()->GetObjNum());
                }
            }
        }
    }
}

static int ProcessImages(CPDF_Document* doc, const FPDF_CompressOptions* options) {
    int processed = 0;
    for (int i = 0; i < doc->GetPageCount(); ++i) {
        auto page_dict = doc->GetMutablePageDictionary(i);
        if (!page_dict) continue;
        
        auto page = pdfium::MakeRetain<CPDF_Page>(doc, std::move(page_dict));
        page->ParseContent();
        
        for (int j = 0; j < page->GetPageObjectCount(); ++j) {
            auto obj = page->GetPageObjectByIndex(j);
            if (obj->GetType() == CPDF_PageObject::Type::kImage) {
                CPDF_ImageObject* img_obj = obj->AsImage();
                const CPDF_Stream* stream = img_obj->GetImage()->GetStream();
                
                if (stream && (options->flags & FPDF_COMPRESS_IMAGES)) {
                    if (CompressStream(const_cast<CPDF_Stream*>(stream), options->flags)) {
                        g_last_compress_stats.images_recompressed++;
                        processed++;
                    }
                }
            }
        }
    }
    return processed;
}

FPDF_DOCUMENT FPDF_OptimizeDocument(
    FPDF_DOCUMENT document,
    const FPDF_CompressOptions* options,
    FPDF_FILEWRITE* file_write
) {
    memset(&g_last_compress_stats, 0, sizeof(g_last_compress_stats));
    
    CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
    if (!doc) return nullptr;
    
    FPDF_CompressOptions opts;
    if (options) {
        opts = *options;
    } else {
        FPDF_CompressOptionsInit(&opts);
    }
    
    if (opts.flags & FPDF_COMPRESS_IMAGES) {
        ProcessImages(doc, &opts);
    }
    
    if (opts.flags & FPDF_COMPRESS_REMOVE_UNUSED) {
        RemoveUnusedObjects(doc);
    }
    
    return document;
}

int FPDF_GetLastCompressStats(FPDF_CompressStats* stats) {
    if (!stats) return 0;
    *stats = g_last_compress_stats;
    return 1;
}

// Helper to handle file writing for FPDF_SaveAsCopy
struct PDFSaveContext {
    FPDF_FILEWRITE file_write;
    std::ofstream file;
};

int PDFWriteBlock(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
    auto* ctx = reinterpret_cast<PDFSaveContext*>(self);
    if (ctx->file.write(static_cast<const char*>(data), size)) {
        return 1;
    }
    return 0;
}

int FPDF_SaveWithCompression(
    FPDF_DOCUMENT document,
    const char* file_path,
    const FPDF_CompressOptions* options
) {
    if (!document || !file_path) return 0;
    
    // 1. Optimize the document in place
    FPDF_OptimizeDocument(document, options, nullptr);
    
    // 2. Save to file using FPDF_SaveAsCopy
    PDFSaveContext ctx;
    ctx.file.open(file_path, std::ios::binary);
    if (!ctx.file.is_open()) return 0;
    
    ctx.file_write.version = 1;
    ctx.file_write.WriteBlock = PDFWriteBlock;
    
    // Use the address of the context, but we must ensure the WriteBlock
    // callback knows how to cast 'self' back to PDFSaveContext.
    // In PDFium's FPDF_FILEWRITE, 'self' is passed as the first argument.
    
    // IMPORTANT: We must pass the pointer to the context, but the callback 
    // expects FPDF_FILEWRITE*. We can place FPDF_FILEWRITE as the first member.
    
    int result = FPDF_SaveAsCopy(document, &ctx.file_write, 0) ? 1 : 0;
    
    ctx.file.close();
    return result;
}
