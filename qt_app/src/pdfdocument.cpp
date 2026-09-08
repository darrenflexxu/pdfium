#include "pdfdocument.h"
#include <QtCore>
#include <QtConcurrent>
#include <QImage>
#include <QRectF>
#include <QMutexLocker>
#include <algorithm>
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
    m_charMaps.clear();
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
    m_charMaps.clear();
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

QPointF PdfDocument::pageToDevice(int pageIndex, const QPointF& pagePos,
                                  const QPoint& origin, const QSize& deviceSize,
                                  int rotation) const {
    if (!m_interface) return QPointF();

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return QPointF();

    int rot = (rotation / 90) * 90;
    int dx = 0, dy = 0;
    page->PageToDevice(origin.x(), origin.y(), deviceSize.width(), deviceSize.height(),
                       rot, pagePos.x(), pagePos.y(), &dx, &dy);
    QPointF result(origin.x() + dx, origin.y() + dy);

    page->Release();
    return result;
}

QPointF PdfDocument::deviceToPage(int pageIndex, const QPointF& devicePos,
                                  const QPoint& origin, const QSize& deviceSize,
                                  int rotation) const {
    if (!m_interface) return QPointF();

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return QPointF();

    int rot = (rotation / 90) * 90;
    double px = 0, py = 0;
    page->DeviceToPage(origin.x(), origin.y(), deviceSize.width(), deviceSize.height(),
                       rot, qRound(devicePos.x() - origin.x()), qRound(devicePos.y() - origin.y()),
                       &px, &py);
    QPointF result(px, py);

    page->Release();
    return result;
}

QFuture<void> PdfDocument::requestRender(int pageIndex, const QSize& size, qreal rotation) {
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
    return future;
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

const QVector<CharInfo>& PdfDocument::charMap(int pageIndex) const {
    static const QVector<CharInfo> empty;
    auto it = m_charMaps.constFind(pageIndex);
    if (it != m_charMaps.constEnd()) return it.value();
    if (!m_interface) return empty;

    IPdfPage* page = m_interface->GetPage(pageIndex);
    if (!page) return empty;

    QVector<CharInfo> map;
    const int count = page->GetCharCount();
    map.reserve(qMax(0, count));
    for (int i = 0; i < count; ++i) {
        double l = 0, t = 0, r = 0, b = 0;
        page->GetCharBox(i, &l, &t, &r, &b);
        CharInfo ci;
        ci.codepoint = page->GetCharUnicode(i);
        ci.bounds = QRectF(QPointF(l, t), QPointF(r, b)).normalized();
        ci.index = i;
        // Skip zero-area glyphs (e.g. PDFium's injected CR/LF control chars
        // and other whitespace placeholders): they carry no selectable width
        // and would corrupt the contiguous codepoint sequence.
        if (ci.bounds.width() <= 0.0 || ci.bounds.height() <= 0.0) continue;
        map.append(ci);
    }
    page->Release();

    if (!map.isEmpty()) {
        // CharInfo::bounds live in PDF user space (bottom-left origin, y-up),
        // the exact convention FPDFText_GetCharBox / PageToDevice use, so
        // bounds.top() is a glyph's LOWER edge (toward page bottom) and
        // bounds.bottom() its UPPER edge (toward page top).
        //
        // Reading order: top-to-bottom, then left-to-right within a line.
        // PDFium reports per-glyph box edges that differ slightly within a
        // line (descender/ascender glyphs get taller boxes), so lines are
        // delimited by VERTICAL OVERLAP: two glyphs are on the same line iff
        // their boxes' vertical ranges overlap (they share a baseline row).
        // Walking glyphs from the top of the page down (descending upper
        // edge) yields reading-ordered line numbers.
        std::stable_sort(map.begin(), map.end(),
                         [](const CharInfo& a, const CharInfo& b) {
                             return a.bounds.bottom() > b.bounds.bottom();
                         });
        int line = -1;
        double lineTop = qInf();      // lowest lower-edge of current line
        double lineBottom = -qInf();  // highest upper-edge of current line
        for (CharInfo& ci : map) {
            const double lo = ci.bounds.top();
            const double hi = ci.bounds.bottom();
            if (lo <= lineBottom + 0.25 && hi >= lineTop - 0.25) {
                lineTop = qMin(lineTop, lo);
                lineBottom = qMax(lineBottom, hi);
            } else {
                ++line;
                lineTop = lo;
                lineBottom = hi;
            }
            ci.line = line;
        }
        std::stable_sort(map.begin(), map.end(),
                         [](const CharInfo& a, const CharInfo& b) {
                             if (a.line != b.line) return a.line < b.line;
                             return a.bounds.left() < b.bounds.left();
                         });
    }

    m_charMaps.insert(pageIndex, map);
    auto cachedIt = m_charMaps.constFind(pageIndex);
    return cachedIt.value();
}

int PdfDocument::findNearestCharIndex(int pageIndex, const QPointF& pos) const {
    const QVector<CharInfo>& map = charMap(pageIndex);
    const int total = map.size();
    if (total == 0) return -1;

    // Contiguous per-line ranges in the sorted map.
    struct L { int start; int end; double top; double bottom; };
    QVector<L> lines;
    for (int i = 0; i < total; ) {
        int j = i;
        double top = map[i].bounds.top(), bottom = map[i].bounds.bottom();
        while (j + 1 < total && map[j + 1].line == map[i].line) {
            ++j;
            top = qMin(top, map[j].bounds.top());
            bottom = qMax(bottom, map[j].bounds.bottom());
        }
        lines.append({i, j + 1, top, bottom});
        i = j + 1;
    }

    // Vertical targeting: the band containing pos, or the nearest band edge.
    // pos is in PDF user space (y-up): ABOVE the first line -> caret 0,
    // below the last line -> charCount (plan's boundary behavior).
    int lineIdx = 0;
    const int lastLine = lines.size() - 1;
    if (pos.y() >= lines[0].bottom) {
        return 0;
    }
    if (pos.y() <= lines[lastLine].top) {
        return total;
    }
    for (int i = 0; i <= lastLine; ++i) {
        const L& ln = lines[i];
        if (pos.y() >= ln.top && pos.y() <= ln.bottom) { lineIdx = i; break; }
        if (i < lastLine) {
            const L& nx = lines[i + 1];
            // Gap band between two lines (upper edge of the higher line down
            // to the lower edge of the next line).
            if (pos.y() > ln.bottom && pos.y() < nx.top) {
                lineIdx =
                    (ln.bottom - pos.y() <= pos.y() - nx.top) ? i : i + 1;
                break;
            }
        }
    }

    // Horizontal targeting: caret boundary within the line (left/right half).
    const int s = lines[lineIdx].start, e = lines[lineIdx].end;
    for (int i = s; i < e; ++i) {
        const CharInfo& c = map[i];
        const double mid = (c.bounds.left() + c.bounds.right()) * 0.5;
        if (pos.x() < mid) return i;
        if (pos.x() <= c.bounds.right()) return i + 1;
    }
    return e;
}

QString PdfDocument::textForRange(int pageIndex, int startIndex, int endIndex) const {
    const QVector<CharInfo>& map = charMap(pageIndex);
    if (map.isEmpty()) return QString();
    const int a = qMax(0, qMin(startIndex, endIndex));
    const int b = qMin(static_cast<int>(map.size()), qMax(startIndex, endIndex));
    if (a >= b) return QString();
    QString out;
    out.reserve(b - a);
    for (int i = a; i < b; ++i) {
        const uint cp = static_cast<uint>(map[i].codepoint);
        out += QString::fromUcs4(&cp, 1);
    }
    return out;
}

QPair<int, int> PdfDocument::wordRange(int pageIndex, int anchor) const {
    const QVector<CharInfo>& map = charMap(pageIndex);
    if (map.isEmpty() || anchor < 0 || anchor >= map.size())
        return qMakePair(qMax(0, anchor), qMax(0, anchor));
    const auto isWordChar = [](int cp) {
        const QChar c(static_cast<ushort>(cp));
        return !c.isSpace() && !c.isPunct();
    };
    const int line = map[anchor].line;
    if (!isWordChar(map[anchor].codepoint)) {
        // Anchor on whitespace: select the whitespace run itself.
        int a = anchor, b = anchor + 1;
        while (a > 0 && map[a - 1].line == line && !isWordChar(map[a - 1].codepoint)) --a;
        while (b < map.size() && map[b].line == line && !isWordChar(map[b].codepoint)) ++b;
        return qMakePair(a, b);
    }
    int a = anchor, b = anchor;
    while (a > 0 && map[a - 1].line == line && isWordChar(map[a - 1].codepoint)) --a;
    while (b < map.size() && map[b].line == line && isWordChar(map[b].codepoint)) ++b;
    return qMakePair(a, b);
}

QPair<int, int> PdfDocument::paragraphRange(int pageIndex, int anchor) const {
    const QVector<CharInfo>& map = charMap(pageIndex);
    if (map.isEmpty() || anchor < 0 || anchor >= map.size())
        return qMakePair(qMax(0, anchor), qMax(0, anchor));

    QVector<QPair<int, int>> spans;   // [start, end) per line
    QVector<double> tops, bottoms;
    for (int i = 0; i < map.size(); ) {
        int j = i;
        while (j + 1 < map.size() && map[j + 1].line == map[i].line) ++j;
        spans.append(qMakePair(i, j + 1));
        tops.append(map[i].bounds.top());
        bottoms.append(map[j].bounds.bottom());
        i = j + 1;
    }

    // Median line pitch; a paragraph break is a gap well beyond the pitch.
    double pitch = 0;
    QVector<double> gaps;
    for (int k = 1; k < tops.size(); ++k) gaps.append(tops[k] - tops[k - 1]);
    if (!gaps.isEmpty()) {
        QVector<double> sorted = gaps;
        std::sort(sorted.begin(), sorted.end());
        pitch = sorted[sorted.size() / 2];
    }
    const double threshold = qMax(2.0, pitch * 1.4);

    const int k0 = map[anchor].line;
    int first = k0;
    for (int k = k0 - 1; k >= 0; --k) {
        if (tops[k + 1] - bottoms[k] > threshold) break;
        first = k;
    }
    int last = k0;
    for (int k = k0 + 1; k < spans.size(); ++k) {
        if (tops[k] - bottoms[k - 1] > threshold) break;
        last = k;
    }
    return qMakePair(spans[first].first, spans[last].second);
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

IPdfOutline* PdfDocument::getOutlineRoot() const {
    if (!m_interface) return nullptr;
    return m_interface->GetOutlineRoot();
}

QString PdfDocument::getMetaText(const QString& key) const {
    if (!m_interface) return QString();
    QByteArray keyUtf8 = key.toUtf8();
    const char* value = m_interface->GetMetaText(keyUtf8.constData());
    return value ? QString::fromUtf8(value) : QString();
}
