#include "fpdf_ext_element.h"
#include "core/fpdfapi/fpdf_page/include/cpdf_pageobject.h"
#include "core/fpdfapi/fpdf_page/include/cpdf_imageobject.h"
#include "core/fpdfapi/fpdf_page/include/cpdf_textobject.h"
#include "core/fpdfapi/fpdf_page/include/cpdf_pathobject.h"
#include "core/fpdfapi/fpdf_parser/include/cpdf_document.h"
#include "core/fpdfapi/fpdf_parser/include/cpdf_dictionary.h"
#include "core/fpdfapi/fpdf_parser/include/cpdf_stream.h"
#include "core/fpdfapi/fpdf_annot/include/cpdf_annot.h"
#include "core/fpdfapi/fpdf_annot/include/cpdf_annotlist.h"
#include "third_party/base/stl_util.h"
#include <vector>
#include <string>
#include <map>

// Forward declarations
struct FPDF_ElementIterator {
    FPDF_PAGE page;
    CPDF_PageObject* current_obj;
    bool first_call;
};

struct FPDF_PageElement {
    FPDF_ElementType type;
    double bounds[4];
    union {
        FPDF_TextAttributes text;
        FPDF_ImageAttributes image;
        FPDF_PathAttributes path;
    } attributes;
    FPDF_PageElement* parent;
    FPDF_PageElement* first_child;
    FPDF_PageElement* next_sibling;
    FPDF_PageElement* prev_sibling;
    void* raw_data;
    size_t raw_data_size;
    
    // Internal
    CPDF_PageObject* pdfium_obj;
    std::string text_cache;
    std::vector<unsigned char> image_cache;
    std::vector<unsigned char> path_cache;
};

static FPDF_PageElement* CreatePageElement(CPDF_PageObject* obj) {
    if (!obj) return nullptr;
    
    FPDF_PageElement* element = new FPDF_PageElement();
    memset(element, 0, sizeof(FPDF_PageElement));
    element->pdfium_obj = obj;
    
    // Get bounds
    CFX_FloatRect rect = obj->GetBoundingBox();
    element->bounds[0] = rect.left;
    element->bounds[1] = rect.bottom;
    element->bounds[2] = rect.right;
    element->bounds[3] = rect.top;
    
    switch (obj->GetType()) {
        case PDFPAGE_TEXT: {
            element->type = FPDF_ELEMENT_TEXT;
            CPDF_TextObject* text_obj = static_cast<CPDF_TextObject*>(obj);
            
            // Extract text attributes
            CPDF_Font* font = text_obj->GetFont();
            if (font) {
                element->attributes.text.font_size = text_obj->GetFontSize();
                element->attributes.text.char_spacing = text_obj->GetCharSpace();
                element->attributes.text.word_spacing = text_obj->GetWordSpace();
                element->attributes.text.horizontal_scaling = text_obj->GetHorizScale();
                element->attributes.text.leading = text_obj->GetLeading();
                element->attributes.text.font_flags = font->GetFontFlags();
                element->attributes.text.writing_mode = text_obj->GetWritingMode();
                
                // Font name
                std::string font_name = font->GetFontName();
                strncpy(element->attributes.text.font_name, font_name.c_str(), 255);
                strncpy(element->attributes.text.font_family, font->GetFamilyName().c_str(), 127);
            }
            
            // Color
            FX_ARGB fill_color = text_obj->GetFillColor();
            element->attributes.text.color_rgb = fill_color & 0xFFFFFF;
            element->attributes.text.color_alpha = (fill_color >> 24) & 0xFF;
            
            // Text matrix
            CFX_Matrix matrix = text_obj->GetTextMatrix();
            for (int i = 0; i < 6; i++) {
                element->attributes.text.text_matrix[i] = matrix.Get(i);
            }
            break;
        }
        case PDFPAGE_IMAGE: {
            element->type = FPDF_ELEMENT_IMAGE;
            CPDF_ImageObject* img_obj = static_cast<CPDF_ImageObject*>(obj);
            CPDF_Stream* stream = img_obj->GetImageStream();
            
            if (stream) {
                CPDF_Dictionary* dict = stream->GetDict();
                element->attributes.image.width = dict->GetIntegerFor("Width");
                element->attributes.image.height = dict->GetIntegerFor("Height");
                element->attributes.image.bits_per_component = dict->GetIntegerFor("BitsPerComponent");
                
                // Color space
                CPDF_Object* cs_obj = dict->GetDirectObjectFor("ColorSpace");
                if (cs_obj) {
                    if (cs_obj->IsName()) {
                        std::string cs_name = cs_obj->GetString();
                        if (cs_name == "DeviceGray") element->attributes.image.color_space = FPDF_COLORSPACE_GRAY;
                        else if (cs_name == "DeviceRGB") element->attributes.image.color_space = FPDF_COLORSPACE_RGB;
                        else if (cs_name == "DeviceCMYK") element->attributes.image.color_space = FPDF_COLORSPACE_CMYK;
                        else if (cs_name == "Indexed") element->attributes.image.color_space = FPDF_COLORSPACE_INDEXED;
                    }
                }
                
                // Filter
                CPDF_Object* filter_obj = dict->GetDirectObjectFor("Filter");
                if (filter_obj) {
                    if (filter_obj->IsName()) {
                        std::string filter = filter_obj->GetString();
                        if (filter == "FlateDecode") element->attributes.image.filter = 1;
                        else if (filter == "DCTDecode") element->attributes.image.filter = 2;
                        else if (filter == "JPXDecode") element->attributes.image.filter = 3;
                        else if (filter == "CCITTFaxDecode") element->attributes.image.filter = 4;
                    }
                }
            }
            
            // Matrix
            CFX_Matrix matrix = img_obj->GetMatrix();
            for (int i = 0; i < 6; i++) {
                element->attributes.image.matrix[i] = matrix.Get(i);
            }
            break;
        }
        case PDFPAGE_PATH: {
            element->type = FPDF_ELEMENT_PATH;
            CPDF_PathObject* path_obj = static_cast<CPDF_PathObject*>(obj);
            
            FX_ARGB fill = path_obj->GetFillColor();
            element->attributes.path.fill_color_rgb = fill & 0xFFFFFF;
            element->attributes.path.fill_color_alpha = (fill >> 24) & 0xFF;
            
            FX_ARGB stroke = path_obj->GetStrokeColor();
            element->attributes.path.stroke_color_rgb = stroke & 0xFFFFFF;
            element->attributes.path.stroke_color_alpha = (stroke >> 24) & 0xFF;
            
            element->attributes.path.line_width = path_obj->GetLineWidth();
            element->attributes.path.line_cap = path_obj->GetLineCap();
            element->attributes.path.line_join = path_obj->GetLineJoin();
            element->attributes.path.miter_limit = path_obj->GetMiterLimit();
            element->attributes.path.fill_rule = path_obj->GetFillRule();
            
            // Dash pattern
            const std::vector<float>& dash = path_obj->GetDashPattern();
            element->attributes.path.dash_count = std::min<int>(dash.size(), 16);
            for (int i = 0; i < element->attributes.path.dash_count; i++) {
                element->attributes.path.dash_pattern[i] = dash[i];
            }
            element->attributes.path.dash_phase = path_obj->GetDashPhase();
            
            // Matrix
            CFX_Matrix matrix = path_obj->GetMatrix();
            for (int i = 0; i < 6; i++) {
                element->attributes.path.matrix[i] = matrix.Get(i);
            }
            break;
        }
        case PDFPAGE_SHADING:
            element->type = FPDF_ELEMENT_SHADING;
            break;
        case PDFPAGE_FORM:
            element->type = FPDF_ELEMENT_FORM;
            break;
        default:
            element->type = FPDF_ELEMENT_UNKNOWN;
            break;
    }
    
    return element;
}

static void DestroyPageElement(FPDF_PageElement* element) {
    if (!element) return;
    delete element;
}

FPDF_ElementIterator* FPDF_CreateElementIterator(FPDF_PAGE page) {
    if (!page) return nullptr;
    
    CPDF_Page* pdfium_page = static_cast<CPDF_Page*>(page);
    FPDF_ElementIterator* iter = new FPDF_ElementIterator();
    iter->page = page;
    iter->current_obj = pdfium_page->GetFirstObject();
    iter->first_call = true;
    return iter;
}

FPDF_PageElement* FPDF_GetNextElement(FPDF_ElementIterator* iterator) {
    if (!iterator || !iterator->current_obj) return nullptr;
    
    CPDF_PageObject* obj = iterator->current_obj;
    iterator->current_obj = obj->GetNext();
    
    return CreatePageElement(obj);
}

void FPDF_DestroyElementIterator(FPDF_ElementIterator* iterator) {
    delete iterator;
}

const char* FPDF_GetElementText(FPDF_PageElement* element, int* out_length) {
    if (!element || element->type != FPDF_ELEMENT_TEXT) {
        if (out_length) *out_length = 0;
        return "";
    }
    
    CPDF_TextObject* text_obj = static_cast<CPDF_TextObject*>(element->pdfium_obj);
    if (!text_obj) {
        if (out_length) *out_length = 0;
        return "";
    }
    
    // Get text from PDFium text object
    WideString text = text_obj->GetText();
    element->text_cache = text.ToUTF8();
    
    if (out_length) *out_length = element->text_cache.size();
    return element->text_cache.c_str();
}

unsigned char* FPDF_GetElementImageData(FPDF_PageElement* element, size_t* out_size) {
    if (!element || element->type != FPDF_ELEMENT_IMAGE) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    CPDF_ImageObject* img_obj = static_cast<CPDF_ImageObject*>(element->pdfium_obj);
    if (!img_obj) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    CPDF_Stream* stream = img_obj->GetImageStream();
    if (!stream) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    // Get raw image data
    element->image_cache = stream->GetRawData();
    *out_size = element->image_cache.size();
    
    if (element->image_cache.empty()) return nullptr;
    return element->image_cache.data();
}

unsigned char* FPDF_GetElementPathData(FPDF_PageElement* element, size_t* out_size) {
    if (!element || element->type != FPDF_ELEMENT_PATH) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    CPDF_PathObject* path_obj = static_cast<CPDF_PathObject*>(element->pdfium_obj);
    if (!path_obj) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    // Serialize path data
    CPDF_Path* path = path_obj->GetPath();
    if (!path) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    // Simple path serialization (commands + coordinates)
    // Real implementation would use a proper path serializer
    const auto& points = path->GetPoints();
    element->path_cache.resize(points.size() * sizeof(CFX_Point) + 1);
    element->path_cache[0] = static_cast<unsigned char>(path->GetPointCount());
    memcpy(element->path_cache.data() + 1, points.data(), points.size() * sizeof(CFX_Point));
    
    *out_size = element->path_cache.size();
    return element->path_cache.data();
}

void FPDF_FreeElementData(void* data) {
    // The data is cached in the element, so we don't actually free it here
    // It will be freed when the element is destroyed
    (void)data;
}

int FPDF_CountPageElements(FPDF_PAGE page) {
    if (!page) return 0;
    CPDF_Page* pdfium_page = static_cast<CPDF_Page*>(page);
    
    int count = 0;
    CPDF_PageObject* obj = pdfium_page->GetFirstObject();
    while (obj) {
        count++;
        obj = obj->GetNext();
    }
    return count;
}

FPDF_PageElement* FPDF_GetPageElement(FPDF_PAGE page, int index) {
    if (!page || index < 0) return nullptr;
    CPDF_Page* pdfium_page = static_cast<CPDF_Page*>(page);
    
    CPDF_PageObject* obj = pdfium_page->GetFirstObject();
    for (int i = 0; i < index && obj; i++) {
        obj = obj->GetNext();
    }
    
    return CreatePageElement(obj);
}

int FPDF_FindElementsByType(FPDF_PAGE page, FPDF_ElementType type, FPDF_PageElement** out_elements, int max_count) {
    if (!page || !out_elements || max_count <= 0) return 0;
    
    CPDF_Page* pdfium_page = static_cast<CPDF_Page*>(page);
    int found = 0;
    
    CPDF_PageObject* obj = pdfium_page->GetFirstObject();
    while (obj && found < max_count) {
        FPDF_ElementType obj_type = FPDF_ELEMENT_UNKNOWN;
        switch (obj->GetType()) {
            case PDFPAGE_TEXT: obj_type = FPDF_ELEMENT_TEXT; break;
            case PDFPAGE_IMAGE: obj_type = FPDF_ELEMENT_IMAGE; break;
            case PDFPAGE_PATH: obj_type = FPDF_ELEMENT_PATH; break;
            case PDFPAGE_SHADING: obj_type = FPDF_ELEMENT_SHADING; break;
            case PDFPAGE_FORM: obj_type = FPDF_ELEMENT_FORM; break;
        }
        
        if (obj_type == type) {
            out_elements[found++] = CreatePageElement(obj);
        }
        obj = obj->GetNext();
    }
    
    return found;
}

int FPDF_SearchTextEx(
    FPDF_PAGE page,
    const char* search_text,
    int flags,
    int start_index,
    int max_results,
    FPDF_TextMatchEx* out_matches,
    int* out_count
) {
    if (!page || !search_text || !out_matches || !out_count) return 0;
    
    CPDF_Page* pdfium_page = static_cast<CPDF_Page*>(page);
    CPDF_TextPage* text_page = pdfium_page->GetTextPage();
    if (!text_page) {
        *out_count = 0;
        return 0;
    }
    
    // Use existing PDFium text search
    FPDF_TEXTPAGE fpdf_text_page = reinterpret_cast<FPDF_TEXTPAGE>(text_page);
    FPDF_SCHHANDLE handle = FPDFText_FindStart(fpdf_text_page, search_text, flags, start_index);
    if (!handle) {
        *out_count = 0;
        return 0;
    }
    
    int found = 0;
    while (found < max_results && FPDFText_FindNext(handle)) {
        int index = FPDFText_GetSchResultIndex(handle);
        if (index < start_index) continue;
        
        double x1, y1, x2, y2;
        FPDFText_GetSchResultRect(handle, &x1, &y1, &x2, &y2);
        
        out_matches[found].bounds[0] = x1;
        out_matches[found].bounds[1] = y1;
        out_matches[found].bounds[2] = x2;
        out_matches[found].bounds[3] = y2;
        out_matches[found].char_index = index;
        out_matches[found].element_index = -1; // Would need element mapping
        out_matches[found].element = nullptr;
        
        found++;
    }
    
    FPDFText_FindClose(handle);
    *out_count = found;
    return found;
}

int FPDF_ExtractFormFields(FPDF_DOCUMENT document, FPDF_FormFieldInfo* out_fields, int max_fields) {
    if (!document || !out_fields || max_fields <= 0) return 0;
    
    CPDF_Document* doc = static_cast<CPDF_Document*>(document);
    CPDF_Dictionary* root = doc->GetRoot();
    if (!root) return 0;
    
    CPDF_Dictionary* acro_form = root->GetDictFor("AcroForm");
    if (!acro_form) return 0;
    
    CPDF_Array* fields = acro_form->GetArrayFor("Fields");
    if (!fields) return 0;
    
    int count = 0;
    for (size_t i = 0; i < fields->size() && count < max_fields; ++i) {
        CPDF_Dictionary* field = fields->GetDictAt(i);
        if (!field) continue;
        
        FPDF_FormFieldInfo* info = &out_fields[count];
        memset(info, 0, sizeof(FPDF_FormFieldInfo));
        
        // Name
        std::string name = field->GetStringFor("T");
        strncpy(info->name, name.c_str(), 255);
        
        // Value
        std::string value = field->GetStringFor("V");
        strncpy(info->value, value.c_str(), 1023);
        
        // Type
        std::string ft = field->GetStringFor("FT");
        if (ft == "Btn") {
            int ff = field->GetIntegerFor("Ff");
            if (ff & 0x10000) info->field_type = FPDF_FIELD_RADIOBUTTON;
            else info->field_type = FPDF_FIELD_CHECKBOX;
        } else if (ft == "Tx") info->field_type = FPDF_FIELD_TEXT;
        else if (ft == "Ch") info->field_type = FPDF_FIELD_CHOICE;
        else if (ft == "Sig") info->field_type = FPDF_FIELD_SIGNATURE;
        else info->field_type = FPDF_FIELD_UNKNOWN;
        
        // Bounds (from widget annotation)
        CPDF_Array* kids = field->GetArrayFor("Kids");
        if (kids && kids->size() > 0) {
            CPDF_Dictionary* widget = kids->GetDictAt(0);
            if (widget) {
                CPDF_Array* rect = widget->GetArrayFor("Rect");
                if (rect && rect->size() >= 4) {
                    info->bounds[0] = rect->GetNumberAt(0);
                    info->bounds[1] = rect->GetNumberAt(1);
                    info->bounds[2] = rect->GetNumberAt(2);
                    info->bounds[3] = rect->GetNumberAt(3);
                }
            }
        }
        
        info->flags = field->GetIntegerFor("Ff");
        count++;
    }
    
    return count;
}

int FPDF_ExtractAnnotations(FPDF_PAGE page, FPDF_AnnotInfo* out_annots, int max_annots) {
    if (!page || !out_annots || max_annots <= 0) return 0;
    
    CPDF_Page* pdfium_page = static_cast<CPDF_Page*>(page);
    CPDF_AnnotList annot_list(pdfium_page);
    
    int count = 0;
    for (size_t i = 0; i < annot_list.GetAnnotCount() && count < max_annots; ++i) {
        CPDF_Annot* annot = annot_list.GetAnnot(i);
        if (!annot) continue;
        
        FPDF_AnnotInfo* info = &out_annots[count];
        memset(info, 0, sizeof(FPDF_AnnotInfo));
        
        // Type
        int subtype = annot->GetSubType();
        switch (subtype) {
            case CPDF_Annot::SUBTYPE_TEXT: info->type = FPDF_ANNOT_TEXT; break;
            case CPDF_Annot::SUBTYPE_LINK: info->type = FPDF_ANNOT_LINK; break;
            case CPDF_Annot::SUBTYPE_FREETEXT: info->type = FPDF_ANNOT_FREETEXT; break;
            case CPDF_Annot::SUBTYPE_LINE: info->type = FPDF_ANNOT_LINE; break;
            case CPDF_Annot::SUBTYPE_SQUARE: info->type = FPDF_ANNOT_SQUARE; break;
            case CPDF_Annot::SUBTYPE_CIRCLE: info->type = FPDF_ANNOT_CIRCLE; break;
            case CPDF_Annot::SUBTYPE_POLYGON: info->type = FPDF_ANNOT_POLYGON; break;
            case CPDF_Annot::SUBTYPE_POLYLINE: info->type = FPDF_ANNOT_POLYLINE; break;
            case CPDF_Annot::SUBTYPE_HIGHLIGHT: info->type = FPDF_ANNOT_HIGHLIGHT; break;
            case CPDF_Annot::SUBTYPE_UNDERLINE: info->type = FPDF_ANNOT_UNDERLINE; break;
            case CPDF_Annot::SUBTYPE_SQUIGGLY: info->type = FPDF_ANNOT_SQUIGGLY; break;
            case CPDF_Annot::SUBTYPE_STRIKEOUT: info->type = FPDF_ANNOT_STRIKEOUT; break;
            case CPDF_Annot::SUBTYPE_STAMP: info->type = FPDF_ANNOT_STAMP; break;
            case CPDF_Annot::SUBTYPE_INK: info->type = FPDF_ANNOT_INK; break;
            case CPDF_Annot::SUBTYPE_FILEATTACHMENT: info->type = FPDF_ANNOT_FILEATTACHMENT; break;
            case CPDF_Annot::SUBTYPE_SOUND: info->type = FPDF_ANNOT_SOUND; break;
            case CPDF_Annot::SUBTYPE_MOVIE: info->type = FPDF_ANNOT_MOVIE; break;
            case CPDF_Annot::SUBTYPE_WIDGET: info->type = FPDF_ANNOT_WIDGET; break;
            case CPDF_Annot::SUBTYPE_SCREEN: info->type = FPDF_ANNOT_SCREEN; break;
            case CPDF_Annot::SUBTYPE_WATERMARK: info->type = FPDF_ANNOT_WATERMARK; break;
            case CPDF_Annot::SUBTYPE_3D: info->type = FPDF_ANNOT_3D; break;
            default: info->type = FPDF_ANNOT_UNKNOWN; break;
        }
        
        // Contents
        std::string contents = annot->GetContents();
        strncpy(info->contents, contents.c_str(), 2047);
        
        // Bounds
        CFX_FloatRect rect = annot->GetRect();
        info->bounds[0] = rect.left;
        info->bounds[1] = rect.bottom;
        info->bounds[2] = rect.right;
        info->bounds[3] = rect.top;
        
        info->flags = annot->GetFlags();
        count++;
    }
    
    return count;
}

int FPDF_ExtractBookmarks(FPDF_DOCUMENT document, FPDF_BookmarkInfo* out_bookmarks, int max_bookmarks) {
    if (!document || !out_bookmarks || max_bookmarks <= 0) return 0;
    
    CPDF_Document* doc = static_cast<CPDF_Document*>(document);
    CPDF_Dictionary* root = doc->GetRoot();
    if (!root) return 0;
    
    CPDF_Dictionary* outlines = root->GetDictFor("Outlines");
    if (!outlines) return 0;
    
    // Recursive bookmark extraction would go here
    // Simplified for now
    return 0;
}

int FPDF_GetDocumentStructure(FPDF_DOCUMENT document, FPDF_DocumentStructure* out_info) {
    if (!document || !out_info) return 0;
    
    CPDF_Document* doc = static_cast<CPDF_Document*>(document);
    memset(out_info, 0, sizeof(FPDF_DocumentStructure));
    
    out_info->page_count = doc->GetPageCount();
    out_info->object_count = doc->GetAllIndirectObjects().size();
    
    // Count fonts, images, etc.
    for (int i = 0; i < doc->GetPageCount(); ++i) {
        CPDF_Page* page = doc->GetPage(i);
        if (!page) continue;
        
        CPDF_PageObject* obj = page->GetFirstObject();
        while (obj) {
            switch (obj->GetType()) {
                case PDFPAGE_IMAGE: out_info->image_count++; break;
                case PDFPAGE_TEXT: {
                    CPDF_TextObject* text_obj = static_cast<CPDF_TextObject*>(obj);
                    if (text_obj->GetFont()) out_info->font_count++;
                    break;
                }
            }
            obj = obj->GetNext();
        }
    }
    
    // Form fields
    CPDF_Dictionary* root = doc->GetRoot();
    if (root) {
        CPDF_Dictionary* acro_form = root->GetDictFor("AcroForm");
        if (acro_form) {
            CPDF_Array* fields = acro_form->GetArrayFor("Fields");
            if (fields) out_info->form_field_count = fields->size();
        }
    }
    
    // Annotations
    for (int i = 0; i < doc->GetPageCount(); ++i) {
        CPDF_Page* page = doc->GetPage(i);
        if (page) {
            CPDF_AnnotList annot_list(page);
            out_info->annotation_count += annot_list.GetAnnotCount();
        }
    }
    
    // Metadata
    CPDF_Dictionary* info = doc->GetInfoDict();
    if (info) {
        std::string producer = info->GetStringFor("Producer");
        std::string creator = info->GetStringFor("Creator");
        std::string creation = info->GetStringFor("CreationDate");
        std::string mod = info->GetStringFor("ModDate");
        
        strncpy(out_info->producer, producer.c_str(), 255);
        strncpy(out_info->creator, creator.c_str(), 255);
        strncpy(out_info->creation_date, creation.c_str(), 63);
        strncpy(out_info->mod_date, mod.c_str(), 63);
    }
    
    return 1;
}