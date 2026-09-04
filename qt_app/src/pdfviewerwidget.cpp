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

void PdfViewerWidget::setDocument(PdfDocument* document) {
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    
    m_document = document;
    m_currentPage = -1;
    m_currentImage = QImage();
    m_renderCache.clear();
    m_scrollOffset = QPoint(0, 0);
    
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
    requestRender();
    emit pageChanged(m_currentPage);
}

void PdfViewerWidget::setZoom(qreal zoom) {
    zoom = qBound(0.1, zoom, 10.0);
    if (qFuzzyCompare(zoom, m_zoom)) return;
    
    m_zoom = zoom;
    requestRender();
    emit zoomChanged(m_zoom);
}

void PdfViewerWidget::setRotation(int rotation) {
    rotation = ((rotation % 360) + 360) % 360;
    if (rotation == m_rotation) return;
    
    m_rotation = rotation;
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
        m_selecting = false;
        m_selectionStart = m_selectionEnd = QPoint();
        update();
    }
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
    if (m_selecting && !m_selectionStart.isNull() && !m_selectionEnd.isNull()) {
        QRect selRect = QRect(m_selectionStart, m_selectionEnd).normalized();
        painter.fillRect(selRect, QColor(0, 120, 215, 100));
        painter.setPen(QPen(QColor(0, 120, 215), 1, Qt::DashLine));
        painter.drawRect(selRect);
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
        if (m_textSelectionEnabled && !m_currentImage.isNull()) {
            // Check if click is on page
            QRectF pRect = pageRect();
            if (pRect.contains(event->pos())) {
                m_selecting = true;
                m_selectionStart = m_selectionEnd = event->pos();
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
        m_selectionEnd = event->pos();
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
            // Extract selected text
            if (m_document && !m_selectionStart.isNull() && !m_selectionEnd.isNull()) {
                QRectF selRect = QRectF(m_selectionStart, m_selectionEnd).normalized();
                QRectF pRect = pageRect();
                
                // Convert widget coordinates to page coordinates (points)
                // This is simplified - would need proper coordinate mapping
                QString selectedText = m_document->getPageText(m_currentPage);
                if (!selectedText.isEmpty()) {
                    emit textSelected(selectedText);
                    QApplication::clipboard()->setText(selectedText);
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
                QString text = m_document->getPageText(m_currentPage);
                if (!text.isEmpty()) {
                    QApplication::clipboard()->setText(text);
                    emit statusMessage("Page text copied to clipboard");
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
    
    m_document->requestRender(m_currentPage, renderSize, m_rotation);
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

QPointF PdfViewerWidget::mapToPage(const QPoint& widgetPos) const {
    QRectF pRect = pageRect();
    if (pRect.isEmpty()) return QPointF();
    
    QPointF rel = widgetPos - pRect.topLeft();
    // Convert to page coordinates (points)
    // This is simplified - assumes 72 DPI base
    return rel / m_zoom;
}

QPoint PdfViewerWidget::mapFromPage(const QPointF& pagePos) const {
    QRectF pRect = pageRect();
    if (pRect.isEmpty()) return QPoint();
    
    return (pagePos * m_zoom + pRect.topLeft()).toPoint();
}

bool PdfViewerWidget::loadFile(const QString& filePath, const QString& password) {
    if (!m_document) return false;
    
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