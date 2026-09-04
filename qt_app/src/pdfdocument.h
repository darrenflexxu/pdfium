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
#include <memory>
#include "pdfium_wrapper.h"

// Element structures (Qt-side convenience wrappers over the COM interface)
struct PdfElementInfo {
    PDF_ElementType type = PDF_ELEMENT_UNKNOWN;
    QRectF bounds;
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

    // Async rendering
    void requestRender(int pageIndex, const QSize& size, qreal rotation = 0);
    void cancelRender();

    // Text extraction
    QString getPageText(int pageIndex) const;
    QList<QRectF> searchText(int pageIndex, const QString& text, bool caseSensitive = false);

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

    void optimizeDocument(const CompressFlags& flags, const QString& outputPath);
    void saveWithCompression(const QString& filePath, const CompressFlags& flags);
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

    // Raw interface access (for advanced use / delegation)
    IPdfDocument* interface() const { return m_interface; }

signals:
    void renderFinished(int pageIndex, QImage image, bool success);
    void loadFinished(bool success, const QString& errorMessage);
    void pageCountChanged(int count);
    void errorOccurred(const QString& message);
    void optimizeFinished(bool success, const QString& outputPath, const QString& errorMessage);
    void saveCompressedFinished(bool success, const QString& filePath, const QString& errorMessage);

private:
    struct Private;
    std::unique_ptr<Private> d;

    // COM interface to the wrapper DLL
    IPdfDocument* m_interface = nullptr;

    // Cached page count
    int m_pageCount = 0;
    mutable QMutex m_mutex;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PdfDocument::CompressFlags)

#endif // PDFDOCUMENT_H
