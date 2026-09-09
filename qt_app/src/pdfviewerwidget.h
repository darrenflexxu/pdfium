#ifndef PDFVIEWERWIDGET_H
#define PDFVIEWERWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QList>
#include <QFuture>

class PdfDocument;
class QMenu;
class QContextMenuEvent;

class PdfViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit PdfViewerWidget(QWidget* parent = nullptr);
    ~PdfViewerWidget() override;
    
    void setDocument(PdfDocument* document);
    PdfDocument* document() const { return m_document; }
    void setPage(int pageIndex);
    void setZoom(qreal zoom);
    void setRotation(int rotation);
    
    int currentPage() const { return m_currentPage; }
    int pageCount() const { return m_pageCount; }
    qreal zoom() const { return m_zoom; }
    int rotation() const { return m_rotation; }
    
    // File operations
    bool loadFile(const QString& filePath, const QString& password = QString());
    
    // Navigation
    void goToNextPage();
    void goToPrevPage();
    void goToFirstPage();
    void goToLastPage();
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToWidth();
    void rotateClockwise();
    void rotateCounterClockwise();
    
    // Text selection (future)
    void setTextSelectionEnabled(bool enabled);

    // Linear text-selection state (char map indices).
    int cursorCharIndex() const { return m_cursorIndex; }
    int selectionStart() const { return m_selectionStart; }
    int selectionEnd() const { return m_selectionEnd; }
    bool isSelecting() const { return m_selecting; }
    
    // Coordinate mapping (PDF page space <-> widget pixels, rotation-aware)
    QPoint mapFromPage(const QPointF& pagePos) const;
    QPointF mapToPage(const QPoint& widgetPos) const;
    QRectF mapRectFromPage(const QRectF& pdfRect) const;
    QRectF pageRect() const;
    
signals:
    void pageChanged(int pageIndex);
    void zoomChanged(qreal zoom);
    void rotationChanged(int rotation);
    void pageCountChanged(int count);
    void statusMessage(const QString& message);
    void textSelected(const QString& text);
    void linkActivated(const QString& url);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    QMenu* buildContextMenu(const QPoint& widgetPos);
    void copySelectedText();
    
private slots:
    void onRenderFinished(int pageIndex, QImage image, bool success);
    void onLoadFinished(bool success, const QString& error);
    
private:
    void updateViewport();
    void requestRender();
    QPointF mapFromPageF(const QPointF& pagePos) const;
    QRectF mappedMatchRect(const QRectF& pageMatch) const;
    void clearTextSelectionState();
    
    PdfDocument* m_document = nullptr;
    QImage m_currentImage;
    int m_currentPage = -1;
    int m_pageCount = 0;
    qreal m_zoom = 1.0;
    int m_rotation = 0; // 0, 90, 180, 270
    
    // Panning
    QPoint m_panStart;
    QPoint m_scrollOffset;
    bool m_panning = false;
    
    // Text selection. The char-index pair drives the linear caret/highlight;
    // the box fields are the fallback overlay for pages with no text at all.
    bool m_textSelectionEnabled = false;
    int m_cursorIndex = -1;
    int m_selectionStart = -1;
    int m_selectionEnd = -1;
    bool m_selecting = false;
    QPoint m_boxSelStart;
    QPoint m_boxSelEnd;

    // Multi-click detection (double/triple click on a char).
    int m_clickCount = 1;
    qint64 m_lastClickTime = 0;
    QPointF m_lastClickPagePos;
    int m_lastClickIndex = -1;
    
    // Render cache
    struct CachedPage {
        QImage image;
        QSize renderSize;
        int rotation = 0;
    };
    QMap<int, CachedPage> m_renderCache;
    static const int MAX_CACHE_SIZE = 10;

    // In-flight async render. The widget waits for it before replacing or
    // destroying the document, because the wrapped PDFium document handles
    // must stay alive until the render worker releases its page ref.
    QFuture<void> m_renderFuture;
};

#endif // PDFVIEWERWIDGET_H