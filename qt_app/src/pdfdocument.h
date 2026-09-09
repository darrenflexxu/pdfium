#ifndef PDFDOCUMENT_H
#define PDFDOCUMENT_H

#include <QObject>
#include <QImage>
#include <QSize>
#include <QRectF>
#include <QMutex>
#include <QThread>
#include <QFuture>
#include <QFutureWatcher>
#include <QMap>
#include <QPair>
#include <QVector>
#include <memory>
#include "pdfium_wrapper.h"

// Element structures (Qt-side convenience wrappers over the COM interface)
struct PdfElementInfo {
    PDF_ElementType type = PDF_ELEMENT_UNKNOWN;
    QRectF bounds;
};

// A single character in reading order (top-to-bottom, then left-to-right).
// `bounds` are in PDF page coordinates (top-left origin, y-down), the same
// convention used by PageToDevice/DeviceToPage and the rendered bitmap.
// `index` is the stable linear selection index; `line` is the reading-order
// line group this char belongs to (0 = first line).
struct CharInfo {
    int codepoint = 0;
    QRectF bounds;
    int index = 0;
    int line = 0;
};

class PdfDocument : public QObject {
    Q_OBJECT
public:
    explicit PdfDocument(QObject* parent = nullptr);
    ~PdfDocument();

    bool load(const QString& filePath, const QString& password = QString());
    void close();

    bool isLoaded() const { return m_interface != nullptr; }
    int pageCount() const;
    QSizeF pageSize(int pageIndex) const;

    // Coordinate conversion (inherits PDFium's native mapping). The device
    // rectangle `origin`/`deviceSize` is the view area the page is drawn into;
    // `rotation` is in degrees (0/90/180/270, clockwise).
    QPointF pageToDevice(int pageIndex, const QPointF& pagePos,
                         const QPoint& origin, const QSize& deviceSize,
                         int rotation) const;
    QPointF deviceToPage(int pageIndex, const QPointF& devicePos,
                         const QPoint& origin, const QSize& deviceSize,
                         int rotation) const;

    // Async rendering. Returns the future backing the render task; callers that
    // are about to replace or destroy the document must wait for it to finish
    // (PDFium document handles must not be freed while a render is in flight).
    QFuture<void> requestRender(int pageIndex, const QSize& size, qreal rotation = 0);
    void cancelRender();

    // Text extraction
    QString getPageText(int pageIndex) const;
    QList<QRectF> searchText(int pageIndex, const QString& text, bool caseSensitive = false);

    // Character map (linear text selection). Lazily built per page and cached;
    // chars are sorted in reading order (top-to-bottom, then left-to-right).
    const QVector<CharInfo>& charMap(int pageIndex) const;
    // Returns the caret index in [0, charCount] for a click position in page
    // coordinates: above the first line -> 0, below the last line -> charCount,
    // otherwise the nearest character boundary. Returns -1 when the page has no
    // text.
    int findNearestCharIndex(int pageIndex, const QPointF& pos) const;
    // Codepoints from `startIndex` (inclusive) to `endIndex` (exclusive).
    QString textForRange(int pageIndex, int startIndex, int endIndex) const;
    // Selection expansion helpers (step 4). Ranges are [start, end), anchored
    // on a character index; degenerate when the page has no text.
    QPair<int, int> wordRange(int pageIndex, int anchor) const;
    QPair<int, int> paragraphRange(int pageIndex, int anchor) const;

    // Document-wide search session
    void startSearch(const QString& text, bool caseSensitive);
    int findNextMatch();
    int findPrevMatch();
    int totalMatches() const;
    QList<QRectF> matchesForPage(int pageIndex) const;
    int currentMatchPage() const;
    int currentMatchIndex() const;
    QRectF currentMatchRect() const;

    // Compression / optimization
    enum CompressFlag {
        CompressNone = 0,
        CompressFlate = 1 << 0,
        CompressObjectStreams = 1 << 1,
        CompressImages = 1 << 2,
        CompressFonts = 1 << 3,
        CompressRemoveUnused = 1 << 4,
        CompressLinearize = 1 << 5,
    };
    Q_DECLARE_FLAGS(CompressFlags, CompressFlag)

    struct CompressOptions {
        CompressFlags flags = CompressFlate;
        int imageQuality = 90;
        int imageDpiThreshold = 300;
        int minImageDpi = 150;
        int fontSubsetThreshold = 80;
        bool removeAnnotations = false;
        bool removeForms = false;
        bool removeBookmarks = false;
        bool removeMetadata = false;
    };

    void optimizeDocument(const CompressOptions& options, const QString& outputPath);
    void saveWithCompression(const QString& filePath, const CompressOptions& options);
    PDF_CompressStats getLastCompressStats() const;

    // Element extraction
    int countPageElements(int pageIndex) const;
    PdfElementInfo getPageElement(int pageIndex, int elementIndex) const;
    QList<PdfElementInfo> findElementsByType(int pageIndex, PDF_ElementType type) const;
    QString getElementText(int pageIndex, int elementIndex) const;
    QByteArray getElementImageData(int pageIndex, int elementIndex) const;
    QByteArray getElementPathData(int pageIndex, int elementIndex) const;

    // Document structure / metadata
    PDF_DocumentStructure* getDocumentStructure() const;
    QString getMetaText(const QString& key) const;

    // Bookmarks / outline. Returns a new IPdfOutline ref rooted at the first
    // top-level bookmark (caller must Release), or nullptr if the document has
    // no bookmarks.
    IPdfOutline* getOutlineRoot() const;

    // Raw interface access (for advanced use / delegation)
    IPdfDocument* interface() const { return m_interface; }

    // Search state
    QString m_currentSearchTerm;
    int m_currentMatchIndex = -1;
    bool m_searchCaseSensitive = false;
    QMap<int, QList<QRectF>> m_searchMatches;

signals:
    void renderFinished(int pageIndex, QImage image, bool success);
    void loadFinished(bool success, const QString& errorMessage);
    void pageCountChanged(int count);
    void errorOccurred(const QString& message);
    void optimizeFinished(bool success, const QString& outputPath, const QString& errorMessage);
    void saveCompressedFinished(bool success, const QString& filePath, const QString& errorMessage);
    void compressionProgress(int percentage, const QString& status);

private:
    struct Private;
    std::unique_ptr<Private> d;

    // COM interface to the wrapper DLL
    IPdfDocument* m_interface = nullptr;

    // Cached page count
    int m_pageCount = 0;
    mutable QMutex m_mutex;

    // Per-page cached character maps (reading-order linear text indices).
    mutable QMap<int, QVector<CharInfo>> m_charMaps;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PdfDocument::CompressFlags)

#endif // PDFDOCUMENT_H
