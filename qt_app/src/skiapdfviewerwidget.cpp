#include "skiapdfviewerwidget.h"
#include "pdfdocument.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QtConcurrent/QtConcurrent>

#ifdef SKIA_AVAILABLE
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#endif

// ---------------------------------------------------------------------------
// Lifecycle / document plumbing (shared by both configurations)
// ---------------------------------------------------------------------------

SkiaPdfViewerWidget::SkiaPdfViewerWidget(QWidget* parent)
#ifdef SKIA_AVAILABLE
    : QOpenGLWidget(parent)
#else
    : QWidget(parent)
#endif
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

SkiaPdfViewerWidget::~SkiaPdfViewerWidget() {
    waitForPendingRenders();
}

void SkiaPdfViewerWidget::setDocument(PdfDocument* document) {
    waitForPendingRenders();
    m_document = document;
    m_currentPage = -1;
    clearRenderCache();
    if (m_document) {
#ifdef SKIA_AVAILABLE
        connect(m_document, &PdfDocument::renderFinished,
                this, &SkiaPdfViewerWidget::onRenderFinished);
#endif
        m_pageCount = m_document->pageCount();
        if (m_pageCount > 0) setPage(0);
        emit pageCountChanged(m_pageCount);
    } else {
        m_pageCount = 0;
        emit pageCountChanged(0);
    }
    update();
}

void SkiaPdfViewerWidget::setPage(int pageIndex) {
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) return;
    if (pageIndex == m_currentPage) return;
    m_currentPage = pageIndex;
    renderCurrentPage();
    emit pageChanged(m_currentPage);
}

void SkiaPdfViewerWidget::setZoom(qreal zoom) {
    zoom = qBound(0.1, zoom, 20.0);
    if (qFuzzyCompare(zoom, m_zoom)) return;
    m_zoom = zoom;
    renderCurrentPage();
    emit zoomChanged(m_zoom);
}

void SkiaPdfViewerWidget::setRotation(int rotation) {
    rotation = ((rotation % 360) + 360) % 360;
    if (rotation == m_rotation) return;
    m_rotation = rotation;
    clearRenderCache();
    renderCurrentPage();
    emit rotationChanged(m_rotation);
}

void SkiaPdfViewerWidget::zoomIn()            { setZoom(m_zoom * 1.25); }
void SkiaPdfViewerWidget::zoomOut()           { setZoom(m_zoom / 1.25); }
void SkiaPdfViewerWidget::goToNextPage()      { setPage(m_currentPage + 1); }
void SkiaPdfViewerWidget::goToPrevPage()      { setPage(m_currentPage - 1); }
void SkiaPdfViewerWidget::goToFirstPage()     { setPage(0); }
void SkiaPdfViewerWidget::goToLastPage()      { setPage(m_pageCount - 1); }
void SkiaPdfViewerWidget::rotateClockwise()   { setRotation(m_rotation + 90); }
void SkiaPdfViewerWidget::rotateCounterClockwise() { setRotation(m_rotation - 90); }

void SkiaPdfViewerWidget::fitToView() {
    if (!m_document || m_currentPage < 0) return;
    const QSizeF ps = m_document->pageSize(m_currentPage);
    if (ps.isEmpty() || width() == 0 || height() == 0) return;
    QSizeF s = ps;
    if ((m_rotation / 90) % 2) s.transpose();
    setZoom(qMin((width() - 16.0) / s.width(), (height() - 16.0) / s.height()));
}

void SkiaPdfViewerWidget::renderCurrentPage() {
    if (!m_document || m_currentPage < 0) return;
    requestRender(m_currentPage);
    update();
}

void SkiaPdfViewerWidget::requestRender(int pageIndex) {
    if (!m_document || pageIndex < 0) return;
    if (m_pendingPages.contains(pageIndex)) return;

    QImage cached = m_renderedPages.value(pageIndex);
    QSize wanted;
    if (m_document) {
        QSizeF ps = m_document->pageSize(pageIndex);
        if (ps.isEmpty()) return;
        if ((m_rotation / 90) % 2) ps.transpose();
        ps *= m_zoom;
        wanted = ps.toSize();
    }
    if (wanted.isEmpty()) return;
    if (!cached.isNull() && cached.size() == wanted) return; // fresh enough

    m_pendingPages.insert(pageIndex);
    QFuture<void> future = m_document->requestRender(pageIndex, wanted, qreal(m_rotation));
    m_renderFutures.append(future);
}

void SkiaPdfViewerWidget::waitForPendingRenders() {
    for (QFuture<void>& future : m_renderFutures) future.waitForFinished();
    m_renderFutures.clear();
}

void SkiaPdfViewerWidget::clearRenderCache() {
    m_renderedPages.clear();
    m_pendingPages.clear();
}

// ---------------------------------------------------------------------------
// GPU path (PDFIUM_ENABLE_SKIA=ON)
// ---------------------------------------------------------------------------
#ifdef SKIA_AVAILABLE

void SkiaPdfViewerWidget::onRenderFinished(int pageIndex, QImage image, bool success) {
    m_pendingPages.remove(pageIndex);
    if (!success || image.isNull()) return;
    m_renderedPages.insert(pageIndex, image);

    // Bound the raster cache (a few GPU texture tiers per page are cheap).
    while (m_renderedPages.size() > 24) {
        const int oldKey = m_renderedPages.firstKey();
        if (oldKey == m_currentPage) {
            // Evict a non-current page instead.
            const bool found = std::any_of(
                m_renderedPages.keyBegin(), m_renderedPages.keyEnd(),
                [this](int k) { return k != m_currentPage; });
            if (found) {
                for (auto it = m_renderedPages.begin(); it != m_renderedPages.end(); ++it) {
                    if (it.key() != m_currentPage) { m_renderedPages.erase(it); break; }
                    ++it;
                }
            } else {
                break;
            }
        } else {
            m_renderedPages.remove(oldKey);
        }
    }
    if (pageIndex == m_currentPage) update();
}

void SkiaPdfViewerWidget::initializeGL() {
    sk_sp<const GrGLInterface> glInterface = GrGLMakeNativeInterface();
    if (!glInterface) {
        emit statusMessage("Skia: GrGLMakeNativeInterface() failed");
        return;
    }
    m_skiaContext = GrDirectContexts::MakeGL(glInterface);
    if (!m_skiaContext) {
        emit statusMessage("Skia: GrDirectContext creation failed");
        return;
    }
    emit statusMessage("Skia: OpenGL GPU context ready");
}

void SkiaPdfViewerWidget::resizeGL(int, int) {
    update();
}

void SkiaPdfViewerWidget::paintEvent(QPaintEvent*) {
    // Rendering is driven by paintGL(); falling back here would indicate a
    // QOpenGLWidget setup problem, so only clear to a neutral colour.
    QPainter p(this);
    p.fillRect(rect(), QColor(230, 230, 230));
}

void SkiaPdfViewerWidget::resizeEvent(QResizeEvent* event) {
    QOpenGLWidget::resizeEvent(event);
    update();
}

QSize SkiaPdfViewerWidget::pagePixelSize() const {
    QSizeF ps = m_document ? m_document->pageSize(m_currentPage) : QSizeF();
    if (ps.isEmpty()) return {};
    if ((m_rotation / 90) % 2) ps.transpose();
    ps *= m_zoom;
    return ps.toSize();
}

QRectF SkiaPdfViewerWidget::contentRect() const {
    const QSize sz = pagePixelSize();
    if (sz.isEmpty()) return {};
    QRectF r(QPointF(0, 0), QSizeF(sz));
    r.moveCenter(QRect(0, 0, width(), height()).center());
    return r;
}

void SkiaPdfViewerWidget::paintGL() {
    if (!m_skiaContext) {
        QPainter p(this);
        p.fillRect(rect(), QColor(240, 240, 240));
        return;
    }

    const int viewW = width();
    const int viewH = height();
    if (viewW <= 0 || viewH <= 0) return;

    // Wrap the QOpenGLWidget back-buffer (an FBO on most platforms, framebuffer
    // 0 otherwise) in a SkSurface the Skia Ganesh context can render into.
    GrGLFramebufferInfo framebufferInfo;
    framebufferInfo.fFBOID = defaultFramebufferObject();
    framebufferInfo.fFormat = 0x8058; // GL_RGBA8
    GrBackendRenderTarget renderTarget =
        GrBackendRenderTargets::MakeGL(viewW, viewH, 0, 0, framebufferInfo);

    sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
        m_skiaContext.get(), renderTarget, kTopLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType, /*colorSpace=*/nullptr, /*surfaceProps=*/nullptr);
    if (!surface) return;

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorWHITE);

    const QImage image = m_renderedPages.value(m_currentPage);
    if (!image.isNull() && m_document) {
        // PDFium produces (after the ARGB conversion) a premultiplied BGRA
        // bitmap, which matches Skia's kBGRA_8888 layout byte-for-byte.
        const SkImageInfo info = SkImageInfo::Make(
            image.width(), image.height(), kBGRA_8888_SkColorType, kPremul_SkAlphaType);
        SkPixmap pixmap(info, image.constBits(), image.bytesPerLine());
        sk_sp<SkImage> pageImage = SkImages::RasterFromPixmapCopy(pixmap);
        if (pageImage) {
            const QRectF dstRect = contentRect();
            const SkRect src = SkRect::MakeIWH(image.width(), image.height());
            const SkRect dst = SkRect::MakeXYWH(
                SkScalar(dstRect.x()), SkScalar(dstRect.y()),
                SkScalar(dstRect.width()), SkScalar(dstRect.height()));
            SkPaint imagePaint;
            imagePaint.setAntiAlias(true);
            canvas->drawImageRect(pageImage.get(), src, dst,
                                  SkSamplingOptions(SkFilterMode::kLinear),
                                  &imagePaint, SkCanvas::kFast_SrcRectConstraint);

            SkPaint borderPaint;
            borderPaint.setStyle(SkPaint::kStroke_Style);
            borderPaint.setColor(SK_ColorDKGRAY);
            borderPaint.setStrokeWidth(1);
            canvas->drawRect(dst, borderPaint);
        }
    }

    m_skiaContext->flushAndSubmit();
}

#else // !SKIA_AVAILABLE

// ---------------------------------------------------------------------------
// Placeholder path (PDFIUM_ENABLE_SKIA=OFF)
// ---------------------------------------------------------------------------

void SkiaPdfViewerWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(240, 240, 240));
    if (!m_renderedPages.isEmpty()) {
        const QImage image = m_renderedPages.value(m_currentPage);
        if (!image.isNull()) {
            QRectF dest(0, 0, image.width(), image.height());
            dest.moveCenter(rect().center());
            p.drawImage(dest.topLeft(), image);
            p.setPen(Qt::gray);
            p.drawRect(dest);
        }
    }
    drawPlaceholder(p);
}

void SkiaPdfViewerWidget::drawPlaceholder(QPainter& painter) {
    painter.setPen(QColor(128, 128, 128));
    painter.drawText(rect(), Qt::AlignHCenter | Qt::AlignBottom,
                     "[ Skia stub \xe2\x80\x93 build with PDFIUM_ENABLE_SKIA=ON ]");
}

void SkiaPdfViewerWidget::resizeEvent(QResizeEvent*) {
    renderCurrentPage();
}

#endif // SKIA_AVAILABLE