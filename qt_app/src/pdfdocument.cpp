#include "pdfdocument.h"
#include <QtCore>
#include <QtConcurrent>
#include <QImage>
#include <QRectF>
#include <QMutexLocker>
#include <pdfium_wrapper.h>

// PIMPL for DLL binding. Only the factory function is resolved at runtime;
// everything else is reached through the COM interfaces.
struct PdfDocument::Private {
    using InitLibFunc = int(*)();
    using DestroyLibFunc = void(*)();
    using CreateDocFunc = IPdfDocument*(*)(const char*, const char*, int*);
    using FreeStringFunc = void(*)(const char*);
    using FreeElementDataFunc = void(*)(void*);

    QLibrary library;
    bool libraryLoaded = false;

    InitLibFunc initLib = nullptr;
    DestroyLibFunc destroyLib = nullptr;
    CreateDocFunc createDocument = nullptr;
    FreeStringFunc freeString = nullptr;
    FreeElementDataFunc freeElementData = nullptr;

    bool loadLibrary() {
        if (libraryLoaded) return true;

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

        initLib = (InitLibFunc)library.resolve("PDF_InitLibrary");
        destroyLib = (DestroyLibFunc)library.resolve("PDF_DestroyLibrary");
        createDocument = (CreateDocFunc)library.resolve("PDF_CreateDocument");
        freeString = (FreeStringFunc)library.resolve("PDF_FreeString");
        freeElementData = (FreeElementDataFunc)library.resolve("PDF_FreeElementData");

        if (!initLib || !createDocument) {
            qWarning() << "Failed to resolve required PDFium wrapper functions";
            library.unload();
            return false;
        }

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

    close();

    QByteArray pathUtf8 = filePath.toUtf8();
    QByteArray pwdUtf8 = password.toUtf8();

    int error = PDF_OK;
    IPdfDocument* iface = d->createDocument(
        pathUtf8.constData(),
        password.isEmpty() ? nullptr : pwdUtf8.constData(),
        &error);

    if (!iface) {
        QString msg;
        switch (error) {
            case PDF_ERR_FILE_NOT_FOUND: msg = "File not found or cannot be opened"; break;
            case PDF_ERR_INVALID_PASSWORD: msg = "Invalid password"; break;
            case PDF_ERR_FORMAT: msg = "Invalid PDF format"; break;
            default: msg = QString("Failed to load PDF (error %1)").arg(error);
        }
        emit loadFinished(false, msg);
        return false;
    }

    m_interface = iface;
    m_pageCount = iface->GetPageCount();
    m_currentSearchTerm.clear();
    m_searchMatches.clear();
    m_currentMatchIndex = -1;
    emit pageCountChanged(m_pageCount);
    emit loadFinished(true, "");
    return true;
}

void PdfDocument::close() {
    if (m_interface) {
        m_interface->Release();
        m_interface = nullptr;
        m_pageCount = 0;
    }
    m_currentSearchTerm.clear();
    m_searchMatches.clear();
    m_currentMatchIndex = -1;
}

int PdfDocument::pageCount() const {
    return m_pageCount;
}

QSizeF PdfDocument::pageSize(int pageIndex) const {
    if (!m_interface) return QSizeF();
    double w = 0, h = 0;
    m_interface->GetSize(pageIndex, &w, &h);
    return QSizeF(w, h);
}

void PdfDocument::requestRender(int pageIndex, const QSize& size, qreal rotation) {
    QFuture<void> future = QtConcurrent::run([this, pageIndex, size, rotation]() {
        if (!m_interface) {
            emit renderFinished(pageIndex, QImage(), false);
            return;
        }

        // Get a page via the interface; we own the ref and must Release it.
        IPdfPage* page = m_interface->GetPage(pageIndex);
        if (!page) {
            emit renderFinished(pageIndex, QImage(), false);
            return;
        }

        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) {
            page->Release();
            emit renderFinished(pageIndex, QImage(), false);
            return;
        }
        if (!image.isDetached()) {
            image = image.copy();
        }

        int stride = image.bytesPerLine();
        int flags = PDF_RENDER_ANNOTATIONS | PDF_RENDER_LCD_TEXT;
        int rot = qRound(rotation);
        if (rot < 0) rot = 0;
        if (rot > 270) rot = 270;
        rot = (rot / 90) * 90;

        bool ok = page->Render(size.width(), size.height(), rot, flags, image.bits(), stride);

        if (ok) {
            // PDFium outputs BGRA; convert to ARGB32.
            for (int y = 0; y < image.height(); ++y) {
                uint32_t* line = reinterpret_cast<uint32_t*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    uint32_t pixel = line[x];
                    uint8_t b = (pixel >> 0) & 0xFF;
                    uint8_t g = (pixel >> 8) & 0xFF;
                    uint8_t r = (pixel >> 16) & 0xFF;
                    uint8_t a = (pixel >> 24) & 0xFF;
                    line[x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }

        page->Release();

        if (ok) {
            emit renderFinished(pageIndex, image, true);
        } else {
            emit renderFinished(pageIndex, QImage(), false);
        }
    });
}

void PdfDocument::cancelRender() {
    // No-op: QtConcurrent tasks run to completion.
}

QString PdfDocument::getPageText(int pageIndex) const {
    if (!m_interface || !d->freeString) return QString();

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return QString();

    int length = 0;
    const char* text = page->GetText(&length);
    QString resultStr = text ? QString::fromUtf8(text, length) : QString();
    if (text) d->freeString(text);

    page->Release();
    return resultStr;
}

QList<QRectF> PdfDocument::searchText(int pageIndex, const QString& text, bool caseSensitive) {
    QList<QRectF> results;
    if (!m_interface) return results;

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return results;

    QByteArray searchUtf8 = text.toUtf8();
    int flags = caseSensitive ? PDF_SEARCH_MATCH_CASE : 0;

    const int maxResults = 100;
    std::vector<double> bounds(maxResults * 4);
    int count = 0;

    page->SearchText(searchUtf8.constData(), flags, 0, maxResults, bounds.data(), &count);

    for (int i = 0; i < count; ++i) {
        double x1 = bounds[i * 4];
        double y1 = bounds[i * 4 + 1];
        double x2 = bounds[i * 4 + 2];
        double y2 = bounds[i * 4 + 3];
        results.append(QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized());
    }

    page->Release();
    return results;
}

void PdfDocument::startSearch(const QString& text, bool caseSensitive) {
    m_currentSearchTerm = text;
    m_searchCaseSensitive = caseSensitive;
    m_searchMatches.clear();
    m_currentMatchIndex = -1;

    if (text.isEmpty() || !m_interface) return;

    for (int i = 0; i < m_pageCount; ++i) {
        QList<QRectF> pageMatches = searchText(i, text, caseSensitive);
        if (!pageMatches.isEmpty()) {
            m_searchMatches.insert(i, pageMatches);
        }
    }
}

int PdfDocument::findNextMatch() {
    if (m_searchMatches.isEmpty()) return -1;

    int total = 0;
    for (auto it = m_searchMatches.begin(); it != m_searchMatches.end(); ++it) {
        total += it.value().size();
    }

    m_currentMatchIndex++;
    if (m_currentMatchIndex >= total) {
        m_currentMatchIndex = 0;
    }
    return m_currentMatchIndex;
}

int PdfDocument::findPrevMatch() {
    if (m_searchMatches.isEmpty()) return -1;

    int total = 0;
    for (auto it = m_searchMatches.begin(); it != m_searchMatches.end(); ++it) {
        total += it.value().size();
    }

    m_currentMatchIndex--;
    if (m_currentMatchIndex < 0) {
        m_currentMatchIndex = total - 1;
    }
    return m_currentMatchIndex;
}

int PdfDocument::totalMatches() const {
    int total = 0;
    for (auto it = m_searchMatches.begin(); it != m_searchMatches.end(); ++it) {
        total += it.value().size();
    }
    return total;
}

QList<QRectF> PdfDocument::matchesForPage(int pageIndex) const {
    return m_searchMatches.value(pageIndex, QList<QRectF>());
}

int PdfDocument::currentMatchPage() const {
    if (m_currentMatchIndex < 0 || m_searchMatches.isEmpty()) return -1;

    int cumulative = 0;
    for (auto it = m_searchMatches.begin(); it != m_searchMatches.end(); ++it) {
        cumulative += it.value().size();
        if (m_currentMatchIndex < cumulative) {
            return it.key();
        }
    }
    return -1;
}

int PdfDocument::currentMatchIndex() const {
    return m_currentMatchIndex;
}

QRectF PdfDocument::currentMatchRect() const {
    if (m_currentMatchIndex < 0 || m_searchMatches.isEmpty()) return QRectF();

    int cumulative = 0;
    for (auto it = m_searchMatches.begin(); it != m_searchMatches.end(); ++it) {
        const QList<QRectF>& pageMatches = it.value();
        if (m_currentMatchIndex < cumulative + pageMatches.size()) {
            return pageMatches[m_currentMatchIndex - cumulative];
        }
        cumulative += pageMatches.size();
    }
    return QRectF();
}

void PdfDocument::optimizeDocument(const CompressFlags& flags, const QString& outputPath) {
    QFuture<void> future = QtConcurrent::run([this, flags, outputPath]() {
        if (!m_interface) {
            emit optimizeFinished(false, outputPath, "No document loaded");
            return;
        }

        PDF_CompressOptions options;
        std::memset(&options, 0, sizeof(options));
        options.flags = static_cast<int>(flags);
        options.image_quality = 90;
        options.image_dpi_threshold = 300;
        options.min_image_dpi = 150;
        options.font_subset_threshold = 80;

        IPdfDocument* optimized = nullptr;
        int result = m_interface->Optimize(&options, &optimized);
        if (result == PDF_OK && optimized) {
            optimized->Release();
            emit optimizeFinished(true, outputPath, "");
        } else if (result == PDF_ERR_UNSUPPORTED) {
            emit optimizeFinished(false, outputPath,
                "Compression not supported (PDFium built without extensions)");
        } else {
            emit optimizeFinished(false, outputPath, "Optimization failed");
        }
    });
}

void PdfDocument::saveWithCompression(const QString& filePath, const CompressFlags& flags) {
    QFuture<void> future = QtConcurrent::run([this, filePath, flags]() {
        if (!m_interface) {
            emit saveCompressedFinished(false, filePath, "No document loaded");
            return;
        }

        PDF_CompressOptions options;
        std::memset(&options, 0, sizeof(options));
        options.flags = static_cast<int>(flags);
        options.image_quality = 90;
        options.image_dpi_threshold = 300;
        options.min_image_dpi = 150;
        options.font_subset_threshold = 80;

        QByteArray pathUtf8 = filePath.toUtf8();
        int result = m_interface->SaveWithCompression(pathUtf8.constData(), &options);

        if (result == PDF_OK) {
            emit saveCompressedFinished(true, filePath, "");
        } else if (result == PDF_ERR_UNSUPPORTED) {
            emit saveCompressedFinished(false, filePath,
                "Compression not supported (PDFium built without extensions)");
        } else {
            emit saveCompressedFinished(false, filePath, "Save with compression failed");
        }
    });
}

PDF_CompressStats PdfDocument::getLastCompressStats() const {
    PDF_CompressStats stats;
    std::memset(&stats, 0, sizeof(stats));
    if (m_interface) {
        m_interface->GetLastCompressStats(&stats);
    }
    return stats;
}

int PdfDocument::countPageElements(int pageIndex) const {
    if (!m_interface) return 0;
    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return 0;
    int count = page->CountPageElements();
    page->Release();
    return count;
}

// Fills a PdfElementInfo from an IPdfElement.
static void fillElementInfo(IPdfElement* element, PdfElementInfo& info) {
    if (!element) return;
    info.type = element->GetType();
    double b[4];
    element->GetBounds(b);
    info.bounds = QRectF(QPointF(b[0], b[1]), QPointF(b[2], b[3])).normalized();
}

PdfElementInfo PdfDocument::getPageElement(int pageIndex, int elementIndex) const {
    PdfElementInfo info;
    if (!m_interface) return info;

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return info;

    IPdfElement* element = page->GetPageElement(elementIndex);
    if (element) {
        fillElementInfo(element, info);
        element->Release();
    }

    page->Release();
    return info;
}

QList<PdfElementInfo> PdfDocument::findElementsByType(int pageIndex, PDF_ElementType type) const {
    QList<PdfElementInfo> results;
    if (!m_interface) return results;

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return results;

    const int maxElements = 100;
    std::vector<IPdfElement*> elements(maxElements, nullptr);
    int count = page->FindElementsByType(type, elements.data(), maxElements);

    for (int i = 0; i < count; ++i) {
        if (!elements[i]) continue;
        PdfElementInfo info;
        fillElementInfo(elements[i], info);
        results.append(info);
        elements[i]->Release();
    }

    page->Release();
    return results;
}

QString PdfDocument::getElementText(int pageIndex, int elementIndex) const {
    if (!m_interface || !d->freeString) return QString();

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return QString();

    IPdfElement* element = page->GetPageElement(elementIndex);
    QString result;
    if (element) {
        int length = 0;
        const char* text = element->GetElementText(&length);
        if (text) {
            result = QString::fromUtf8(text, length);
            d->freeString(text);
        }
        element->Release();
    }

    page->Release();
    return result;
}

QByteArray PdfDocument::getElementImageData(int pageIndex, int elementIndex) const {
    QByteArray data;
    if (!m_interface || !d->freeElementData) return data;

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return data;

    IPdfElement* element = page->GetPageElement(elementIndex);
    if (element) {
        size_t size = 0;
        unsigned char* img = element->GetImageData(&size);
        if (img && size > 0) {
            data = QByteArray(reinterpret_cast<const char*>(img), static_cast<int>(size));
        }
        if (img) d->freeElementData(img);
        element->Release();
    }

    page->Release();
    return data;
}

QByteArray PdfDocument::getElementPathData(int pageIndex, int elementIndex) const {
    QByteArray data;
    if (!m_interface || !d->freeElementData) return data;

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return data;

    IPdfElement* element = page->GetPageElement(elementIndex);
    if (element) {
        size_t size = 0;
        unsigned char* path = element->GetPathData(&size);
        if (path && size > 0) {
            data = QByteArray(reinterpret_cast<const char*>(path), static_cast<int>(size));
        }
        if (path) d->freeElementData(path);
        element->Release();
    }

    page->Release();
    return data;
}

PDF_DocumentStructure* PdfDocument::getDocumentStructure() const {
    if (!m_interface) return nullptr;
    return m_interface->GetDocumentStructure();
}

QString PdfDocument::getMetaText(const QString& key) const {
    if (!m_interface) return QString();
    QByteArray keyUtf8 = key.toUtf8();
    const char* value = m_interface->GetMetaText(keyUtf8.constData());
    return value ? QString::fromUtf8(value) : QString();
}
