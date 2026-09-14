#ifndef ABSTRACTPDFVIEWER_H
#define ABSTRACTPDFVIEWER_H

// Unified PDF viewer interface (PLAN.md §9).
//
// Both viewer backends implement this interface:
//   - PdfViewerWidget      (CPU: QWidget + QPainter raster via PDFium)
//   - SkiaPdfViewerWidget  (GPU: QOpenGLWidget + Skia, when SKIA_AVAILABLE)
// MainWindow drives either backend through a single AbstractPdfViewer*.
//
// NOTE: this is intentionally a pure abstract class rather than a
// QObject/QWidget subclass. SkiaPdfViewerWidget must stay a QOpenGLWidget in
// GPU builds, so the shared interface cannot contribute its own QObject/QWidget
// base (multiple QObject bases are illegal). The QWidget surface is exposed per
// concrete class through widget(), and each concrete viewer mirrors "widget()"
// returning itself. The signals still live on the concrete classes (identical
// names/signatures); MainWindow connects them in one setup function.

#include <QPoint>
#include <QRectF>
#include <QString>

class PdfDocument;
class QWidget;

class AbstractPdfViewer {
public:
    virtual ~AbstractPdfViewer() = default;

    enum class ViewMode { SinglePage, Continuous };

    // The concrete QWidget surface (self). Lets MainWindow treat either
    // backend as the central widget without knowing its concrete type.
    virtual QWidget* widget() const = 0;

    // Document.
    virtual void setDocument(PdfDocument* document) = 0;
    virtual PdfDocument* document() const = 0;

    // Page / document state.
    virtual void setPage(int pageIndex) = 0;
    virtual int currentPage() const = 0;
    virtual int pageCount() const = 0;

    // Zoom / rotation.
    virtual void setZoom(qreal zoom) = 0;
    virtual qreal zoom() const = 0;
    virtual void setRotation(int rotation) = 0;
    virtual int rotation() const = 0;

    // File.
    virtual bool loadFile(const QString& filePath, const QString& password = QString()) = 0;

    // Navigation.
    virtual void goToNextPage() = 0;
    virtual void goToPrevPage() = 0;
    virtual void goToFirstPage() = 0;
    virtual void goToLastPage() = 0;
    virtual void zoomIn() = 0;
    virtual void zoomOut() = 0;
    virtual void zoomToFit() = 0;
    virtual void zoomToWidth() = 0;
    virtual void rotateClockwise() = 0;
    virtual void rotateCounterClockwise() = 0;

    // Display mode.
    virtual void setViewMode(ViewMode mode) = 0;
    virtual ViewMode viewMode() const = 0;

    // Text selection.
    virtual void setTextSelectionEnabled(bool enabled) = 0;
    virtual bool isTextSelectionEnabled() const = 0;

    // Coordinate mapping (PDF page space <-> widget pixels, rotation-aware).
    virtual QPointF mapToPage(const QPoint& widgetPos) const = 0;
    virtual QRectF pageRect() const = 0;
    virtual QRectF pageRect(int pageIndex) const = 0;
};

#endif // ABSTRACTPDFVIEWER_H