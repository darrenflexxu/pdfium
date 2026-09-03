#include "pdfdocument.h"
#include <QtCore>
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
    
    QLibrary library;
    bool libraryLoaded = false;
    
    bool loadLibrary() {
        if (libraryLoaded) return true;
        
        // Try to load the wrapper DLL
        #ifdef _WIN32
        library.setFileName("pdfium_wrapper.dll");
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
        loadDoc = (LoadFunc)library.resolve("PDF_LoadDocument");
        closeDoc = (CloseDocFunc)library.resolve("PDF_CloseDocument");
        getPageCount = (GetPageCountFunc)library.resolve("PDF_GetPageCount");
        loadPage = (LoadPageFunc)library.resolve("PDF_LoadPage");
        closePage = (ClosePageFunc)library.resolve("PDF_ClosePage");
        getPageSize = (GetPageSizeFunc)library.resolve("PDF_GetPageSize");
        renderPage = (RenderPageFunc)library.resolve("PDF_RenderPage");
        searchText = (SearchTextFunc)library.resolve("PDF_SearchText");
        getPageText = (GetPageTextFunc)library.resolve("PDF_GetPageText");
        freeString = (FreeStringFunc)library.resolve("PDF_FreeString");
        
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