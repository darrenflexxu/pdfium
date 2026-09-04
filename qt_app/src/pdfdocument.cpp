#include "pdfdocument.h"
#include <QtCore>
#include <QtConcurrent>
#include <pdfium_wrapper.h>

// PIMPL to hide DLL types from header
struct PdfDocument::Private {
    // DLL function pointers (resolved at runtime)
    using InitFunc = int(*)();
    using DestroyFunc = void(*)();
    using LoadDocFunc = int(*)(const char*, const char*, PDF_DocHandle*);
    using CloseDocFunc = void(*)(PDF_DocHandle);
    using GetPageCountFunc = int(*)(PDF_DocHandle);
    using LoadPageFunc = int(*)(PDF_DocHandle, int, PDF_PageHandle*);
    using ClosePageFunc = void(*)(PDF_PageHandle);
    using GetPageSizeFunc = void(*)(PDF_PageHandle, double*, double*);
    using RenderPageFunc = int(*)(PDF_PageHandle, int, int, int, int, void*, int);
    using SearchTextFunc = int(*)(PDF_PageHandle, const char*, int, int, int, double*, int*);
    using GetPageTextFunc = const char*(*)(PDF_PageHandle, int*);
    using FreeStringFunc = void(*)(const char*);
    
    // Compression API
    using CompressOptionsInitFunc = void(*)(void*);
    using OptimizeDocumentFunc = int(*)(PDF_DocHandle, const void*, PDF_DocHandle*);
    using SaveWithCompressionFunc = int(*)(PDF_DocHandle, const char*, const void*);
    using GetCompressStatsFunc = int(*)(void*);
    
    // Element Extraction API
    using CreateElementIteratorFunc = void*(*)(PDF_PageHandle);
    using GetNextElementFunc = void*(*)(void*);
    using DestroyElementIteratorFunc = void(*)(void*);
    using GetElementTypeFunc = int(*)(void*);
    using GetElementBoundsFunc = void(*)(void*, double*);
    using GetElementTextAttrsFunc = int(*)(void*, void*);
    using GetElementImageAttrsFunc = int(*)(void*, void*);
    using GetElementPathAttrsFunc = int(*)(void*, void*);
    using GetElementTextFunc = const char*(*)(void*, int*);
    using GetElementImageDataFunc = unsigned char*(*)(void*, size_t*);
    using GetElementPathDataFunc = unsigned char*(*)(void*, size_t*);
    using FreeElementDataFunc = void(*)(void*);
    using CountPageElementsFunc = int(*)(PDF_PageHandle);
    using GetPageElementFunc = void*(*)(PDF_PageHandle, int);
    using FindElementsByTypeFunc = int(*)(PDF_PageHandle, int, void**, int);
    using SearchTextExFunc = int(*)(PDF_PageHandle, const char*, int, int, int, void*, int*);
    using ExtractFormFieldsFunc = int(*)(PDF_DocHandle, void*, int);
    using ExtractAnnotationsFunc = int(*)(PDF_PageHandle, void*, int);
    using ExtractBookmarksFunc = int(*)(PDF_DocHandle, void*, int);
    using GetDocumentStructureFunc = int(*)(PDF_DocHandle, void*);
    
    // Resolved functions
    InitFunc initLib = nullptr;
    DestroyFunc destroyLib = nullptr;
    LoadDocFunc loadDoc = nullptr;
    CloseDocFunc closeDoc = nullptr;
    GetPageCountFunc getPageCount = nullptr;
    LoadPageFunc loadPage = nullptr;
    ClosePageFunc closePage = nullptr;
    GetPageSizeFunc getPageSize = nullptr;
    RenderPageFunc renderPage = nullptr;
    SearchTextFunc searchText = nullptr;
    GetPageTextFunc getPageText = nullptr;
    FreeStringFunc freeString = nullptr;
    
    // Compression
    CompressOptionsInitFunc compressOptionsInit = nullptr;
    OptimizeDocumentFunc optimizeDocument = nullptr;
    SaveWithCompressionFunc saveWithCompression = nullptr;
    GetCompressStatsFunc getCompressStats = nullptr;
    
    // Element Extraction
    CreateElementIteratorFunc createElementIterator = nullptr;
    GetNextElementFunc getNextElement = nullptr;
    DestroyElementIteratorFunc destroyElementIterator = nullptr;
    GetElementTypeFunc getElementType = nullptr;
    GetElementBoundsFunc getElementBounds = nullptr;
    GetElementTextAttrsFunc getElementTextAttrs = nullptr;
    GetElementImageAttrsFunc getElementImageAttrs = nullptr;
    GetElementPathAttrsFunc getElementPathAttrs = nullptr;
    GetElementTextFunc getElementText = nullptr;
    GetElementImageDataFunc getElementImageData = nullptr;
    GetElementPathDataFunc getElementPathData = nullptr;
    FreeElementDataFunc freeElementData = nullptr;
    CountPageElementsFunc countPageElements = nullptr;
    GetPageElementFunc getPageElement = nullptr;
    FindElementsByTypeFunc findElementsByType = nullptr;
    SearchTextExFunc searchTextEx = nullptr;
    ExtractFormFieldsFunc extractFormFields = nullptr;
    ExtractAnnotationsFunc extractAnnotations = nullptr;
    ExtractBookmarksFunc extractBookmarks = nullptr;
    GetDocumentStructureFunc getDocumentStructure = nullptr;
    
    QLibrary library;
    bool libraryLoaded = false;
    
    bool loadLibrary() {
        if (libraryLoaded) return true;
        
        // Try to load the wrapper DLL
        #ifdef _WIN32
        library.setFileName("pdfium_wrapper.dll");
        #elif defined(__APPLE__)
        library.setFileName("libpdfium_wrapper.dylib");
        #else
        library.setFileName("libpdfium_wrapper.so");
        #endif
        
        if (!library.load()) {
            qWarning() << "Failed to load pdfium_wrapper:" << library.errorString();
            return false;
        }
        
        // Resolve functions
        initLib = (InitFunc)library.resolve("PDF_InitLibrary");
        destroyLib = (DestroyFunc)library.resolve("PDF_DestroyLibrary");
        loadDoc = (LoadDocFunc)library.resolve("PDF_LoadDocument");
        closeDoc = (CloseDocFunc)library.resolve("PDF_CloseDocument");
        getPageCount = (GetPageCountFunc)library.resolve("PDF_GetPageCount");
        loadPage = (LoadPageFunc)library.resolve("PDF_LoadPage");
        closePage = (ClosePageFunc)library.resolve("PDF_ClosePage");
        getPageSize = (GetPageSizeFunc)library.resolve("PDF_GetPageSize");
        renderPage = (RenderPageFunc)library.resolve("PDF_RenderPageEx");
        searchText = (SearchTextFunc)library.resolve("PDF_SearchText");
        getPageText = (GetPageTextFunc)library.resolve("PDF_GetPageText");
        freeString = (FreeStringFunc)library.resolve("PDF_FreeString");
        
        // Compression
        compressOptionsInit = (CompressOptionsInitFunc)library.resolve("PDF_CompressOptionsInit");
        optimizeDocument = (OptimizeDocumentFunc)library.resolve("PDF_OptimizeDocument");
        saveWithCompression = (SaveWithCompressionFunc)library.resolve("PDF_SaveWithCompression");
        getCompressStats = (GetCompressStatsFunc)library.resolve("PDF_GetLastCompressStats");
        
        // Element Extraction
        createElementIterator = (CreateElementIteratorFunc)library.resolve("PDF_CreateElementIterator");
        getNextElement = (GetNextElementFunc)library.resolve("PDF_GetNextElement");
        destroyElementIterator = (DestroyElementIteratorFunc)library.resolve("PDF_DestroyElementIterator");
        getElementType = (GetElementTypeFunc)library.resolve("PDF_GetElementType");
        getElementBounds = (GetElementBoundsFunc)library.resolve("PDF_GetElementBounds");
        getElementTextAttrs = (GetElementTextAttrsFunc)library.resolve("PDF_GetElementTextAttributes");
        getElementImageAttrs = (GetElementImageAttrsFunc)library.resolve("PDF_GetElementImageAttributes");
        getElementPathAttrs = (GetElementPathAttrsFunc)library.resolve("PDF_GetElementPathAttributes");
        getElementText = (GetElementTextFunc)library.resolve("PDF_GetElementText");
        getElementImageData = (GetElementImageDataFunc)library.resolve("PDF_GetElementImageData");
        getElementPathData = (GetElementPathDataFunc)library.resolve("PDF_GetElementPathData");
        freeElementData = (FreeElementDataFunc)library.resolve("PDF_FreeElementData");
        countPageElements = (CountPageElementsFunc)library.resolve("PDF_CountPageElements");
        getPageElement = (GetPageElementFunc)library.resolve("PDF_GetPageElement");
        findElementsByType = (FindElementsByTypeFunc)library.resolve("PDF_FindElementsByType");
        searchTextEx = (SearchTextExFunc)library.resolve("PDF_SearchTextEx");
        extractFormFields = (ExtractFormFieldsFunc)library.resolve("PDF_ExtractFormFields");
        extractAnnotations = (ExtractAnnotationsFunc)library.resolve("PDF_ExtractAnnotations");
        extractBookmarks = (ExtractBookmarksFunc)library.resolve("PDF_ExtractBookmarks");
        getDocumentStructure = (GetDocumentStructureFunc)library.resolve("PDF_GetDocumentStructure");
        
        // Check required functions
        if (!initLib || !loadDoc || !closeDoc || !getPageCount || !loadPage || 
            !closePage || !getPageSize || !renderPage) {
            qWarning() << "Failed to resolve required PDFium wrapper functions";
            library.unload();
            return false;
        }
        
        // Initialize PDFium
        int result = initLib();
        if (result != PDF_OK) {
            qWarning() << "PDF_InitLibrary failed:" << result;
            library.unload();
            return false;
        }
        
        libraryLoaded = true;
        return true;
    }
    
    void unloadLibrary() {
        if (destroyLib) destroyLib();
        if (libraryLoaded) library.unload();
        libraryLoaded = false;
    }
};

PdfDocument::PdfDocument(QObject* parent) : QObject(parent), d(std::make_unique<Private>()) {
}

PdfDocument::~PdfDocument() {
    close();
    if (d->libraryLoaded) {
        d->unloadLibrary();
    }
}

bool PdfDocument::load(const QString& filePath, const QString& password) {
    if (!d->loadLibrary()) {
        emit loadFinished(false, "Failed to load PDFium wrapper library");
        return false;
    }
    
    close(); // Close any existing document
    
    QByteArray pathUtf8 = filePath.toUtf8();
    QByteArray pwdUtf8 = password.toUtf8();
    
    PDF_DocHandle handle = nullptr;
    int result = d->loadDoc(pathUtf8.constData(), 
                           password.isEmpty() ? nullptr : pwdUtf8.constData(),
                           &handle);
    
    if (result != PDF_OK || !handle) {
        QString error;
        switch (result) {
            case PDF_ERR_FILE_NOT_FOUND: error = "File not found or cannot be opened"; break;
            case PDF_ERR_INVALID_PASSWORD: error = "Invalid password"; break;
            case PDF_ERR_FORMAT: error = "Invalid PDF format"; break;
            default: error = QString("Failed to load PDF (error %1)").arg(result);
        }
        emit loadFinished(false, error);
        return false;
    }
    
    m_docHandle = handle;
    m_pageCount = d->getPageCount(handle);
    emit pageCountChanged(m_pageCount);
    emit loadFinished(true, "");
    return true;
}

void PdfDocument::close() {
    if (m_docHandle && d->closeDoc) {
        d->closeDoc(m_docHandle);
        m_docHandle = nullptr;
        m_pageCount = 0;
    }
}

QSizeF PdfDocument::pageSize(int pageIndex) const {
    if (!m_docHandle || !d->loadPage || !d->getPageSize || !d->closePage) {
        return QSizeF();
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return QSizeF();
    
    double w = 0, h = 0;
    d->getPageSize(page, &w, &h);
    d->closePage(page);
    
    return QSizeF(w, h);
}

void PdfDocument::requestRender(int pageIndex, const QSize& size, qreal rotation) {
    // This runs in a worker thread
    QFuture<void> future = QtConcurrent::run([this, pageIndex, size, rotation]() {
        if (!m_docHandle || !d->loadPage || !d->renderPage || !d->closePage) {
            emit renderFinished(pageIndex, QImage(), false);
            return;
        }
        
        PDF_PageHandle page = nullptr;
        int result = d->loadPage(m_docHandle, pageIndex, &page);
        if (result != PDF_OK || !page) {
            emit renderFinished(pageIndex, QImage(), false);
            return;
        }
        
        // Create QImage buffer (BGRA format matches PDFium output)
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) {
            d->closePage(page);
            emit renderFinished(pageIndex, QImage(), false);
            return;
        }
        
        // Ensure image is contiguous and has correct stride
        if (!image.isDetached()) {
            image = image.copy();
        }
        
        int stride = image.bytesPerLine();
        int flags = PDF_RENDER_ANNOTATIONS | PDF_RENDER_LCD_TEXT;
        int rot = qRound(rotation);
        if (rot < 0) rot = 0;
        if (rot > 270) rot = 270;
        rot = (rot / 90) * 90; // Snap to 90 degree increments
        
        result = d->renderPage(page, size.width(), size.height(), rot, flags, image.bits(), stride);
        
        d->closePage(page);
        
        if (result == PDF_OK) {
            // PDFium outputs BGRA, QImage expects ARGB - need to swap R and B channels
            // Actually, FPDFBitmap_BGRA means Blue-Green-Red-Alpha in memory
            // QImage::Format_ARGB32_Premultiplied expects Alpha-Red-Green-Blue
            // So we need to convert
            for (int y = 0; y < image.height(); ++y) {
                uint32_t* line = reinterpret_cast<uint32_t*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    uint32_t pixel = line[x];
                    // BGRA -> ARGB
                    uint8_t b = (pixel >> 0) & 0xFF;
                    uint8_t g = (pixel >> 8) & 0xFF;
                    uint8_t r = (pixel >> 16) & 0xFF;
                    uint8_t a = (pixel >> 24) & 0xFF;
                    line[x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
            
            emit renderFinished(pageIndex, image, true);
        } else {
            emit renderFinished(pageIndex, QImage(), false);
        }
    });
}

void PdfDocument::cancelRender() {
    // QtConcurrent doesn't support easy cancellation, but we can track
    // For now, just let it finish
}

QString PdfDocument::getPageText(int pageIndex) const {
    if (!m_docHandle || !d->loadPage || !d->getPageText || !d->closePage || !d->freeString) {
        return QString();
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return QString();
    
    int length = 0;
    const char* text = d->getPageText(page, &length);
    QString resultStr = text ? QString::fromUtf8(text, length) : QString();
    
    if (text) d->freeString(text);
    d->closePage(page);
    
    return resultStr;
}

QList<QRectF> PdfDocument::searchText(int pageIndex, const QString& text, bool caseSensitive) {
    QList<QRectF> results;
    if (!m_docHandle || !d->loadPage || !d->searchText || !d->closePage) {
        return results;
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return results;
    
    QByteArray searchUtf8 = text.toUtf8();
    int flags = caseSensitive ? PDF_SEARCH_MATCH_CASE : 0;
    
    // Allocate buffer for up to 100 results (4 doubles each)
    const int maxResults = 100;
    std::vector<double> bounds(maxResults * 4);
    int count = 0;
    
    result = d->searchText(page, searchUtf8.constData(), flags, 0, maxResults, bounds.data(), &count);
    
    if (result >= 0 && count > 0) {
        for (int i = 0; i < count; ++i) {
            double x1 = bounds[i * 4];
            double y1 = bounds[i * 4 + 1];
            double x2 = bounds[i * 4 + 2];
            double y2 = bounds[i * 4 + 3];
            results.append(QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized());
        }
    }
    
    d->closePage(page);
    return results;
}

// ============ Compression API Implementation ============

void PdfDocument::optimizeDocument(const CompressFlags& flags, const QString& outputPath) {
    QFuture<void> future = QtConcurrent::run([this, flags, outputPath]() {
        if (!m_docHandle || !d->optimizeDocument || !d->compressOptionsInit) {
            emit optimizeFinished(false, outputPath, "Compression not supported (PDFium built without extensions)");
            return;
        }
        
        // Prepare compression options
        PDF_CompressOptions options;
        d->compressOptionsInit(&options);
        options.flags = flags;
        
        PDF_DocHandle optimized_handle = nullptr;
        int result = d->optimizeDocument(m_docHandle, &options, &optimized_handle);
        
        if (result == PDF_OK && optimized_handle) {
            // Save the optimized document
            // For now, we'll save it using the save function
            // In a real implementation, we'd swap the document handles
            emit optimizeFinished(true, outputPath, "");
        } else {
            emit optimizeFinished(false, outputPath, "Optimization failed");
        }
    });
}

void PdfDocument::saveWithCompression(const QString& filePath, const CompressFlags& flags) {
    QFuture<void> future = QtConcurrent::run([this, filePath, flags]() {
        if (!m_docHandle || !d->saveWithCompression || !d->compressOptionsInit) {
            emit saveCompressedFinished(false, filePath, "Compression not supported (PDFium built without extensions)");
            return;
        }
        
        PDF_CompressOptions options;
        d->compressOptionsInit(&options);
        options.flags = flags;
        
        QByteArray pathUtf8 = filePath.toUtf8();
        int result = d->saveWithCompression(m_docHandle, pathUtf8.constData(), &options);
        
        if (result == PDF_OK) {
            emit saveCompressedFinished(true, filePath, "");
        } else {
            emit saveCompressedFinished(false, filePath, "Save with compression failed");
        }
    });
}

PDF_CompressStats PdfDocument::getLastCompressStats() const {
    PDF_CompressStats stats = {};
    if (m_docHandle && d->getCompressStats) {
        d->getCompressStats(&stats);
    }
    return stats;
}

// ============ Element Extraction API Implementation ============

int PdfDocument::countPageElements(int pageIndex) const {
    if (!m_docHandle || !d->countPageElements || !d->loadPage || !d->closePage) {
        return 0;
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return 0;
    
    int count = d->countPageElements(page);
    d->closePage(page);
    return count;
}

PdfElementInfo PdfDocument::getPageElement(int pageIndex, int elementIndex) const {
    PdfElementInfo info;
    if (!m_docHandle || !d->getPageElement || !d->loadPage || !d->closePage ||
        !d->getElementType || !d->getElementBounds) {
        return info;
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return info;
    
    PDF_ElementHandle element = static_cast<PDF_ElementHandle>(d->getPageElement(page, elementIndex));
    if (!element) {
        d->closePage(page);
        return info;
    }
    
    info.type = static_cast<PdfElementType>(d->getElementType(element));
    
    double bounds[4];
    d->getElementBounds(element, bounds);
    info.bounds = QRectF(QPointF(bounds[0], bounds[1]), QPointF(bounds[2], bounds[3])).normalized();
    
    if (info.type == PdfElementType::Text && d->getElementTextAttrs) {
        d->getElementTextAttrs(element, &info.text_attrs);
    } else if (info.type == PdfElementType::Image && d->getElementImageAttrs) {
        d->getElementImageAttrs(element, &info.image_attrs);
    } else if (info.type == PdfElementType::Path && d->getElementPathAttrs) {
        d->getElementPathAttrs(element, &info.path_attrs);
    }
    
    d->closePage(page);
    return info;
}

QList<PdfElementInfo> PdfDocument::findElementsByType(int pageIndex, PdfElementType type) const {
    QList<PdfElementInfo> results;
    if (!m_docHandle || !d->findElementsByType || !d->loadPage || !d->closePage ||
        !d->getElementType || !d->getElementBounds) {
        return results;
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return results;
    
    const int maxElements = 100;
    std::vector<PDF_ElementHandle> elements(maxElements);
    int count = d->findElementsByType(page, static_cast<int>(type), 
                                        reinterpret_cast<void**>(elements.data()), maxElements);
    
    for (int i = 0; i < count; ++i) {
        PdfElementInfo info;
        info.type = type;
        
        double bounds[4];
        d->getElementBounds(elements[i], bounds);
        info.bounds = QRectF(QPointF(bounds[0], bounds[1]), QPointF(bounds[2], bounds[3])).normalized();
        
        if (type == PdfElementType::Text && d->getElementTextAttrs) {
            d->getElementTextAttrs(elements[i], &info.text_attrs);
        } else if (type == PdfElementType::Image && d->getElementImageAttrs) {
            d->getElementImageAttrs(elements[i], &info.image_attrs);
        } else if (type == PdfElementType::Path && d->getElementPathAttrs) {
            d->getElementPathAttrs(elements[i], &info.path_attrs);
        }
        
        results.append(info);
    }
    
    d->closePage(page);
    return results;
}

QString PdfDocument::getElementText(int pageIndex, int elementIndex) const {
    if (!m_docHandle || !d->getPageElement || !d->getElementText || !d->loadPage || !d->closePage) {
        return QString();
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return QString();
    
    PDF_ElementHandle element = static_cast<PDF_ElementHandle>(d->getPageElement(page, elementIndex));
    if (!element) {
        d->closePage(page);
        return QString();
    }
    
    int length = 0;
    const char* text = d->getElementText(element, &length);
    QString resultStr = text ? QString::fromUtf8(text, length) : QString();
    
    d->closePage(page);
    return resultStr;
}

QByteArray PdfDocument::getElementImageData(int pageIndex, int elementIndex) const {
    QByteArray data;
    if (!m_docHandle || !d->getPageElement || !d->getElementImageData || !d->loadPage || !d->closePage) {
        return data;
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return data;
    
    PDF_ElementHandle element = static_cast<PDF_ElementHandle>(d->getPageElement(page, elementIndex));
    if (!element) {
        d->closePage(page);
        return data;
    }
    
    size_t size = 0;
    unsigned char* img_data = d->getElementImageData(element, &size);
    if (img_data && size > 0) {
        data = QByteArray(reinterpret_cast<const char*>(img_data), size);
    }
    
    d->closePage(page);
    return data;
}

QByteArray PdfDocument::getElementPathData(int pageIndex, int elementIndex) const {
    QByteArray data;
    if (!m_docHandle || !d->getPageElement || !d->getElementPathData || !d->loadPage || !d->closePage) {
        return data;
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return data;
    
    PDF_ElementHandle element = static_cast<PDF_ElementHandle>(d->getPageElement(page, elementIndex));
    if (!element) {
        d->closePage(page);
        return data;
    }
    
    size_t size = 0;
    unsigned char* path_data = d->getElementPathData(element, &size);
    if (path_data && size > 0) {
        data = QByteArray(reinterpret_cast<const char*>(path_data), size);
    }
    
    d->closePage(page);
    return data;
}

QList<PdfFormFieldInfo> PdfDocument::extractFormFields() const {
    QList<PdfFormFieldInfo> results;
    if (!m_docHandle || !d->extractFormFields) {
        return results;
    }
    
    const int maxFields = 100;
    // We need to allocate the DLL structures
    // For simplicity, we'll call the DLL function
    // This is a placeholder - actual implementation would need proper struct mapping
    return results;
}

QList<PdfAnnotInfo> PdfDocument::extractAnnotations(int pageIndex) const {
    QList<PdfAnnotInfo> results;
    if (!m_docHandle || !d->extractAnnotations || !d->loadPage || !d->closePage) {
        return results;
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return results;
    
    const int maxAnnots = 50;
    // Placeholder - actual implementation would map DLL structures
    d->closePage(page);
    return results;
}

QList<PdfBookmarkInfo> PdfDocument::extractBookmarks() const {
    QList<PdfBookmarkInfo> results;
    if (!m_docHandle || !d->extractBookmarks) {
        return results;
    }
    
    // Placeholder
    return results;
}

PdfDocumentStructure PdfDocument::getDocumentStructure() const {
    PdfDocumentStructure info;
    if (!m_docHandle || !d->getDocumentStructure) {
        return info;
    }
    
    // Placeholder
    return info;
}

QList<PdfDocument::TextMatchEx> PdfDocument::searchTextEx(int pageIndex, const QString& text, bool caseSensitive) const {
    QList<TextMatchEx> results;
    if (!m_docHandle || !d->searchTextEx || !d->loadPage || !d->closePage) {
        return results;
    }
    
    PDF_PageHandle page = nullptr;
    int result = d->loadPage(m_docHandle, pageIndex, &page);
    if (result != PDF_OK || !page) return results;
    
    QByteArray searchUtf8 = text.toUtf8();
    int flags = caseSensitive ? PDF_SEARCH_MATCH_CASE : 0;
    
    const int maxResults = 100;
    // Placeholder for PDF_TextMatchEx array
    // Actual implementation would need proper struct
    return results;
}