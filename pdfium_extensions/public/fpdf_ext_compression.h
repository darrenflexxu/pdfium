#ifndef PDFIUM_EXT_COMPRESSION_H
#define PDFIUM_EXT_COMPRESSION_H

#include "fpdfview.h"
#include "fpdf_save.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compression/optimization flags
typedef enum {
    FPDF_COMPRESS_NONE = 0,
    FPDF_COMPRESS_FLATE = 1 << 0,        // FlateDecode (zlib)
    FPDF_COMPRESS_OBJECT_STREAMS = 1 << 1,  // Compress object streams
    FPDF_COMPRESS_IMAGES = 1 << 2,        // Downsample/recompress images
    FPDF_COMPRESS_FONTS = 1 << 3,         // Subset and compress fonts
    FPDF_COMPRESS_REMOVE_UNUSED = 1 << 4, // Remove unused objects
    FPDF_COMPRESS_LINEARIZE = 1 << 5,     // Linearize for fast web view
    FPDF_COMPRESS_LOSSLESS = FPDF_COMPRESS_FLATE | FPDF_COMPRESS_OBJECT_STREAMS | FPDF_COMPRESS_REMOVE_UNUSED,
    FPDF_COMPRESS_DEFAULT = FPDF_COMPRESS_LOSSLESS | FPDF_COMPRESS_LINEARIZE,
} FPDF_CompressFlags;

// Image compression quality (1-100, for lossy compression)
typedef enum {
    FPDF_IMAGE_QUALITY_LOW = 50,
    FPDF_IMAGE_QUALITY_MEDIUM = 75,
    FPDF_IMAGE_QUALITY_HIGH = 90,
    FPDF_IMAGE_QUALITY_LOSSLESS = 100,
} FPDF_ImageQuality;

// Compression options structure
typedef struct {
    int flags;                    // FPDF_CompressFlags
    int image_quality;            // FPDF_ImageQuality (for lossy)
    int image_dpi_threshold;      // Downsample images above this DPI
    int min_image_dpi;            // Minimum DPI after downsampling
    int font_subset_threshold;    // Subset fonts if used chars < threshold%
    int remove_annotations;       // Remove annotations
    int remove_forms;             // Remove form fields
    int remove_bookmarks;         // Remove bookmarks/outlines
    int remove_metadata;          // Remove document metadata
} FPDF_CompressOptions;

// Initialize with default options
void FPDF_CompressOptionsInit(FPDF_CompressOptions* options);

// Optimize/Compress a PDF document
// Returns a new FPDF_DOCUMENT that must be closed with FPDF_CloseDocument
// The original document is not modified
FPDF_DOCUMENT FPDF_OptimizeDocument(
    FPDF_DOCUMENT document,
    const FPDF_CompressOptions* options,
    FPDF_FILEWRITE* file_write  // Optional: write directly to file
);

// Get compression statistics
typedef struct {
    size_t original_size;
    size_t compressed_size;
    double compression_ratio;
    int objects_removed;
    int images_recompressed;
    int fonts_subsets;
} FPDF_CompressStats;

int FPDF_GetLastCompressStats(FPDF_CompressStats* stats);

// Save document with compression (convenience function)
int FPDF_SaveWithCompression(
    FPDF_DOCUMENT document,
    const char* file_path,
    const FPDF_CompressOptions* options
);

#ifdef __cplusplus
}
#endif

#endif // PDFIUM_EXT_COMPRESSION_H