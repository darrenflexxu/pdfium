#include "skiapdfviewerwidget.h"
#include "pdfdocument.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QPolygonF>
#include <QTransform>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QHash>
#include <QPainter>
#include <QStringList>
#include <QtConcurrent>
#include <algorithm>

#ifdef SKIA_AVAILABLE
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "include/core/SkSurface.h"
#include "include/core/SkBitmap.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkImageGanesh.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/core/SkSpan.h"
#endif

namespace {
    // Actual pixel size of the framebuffer Qt will present for this widget.
    // The widget's default framebuffer may be smaller (e.g. the native window
    // backing store on macOS) than the widget's rect()/GL_VIEWPORT, so neither
    // can be trusted as the drawable size.
    QSize framebufferSize(QOpenGLContext* ctx, GLuint fboId) {
        QOpenGLFunctions* f = ctx->functions();
        GLint w = -1, h = -1;
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboId);
        GLint attType = GL_NONE;
        f->glGetFramebufferAttachmentParameteriv(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attType);
        if (attType == GL_RENDERBUFFER) {
            GLint rw = 0, rh = 0;
            f->glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                            GL_RENDERBUFFER_WIDTH, &rw);
            f->glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                            GL_RENDERBUFFER_HEIGHT, &rh);
            w = rw; h = rh;
        } else if (attType == GL_TEXTURE) {
            GLint texName = 0;
            f->glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &texName);
            typedef void (*TexLevelFn)(GLenum, GLint, GLenum, GLint*);
            TexLevelFn glGetTexLevelParameteriv =
                reinterpret_cast<TexLevelFn>(ctx->getProcAddress(
                    "glGetTexLevelParameteriv"));
            if (glGetTexLevelParameteriv) {
                GLint tw = 0, th = 0;
                f->glBindTexture(GL_TEXTURE_2D, texName);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
                f->glBindTexture(GL_TEXTURE_2D, 0);
                w = tw; h = th;
            }
        }
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboId);
        if (w > 0 && h > 0) return QSize(w, h);
        return QSize();
    }
} // namespace

namespace {
    constexpr qreal kTopMargin = 16.0;
    constexpr qreal kPageGap = 24.0;
#ifdef SKIA_AVAILABLE
    constexpr GLenum kRGBA8 = 0x8058;
    constexpr int kRenderCacheLimit = 64;

    SkRect toSkRect(const QRectF& r) {
        return SkRect::MakeXYWH(r.x(), r.y(), r.width(), r.height());
    }
#endif
}

SkiaPdfViewerWidget::SkiaPdfViewerWidget(QWidget* parent)
#ifdef SKIA_AVAILABLE
    : QOpenGLWidget(parent)
#else
    : QWidget(parent)
#endif
{
#ifdef SKIA_AVAILABLE
    // Skia's Ganesh GL backend emits shaders that use GLSL 1.30+ features
    // (e.g. gl_VertexID in the glyph-atlas shader). Qt defaults to a legacy
    // OpenGL 2.1 / GLSL 1.20 context on macOS, where those fail to compile,
    // so request a 3.3 core-profile context before the widget is shown.
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        fmt.setVersion(3, 3);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        fmt.setDepthBufferSize(24);
        fmt.setStencilBufferSize(8);
        setFormat(fmt);
    }
#endif
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

SkiaPdfViewerWidget::~SkiaPdfViewerWidget() {
    // NOTE: do NOT disconnect(m_document, ...) here.  The document may
    // have already been destroyed (e.g. sibling child of a parent that
    // destroys in creation order).  Qt auto-disconnects all signal
    // connections from/to `this' in ~QObject, which runs after our body.
}

void SkiaPdfViewerWidget::setDocument(PdfDocument* document) {
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }


    m_document = document;
    m_currentPage = -1;
    m_pageCount = 0;
    clearTextSelectionState();
    m_scrollOffset = QPoint(0, 0);

    if (m_document) {
        connect(m_document, &PdfDocument::loadFinished,
                this, &SkiaPdfViewerWidget::onLoadFinished);
        m_pageCount = m_document->pageCount();
        if (m_pageCount > 0) {
            setPage(0);
        }
        emit pageCountChanged(m_pageCount);
    }

    update();
}

bool SkiaPdfViewerWidget::loadFile(const QString& filePath, const QString& password) {
    if (!m_document)
        return false;

    m_currentPage = -1;
    m_pageCount = 0;
    m_zoom = 1.0;
    m_rotation = 0;
    clearTextSelectionState();
    m_scrollOffset = QPoint(0, 0);

    const bool ok = m_document->load(filePath, password);
    if (ok) {
        m_pageCount = m_document->pageCount();
        if (m_pageCount > 0) {
            setPage(0);
        }
        emit pageCountChanged(m_pageCount);
    }
    update();
    return ok;
}

void SkiaPdfViewerWidget::setPage(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= m_pageCount) return;
    if (pageIndex == m_currentPage) return;

    m_currentPage = pageIndex;
    if (m_viewMode == ViewMode::Continuous) {
        setScrollToPage(pageIndex);
    } else {
        m_scrollOffset = QPoint(0, 0);
    }
    clearTextSelectionState();
    renderCurrentPage();
    emit pageChanged(m_currentPage);
}

void SkiaPdfViewerWidget::setZoom(qreal zoom) {
    zoom = qBound(0.1, zoom, 10.0);
    if (qFuzzyCompare(zoom, m_zoom)) return;

    m_zoom = zoom;
    renderCurrentPage();
    emit zoomChanged(m_zoom);
}

void SkiaPdfViewerWidget::setRotation(int rotation) {
    rotation = ((rotation % 360) + 360) % 360;
    if (rotation == m_rotation) return;

    m_rotation = rotation;
    renderCurrentPage();
    emit rotationChanged(m_rotation);
}

void SkiaPdfViewerWidget::setViewMode(ViewMode mode) {
    if (mode == m_viewMode) return;
    m_viewMode = mode;
    clearTextSelectionState();
    if (m_viewMode == ViewMode::Continuous) {
        if (m_currentPage < 0 && m_pageCount > 0) {
            m_currentPage = 0;
            emit pageChanged(0);
        }
        if (m_currentPage >= 0) {
            setScrollToPage(m_currentPage);
        }
        refreshVisiblePages();
    } else {
        renderCurrentPage();
    }
    update();
}

void SkiaPdfViewerWidget::goToNextPage() {
    if (m_currentPage < m_pageCount - 1) setPage(m_currentPage + 1);
}

void SkiaPdfViewerWidget::goToPrevPage() {
    if (m_currentPage > 0) setPage(m_currentPage - 1);
}

void SkiaPdfViewerWidget::goToFirstPage() {
    setPage(0);
}

void SkiaPdfViewerWidget::goToLastPage() {
    setPage(m_pageCount - 1);
}

void SkiaPdfViewerWidget::zoomIn() {
    setZoom(m_zoom * 1.2);
}

void SkiaPdfViewerWidget::zoomOut() {
    setZoom(m_zoom / 1.2);
}

void SkiaPdfViewerWidget::zoomToFit() {
    if (!m_document || m_currentPage < 0) return;

    QSizeF pageSize = m_document->pageSize(m_currentPage);
    if (pageSize.isEmpty()) return;

    QSize widgetSize = size();
    if (widgetSize.isEmpty()) return;

    if (m_rotation == 90 || m_rotation == 270) {
        pageSize.transpose();
    }

    qreal zoomX = widgetSize.width() / pageSize.width();
    qreal zoomY = widgetSize.height() / pageSize.height();
    setZoom(qMin(zoomX, zoomY) * 0.95);
}

void SkiaPdfViewerWidget::zoomToWidth() {
    if (!m_document || m_currentPage < 0) return;

    QSizeF pageSize = m_document->pageSize(m_currentPage);
    if (pageSize.isEmpty()) return;

    QSize widgetSize = size();
    if (widgetSize.isEmpty()) return;

    if (m_rotation == 90 || m_rotation == 270) {
        pageSize.transpose();
    }

    qreal zoomX = widgetSize.width() / pageSize.width();
    setZoom(zoomX * 0.95);
}

void SkiaPdfViewerWidget::rotateClockwise() {
    setRotation(m_rotation + 90);
}

void SkiaPdfViewerWidget::rotateCounterClockwise() {
    setRotation(m_rotation - 90);
}

void SkiaPdfViewerWidget::setTextSelectionEnabled(bool enabled) {
    m_textSelectionEnabled = enabled;
    if (!enabled) {
        clearTextSelectionState();
        update();
    }
}

// ---------------------------------------------------------------------------
// Layout / geometry
// ---------------------------------------------------------------------------

QSizeF SkiaPdfViewerWidget::pagePixelSize(int pageIndex) const {
    QSizeF size;
    if (m_document && pageIndex >= 0 && pageIndex < m_pageCount) {
        QSizeF pageSize = m_document->pageSize(pageIndex);
        if (pageSize.isEmpty()) return size;
        if (m_rotation == 90 || m_rotation == 270) {
            pageSize.transpose();
        }
        size = (pageSize * m_zoom).toSize();
    }
    return size;
}

QRectF SkiaPdfViewerWidget::contentRect() const {
    return pageRectOf(m_currentPage);
}

QRectF SkiaPdfViewerWidget::pageRect() const {
    return pageRectOf(m_currentPage);
}

QRectF SkiaPdfViewerWidget::pageRect(int pageIndex) const {
    return pageRectOf(pageIndex);
}

// The authoritative layout viewport. In the Skia build the true renderable
// logical size comes from resizeGL/paintGL (the actual GL viewport), never
// from rect() which can be stale or oversized before the first layout.
QRectF SkiaPdfViewerWidget::viewportRect() const {
#ifdef SKIA_AVAILABLE
    if (m_viewportLogical.isEmpty())
        return rect();
    return QRectF(QPointF(0, 0), m_viewportLogical);
#else
    return rect();
#endif
}

QRectF SkiaPdfViewerWidget::pageRectOf(int pageIndex) const {
    const QRectF vp = viewportRect();
    if (m_viewMode == ViewMode::Continuous) {
        QSizeF size = pagePixelSize(pageIndex);
        if (size.isEmpty()) return QRectF();
        qreal x = (vp.width() - size.width()) / 2.0;
        return QRectF(x, m_scrollOffset.y() + continuousY(pageIndex),
                      size.width(), size.height());
    }

    QSize size;
    if (m_document && pageIndex >= 0 && pageIndex < m_pageCount) {
        QSizeF pageSize = m_document->pageSize(pageIndex);
        if (pageSize.isEmpty()) return QRectF();
        if (m_rotation == 90 || m_rotation == 270) {
            pageSize.transpose();
        }
        size = (pageSize * m_zoom).toSize();
    } else {
        return QRectF();
    }

    QPointF center = vp.center() + m_scrollOffset;
    QRectF pageRect;
    pageRect.setSize(size);
    pageRect.moveCenter(center);
    return pageRect;
}

qreal SkiaPdfViewerWidget::continuousY(int pageIndex) const {
    qreal y = kTopMargin;
    for (int i = 0; i < pageIndex; ++i)
        y += pagePixelSize(i).height() + kPageGap;
    return y;
}

int SkiaPdfViewerWidget::pageAtY(qreal y, bool clamp) const {
    if (m_viewMode != ViewMode::Continuous || m_pageCount <= 0) return -1;
    for (int p = 0; p < m_pageCount; ++p) {
        const qreal top = m_scrollOffset.y() + continuousY(p);
        const qreal bottom = top + pagePixelSize(p).height();
        if (y >= top && y < bottom) return p;
    }
    if (!clamp) return -1;
    if (y < m_scrollOffset.y() + continuousY(0)) return 0;
    return m_pageCount - 1;
}

int SkiaPdfViewerWidget::pageUnderPoint(const QPoint& widgetPos) const {
    if (m_viewMode == ViewMode::Continuous)
        return pageAtY(widgetPos.y(), true);
    return m_currentPage;
}

void SkiaPdfViewerWidget::setScrollToPage(int pageIndex) {
    if (pageIndex < 0) return;
    m_scrollOffset.setY(int(kTopMargin - continuousY(pageIndex)));
    ++m_scrollVersion;
}

void SkiaPdfViewerWidget::refreshVisiblePages() {
    if (!m_document || !m_document->isLoaded() || m_pageCount <= 0) return;
    if (m_viewMode != ViewMode::Continuous) return;
    
    update();
}


void SkiaPdfViewerWidget::updateCurrentPageForViewport() {
    if (m_viewMode != ViewMode::Continuous || m_pageCount <= 0) return;
    if (m_scrollVersion == m_trackedScrollVersion) return;
    m_trackedScrollVersion = m_scrollVersion;
    const int p = pageAtY(viewportRect().center().y(), true);
    if (p >= 0 && p != m_currentPage) {
        m_currentPage = p;
        clearTextSelectionState();
        emit pageChanged(p);
    }
}

// ---------------------------------------------------------------------------
// Coordinate mapping (PDF page space <-> widget pixels, rotation-aware).
// ---------------------------------------------------------------------------

QPointF SkiaPdfViewerWidget::mapToPage(const QPoint& widgetPos) const {
    return mapToPageOnPage(widgetPos, m_currentPage);
}

QPointF SkiaPdfViewerWidget::mapFromPageF(const QPointF& pagePos) const {
    return mapFromPageOnPage(pagePos, m_currentPage);
}

// Build the affine map page-space -> widget-space for one page.  Computing it
// once per page per frame is cheap; applying it per char rect below avoids
// calling pageToDevice() (which loads the page via FPDF_LoadPage each time)
// once per character in the selection/search draw hot paths.
bool SkiaPdfViewerWidget::pageToWidgetTransform(int pageIndex,
                                                QTransform& out) const {
    QRectF pRect = pageRectOf(pageIndex);
    if (pRect.isEmpty() || !m_document || pageIndex < 0 || pageIndex >= m_pageCount)
        return false;

    const QSizeF pageSize = m_document->pageSize(pageIndex);
    if (pageSize.isEmpty())
        return false;

    QPolygonF src, dst;
    src << QPointF(0, 0) << QPointF(pageSize.width(), 0)
        << QPointF(pageSize.width(), pageSize.height())
        << QPointF(0, pageSize.height());
    dst << mapFromPageOnPage(QPointF(0, 0), pageIndex)
        << mapFromPageOnPage(QPointF(pageSize.width(), 0), pageIndex)
        << mapFromPageOnPage(QPointF(pageSize.width(), pageSize.height()),
                             pageIndex)
        << mapFromPageOnPage(QPointF(0, pageSize.height()), pageIndex);
    if (!QTransform::quadToQuad(src, dst, out))
        return false;
    return true;
}

QPointF SkiaPdfViewerWidget::mapFromPageOnPage(const QPointF& pagePos,
                                               int pageIndex) const {
    QRectF pRect = pageRectOf(pageIndex);
    if (pRect.isEmpty() || !m_document || pageIndex < 0 || pageIndex >= m_pageCount)
        return QPointF();

    return m_document->pageToDevice(pageIndex, pagePos, QPoint(0, 0),
                                    pRect.size().toSize(), m_rotation)
        + pRect.topLeft();
}

QPointF SkiaPdfViewerWidget::mapToPageOnPage(const QPoint& widgetPos,
                                             int pageIndex) const {
    QRectF pRect = pageRectOf(pageIndex);
    if (pRect.isEmpty() || !m_document || pageIndex < 0 || pageIndex >= m_pageCount)
        return QPointF();

    return m_document->deviceToPage(pageIndex,
                                    QPointF(widgetPos) - pRect.topLeft(),
                                    QPoint(0, 0), pRect.size().toSize(),
                                    m_rotation);
}

QRectF SkiaPdfViewerWidget::mapRectFromPageOnPage(const QRectF& pdfRect,
                                                  int pageIndex) const {
    QRectF r = pdfRect.normalized();
    if (r.isEmpty() || !m_document || pageIndex < 0 || pageIndex >= m_pageCount)
        return QRectF();

    const QPointF mapped[4] = {
        mapFromPageOnPage(r.topLeft(), pageIndex),
        mapFromPageOnPage(r.topRight(), pageIndex),
        mapFromPageOnPage(r.bottomLeft(), pageIndex),
        mapFromPageOnPage(r.bottomRight(), pageIndex),
    };
    qreal left = mapped[0].x(), right = left;
    qreal top = mapped[0].y(), bottom = top;
    for (int i = 1; i < 4; ++i) {
        left = qMin(left, mapped[i].x());
        right = qMax(right, mapped[i].x());
        top = qMin(top, mapped[i].y());
        bottom = qMax(bottom, mapped[i].y());
    }
    return QRectF(left, top, right - left, bottom - top);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void SkiaPdfViewerWidget::renderCurrentPage() {
    update();
}




void SkiaPdfViewerWidget::clearTextSelectionState() {
    m_selecting = false;
    m_cursorIndex = -1;
    m_selectionStart = -1;
    m_selectionEnd = -1;
    m_boxSelStart = m_boxSelEnd = QPoint();
    m_clickCount = 1;
    m_lastClickIndex = -1;
}

void SkiaPdfViewerWidget::onLoadFinished(bool success, const QString& error) {
    if (!m_document)
        return;
    if (success) {
        // A document may be (re)loaded while this viewer is idle/not central;
        // resync so a later runtime toggle starts from a consistent state.
        m_pageCount = m_document->pageCount();
        if (m_currentPage < 0 && m_pageCount > 0) {
            setPage(0);
        } else {
            renderCurrentPage();
        }
        emit pageCountChanged(m_pageCount);
        emit statusMessage(QString("Loaded: %1 pages").arg(m_pageCount));
    } else {
        emit statusMessage("Error: " + error);
    }
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void SkiaPdfViewerWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom with Ctrl+Wheel; in Continuous mode anchor the cursor's point.
        const qreal f = event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
        if (m_viewMode == ViewMode::Continuous && m_document && m_document->isLoaded()) {
            const int page = pageUnderPoint(event->position().toPoint());
            const QPointF pagePt = page >= 0
                ? mapToPageOnPage(event->position().toPoint(), page)
                : QPointF();
            setZoom(m_zoom * f);
            if (page >= 0 && !pagePt.isNull()) {
                const QPointF after = mapFromPageOnPage(pagePt, page);
                m_scrollOffset += (QPointF(event->position().toPoint()) - after).toPoint();
                ++m_scrollVersion;
                update();
            }
        } else {
            setZoom(m_zoom * f);
        }
        event->accept();
    } else {
        m_scrollOffset.setY(m_scrollOffset.y() - event->angleDelta().y());
        ++m_scrollVersion;
        update();
        refreshVisiblePages();
        event->accept();
    }
}

void SkiaPdfViewerWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_textSelectionEnabled && m_document && m_document->isLoaded() &&
            m_currentPage >= 0) {
            const int pressedPage = pageUnderPoint(event->pos());
            if (m_viewMode == ViewMode::Continuous && pressedPage >= 0 &&
                pressedPage != m_currentPage) {
                m_currentPage = pressedPage;
                clearTextSelectionState();
                emit pageChanged(pressedPage);
            }
            QRectF pRect = pageRect();
            if (pRect.contains(event->pos())) {
                QPointF pagePos = mapToPage(event->pos());
                int idx = m_document->findNearestCharIndex(m_currentPage, pagePos);

                const bool multiClick =
                    event->type() == QEvent::MouseButtonDblClick;
                m_lastClickPagePos = pagePos;
                if (multiClick && m_lastClickIndex == idx) {
                    ++m_clickCount;
                } else {
                    m_clickCount = 1;
                }
                m_lastClickIndex = idx;
                m_selecting = true;

                if (idx >= 0 && m_document->charMap(m_currentPage).size() > 0) {
                    m_cursorIndex = idx;
                    m_selectionStart = m_selectionEnd = idx;
                    m_boxSelStart = m_boxSelEnd = event->pos();

                    if (m_clickCount == 2) {
                        const QPair<int, int> wr =
                            m_document->wordRange(m_currentPage, idx);
                        m_selectionStart = wr.first;
                        m_selectionEnd = wr.second;
                        m_cursorIndex = wr.second;
                    } else if (m_clickCount == 3) {
                        const QPair<int, int> pr =
                            m_document->paragraphRange(m_currentPage, idx);
                        m_selectionStart = pr.first;
                        m_selectionEnd = pr.second;
                        m_cursorIndex = pr.second;
                    }
                } else {
                    m_cursorIndex = -1;
                    m_selectionStart = m_selectionEnd = -1;
                    m_boxSelStart = m_boxSelEnd = event->pos();
                }
                emit statusMessage(QString("Caret index: %1").arg(m_cursorIndex));
                update();
                event->accept();
                return;
            }
        }

        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void SkiaPdfViewerWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        QPoint delta = event->pos() - m_panStart;
        m_scrollOffset += delta;
        ++m_scrollVersion;
        m_panStart = event->pos();
        update();
        event->accept();
    } else if (m_selecting) {
        if (m_viewMode == ViewMode::Continuous) {
            const int overPage = pageUnderPoint(event->pos());
            if (overPage >= 0 && overPage != m_currentPage) {
                m_currentPage = overPage;
                m_cursorIndex = -1;
                m_selectionStart = m_selectionEnd = -1;
                m_boxSelStart = m_boxSelEnd = QPoint();
                emit pageChanged(overPage);
            }
        }
        QPointF pagePos = mapToPage(event->pos());
        if (m_document && m_currentPage >= 0) {
            const int idx = m_document->findNearestCharIndex(m_currentPage, pagePos);
            if (idx >= 0) {
                m_selectionEnd = idx;
                m_cursorIndex = idx;
            }
        }
        m_boxSelEnd = event->pos();
        update();
        event->accept();
    }
}

void SkiaPdfViewerWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        if (m_panning) {
            m_panning = false;
            unsetCursor();
            event->accept();
        } else if (m_selecting) {
            m_selecting = false;
            const QString selectedText = this->selectedText();
            if (!selectedText.isEmpty()) {
                emit textSelected(selectedText);
                QApplication::clipboard()->setText(selectedText);
                emit statusMessage(QString("Selected %1 chars: %2")
                                       .arg(selectedText.length())
                                       .arg(selectedText.simplified()));
            }
            update();
            event->accept();
        }
    }
}

void SkiaPdfViewerWidget::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Up:
            if (event->modifiers() & Qt::ControlModifier) {
                goToPrevPage();
            } else {
                m_scrollOffset.setY(m_scrollOffset.y() - 50);
                ++m_scrollVersion;
                update();
            }
            break;
        case Qt::Key_Right:
        case Qt::Key_Down:
        case Qt::Key_Space:
            if (event->modifiers() & Qt::ControlModifier) {
                goToNextPage();
            } else {
                m_scrollOffset.setY(m_scrollOffset.y() + 50);
                ++m_scrollVersion;
                update();
            }
            break;
        case Qt::Key_Home:
            goToFirstPage();
            break;
        case Qt::Key_End:
            goToLastPage();
            break;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            if (event->modifiers() & Qt::ControlModifier) zoomIn();
            break;
        case Qt::Key_Minus:
            if (event->modifiers() & Qt::ControlModifier) zoomOut();
            break;
        case Qt::Key_0:
            if (event->modifiers() & Qt::ControlModifier) zoomToFit();
            break;
        case Qt::Key_R:
            if (event->modifiers() & Qt::ControlModifier) rotateClockwise();
            break;
        case Qt::Key_C:
            if (event->modifiers() & Qt::ControlModifier && m_textSelectionEnabled) {
                QString text = m_document ? selectedText() : QString();
                if (text.isEmpty()) {
                    text = m_document->getPageText(m_currentPage);
                }
                if (!text.isEmpty()) {
                    QApplication::clipboard()->setText(text);
                    emit statusMessage("Selection copied to clipboard");
                }
            }
            break;
        case Qt::Key_A:
            if (event->modifiers() & Qt::ControlModifier && m_textSelectionEnabled) {
                if (m_document && m_currentPage >= 0) {
                    const QVector<CharInfo>& cm = m_document->charMap(m_currentPage);
                    if (!cm.isEmpty()) {
                        m_selectionStart = 0;
                        m_selectionEnd = cm.size();
                        m_cursorIndex = cm.size();
                        m_boxSelStart = m_boxSelEnd = QPoint();
                        update();
                        emit statusMessage(QString("Selected all %1 chars").arg(cm.size()));
                    }
                }
            }
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    event->accept();
}

QString SkiaPdfViewerWidget::selectedText() const {
    if (!m_document || !m_document->isLoaded() || m_currentPage < 0)
        return QString();

    const int a = qMin(m_selectionStart, m_selectionEnd);
    const int b = qMax(m_selectionStart, m_selectionEnd);
    const bool plainDrag = m_clickCount <= 1;

    QString text;
    if (plainDrag && !m_boxSelStart.isNull() && !m_boxSelEnd.isNull()) {
        const QRectF pageSel = widgetToPageRect(
            QRect(m_boxSelStart, m_boxSelEnd).normalized());
        if (!pageSel.isEmpty()) {
            text = m_document->extractTextInRect(m_currentPage, pageSel);
        }
    }
    if (text.isEmpty() && a >= 0 && b > a) {
        text = m_document->textForRange(m_currentPage, a, b);
    }
    return text;
}

QRectF SkiaPdfViewerWidget::widgetToPageRect(const QRect& widgetRect) const {
    if (!m_document || m_currentPage < 0)
        return QRectF();
    const QPointF p1 = mapToPage(widgetRect.topLeft());
    const QPointF p2 = mapToPage(widgetRect.bottomRight());
    return QRectF(p1, p2).normalized();
}

// ---------------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------------

void SkiaPdfViewerWidget::paintEvent(QPaintEvent* event) {
#ifdef SKIA_AVAILABLE
    QOpenGLWidget::paintEvent(event);
#else
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Dark));
    drawPlaceholder(painter);
#endif
}

void SkiaPdfViewerWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    renderCurrentPage();
    if (m_viewMode == ViewMode::Continuous) update();
}

#ifndef SKIA_AVAILABLE
void SkiaPdfViewerWidget::drawPlaceholder(QPainter& painter) {
    if (!m_document || !m_document->isLoaded()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter,
                         "GPU viewer inactive\n\nBuilt without PDFIUM_ENABLE_SKIA");
    } else if (m_pageCount > 0) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter,
                         QString("GPU viewer placeholder\n\nPage %1 of %2")
                             .arg(m_currentPage + 1).arg(m_pageCount));
    }
}
#endif

#ifndef SKIA_AVAILABLE
void SkiaPdfViewerWidget::onRenderFinished(int, QImage, bool) {
    // Build without Skia: nothing to do with results here.
}
#endif

#ifdef SKIA_AVAILABLE

// ---------------------------------------------------------------------------
// Skia GPU path
// ---------------------------------------------------------------------------

void SkiaPdfViewerWidget::initializeGL() {
    auto native = GrGLMakeNativeInterface();
    if (!native) return;
    m_skiaContext = GrDirectContexts::MakeGL(native);
    qDebug().noquote() << "[PROBE] initializeGL"
                       << "native=" << (native != nullptr)
                       << "ctx=" << (m_skiaContext != nullptr);
}

void SkiaPdfViewerWidget::resizeGL(int w, int h) {
    m_viewportLogical = QSizeF(w, h) / devicePixelRatioF();
}

void SkiaPdfViewerWidget::paintGL() {
    if (m_probeFrame < 8) ++m_probeFrame; // [PROBE] shared frame counter
    if (!m_skiaContext) {
        QOpenGLFunctions* f = context()->functions();
        if (f) f->glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        if (f) f->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // Qt does its own GL work between frames (FBO recreation on resize, swap,
    // texture cleanup). Skia caches GL state, so re-sync every frame before
    // drawing or stale state makes draws silently target a dead FBO.
    m_skiaContext->resetContext();

    const qreal dpr = devicePixelRatioF();

    // The authoritative drawable size is the actual default framebuffer
    // attachment -- NOT rect(), width()*dpr, or GL_VIEWPORT. QOpenGLWidget's
    // default framebuffer can be smaller than the widget (on macOS it is the
    // window backing store sized in physical pixels), so wrapping a larger
    // surface silently leaves the excess area unpainted and skews layout.
    // Query the real size every frame; Qt recreates the FBO on relayout but
    // the viewport query may lag behind what is actually presentable.
    const QSize fboPx = framebufferSize(context(), defaultFramebufferObject());
    const int w = fboPx.isValid() ? fboPx.width() : qMax(1, qRound(width() * dpr));
    const int h = fboPx.isValid() ? fboPx.height() : qMax(1, qRound(height() * dpr));
    m_viewportLogical = QSizeF(w, h) / dpr;

    // Direct-wrap the widget's default framebuffer. Qt may recreate the FBO
    // when the widget is relaid out, but each paint wraps the *current* object.
    GrGLFramebufferInfo fbInfo = {};
    fbInfo.fFBOID = defaultFramebufferObject();
    fbInfo.fFormat = kRGBA8;
    GrBackendRenderTarget backend =
        GrBackendRenderTargets::MakeGL(w, h, 0, 8, fbInfo);
    sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
        m_skiaContext.get(), backend, kTopLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType, nullptr, nullptr);
    if (!surface) {
        qDebug().noquote() << "[PROBE] paintGL WRAP_FAIL w=" << w << "h=" << h
                           << "fbo=" << fbInfo.fFBOID;
        return;
    }
    qDebug().noquote() << "[PROBE] paintGL ok"
                       << "geom=" << geometry()
                       << "dpr=" << dpr
                       << "fboPx=" << w << "x" << h
                       << "fbo=" << fbInfo.fFBOID
                       << "page=" << m_currentPage
                       << "pageRect=" << pageRectOf(m_currentPage).toRect();

    SkCanvas* canvas = surface->getCanvas();

    const QColor bg = palette().color(QPalette::Dark);
    canvas->drawColor(SkColorSetARGB(bg.alpha() == 255 ? 255 : bg.alpha(),
                                     bg.red(), bg.green(), bg.blue()));

    canvas->save();
    // macOS presents the native default framebuffer vertically flipped, so the
    // logical widget space (y down, y=0 at top) must be mirrored into the
    // surface: top of the widget -> bottom of the surface -> top on screen.
    if (!qEnvironmentVariableIsSet("SKIA_NO_FLIP")) {
        canvas->translate(0, h);
        canvas->scale(dpr, -dpr);
    }
    drawViewContent(canvas);
    canvas->restore();

    m_skiaContext->flushAndSubmit();
    if (context())
        qDebug().noquote() << "[PROBE] ERR_after_flush="
                           << context()->functions()->glGetError();

    // [PROBE] read back the FBO right after drawing to see what actually
    // landed (this is the *widget* default FBO being presented to screen).
    {
        QOpenGLFunctions* fb = context()->functions();
        if (fb) {
            fb->glFinish();
#if !defined(NDEBUG) || defined(_DEBUG) || defined(DEBUG)
            if (m_probeFrame <= 8) {
                QImage dump(w, h, QImage::Format_RGBA8888);
                fb->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, dump.bits());
                const QString path = QString("/tmp/fbo_frame%1.png").arg(m_probeFrame);
                dump.mirrored(false, true).save(path);
                qDebug().noquote() << "[PROBE] saved" << path;
            }
#endif
            GLint vp[4] = {0, 0, 0, 0};
            fb->glGetIntegerv(GL_VIEWPORT, vp);
            GLint atType = 0, atName = 0;
            if (fb->glGetError() == 0) {
                fb->glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &atType);
                fb->glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &atName);
            }
            qDebug().noquote() << "[PROBE] state viewport=" << vp[0] << vp[1] << vp[2] << vp[3]
                               << "attach_type=" << atType << "name=" << atName;
        }
    }
    return;
}

namespace {
    void drawCenteredString(SkCanvas* canvas, const QString& text,
                            const QRectF& area, qreal fontSize, SkColor color) {
        SkFont font;
        font.setSize(fontSize);
        SkPaint paint;
        paint.setColor(color);
        paint.setAntiAlias(true);

        SkScalar lineHeight = font.getSpacing();
        QStringList lines = text.split(QLatin1Char('\n'));
        qreal totalHeight = lineHeight * lines.size();
        qreal y = area.center().y() - totalHeight / 2.0 + lineHeight;

        for (const QString& line : lines) {
            const QByteArray utf8 = line.toUtf8();
            SkRect bounds;
            font.measureText(utf8.constData(), utf8.size(),
                             SkTextEncoding::kUTF8, &bounds);
            canvas->drawSimpleText(
                utf8.constData(), utf8.size(), SkTextEncoding::kUTF8,
                area.center().x() - bounds.width() / 2.0, y, font, paint);
            y += lineHeight;
        }
    }
}

void SkiaPdfViewerWidget::drawViewContent(SkCanvas* canvas) {
    if (!m_document || !m_document->isLoaded() || m_pageCount <= 0) {
        drawCenteredString(canvas,
                           QStringLiteral("No document loaded\n\nDrag & drop a PDF "
                                          "file or use File > Open"),
                           QRectF(QPointF(0, 0), viewportRect().size()), 16, 0xFFB0B0B0);
        return;
    }

    const QRect viewport = viewportRect().toRect();
    static const bool pageDebug = qEnvironmentVariableIsSet("SKIA_PAGE_DEBUG");
    if (pageDebug)
        qDebug().noquote() << "[PAGE] frame viewport=" << viewport
                           << "count=" << m_pageCount
                           << "current=" << m_currentPage;
    if (m_viewMode == ViewMode::Continuous) {
        for (int p = 0; p < m_pageCount; ++p) {
            const QRectF pr = pageRectOf(p);
            if (pageDebug)
                qDebug().noquote() << "[PAGE] p=" << p << "rect=" << pr.toRect()
                                   << "size=" << pagePixelSize(p)
                                   << "hit=" << pr.intersects(viewport);
            if (!pr.intersects(viewport)) continue;
            drawPageOnCanvas(canvas, p, pr);
            drawSearchHighlights(canvas, p, pr);
            if (p == m_currentPage)
                drawSelection(canvas, p);
        }
    } else {
        const QRectF pr = pageRectOf(m_currentPage);
        if (!pr.isEmpty()) {
            drawPageOnCanvas(canvas, m_currentPage, pr);
            drawSearchHighlights(canvas, m_currentPage, pr);
            drawSelection(canvas, m_currentPage);
        }
    }

    // Page indicator.
    if (m_pageCount > 0) {
        const QString info = QString("Page %1 of %2")
                                 .arg(m_currentPage + 1).arg(m_pageCount);
        SkFont font;
        font.setSize(13);
        SkPaint paint;
        paint.setColor(0xFFFFFFFF);
        canvas->drawSimpleText(info.toUtf8().constData(), info.toUtf8().size(),
                               SkTextEncoding::kUTF8, 10, 18, font, paint);
    }

    refreshVisiblePages();
    updateCurrentPageForViewport();

    // [PROBE] temp: dump what Skia actually wrote to the wrapped surface,
    // independently of Qt's FBO readback, to compare drawing vs presentation.
    probeDumpCanvas(canvas);
}

void SkiaPdfViewerWidget::drawPageOnCanvas(SkCanvas* canvas, int pageIndex,
                                            const QRectF& widgetRect) {
    // Page shadow.
    SkPaint shadow;
    shadow.setColor(SkColorSetARGB(80, 0, 0, 0));
    canvas->drawRect(SkRect::MakeXYWH(widgetRect.x() + 4, widgetRect.y() + 4,
                                       widgetRect.width(), widgetRect.height()),
                      shadow);
    
    // Direct render to canvas via PDFium extension.
    if (m_document && pageIndex >= 0 && pageIndex < m_pageCount) {
        IPdfDocument* doc = m_document->interface();
        IPdfPage* page = doc ? doc->GetPage(pageIndex) : nullptr;
        if (page) {
            const int savedCount = canvas->getSaveCount();
            canvas->save();
            // Pass the page rectangle to PDFium instead of translating the
            // canvas: the Skia device driver reports its clip box in device
            // pixels (canvas_->getDeviceClipBounds()), so PDFium's object
            // culling assumes its layout rectangle starts at the canvas
            // origin. Pre-translating the canvas shifts that clip box out of
            // PDFium's coordinate space and culls visible content when zoomed.
            const int pageX = qRound(widgetRect.x());
            const int pageY = qRound(widgetRect.y());
            const int pageW = qRound(widgetRect.width());
            const int pageH = qRound(widgetRect.height());
            // PDFium does not paint the default page background when rendering
            // to an external SkCanvas. FPDF_RenderPageBitmap clears its bitmap
            // to white first, so the bitmap path gets a white page; fill it
            // here too, otherwise white/light page elements would be invisible
            // against the widget's dark background.
            SkPaint bgPaint;
            bgPaint.setColor(SK_ColorWHITE);
            canvas->drawRect(SkRect::MakeXYWH(pageX, pageY, pageW, pageH), bgPaint);
            bool success = page->RenderToCanvas(canvas, pageX, pageY, pageW,
                                                pageH, m_rotation,
                                                PDF_RENDER_ANNOTATIONS);
            // Restore to the exact save level we started at instead of a plain
            // restore(): PDFium's external-canvas render pops the canvas level
            // our save() pushed (after-render save count == savedCount), so a
            // plain restore() would over-pop and drop the presentation
            // transform applied in paintGL. That left every subsequent Qt-side
            // draw (page border, search highlights, text selection) rendered
            // without the y-flip, i.e. vertically mirrored.
            static const bool pageDebug2 = qEnvironmentVariableIsSet("SKIA_PAGE_DEBUG");
            if (pageDebug2)
                qDebug().noquote()
                    << "[PAGE] render p=" << pageIndex << "rect="
                    << QRect(pageX, pageY, pageW, pageH) << "ok=" << success
                    << "saveBefore=" << savedCount
                    << "saveAfter=" << canvas->getSaveCount();
            canvas->restoreToCount(savedCount);
            if (!success) {
                qDebug().noquote() << "[PROBE] RenderToCanvas FAILED page=" << pageIndex;
                drawCenteredString(canvas, QStringLiteral("Render Error"),
                                   widgetRect, 14, 0xFFFF0000);
            }
            page->Release();
        } else {
            drawCenteredString(canvas, QStringLiteral("Page Not Found"),
                               widgetRect, 14, 0xFFFFFFFF);
        }
    } else {
        drawCenteredString(canvas, QStringLiteral("Invalid Page"),
                           widgetRect, 14, 0xFFFFFFFF);
    }
    
    // Page border.
    SkPaint border;
    border.setStyle(SkPaint::kStroke_Style);
    border.setStrokeWidth(1);
    border.setColor(0xFFB0B0B0);
    canvas->drawRect(SkRect::MakeXYWH(widgetRect.x(), widgetRect.y(),
                                       widgetRect.width(), widgetRect.height()),
                      border);
}


void SkiaPdfViewerWidget::drawSearchHighlights(SkCanvas* canvas, int pageIndex,
                                               const QRectF& /*pageWidgetRect*/) {
    if (!m_document || !m_document->isLoaded())
        return;

    QTransform tx;
    if (!pageToWidgetTransform(pageIndex, tx)) return;

    SkPaint fill;
    fill.setColor(SkColorSetARGB(90, 255, 255, 0));
    const QList<QRectF> matches = m_document->matchesForPage(pageIndex);
    for (const QRectF& match : matches) {
        const QRectF mapped = tx.mapRect(match);
        if (mapped.isEmpty()) continue;
        canvas->drawRect(toSkRect(mapped), fill);
    }

    if (m_document->currentMatchPage() == pageIndex) {
        const QRectF currentMatch = m_document->currentMatchRect();
        if (!currentMatch.isNull()) {
            const QRectF mapped = tx.mapRect(currentMatch);
            if (!mapped.isEmpty()) {
                const SkRect r = toSkRect(mapped);
                SkPaint cfill;
                cfill.setColor(SkColorSetARGB(150, 255, 165, 0));
                canvas->drawRect(r, cfill);
                SkPaint cstroke;
                cstroke.setStyle(SkPaint::kStroke_Style);
                cstroke.setStrokeWidth(1.5f);
                cstroke.setColor(0xFFFF8C00);
                canvas->drawRect(r, cstroke);
            }
        }
    }
}

void SkiaPdfViewerWidget::drawSelection(SkCanvas* canvas, int pageIndex) {
    if (!m_textSelectionEnabled || !m_document || !m_document->isLoaded() ||
        pageIndex < 0)
        return;
    const QVector<CharInfo>& cm = m_document->charMap(pageIndex);
    const int a = qMin(m_selectionStart, m_selectionEnd);
    const int b = qMax(m_selectionStart, m_selectionEnd);
    QTransform tx;
    const bool haveTx = pageToWidgetTransform(pageIndex, tx);
    SkPaint hl;
    hl.setColor(SkColorSetARGB(90, 0, 120, 215));
    if (a >= 0 && b > a && a < cm.size() && haveTx) {
        const int hi = qMin(b, (int)cm.size());
        QRectF lineRect;
        for (int i = a; i < hi; ++i) {
            QRectF charRect = cm[i].bounds;
            if (i == a || qAbs(charRect.top() - lineRect.top()) > 2.0) {
                if (!lineRect.isEmpty()) {
                    const QRectF mapped = tx.mapRect(lineRect);
                    if (!mapped.isEmpty()) canvas->drawRect(toSkRect(mapped), hl);
                }
                lineRect = charRect;
            } else {
                lineRect = lineRect.united(charRect);
            }
        }
        if (!lineRect.isEmpty()) {
            const QRectF mapped = tx.mapRect(lineRect);
            if (!mapped.isEmpty()) canvas->drawRect(toSkRect(mapped), hl);
        }
    }

    // Box fallback overlay, only on pages with no selectable text at all.
    // NOTE: it must NOT draw a dashed/path-effect stroke: on GL the skia
    // stroke tessellator shader fails to compile on some drivers and default
    // cerr handler aborts the process.
    if (cm.isEmpty() && m_selecting && !m_boxSelStart.isNull() &&
        !m_boxSelEnd.isNull()) {
        const QRect selRect = QRect(m_boxSelStart, m_boxSelEnd).normalized();
        const SkRect r = toSkRect(QRectF(selRect));
        SkPaint bfill;
        bfill.setColor(SkColorSetARGB(100, 0, 120, 215));
        canvas->drawRect(r, bfill);
        SkPaint bstroke;
        bstroke.setStyle(SkPaint::kStroke_Style);
        bstroke.setStrokeWidth(1);
        bstroke.setColor(0xFF0078D7);
        canvas->drawRect(r, bstroke);
    }

    // Caret: a 1px vertical line at the left edge of the char that follows the
    // caret index (right edge of the last char when at the end).
    if (m_cursorIndex >= 0 && !cm.isEmpty() && haveTx) {
        QRectF cb;
        if (m_cursorIndex < cm.size()) {
            cb = cm[m_cursorIndex].bounds;
        } else {
            cb = cm.last().bounds;
            cb.setLeft(cb.right());
        }
        const QPointF p1 = tx.map(QPointF(cb.left(), cb.top()));
        const QPointF p2 = tx.map(QPointF(cb.left(), cb.bottom()));
        SkPaint caret;
        caret.setColor(0xFFFF5050);
        caret.setStrokeWidth(1);
        canvas->drawLine(SkPoint::Make(p1.x(), p1.y()),
                         SkPoint::Make(p2.x(), p2.y()), caret);
    }
}


void SkiaPdfViewerWidget::probeDumpCanvas(SkCanvas* canvas) {
#if !defined(NDEBUG) || defined(_DEBUG) || defined(DEBUG)
    if (m_probeFrame > 8 || !canvas) return;
    auto surface = canvas->getSurface();
    if (!surface) {
        qDebug().noquote() << "[PROBE] drawview NO_SURFACE";
        return;
    }
    const SkImageInfo info = surface->imageInfo();
    SkBitmap bmp;
    bmp.allocPixels(info);
    if (!canvas->readPixels(bmp, 0, 0)) {
        qDebug().noquote() << "[PROBE] drawview readPixels FAILED";
        return;
    }
    QImage dump(static_cast<const uchar*>(bmp.getPixels()),
                info.width(), info.height(), static_cast<int>(bmp.rowBytes()),
                QImage::Format_RGBA8888);
    // readPixels is device-pixel coords; save directly (no flip, kTopLeft origin).
    const QString path = QString("/tmp/drawview_frame%1.png").arg(m_probeFrame);
    dump.save(path);
    qDebug().noquote() << "[PROBE] drawview saved" << path
                       << "size=" << dump.size();
#endif
}

#endif // SKIA_AVAILABLE
