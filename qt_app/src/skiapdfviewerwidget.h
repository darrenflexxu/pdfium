#ifndef SKIAPDFVIEWERWIDGET_H
#define SKIAPDFVIEWERWIDGET_H

// Skia-accelerated PDF viewer widget.
//
// When the project is built with PDFIUM_ENABLE_SKIA=ON the widget derives from
// QOpenGLWidget and renders pages through a Skia Ganesh GPU (OpenGL) context,
// upgrading the pipeline to PDFium -> Skia -> Screen.  In the default OFF build
// it is a lightweight QWidget placeholder that exposes the same public API.
//
// The Skia headless GL harness (see PLAN.md section 8) exercises the same
// drawing path used by paintGL() and verifies the GPU pixels against the CPU
// raster, so the accelerator can be validated without a windowing session.

#include <QWidget>
#include <QImage>
#include <QRectF>
#include <QList>
#include <QString>
#include <QMap>
#include <QSet>
#include <QSize>
#include <QFuture>
#include <memory>

#ifdef SKIA_AVAILABLE
#include <QOpenGLWidget>
#include "include/core/SkRefCnt.h"
class GrDirectContext;
#endif

class PdfDocument;

#ifdef SKIA_AVAILABLE
class SkiaPdfViewerWidget : public QOpenGLWidget {
#else
class SkiaPdfViewerWidget : public QWidget {
#endif
    Q_OBJECT
public:
    explicit SkiaPdfViewerWidget(QWidget* parent = nullptr);
    ~SkiaPdfViewerWidget() override;

    void setDocument(PdfDocument* document);
    void setPage(int pageIndex);
    void setZoom(qreal zoom);
    void setRotation(int rotation);
    void zoomIn();
    void zoomOut();
    void goToNextPage();
    void goToPrevPage();
    void goToFirstPage();
    void goToLastPage();
    void rotateClockwise();
    void rotateCounterClockwise();
    void fitToView();

    PdfDocument* document() const { return m_document; }
    int currentPage() const { return m_currentPage; }
    int pageCount() const { return m_pageCount; }
    qreal zoom() const { return m_zoom; }
    int rotation() const { return m_rotation; }

signals:
    void pageChanged(int pageIndex);
    void zoomChanged(qreal zoom);
    void rotationChanged(int rotation);
    void pageCountChanged(int count);
    void statusMessage(const QString& message);
    void textSelected(const QString& text);
    void linkActivated(const QString& url);

#ifdef SKIA_AVAILABLE
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
#endif

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void renderCurrentPage();
    void requestRender(int pageIndex);
    void waitForPendingRenders();
    void clearRenderCache();

#ifndef SKIA_AVAILABLE
    void drawPlaceholder(QPainter& painter);
#endif

#ifdef SKIA_AVAILABLE
private:
    // Page geometry in view/window coordinates: pagePixelSize() is the raster
    // size used for the auto-rotated page at the current zoom; contentRect()
    // is that page centred in the widget.
    QSize pagePixelSize() const;
    QRectF contentRect() const;

    // Callback resurrected on the GUI thread when a page render completes.
    void onRenderFinished(int pageIndex, QImage image, bool success);

    sk_sp<GrDirectContext> m_skiaContext;
#endif

private:
    PdfDocument* m_document = nullptr;
    int m_currentPage = -1;
    int m_pageCount   = 0;
    qreal m_zoom      = 1.0;
    int m_rotation    = 0;

    // Raster cache of PDFium-rendered pages (pixel size = page size at the
    // current zoom / rotation), the GPU texture source for each frame.
    QMap<int, QImage> m_renderedPages;
    QSet<int> m_pendingPages;
    QList<QFuture<void>> m_renderFutures;
};

#endif // SKIAPDFVIEWERWIDGET_H