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

// Forward declare DLL handle types
struct PDF_Document;
struct PDF_Page;

class PdfDocument : public QObject {
    Q_OBJECT
public:
    explicit PdfDocument(QObject* parent = nullptr);
    ~PdfDocument();

    bool load(const QString& filePath, const QString& password = QString());
    void close();
    
    bool isLoaded() const { return m_docHandle != nullptr; }
    int pageCount() const { return m_pageCount; }
    QSizeF pageSize(int pageIndex) const;
    
    // Async rendering
    void requestRender(int pageIndex, const QSize& size, qreal rotation = 0);
    void cancelRender();
    
    // Text extraction
    QString getPageText(int pageIndex) const;
    QList<QRectF> searchText(int pageIndex, const QString& text, bool caseSensitive = false);
    
signals:
    void renderFinished(int pageIndex, QImage image, bool success);
    void loadFinished(bool success, const QString& errorMessage);
    void pageCountChanged(int count);
    void errorOccurred(const QString& message);

private:
    struct Private;
    std::unique_ptr<Private> d;
    
    // DLL handles
    PDF_Document* m_docHandle = nullptr;
    int m_pageCount = 0;
    mutable QMutex m_mutex;
};

#endif // PDFDOCUMENT_H