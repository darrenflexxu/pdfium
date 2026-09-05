#include "fpdf_ext_element.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdfapi/page/cpdf_pathobject.h"
#include "core/fpdfapi/page/cpdf_image.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfdoc/cpdf_annot.h"
#include "core/fpdfdoc/cpdf_annotlist.h"
#include "core/fpdfdoc/cpdf_viewerpreferences.h"
#include "core/fpdftext/cpdf_textpage.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "fpdf_text.h"
#include "stl_util.h"
#include <vector>
#include <string>
#include <map>

// Internal wrapper to hold PDFium objects since FPDF_PageElement 
// in the public header is just a data struct.
struct PageElementInternal {
    FPDF_PageElement public_elem;
    CPDF_PageObject* pdfium_obj;
    std::string text_cache;
    std::vector<uint8_t> image_cache;
    std::vector<unsigned char> path_cache;

    PageElementInternal() {
        std::memset(&public_elem, 0, sizeof(public_elem));
    }
};

// Forward declarations
struct FPDF_ElementIterator {
    FPDF_PAGE page;
    CPDF_PageObject* current_obj;
    bool first_call;
    int index = 0;
};

static FPDF_PageElement* CreatePageElement(CPDF_PageObject* obj) {
    if (!obj) return nullptr;
    
    PageElementInternal* internal = new PageElementInternal();
    internal->pdfium_obj = obj;
    
    // Bounds: Use GetRect()
    CFX_FloatRect rect = obj->GetRect();
    internal->public_elem.bounds[0] = rect.left;
    internal->public_elem.bounds[1] = rect.bottom;
    internal->public_elem.bounds[2] = rect.right;
    internal->public_elem.bounds[3] = rect.top;
    
    switch (obj->GetType()) {
        case CPDF_PageObject::Type::kText: {
            internal->public_elem.type = FPDF_ELEMENT_TEXT;
            CPDF_TextObject* text_obj = static_cast<CPDF_TextObject*>(obj);
            
            CPDF_Font* font = text_obj->GetFont();
            if (font) {
                internal->public_elem.attributes.text.font_size = text_obj->GetFontSize();
                internal->public_elem.attributes.text.char_spacing = 0.0;
                internal->public_elem.attributes.text.word_spacing = 0.0;
                internal->public_elem.attributes.text.horizontal_scaling = 1.0;
                internal->public_elem.attributes.text.leading = 0.0;
                internal->public_elem.attributes.text.font_flags = font->GetFontFlags();
                strncpy(internal->public_elem.attributes.text.font_name, font->GetBaseFontName().c_str(), 255);
                strncpy(internal->public_elem.attributes.text.font_family, font->GetFont()->GetFamilyName().c_str(), 127);
            }
            
            CFX_Matrix matrix = text_obj->GetTextMatrix();
            internal->public_elem.attributes.text.text_matrix[0] = matrix.a;
            internal->public_elem.attributes.text.text_matrix[1] = matrix.b;
            internal->public_elem.attributes.text.text_matrix[2] = matrix.c;
            internal->public_elem.attributes.text.text_matrix[3] = matrix.d;
            internal->public_elem.attributes.text.text_matrix[4] = matrix.e;
            internal->public_elem.attributes.text.text_matrix[5] = matrix.f;
            break;
        }
        case CPDF_PageObject::Type::kImage: {
            internal->public_elem.type = FPDF_ELEMENT_IMAGE;
            CPDF_ImageObject* img_obj = static_cast<CPDF_ImageObject*>(obj);
            const CPDF_Stream* stream = img_obj->GetImage()->GetStream();
            
            if (stream) {
                const CPDF_Dictionary* dict = stream->GetDict();
                internal->public_elem.attributes.image.width = dict->GetIntegerFor("Width");
                internal->public_elem.attributes.image.height = dict->GetIntegerFor("Height");
                internal->public_elem.attributes.image.bits_per_component = dict->GetIntegerFor("BitsPerComponent");
                
                const CPDF_Object* cs_obj = dict->GetDirectObjectFor("ColorSpace");
                if (cs_obj && cs_obj->IsName()) {
                    auto cs_name = cs_obj->GetString();
                    if (cs_name == "DeviceGray") internal->public_elem.attributes.image.color_space = FPDF_COLORSPACE_GRAY;
                    else if (cs_name == "DeviceRGB") internal->public_elem.attributes.image.color_space = FPDF_COLORSPACE_RGB;
                    else if (cs_name == "DeviceCMYK") internal->public_elem.attributes.image.color_space = FPDF_COLORSPACE_CMYK;
                    else if (cs_name == "Indexed") internal->public_elem.attributes.image.color_space = FPDF_COLORSPACE_INDEXED;
                }
                
                const CPDF_Object* filter_obj = dict->GetDirectObjectFor("Filter");
                if (filter_obj && filter_obj->IsName()) {
                    auto filter = filter_obj->GetString();
                    if (filter == "FlateDecode") internal->public_elem.attributes.image.filter = 1;
                    else if (filter == "DCTDecode") internal->public_elem.attributes.image.filter = 2;
                    else if (filter == "JPXDecode") internal->public_elem.attributes.image.filter = 3;
                    else if (filter == "CCITTFaxDecode") internal->public_elem.attributes.image.filter = 4;
                }
            }
            
            CFX_Matrix matrix = img_obj->matrix();
            internal->public_elem.attributes.image.matrix[0] = matrix.a;
            internal->public_elem.attributes.image.matrix[1] = matrix.b;
            internal->public_elem.attributes.image.matrix[2] = matrix.c;
            internal->public_elem.attributes.image.matrix[3] = matrix.d;
            internal->public_elem.attributes.image.matrix[4] = matrix.e;
            internal->public_elem.attributes.image.matrix[5] = matrix.f;
            break;
        }
        case CPDF_PageObject::Type::kPath: {
            internal->public_elem.type = FPDF_ELEMENT_PATH;
            CPDF_PathObject* path_obj = static_cast<CPDF_PathObject*>(obj);
            
            CFX_FloatRect rect = path_obj->GetRect();
            internal->public_elem.attributes.path.fill_color_rgb = 0; 
            internal->public_elem.attributes.path.fill_color_alpha = 0;
            internal->public_elem.attributes.path.stroke_color_rgb = 0;
            internal->public_elem.attributes.path.stroke_color_alpha = 0;
            internal->public_elem.attributes.path.line_width = 1.0;
            internal->public_elem.attributes.path.line_cap = 0;
            internal->public_elem.attributes.path.line_join = 0;
            internal->public_elem.attributes.path.miter_limit = 10.0;
            internal->public_elem.attributes.path.fill_rule = 0;
            
            const std::vector<float>& dash = path_obj->graphic_states().graph_state().GetLineDashArray();
            internal->public_elem.attributes.path.dash_count = std::min<int>(dash.size(), 16);
            for (int i = 0; i < internal->public_elem.attributes.path.dash_count; i++) {
                internal->public_elem.attributes.path.dash_pattern[i] = dash[i];
            }
            internal->public_elem.attributes.path.dash_phase = path_obj->graphic_states().graph_state().GetLineDashPhase();
            
            CFX_Matrix matrix = path_obj->matrix();
            internal->public_elem.attributes.image.matrix[0] = matrix.a;
            internal->public_elem.attributes.image.matrix[1] = matrix.b;
            internal->public_elem.attributes.image.matrix[2] = matrix.c;
            internal->public_elem.attributes.image.matrix[3] = matrix.d;
            internal->public_elem.attributes.image.matrix[4] = matrix.e;
            internal->public_elem.attributes.image.matrix[5] = matrix.f;
            break;
        }
        case CPDF_PageObject::Type::kShading:
            internal->public_elem.type = FPDF_ELEMENT_SHADING;
            break;
        case CPDF_PageObject::Type::kForm:
            internal->public_elem.type = FPDF_ELEMENT_FORM;
            break;
        default:
            internal->public_elem.type = FPDF_ELEMENT_UNKNOWN;
            break;
    }
    
    return &internal->public_elem;
}

static void DestroyPageElement(FPDF_PageElement* element) {
    if (!element) return;
    PageElementInternal* internal = reinterpret_cast<PageElementInternal*>(element);
    delete internal;
}

FPDF_ElementIterator* FPDF_CreateElementIterator(FPDF_PAGE page) {
    if (!page) return nullptr;
    
    CPDF_Page* pdfium_page = CPDFPageFromFPDFPage(page);
    FPDF_ElementIterator* iter = new FPDF_ElementIterator();
    iter->page = page;
    iter->current_obj = pdfium_page->GetPageObjectByIndex(iter->index);
    iter->first_call = true;
    return iter;
}

FPDF_PageElement* FPDF_GetNextElement(FPDF_ElementIterator* iterator) {
    if (!iterator || !iterator->current_obj) return nullptr;
    
    CPDF_PageObject* obj = iterator->current_obj;
    CPDF_Page* pdfium_page = CPDFPageFromFPDFPage(iterator->page);
    iterator->index += 1;
    iterator->current_obj = pdfium_page->GetPageObjectByIndex(iterator->index);
    
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
    
    PageElementInternal* internal = reinterpret_cast<PageElementInternal*>(element);
    CPDF_TextObject* text_obj = static_cast<CPDF_TextObject*>(internal->pdfium_obj);
    if (!text_obj) {
        if (out_length) *out_length = 0;
        return "";
    }
    
    WideString text;
    
    for (int i = 0; i < text_obj->CountWords(); ++i) {
        text += text_obj->GetWordString(i);
    }
    internal->text_cache = text.ToUTF8().c_str();
    
    if (out_length) *out_length = internal->text_cache.size();
    return internal->text_cache.c_str();
}

unsigned char* FPDF_GetElementImageData(FPDF_PageElement* element, size_t* out_size) {
    if (!element || element->type != FPDF_ELEMENT_IMAGE) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    PageElementInternal* internal = reinterpret_cast<PageElementInternal*>(element);
    CPDF_ImageObject* img_obj = static_cast<CPDF_ImageObject*>(internal->pdfium_obj);
    if (!img_obj) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    const CPDF_Stream* stream = img_obj->GetImage()->GetStream();
    if (!stream) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    auto raw = stream->ReadAllRawData();
    internal->image_cache.clear();
    internal->image_cache.insert(internal->image_cache.end(), raw.begin(), raw.end());
    *out_size = internal->image_cache.size();
    
    if (internal->image_cache.empty()) return nullptr;
    return internal->image_cache.data();
}

unsigned char* FPDF_GetElementPathData(FPDF_PageElement* element, size_t* out_size) {
    if (!element || element->type != FPDF_ELEMENT_PATH) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    
    PageElementInternal* internal = reinterpret_cast<PageElementInternal*>(element);
    CPDF_PathObject* path_obj = static_cast<CPDF_PathObject*>(internal->pdfium_obj);
    if (!path_obj) {
        if (out_size) *out_size = 0;
        return nullptr;
    }
    auto path = path_obj->path();
    const auto& points = path.GetPoints();
    internal->path_cache.resize(points.size() * sizeof(CFX_Point) + 1);
    internal->path_cache[0] = static_cast<unsigned char>(points.size());
    memcpy(internal->path_cache.data() + 1, points.data(), points.size() * sizeof(CFX_Point));
    
    *out_size = internal->path_cache.size();
    return internal->path_cache.data();
}

void FPDF_FreeElementData(void* data) {
    if (data) {
        DestroyPageElement(reinterpret_cast<FPDF_PageElement*>(data));
    }
}

int FPDF_CountPageElements(FPDF_PAGE page) {
    if (!page) return 0;
    CPDF_Page* pdfium_page = CPDFPageFromFPDFPage(page);
    int count = pdfium_page->GetPageObjectCount();
    return count;
}

FPDF_PageElement* FPDF_GetPageElement(FPDF_PAGE page, int index) {
    if (!page || index < 0) return nullptr;
    CPDF_Page* pdfium_page = CPDFPageFromFPDFPage(page);
    CPDF_PageObject* obj = pdfium_page->GetPageObjectByIndex(index);
    return CreatePageElement(obj);
}

int FPDF_FindElementsByType(FPDF_PAGE page, FPDF_ElementType type, FPDF_PageElement** out_elements, int max_count) {
    if (!page || !out_elements || max_count <= 0) return 0;
    
    CPDF_Page* pdfium_page = CPDFPageFromFPDFPage(page);
    int found = 0;
    
    int page_obj_count = pdfium_page->GetPageObjectCount();
    for (int i = 0; i < page_obj_count && found < max_count; ++i) {
        auto obj = pdfium_page->GetPageObjectByIndex(i);
        FPDF_ElementType obj_type = FPDF_ELEMENT_UNKNOWN;
        switch (obj->GetType()) {
            case CPDF_PageObject::Type::kText: obj_type = FPDF_ELEMENT_TEXT; break;
            case CPDF_PageObject::Type::kImage: obj_type = FPDF_ELEMENT_IMAGE; break;
            case CPDF_PageObject::Type::kPath: obj_type = FPDF_ELEMENT_PATH; break;
            case CPDF_PageObject::Type::kShading: obj_type = FPDF_ELEMENT_SHADING; break;
            case CPDF_PageObject::Type::kForm: obj_type = FPDF_ELEMENT_FORM; break;
        }
        
        if (obj_type == type) {
            out_elements[found++] = CreatePageElement(obj);
        }
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
    
    CPDF_Page* pdfium_page = CPDFPageFromFPDFPage(page);
    CPDF_ViewerPreferences viewRef(pdfium_page->GetDocument());
    auto text_page =
        std::make_unique<CPDF_TextPage>(pdfium_page, viewRef.IsDirectionR2L());
    FPDF_TEXTPAGE fpdf_text_page = FPDFTextPageFromCPDFTextPage(text_page.get());
    auto wide_str = WideString::FromUTF8(search_text);
    FPDF_SCHHANDLE handle = FPDFText_FindStart(fpdf_text_page, (FPDF_WIDESTRING)wide_str.c_str(), flags, start_index);
    if (!handle) {
        *out_count = 0;
        return 0;
    }
    
    int found = 0;
    while (found < max_results && FPDFText_FindNext(handle)) {
        int index = FPDFText_GetSchResultIndex(handle);
        if (index < start_index) continue;
        
        double x1, y1, x2, y2;
        //FPDFText_GetSchResultRect(handle, &x1, &y1, &x2, &y2);
        
        out_matches[found].bounds[0] = x1;
        out_matches[found].bounds[1] = y1;
        out_matches[found].bounds[2] = x2;
        out_matches[found].bounds[3] = y2;
        out_matches[found].char_index = index;
        out_matches[found].element_index = -1;
        out_matches[found].element = nullptr;
        
        found++;
    }
    
    FPDFText_FindClose(handle);
    *out_count = found;
    return found;
}

int FPDF_ExtractFormFields(FPDF_DOCUMENT document, FPDF_FormFieldInfo* out_fields, int max_fields) {
    if (!document || !out_fields || max_fields <= 0) return 0;
    
    CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
    const CPDF_Dictionary* root = doc->GetRoot();
    if (!root) return 0;
    
    const CPDF_Dictionary* acro_form = root->GetDictFor("AcroForm");
    if (!acro_form) return 0;
    
    const CPDF_Array* fields = acro_form->GetArrayFor("Fields");
    if (!fields) return 0;
    
    int count = 0;
    for (size_t i = 0; i < fields->size() && count < max_fields; ++i) {
        const CPDF_Dictionary* field = fields->GetDictAt(i);
        if (!field) continue;
        
        FPDF_FormFieldInfo* info = &out_fields[count];
        memset(info, 0, sizeof(FPDF_FormFieldInfo));
        
        auto name = field->GetStringFor("T")->GetString();
        strncpy(info->name, name.c_str(), 255);
        
        auto value = field->GetStringFor("V")->GetString();
        strncpy(info->value, value.c_str(), 1023);
        
        auto ft = field->GetStringFor("FT")->GetString();
        if (ft == "Btn") {
            int ff = field->GetIntegerFor("Ff");
            if (ff & 0x10000) info->field_type = FPDF_FIELD_RADIOBUTTON;
            else info->field_type = FPDF_FIELD_CHECKBOX;
        } else if (ft == "Tx") info->field_type = FPDF_FIELD_TEXT;
        else if (ft == "Ch") info->field_type = FPDF_FIELD_CHOICE;
        else if (ft == "Sig") info->field_type = FPDF_FIELD_SIGNATURE;
        else info->field_type = FPDF_FIELD_UNKNOWN;
        
        const CPDF_Array* kids = field->GetArrayFor("Kids");
        if (kids && kids->size() > 0) {
            const CPDF_Dictionary* widget = kids->GetDictAt(0);
            if (widget) {
                const CPDF_Array* rect = widget->GetArrayFor("Rect");
                if (rect && rect->size() >= 4) {
                    info->bounds[0] = rect->GetNumberAt(0)->GetNumber();
                    info->bounds[1] = rect->GetNumberAt(1)->GetNumber();
                    info->bounds[2] = rect->GetNumberAt(2)->GetNumber();
                    info->bounds[3] = rect->GetNumberAt(3)->GetNumber();
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
    
    CPDF_Page* pdfium_page = CPDFPageFromFPDFPage(page);
    CPDF_AnnotList annot_list(pdfium_page);
    
    int count = 0;
    for (size_t i = 0; i < annot_list.Count() && count < max_annots; ++i) {
        CPDF_Annot* annot = annot_list.GetAt(i);
        if (!annot) continue;
        
        FPDF_AnnotInfo* info = &out_annots[count];
        memset(info, 0, sizeof(FPDF_AnnotInfo));
        
        auto subtype = annot->GetSubtype();
        switch (subtype) {
            case CPDF_Annot::Subtype::TEXT: info->type = FPDF_ANNOT_TEXT; break;
            case CPDF_Annot::Subtype::LINK: info->type = FPDF_ANNOT_LINK; break;
            case CPDF_Annot::Subtype::FREETEXT: info->type = FPDF_ANNOT_FREETEXT; break;
            case CPDF_Annot::Subtype::LINE: info->type = FPDF_ANNOT_LINE; break;
            case CPDF_Annot::Subtype::SQUARE: info->type = FPDF_ANNOT_SQUARE; break;
            case CPDF_Annot::Subtype::CIRCLE: info->type = FPDF_ANNOT_CIRCLE; break;
            case CPDF_Annot::Subtype::POLYGON: info->type = FPDF_ANNOT_POLYGON; break;
            case CPDF_Annot::Subtype::POLYLINE: info->type = FPDF_ANNOT_POLYLINE; break;
            case CPDF_Annot::Subtype::HIGHLIGHT: info->type = FPDF_ANNOT_HIGHLIGHT; break;
            case CPDF_Annot::Subtype::UNDERLINE: info->type = FPDF_ANNOT_UNDERLINE; break;
            case CPDF_Annot::Subtype::SQUIGGLY: info->type = FPDF_ANNOT_SQUIGGLY; break;
            case CPDF_Annot::Subtype::STRIKEOUT: info->type = FPDF_ANNOT_STRIKEOUT; break;
            case CPDF_Annot::Subtype::STAMP: info->type = FPDF_ANNOT_STAMP; break;
            case CPDF_Annot::Subtype::INK: info->type = FPDF_ANNOT_INK; break;
            case CPDF_Annot::Subtype::FILEATTACHMENT: info->type = FPDF_ANNOT_FILEATTACHMENT; break;
            case CPDF_Annot::Subtype::SOUND: info->type = FPDF_ANNOT_SOUND; break;
            case CPDF_Annot::Subtype::MOVIE: info->type = FPDF_ANNOT_MOVIE; break;
            case CPDF_Annot::Subtype::WIDGET: info->type = FPDF_ANNOT_WIDGET; break;
            case CPDF_Annot::Subtype::SCREEN: info->type = FPDF_ANNOT_SCREEN; break;
            case CPDF_Annot::Subtype::WATERMARK: info->type = FPDF_ANNOT_WATERMARK; break;
            case CPDF_Annot::Subtype::THREED: info->type = FPDF_ANNOT_3D; break;
            default: info->type = FPDF_ANNOT_UNKNOWN; break;
        }
        
        auto contents = annot->GetAnnotDict()->GetStringFor("Contents")->GetString();
        strncpy(info->contents, contents.c_str(), 2047);
        
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
    
    CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
    const CPDF_Dictionary* root = doc->GetRoot();
    if (!root) return 0;
    
    const CPDF_Dictionary* outlines = root->GetDictFor("Outlines");
    if (!outlines) return 0;
    
    return 0;
}

int FPDF_GetDocumentStructure(FPDF_DOCUMENT document, FPDF_DocumentStructure* out_info) {
    if (!document || !out_info) return 0;
    
    CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
    memset(out_info, 0, sizeof(FPDF_DocumentStructure));
    
    // 1. Basic counts
    out_info->page_count = doc->GetPageCount();
    
    // 2. Traverse all pages for resource counts
    for (int i = 0; i < doc->GetPageCount(); ++i) {
        auto page_dict = doc->GetMutablePageDictionary(i);
        if (!page_dict) continue;
        auto page = pdfium::MakeRetain<CPDF_Page>(doc, std::move(page_dict));
        page->ParseContent();
        
        for (int j = 0; j < page->GetPageObjectCount(); ++j) {
            auto obj = page->GetPageObjectByIndex(j);
            switch (obj->GetType()) {
                case CPDF_PageObject::Type::kImage: out_info->image_count++; break;
                case CPDF_PageObject::Type::kText: 
                    if (static_cast<CPDF_TextObject*>(obj)->GetFont()) out_info->font_count++;
                    break;
                case CPDF_PageObject::Type::kPath: 
                    // path_count is not in FPDF_DocumentStructure, skipping
                    break;
                default: break;
            }
        }
        
        // Count annotations for this page
        CPDF_AnnotList annots(page);
        out_info->annotation_count += annots.Count();
    }
    
    // 3. Count Form Fields from root /AcroForm
    const CPDF_Dictionary* root = doc->GetRoot();
    if (root) {
        const CPDF_Dictionary* acro_form = root->GetDictFor("AcroForm");
        if (acro_form) {
            const CPDF_Array* fields = acro_form->GetArrayFor("Fields");
            if (fields) out_info->form_field_count = fields->size();
        }
    }
    
    // 4. Metadata extraction
    const CPDF_Dictionary* info = doc->GetInfo();
    if (info) {
        auto get_str = [&](const char* key) {
            const CPDF_String* s = info->GetStringFor(key);
            return s ? s->GetString() : "";
        };
        strncpy(out_info->producer, get_str("Producer").c_str(), 255);
        strncpy(out_info->creator, get_str("Creator").c_str(), 255);
        strncpy(out_info->creation_date, get_str("CreationDate").c_str(), 63);
        strncpy(out_info->mod_date, get_str("ModDate").c_str(), 63);
    }
    
    return 1;
}
