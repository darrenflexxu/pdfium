#ifndef SKIAPDFVIEWERWIDGET_H
#define SKIAPDFVIEWERWIDGET_H

// Skia-accelerated PDF viewer widget.
//
// When the project is built with PDFIUM_ENABLE_SKIA=ON the widget derives from
// QOpenGLWidget and renders pages through a Skia Ganesh GPU (OpenGL) context,
// upgrading the pipeline to PDFium -> Skia -> Screen.  In the default OFF build
// it is a lightweight QWidget placeholder that exposes the same public API.
//
// The public API mirrors PdfViewerWidget so MainWindow can runtime-toggle
// between the CPU viewer and this GPU viewer (View -> GPU Acceleration).

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QList>
#include <QString>
#include <QMap>
#include <QSet>
#include <QSize>
#include <QTransform>
#include <QFuture>
#include <memory>

#ifdef SKIA_AVAILABLE
#include <QOpenGLWidget>
#include "include/core/SkRefCnt.h"
class GrDirectContext;
class SkCanvas;
#endif

#include "abstractpdfviewer.h"

class PdfDocument;

#ifdef SKIA_AVAILABLE
class SkiaPdfViewerWidget : public QOpenGLWidget, public AbstractPdfViewer {
#else
class SkiaPdfViewerWidget : public QWidget, public AbstractPdfViewer {
#endif
    Q_OBJECT
public:
    explicit SkiaPdfViewerWidget(QWidget* parent = nullptr);
    ~SkiaPdfViewerWidget() override;

    QWidget* widget() const override { return const_cast<SkiaPdfViewerWidget*>(this); }

    void setDocument(PdfDocument* document) override;
    PdfDocument* document() const override { return m_document; }
    void setPage(int pageIndex) override;
    void setZoom(qreal zoom) override;
    void setRotation(int rotation) override;

    // File / navigation / view API mirrors PdfViewerWidget.
    bool loadFile(const QString& filePath, const QString& password = QString()) override;
    void goToNextPage() override;
    void goToPrevPage() override;
    void goToFirstPage() override;
    void goToLastPage() override;
    void zoomIn() override;
    void zoomOut() override;
    void zoomToFit() override;
    void zoomToWidth() override;
    void rotateClockwise() override;
    void rotateCounterClockwise() override;

    void setViewMode(ViewMode mode) override;
    ViewMode viewMode() const override { return m_viewMode; }

    void setTextSelectionEnabled(bool enabled) override;
    bool isTextSelectionEnabled() const override { return m_textSelectionEnabled; }

    int currentPage() const override { return m_currentPage; }
    int pageCount() const override { return m_pageCount; }
    qreal zoom() const override { return m_zoom; }
    int rotation() const override { return m_rotation; }

    void renderCurrentPage() override;

    // Coordinate mapping (PDF page space <-> widget pixels, rotation-aware).
    QPointF mapToPage(const QPoint& widgetPos) const override;
    QRectF pageRect() const override;
    QRectF pageRect(int pageIndex) const override;

signals:
    void pageChanged(int pageIndex);
    void zoomChanged(qreal zoom);
    void rotationChanged(int rotation);
    void pageCountChanged(int count);
    void statusMessage(const QString& message);
    void textSelected(const QString& text);
    void linkActivated(const QString& url);

protected:
#ifdef SKIA_AVAILABLE
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
#endif
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void onLoadFinished(bool success, const QString& error);
    void clearTextSelectionState();
    QString selectedText() const;
    QRectF widgetToPageRect(const QRect& widgetRect) const;
    void finishSelection();

    // Geometry / layout (page space <-> widget space).
    QSizeF pagePixelSize(int pageIndex) const;
    QRectF pageRectOf(int pageIndex) const;
    QRectF contentRect() const;
    QRectF viewportRect() const;
    qreal continuousY(int pageIndex) const;
    int pageAtY(qreal y, bool clamp) const;
    int pageUnderPoint(const QPoint& widgetPos) const;
    void refreshVisiblePages();
    void updateCurrentPageForViewport();
    void setScrollToPage(int pageIndex);
    QPointF mapFromPageF(const QPointF& pagePos) const;
    bool pageToWidgetTransform(int pageIndex, QTransform& out) const;
    QPointF mapFromPageOnPage(const QPointF& pagePos, int pageIndex) const;
    QPointF mapToPageOnPage(const QPoint& widgetPos, int pageIndex) const;
    QRectF mapRectFromPageOnPage(const QRectF& pageRect, int pageIndex) const;

#ifndef SKIA_AVAILABLE
    void drawPlaceholder(QPainter& painter);
#endif

#ifdef SKIA_AVAILABLE
    void drawViewContent(SkCanvas* canvas);
    void drawPageOnCanvas(SkCanvas* canvas, int pageIndex, const QRectF& widgetRect);
    void drawSearchHighlights(SkCanvas* canvas, int pageIndex, const QRectF& pageWidgetRect);
    void drawSelection(SkCanvas* canvas, int pageIndex);
    void probeDumpCanvas(SkCanvas* canvas); // [PROBE] temp

    sk_sp<GrDirectContext> m_skiaContext;
#endif

    PdfDocument* m_document = nullptr;
    int m_currentPage = -1;
    int m_pageCount   = 0;
    qreal m_zoom      = 1.0;
    int m_rotation    = 0;
    ViewMode m_viewMode = ViewMode::SinglePage;

    // Pan / scrolling.
    QPoint m_scrollOffset;
    int m_scrollVersion = 0;
    int m_trackedScrollVersion = -1;
    bool m_panning = false;
    QPoint m_panStart;

    // Text selection.
    bool m_textSelectionEnabled = false;
    bool m_selecting = false;
    int m_cursorIndex = -1;
    int m_selectionStart = -1;
    int m_selectionEnd = -1;
    QPoint m_boxSelStart;
    QPoint m_boxSelEnd;
    int m_clickCount = 1;
    QPointF m_lastClickPagePos;
    int m_lastClickIndex = -1;

#ifdef SKIA_AVAILABLE
    int m_probeFrame = 0; // [PROBE] temp
    QSizeF m_viewportLogical; // actual logical size from resizeGL / paintGL
#endif
};

#endif // SKIAPDFVIEWERWIDGET_H