#include "pdfviewerwidget.h"
#include "pdfdocument.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QDebug>

PdfViewerWidget::PdfViewerWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundRole(QPalette::Dark);
    setAutoFillBackground(true);
}

PdfViewerWidget::~PdfViewerWidget() {
    m_renderFuture.waitForFinished();
}

void PdfViewerWidget::setDocument(PdfDocument* document) {
    m_renderFuture.waitForFinished();

    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    
    m_document = document;
    m_currentPage = -1;
    m_currentImage = QImage();
    m_renderCache.clear();
    m_scrollOffset = QPoint(0, 0);
    clearTextSelectionState();
    
    if (m_document) {
        connect(m_document, &PdfDocument::renderFinished, this, &PdfViewerWidget::onRenderFinished);
        connect(m_document, &PdfDocument::loadFinished, this, &PdfViewerWidget::onLoadFinished);
        
        m_pageCount = m_document->pageCount();
        if (m_pageCount > 0) {
            setPage(0);
        }
        emit pageCountChanged(m_pageCount);
    }
    
    update();
}

void PdfViewerWidget::setPage(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= m_pageCount) return;
    if (pageIndex == m_currentPage) return;
    
    m_currentPage = pageIndex;
    m_scrollOffset = QPoint(0, 0);
    clearTextSelectionState();
    requestRender();
    emit pageChanged(m_currentPage);
}

void PdfViewerWidget::setZoom(qreal zoom) {
    zoom = qBound(0.1, zoom, 10.0);
    if (qFuzzyCompare(zoom, m_zoom)) return;
    
    m_zoom = zoom;
    // The rendered pixels no longer match the new scale; force pageRect() to
    // recompute the page size from pageSize*zoom so the canvas is re-rendered
    // at the correct pixel dimensions.
    m_currentImage = QImage();
    requestRender();
    emit zoomChanged(m_zoom);
}

void PdfViewerWidget::setRotation(int rotation) {
    rotation = ((rotation % 360) + 360) % 360;
    if (rotation == m_rotation) return;
    
    m_rotation = rotation;
    // Same as setZoom(): the rendered image is for the previous orientation.
    m_currentImage = QImage();
    requestRender();
    emit rotationChanged(m_rotation);
}

void PdfViewerWidget::goToNextPage() {
    if (m_currentPage < m_pageCount - 1) setPage(m_currentPage + 1);
}

void PdfViewerWidget::goToPrevPage() {
    if (m_currentPage > 0) setPage(m_currentPage - 1);
}

void PdfViewerWidget::goToFirstPage() {
    setPage(0);
}

void PdfViewerWidget::goToLastPage() {
    setPage(m_pageCount - 1);
}

void PdfViewerWidget::zoomIn() {
    setZoom(m_zoom * 1.2);
}

void PdfViewerWidget::zoomOut() {
    setZoom(m_zoom / 1.2);
}

void PdfViewerWidget::zoomToFit() {
    if (!m_document || m_currentPage < 0) return;
    
    QSizeF pageSize = m_document->pageSize(m_currentPage);
    if (pageSize.isEmpty()) return;
    
    QSize widgetSize = size();
    if (widgetSize.isEmpty()) return;
    
    // Account for rotation
    if (m_rotation == 90 || m_rotation == 270) {
        pageSize.transpose();
    }
    
    qreal zoomX = widgetSize.width() / pageSize.width();
    qreal zoomY = widgetSize.height() / pageSize.height();
    setZoom(qMin(zoomX, zoomY) * 0.95); // 5% margin
}

void PdfViewerWidget::zoomToWidth() {
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

void PdfViewerWidget::rotateClockwise() {
    setRotation(m_rotation + 90);
}

void PdfViewerWidget::rotateCounterClockwise() {
    setRotation(m_rotation - 90);
}

void PdfViewerWidget::setTextSelectionEnabled(bool enabled) {
    m_textSelectionEnabled = enabled;
    if (!enabled) {
        clearTextSelectionState();
        update();
    }
}

void PdfViewerWidget::clearTextSelectionState() {
    m_selecting = false;
    m_cursorIndex = -1;
    m_selectionStart = -1;
    m_selectionEnd = -1;
    m_boxSelStart = m_boxSelEnd = QPoint();
    m_clickCount = 1;
    m_lastClickIndex = -1;
}

void PdfViewerWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Dark));
    
    if (!m_currentImage.isNull()) {
        // Draw page shadow
        QRectF pageRect = this->pageRect();
        QRectF shadowRect = pageRect.translated(4, 4);
        painter.fillRect(shadowRect, QColor(0, 0, 0, 80));
        
        // Draw page image
        painter.drawImage(pageRect.topLeft(), m_currentImage);
        
        // Draw page border
        painter.setPen(QPen(Qt::gray, 1));
        painter.drawRect(pageRect);
        
        // Draw search highlights
        if (m_document && m_document->isLoaded() && m_currentPage >= 0) {
            const QList<QRectF> matches = m_document->matchesForPage(m_currentPage);
            QRectF currentMatch;
            if (m_document->currentMatchPage() == m_currentPage) {
                currentMatch = m_document->currentMatchRect();
            }
            
            for (const QRectF& match : matches) {
                QRectF mapped = mappedMatchRect(match);
                if (mapped.isEmpty()) continue;
                painter.fillRect(mapped, QColor(255, 255, 0, 90));
            }
            
            if (!currentMatch.isNull()) {
                QRectF mapped = mappedMatchRect(currentMatch);
                if (!mapped.isEmpty()) {
                    painter.fillRect(mapped, QColor(255, 165, 0, 150));
                    painter.setPen(QPen(QColor(255, 140, 0), 1.5));
                    painter.drawRect(mapped);
                }
            }
        }
    } else if (m_document && m_document->isLoaded()) {
        // Loading indicator
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "Rendering page...");
    } else {
        // No document
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, 
            "No document loaded\n\nDrag & drop a PDF file or use File > Open");
    }
    
    // Draw text selection
    if (m_textSelectionEnabled && m_document && m_document->isLoaded() &&
        m_currentPage >= 0) {
        const QVector<CharInfo>& cm = m_document->charMap(m_currentPage);
        const int a = qMin(m_selectionStart, m_selectionEnd);
        const int b = qMax(m_selectionStart, m_selectionEnd);
        if (a >= 0 && b > a && a < cm.size()) {
            const int hi = qMin(b, (int)cm.size());
            for (int i = a; i < hi; ++i) {
                QRectF mapped = mapRectFromPage(cm[i].bounds);
                if (mapped.isEmpty()) continue;
                painter.fillRect(mapped, QColor(0, 120, 215, 90));
            }
        }
        // Box fallback overlay (pages with no selectable text at all).
        if (m_selecting && !m_boxSelStart.isNull() && !m_boxSelEnd.isNull()) {
            QRect selRect = QRect(m_boxSelStart, m_boxSelEnd).normalized();
            painter.fillRect(selRect, QColor(0, 120, 215, 100));
            painter.setPen(QPen(QColor(0, 120, 215), 1, Qt::DashLine));
            painter.drawRect(selRect);
        }
        // Caret: a 1px vertical line at the left edge of the char that follows
        // the caret index (right edge of the last char when at the end).
        if (m_cursorIndex >= 0 && !cm.isEmpty()) {
            QRectF cb;
            if (m_cursorIndex < cm.size()) {
                cb = cm[m_cursorIndex].bounds;
            } else {
                cb = cm.last().bounds;
                cb.setLeft(cb.right());
            }
            const QPointF p1 = mapFromPageF(QPointF(cb.left(), cb.top()));
            const QPointF p2 = mapFromPageF(QPointF(cb.left(), cb.bottom()));
            painter.setPen(QPen(QColor(255, 80, 80), 1));
            painter.drawLine(p1, p2);
        }
    }
    
    // Page indicator
    if (m_pageCount > 0) {
        QString pageInfo = QString("Page %1 of %2").arg(m_currentPage + 1).arg(m_pageCount);
        painter.setPen(Qt::white);
        painter.drawText(10, 20, pageInfo);
    }
}

void PdfViewerWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateViewport();
    requestRender();
}

void PdfViewerWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom with Ctrl+Wheel
        qreal delta = event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
        setZoom(m_zoom * delta);
        event->accept();
    } else {
        // Pan vertically
        m_scrollOffset.setY(m_scrollOffset.y() - event->angleDelta().y());
        update();
        event->accept();
    }
}

void PdfViewerWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_textSelectionEnabled && m_document && m_document->isLoaded() &&
            m_currentPage >= 0) {
            QRectF pRect = pageRect();
            if (pRect.contains(event->pos())) {
                QPointF pagePos = mapToPage(event->pos());
                int idx = m_document->findNearestCharIndex(m_currentPage, pagePos);

                // Multi-click detection for word/paragraph expansion (step 4).
                // Synthetic and real double-clicks both arrive as
                // MouseButtonDblClick; relying on the timestamp heuristic is
                // unreliable for synthesized events.
                const bool multiClick =
                    event->type() == QEvent::MouseButtonDblClick;
                m_lastClickTime = event->timestamp();
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
                    m_boxSelStart = m_boxSelEnd = QPoint();

                    // Word / paragraph expansion on multi-click (step 4).
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
                    // Page with no selectable text: keep the caret, clear the
                    // char selection, start the box-fallback overlay instead.
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
        
        // Pan with left button
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else if (event->button() == Qt::MiddleButton) {
        // Pan with middle button
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void PdfViewerWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        QPoint delta = event->pos() - m_panStart;
        m_scrollOffset += delta;
        m_panStart = event->pos();
        update();
        event->accept();
    } else if (m_selecting) {
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

void PdfViewerWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        if (m_panning) {
            m_panning = false;
            unsetCursor();
            event->accept();
        } else if (m_selecting) {
            m_selecting = false;
            // Extract selected text from the char-index range.
            const int a = qMin(m_selectionStart, m_selectionEnd);
            const int b = qMax(m_selectionStart, m_selectionEnd);
            if (m_document && m_currentPage >= 0 && a >= 0 && b > a) {
                QString selectedText = m_document->textForRange(m_currentPage, a, b);
                if (!selectedText.isEmpty()) {
                    emit textSelected(selectedText);
                    QApplication::clipboard()->setText(selectedText);
                    emit statusMessage(QString("Selected %1 chars: %2")
                                           .arg(b - a)
                                           .arg(selectedText.simplified()));
                }
            }
            update();
            event->accept();
        }
    }
}

void PdfViewerWidget::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Up:
            if (event->modifiers() & Qt::ControlModifier) {
                goToPrevPage();
            } else {
                m_scrollOffset.setY(m_scrollOffset.y() - 50);
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
                const int a = qMin(m_selectionStart, m_selectionEnd);
                const int b = qMax(m_selectionStart, m_selectionEnd);
                QString text;
                if (m_document && a >= 0 && b > a) {
                    text = m_document->textForRange(m_currentPage, a, b);
                }
                if (text.isEmpty()) {
                    // Fallback: whole page.
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

void PdfViewerWidget::leaveEvent(QEvent* event) {
    if (m_panning) {
        m_panning = false;
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

void PdfViewerWidget::onRenderFinished(int pageIndex, QImage image, bool success) {
    if (pageIndex != m_currentPage) return; // Ignore stale renders
    
    if (success && !image.isNull()) {
        m_currentImage = image;
        
        // Cache the rendered image
        if (m_renderCache.size() >= MAX_CACHE_SIZE) {
            // Remove oldest (first) entry
            auto it = m_renderCache.begin();
            m_renderCache.erase(it);
        }
        m_renderCache[m_currentPage] = {image, image.size(), m_rotation};
    } else {
        m_currentImage = QImage();
        emit statusMessage("Failed to render page");
    }
    
    update();
}

void PdfViewerWidget::onLoadFinished(bool success, const QString& error) {
    if (success) {
        emit statusMessage(QString("Loaded: %1 pages").arg(m_pageCount));
    } else {
        emit statusMessage("Error: " + error);
    }
}

void PdfViewerWidget::updateViewport() {
    // Called on resize - could adjust zoom to fit if needed
}

void PdfViewerWidget::requestRender() {
    if (!m_document || m_currentPage < 0) return;
    
    QRectF pRect = pageRect();
    if (pRect.isEmpty()) return;
    
    QSize renderSize = pRect.size().toSize();
    renderSize = renderSize.boundedTo(QSize(8192, 8192)); // Limit max size
    if (renderSize.isEmpty()) return;
    
    // Check cache
    auto it = m_renderCache.find(m_currentPage);
    if (it != m_renderCache.end() && it.value().renderSize == renderSize && it.value().rotation == m_rotation) {
        m_currentImage = it.value().image;
        update();
        return;
    }
    
    m_renderFuture = m_document->requestRender(m_currentPage, renderSize, m_rotation);
}

QRectF PdfViewerWidget::pageRect() const {
    QSize size;
    if (!m_currentImage.isNull()) {
        size = m_currentImage.size();
    } else if (m_document && m_currentPage >= 0) {
        QSizeF pageSize = m_document->pageSize(m_currentPage);
        if (pageSize.isEmpty()) return QRectF();
        
        if (m_rotation == 90 || m_rotation == 270) {
            pageSize.transpose();
        }
        size = (pageSize * m_zoom).toSize();
    } else {
        return QRectF();
    }
    
    QPointF center = rect().center() + m_scrollOffset;
    QRectF pageRect;
    pageRect.setSize(size);
    pageRect.moveCenter(center);
    return pageRect;
}

QPointF PdfViewerWidget::mapFromPageF(const QPointF& pagePos) const {
    QRectF pRect = pageRect();
    if (pRect.isEmpty() || !m_document || m_currentPage < 0) return QPointF();

    // PDFium-native mapping into the page's device rectangle (origin at the
    // bitmap's pixel origin), then offset by the page rect origin in the view.
    return m_document->pageToDevice(m_currentPage, pagePos, QPoint(0, 0),
                                    pRect.size().toSize(), m_rotation)
        + pRect.topLeft();
}

QPoint PdfViewerWidget::mapFromPage(const QPointF& pagePos) const {
    return mapFromPageF(pagePos).toPoint();
}

QPointF PdfViewerWidget::mapToPage(const QPoint& widgetPos) const {
    QRectF pRect = pageRect();
    if (pRect.isEmpty() || !m_document || m_currentPage < 0) return QPointF();

    // Relative to the page rect's origin, then inverted by PDFium.
    return m_document->deviceToPage(m_currentPage,
                                    QPointF(widgetPos) - pRect.topLeft(),
                                    QPoint(0, 0), pRect.size().toSize(),
                                    m_rotation);
}

QRectF PdfViewerWidget::mapRectFromPage(const QRectF& pdfRect) const {
    QRectF r = pdfRect.normalized();
    if (r.isEmpty() || !m_document || m_currentPage < 0) return QRectF();
    
    // Rotating the 4 corners produces a rotated rectangle whose axis-aligned
    // bounding box is the visible highlight. Use explicit min/max: QRectF::united
    // treats degenerate (point) rects as no-ops, so it cannot be used here.
    const QPointF mapped[4] = {
        mapFromPageF(r.topLeft()),
        mapFromPageF(r.topRight()),
        mapFromPageF(r.bottomLeft()),
        mapFromPageF(r.bottomRight()),
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

QRectF PdfViewerWidget::mappedMatchRect(const QRectF& pageMatch) const {
    if (!m_document || m_currentPage < 0) return QRectF();
    return mapRectFromPage(pageMatch.normalized());
}

bool PdfViewerWidget::loadFile(const QString& filePath, const QString& password) {
    if (!m_document) return false;

    // The document handles are about to be reused for a different file; the
    // in-flight render still points at the old FPDF handles.
    m_renderFuture.waitForFinished();

    m_currentPage = -1;
    m_currentImage = QImage();
    m_renderCache.clear();
    m_scrollOffset = QPoint(0, 0);
    
    bool success = m_document->load(filePath, password);
    if (success) {
        m_pageCount = m_document->pageCount();
        if (m_pageCount > 0) {
            setPage(0);
        }
        emit pageCountChanged(m_pageCount);
    }
    return success;
}