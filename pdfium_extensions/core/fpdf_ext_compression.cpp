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
    if (!stream || stream->GetRawSize() == 0) return 0;
    
    if (flags & FPDF_COMPRESS_FLATE) {
        const auto& raw_data = stream->ReadAllRawData();
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
    
    // Collect all referenced objects from trailer
    const CPDF_Dictionary* root = doc->GetRoot();
    if (root) to_visit.push_back(root->GetObjNum());
    
    // Traverse references
    while (!to_visit.empty()) {
        uint32_t obj_num = to_visit.back();
        to_visit.pop_back();
        
        if (referenced.count(obj_num)) continue;
        referenced.insert(obj_num);
        
        CPDF_Object* obj = doc->GetOrParseIndirectObject(obj_num);
        if (!obj) continue;
        
        // Find all references in this object
        if (obj->IsDictionary()) {
            const CPDF_Dictionary* dict = obj->AsDictionary();
            auto keys = dict->GetKeys();
            for (auto key : keys) {
                auto obj = dict->GetObjectFor(key.AsStringView());
                if (obj->IsReference()) {
                    to_visit.push_back(obj->GetDict()->GetObjNum());
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
    
    // Remove unreferenced objects
    for (auto ite = doc->begin() ; ite != doc->end(); ++ite) {
        if (!referenced.count(ite->first)) {
            doc->DeleteIndirectObject(ite->first);
            g_last_compress_stats.objects_removed++;
        }
    }
}

static int ProcessImages(CPDF_Document* doc, const FPDF_CompressOptions* options) {
    int processed = 0;
    
    for (int i = 0; i < doc->GetPageCount(); ++i) {
        auto page_dict = doc->GetMutablePageDictionary(i);
        
        if (!CPDF_Page::IsValidPageDictLoose(page_dict)) {
            continue;
        }
        auto page = pdfium::MakeRetain<CPDF_Page>(doc, std::move(page_dict));
        page->ParseContent();
        
        int page_obj_count = page->GetPageObjectCount();
        for (int j = 0; j < page_obj_count; ++j) {
            auto obj = page->GetPageObjectByIndex(j);
            if (obj->GetType() == CPDF_PageObject::Type::kImage) {
                CPDF_ImageObject* img_obj = obj->AsImage();
                const CPDF_Stream* stream = img_obj->GetImage()->GetStream();
                
                if (stream && (options->flags & FPDF_COMPRESS_IMAGES)) {
                    // Check image DPI and downsample if needed
                    // This is a simplified version - real implementation would
                    // decode, resample, and re-encode the image
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

static int SubsetFonts(CPDF_Document* doc, const FPDF_CompressOptions* options) {
    int processed = 0;
    // Font subsetting implementation would go here
    // This requires analyzing used characters and creating subset fonts
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
    
    // 1. Compress streams
    if (opts.flags & FPDF_COMPRESS_FLATE) {
        for (auto ite = doc->begin(); ite != doc->end(); ++ite) {
            CPDF_Object* obj = ite->second.Get();
            if (obj && obj->IsStream()) {
                CompressStream(const_cast<CPDF_Stream*>(obj->AsStream()), opts.flags);
            }
        }
    }
    
    // 2. Compress object streams
    if (opts.flags & FPDF_COMPRESS_OBJECT_STREAMS) {
        // Implementation would create object streams for small objects
    }
    
    // 3. Process images
    if (opts.flags & FPDF_COMPRESS_IMAGES) {
        ProcessImages(doc, &opts);
    }
    
    // 4. Subset fonts
    if (opts.flags & FPDF_COMPRESS_FONTS) {
        SubsetFonts(doc, &opts);
    }
    
    // 5. Remove unused objects
    if (opts.flags & FPDF_COMPRESS_REMOVE_UNUSED) {
        RemoveUnusedObjects(doc);
    }
    
    // 6. Remove optional content
    if (opts.remove_annotations) {
        // Remove annotation objects
    }
    if (opts.remove_forms) {
        // Remove form fields
    }
    if (opts.remove_bookmarks) {
        // Remove outlines
    }
    if (opts.remove_metadata) {
        // Remove metadata
    }
    
    return document; // Return same document (modified in place)
}

int FPDF_GetLastCompressStats(FPDF_CompressStats* stats) {
    if (!stats) return 0;
    *stats = g_last_compress_stats;
    if (stats->original_size > 0) {
        stats->compression_ratio = 
            static_cast<double>(stats->compressed_size) / stats->original_size;
    }
    return 1;
}

int FPDF_SaveWithCompression(
    FPDF_DOCUMENT document,
    const char* file_path,
    const FPDF_CompressOptions* options
) {
    if (!document || !file_path) return 0;
    
    CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
    
    // Apply optimizations
    FPDF_OptimizeDocument(document, options, nullptr);
    
    // Save to file
    FPDF_FILEWRITE file_write;
    file_write.version = 1;
    
    
//    CPDF_Creator creator(doc, );
//    // Use PDFium's built-in file writer
//    return doc->Save(file_path, FPDF_SAVE_DEFAULT);
    return 0;
}
