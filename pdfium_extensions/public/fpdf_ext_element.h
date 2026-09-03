#ifndef PDFIUM_EXT_ELEMENT_H
#define PDFIUM_EXT_ELEMENT_H

#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif

// Element types
typedef enum {
    FPDF_ELEMENT_UNKNOWN = 0,
    FPDF_ELEMENT_TEXT = 1,
    FPDF_ELEMENT_IMAGE = 2,
    FPDF_ELEMENT_PATH = 3,
    FPDF_ELEMENT_SHADING = 4,
    FPDF_ELEMENT_FORM = 5,
    FPDF_ELEMENT_GROUP = 6,
    FPDF_ELEMENT_REFERENCE = 7,
} FPDF_ElementType;

// Text element attributes
typedef struct {
    double font_size;
    double char_spacing;
    double word_spacing;
    double horizontal_scaling;
    double leading;
    unsigned int font_flags;      // Bold, italic, serif, etc.
    unsigned int color_rgb;       // RGB color (0xRRGGBB)
    unsigned int color_alpha;     // Alpha (0-255)
    char font_name[256];          // Font name
    char font_family[128];        // Font family
    int writing_mode;             // 0=horizontal, 1=vertical
    double text_matrix[6];        // Text transformation matrix
} FPDF_TextAttributes;

// Image element attributes
typedef struct {
    int width;
    int height;
    int bits_per_component;
    int color_space;              // FPDF_ColorSpace enum
    int filter;                   // Compression filter
    size_t data_size;
    double matrix[6];             // Image transformation matrix
    int has_mask;
    int is_inline;
} FPDF_ImageAttributes;

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
    int fill_rule;                // 0=non-zero, 1=even-odd
    double dash_pattern[16];
    int dash_count;
    double dash_phase;
    double matrix[6];
} FPDF_PathAttributes;

// Generic element structure
typedef struct FPDF_PageElement FPDF_PageElement;

struct FPDF_PageElement {
    FPDF_ElementType type;
    double bounds[4];             // x1, y1, x2, y2 in page coordinates
    union {
        FPDF_TextAttributes text;
        FPDF_ImageAttributes image;
        FPDF_PathAttributes path;
    } attributes;
    
    // Navigation
    FPDF_PageElement* parent;
    FPDF_PageElement* first_child;
    FPDF_PageElement* next_sibling;
    FPDF_PageElement* prev_sibling;
    
    // Raw content access
    void* raw_data;               // Type-specific data
    size_t raw_data_size;
};

// Element iterator for traversing page elements
typedef struct FPDF_ElementIterator FPDF_ElementIterator;

// Create element iterator for a page
FPDF_ElementIterator* FPDF_CreateElementIterator(FPDF_PAGE page);

// Get next element (returns NULL at end)
FPDF_PageElement* FPDF_GetNextElement(FPDF_ElementIterator* iterator);

// Destroy iterator
void FPDF_DestroyElementIterator(FPDF_ElementIterator* iterator);

// Extract text from element (for text elements)
const char* FPDF_GetElementText(FPDF_PageElement* element, int* out_length);

// Extract image data from element (for image elements)
// Returns allocated buffer that must be freed with FPDF_FreeElementData
unsigned char* FPDF_GetElementImageData(FPDF_PageElement* element, size_t* out_size);

// Extract path data from element (for path elements)
// Returns allocated buffer of path commands
unsigned char* FPDF_GetElementPathData(FPDF_PageElement* element, size_t* out_size);

// Free data returned by extraction functions
void FPDF_FreeElementData(void* data);

// Get element count on page
int FPDF_CountPageElements(FPDF_PAGE page);

// Get element by index
FPDF_PageElement* FPDF_GetPageElement(FPDF_PAGE page, int index);

// Find elements by type
int FPDF_FindElementsByType(FPDF_PAGE page, FPDF_ElementType type, FPDF_PageElement** out_elements, int max_count);

// Search text with element context
typedef struct {
    double bounds[4];
    int char_index;
    int element_index;
    FPDF_PageElement* element;
} FPDF_TextMatchEx;

int FPDF_SearchTextEx(
    FPDF_PAGE page,
    const char* search_text,
    int flags,
    int start_index,
    int max_results,
    FPDF_TextMatchEx* out_matches,
    int* out_count
);

// Extract form field elements
typedef struct {
    char name[256];
    char value[1024];
    int field_type;           // FPDF_FormFieldType
    double bounds[4];
    int flags;
    FPDF_PageElement* element;
} FPDF_FormFieldInfo;

int FPDF_ExtractFormFields(FPDF_DOCUMENT document, FPDF_FormFieldInfo* out_fields, int max_fields);

// Extract annotations
typedef struct {
    int type;                 // FPDF_AnnotType
    char contents[2048];
    double bounds[4];
    int flags;
    FPDF_PageElement* element;
} FPDF_AnnotInfo;

int FPDF_ExtractAnnotations(FPDF_PAGE page, FPDF_AnnotInfo* out_annots, int max_annots);

// Extract bookmarks/outline
typedef struct {
    char title[512];
    int page_index;
    double dest_x, dest_y;
    double zoom;
    int level;
    int child_count;
} FPDF_BookmarkInfo;

int FPDF_ExtractBookmarks(FPDF_DOCUMENT document, FPDF_BookmarkInfo* out_bookmarks, int max_bookmarks);

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
} FPDF_DocumentStructure;

int FPDF_GetDocumentStructure(FPDF_DOCUMENT document, FPDF_DocumentStructure* out_info);

// Color space enum
typedef enum {
    FPDF_COLORSPACE_UNKNOWN = 0,
    FPDF_COLORSPACE_GRAY = 1,
    FPDF_COLORSPACE_RGB = 2,
    FPDF_COLORSPACE_CMYK = 3,
    FPDF_COLORSPACE_INDEXED = 4,
    FPDF_COLORSPACE_DEVICE_N = 5,
} FPDF_ColorSpace;

// Form field types
typedef enum {
    FPDF_FIELD_UNKNOWN = 0,
    FPDF_FIELD_BUTTON = 1,
    FPDF_FIELD_CHECKBOX = 2,
    FPDF_FIELD_RADIOBUTTON = 3,
    FPDF_FIELD_TEXT = 4,
    FPDF_FIELD_CHOICE = 5,
    FPDF_FIELD_SIGNATURE = 6,
} FPDF_FormFieldType;

// Annotation types
typedef enum {
    FPDF_ANNOT_UNKNOWN = 0,
    FPDF_ANNOT_TEXT = 1,
    FPDF_ANNOT_LINK = 2,
    FPDF_ANNOT_FREETEXT = 3,
    FPDF_ANNOT_LINE = 4,
    FPDF_ANNOT_SQUARE = 5,
    FPDF_ANNOT_CIRCLE = 6,
    FPDF_ANNOT_POLYGON = 7,
    FPDF_ANNOT_POLYLINE = 8,
    FPDF_ANNOT_HIGHLIGHT = 9,
    FPDF_ANNOT_UNDERLINE = 10,
    FPDF_ANNOT_SQUIGGLY = 11,
    FPDF_ANNOT_STRIKEOUT = 12,
    FPDF_ANNOT_STAMP = 13,
    FPDF_ANNOT_INK = 14,
    FPDF_ANNOT_POPUP = 15,
    FPDF_ANNOT_FILEATTACHMENT = 16,
    FPDF_ANNOT_SOUND = 17,
    FPDF_ANNOT_MOVIE = 18,
    FPDF_ANNOT_WIDGET = 19,
    FPDF_ANNOT_SCREEN = 20,
    FPDF_ANNOT_PRINTERMARK = 21,
    FPDF_ANNOT_TRAPNET = 22,
    FPDF_ANNOT_WATERMARK = 23,
    FPDF_ANNOT_3D = 24,
} FPDF_AnnotType;

#ifdef __cplusplus
}
#endif

#endif // PDFIUM_EXT_ELEMENT_H