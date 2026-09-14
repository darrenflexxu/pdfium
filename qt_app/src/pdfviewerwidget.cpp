#include "pdfviewerwidget.h"
#include "pdfdocument.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QContextMenuEvent>
#include <QDebug>
#include <algorithm>

namespace {
// Continuous-mode strip geometry: pages stack vertically with a fixed gap,
// starting at the top margin off the view's vertical scroll offset.
const qreal kTopMargin = 16.0;
const qreal kPageGap = 24.0;
}

PdfViewerWidget::PdfViewerWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundRole(QPalette::Dark);
    setAutoFillBackground(true);
}

PdfViewerWidget::~PdfViewerWidget() {
    waitForRenders();
}

void PdfViewerWidget::setDocument(PdfDocument* document) {
    waitForRenders();

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
    if (m_viewMode == ViewMode::Continuous) {
        // Scroll the strip so the page top aligns with the top margin; the
        // viewport-center tracker then keeps this page current.
        m_scrollOffset.setY(kTopMargin - continuousY(pageIndex));
        ++m_scrollVersion;
    } else {
        m_scrollOffset = QPoint(0, 0);
    }
    clearTextSelectionState();
    requestRender();
    refreshVisiblePages();
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
    m_renderCache.clear();
    requestRender();
    emit zoomChanged(m_zoom);
}

void PdfViewerWidget::setRotation(int rotation) {
    rotation = ((rotation % 360) + 360) % 360;
    if (rotation == m_rotation) return;
    
    m_rotation = rotation;
    // Same as setZoom(): the rendered image is for the previous orientation.
    m_currentImage = QImage();
    m_renderCache.clear();
    requestRender();
    emit rotationChanged(m_rotation);
}

void PdfViewerWidget::setViewMode(ViewMode mode) {
    if (mode == m_viewMode) return;
    m_viewMode = mode;
    m_currentImage = QImage();
    m_renderCache.clear();
    clearTextSelectionState();
    if (m_viewMode == ViewMode::Continuous) {
        if (m_currentPage < 0 && m_pageCount > 0) {
            m_currentPage = 0;
            emit pageChanged(0);
        }
        if (m_currentPage >= 0) {
            m_scrollOffset.setY(kTopMargin - continuousY(m_currentPage));
            ++m_scrollVersion;
        }
    } else {
        requestRender();
    }
    update();
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

void PdfViewerWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Dark));

    if (m_viewMode == ViewMode::Continuous)
        paintContinuous(painter);
    else
        paintSinglePage(painter);

    // Keep the visible strip rendered and the tracked page in sync after any
    // scroll/zoom/rotation change (idempotent: requests are gated by cache
    // hits and the in-flight page set).
    refreshVisiblePages();
}

void PdfViewerWidget::paintSinglePage(QPainter& painter) {
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
    drawSelectionForCurrentPage(painter);
    
    // Page indicator
    if (m_pageCount > 0) {
        QString pageInfo = QString("Page %1 of %2").arg(m_currentPage + 1).arg(m_pageCount);
        painter.setPen(Qt::white);
        painter.drawText(10, 20, pageInfo);
    }
}

void PdfViewerWidget::paintContinuous(QPainter& painter) {
    if (!m_document || !m_document->isLoaded() || m_pageCount <= 0) {
        // No document
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, 
            "No document loaded\n\nDrag & drop a PDF file or use File > Open");
        return;
    }
    
    const QRect viewport = rect();
    for (int p = 0; p < m_pageCount; ++p) {
        QRectF pr = pageRectOf(p);
        if (!pr.intersects(viewport)) continue;
        
        // Draw page shadow
        painter.fillRect(pr.translated(4, 4), QColor(0, 0, 0, 80));
        
        // Draw page image from the cache (re-render requested on refresh when
        // absent or stale); a placeholder is shown until the render lands.
        QImage cached;
        const auto it = m_renderCache.find(p);
        const QSize wantSize = pagePixelSize(p).toSize();
        if (it != m_renderCache.end() && it->rotation == m_rotation &&
            it->renderSize == wantSize) {
            cached = it->image;
        }
        if (!cached.isNull()) {
            painter.drawImage(pr.topLeft(), cached);
        } else {
            painter.setPen(Qt::white);
            painter.drawText(pr, Qt::AlignCenter, "Rendering page...");
        }
        
        // Draw page border
        painter.setPen(QPen(Qt::gray, 1));
        painter.drawRect(pr);
        
        // Search highlights for this page.
        const QList<QRectF> matches = m_document->matchesForPage(p);
        QRectF currentMatch;
        if (m_document->currentMatchPage() == p) {
            currentMatch = m_document->currentMatchRect();
        }
        for (const QRectF& match : matches) {
            QRectF mapped = mapRectFromPageOnPage(match, p);
            if (mapped.isEmpty()) continue;
            painter.fillRect(mapped, QColor(255, 255, 0, 90));
        }
        if (!currentMatch.isNull()) {
            QRectF mapped = mapRectFromPageOnPage(currentMatch, p);
            if (!mapped.isEmpty()) {
                painter.fillRect(mapped, QColor(255, 165, 0, 150));
                painter.setPen(QPen(QColor(255, 140, 0), 1.5));
                painter.drawRect(mapped);
            }
        }
    }
    
    // Text selection only renders on the page that owns it.
    drawSelectionForCurrentPage(painter);
    
    // Page indicator (tracks the viewport-center page).
    if (m_pageCount > 0) {
        QString pageInfo = QString("Page %1 of %2").arg(m_currentPage + 1).arg(m_pageCount);
        painter.setPen(Qt::white);
        painter.drawText(10, 20, pageInfo);
    }
}

void PdfViewerWidget::drawSelectionForCurrentPage(QPainter& painter) {
    if (!m_textSelectionEnabled || !m_document || !m_document->isLoaded() ||
        m_currentPage < 0)
        return;
    const QVector<CharInfo>& cm = m_document->charMap(m_currentPage);
    const int a = qMin(m_selectionStart, m_selectionEnd);
    const int b = qMax(m_selectionStart, m_selectionEnd);
    if (a >= 0 && b > a && a < cm.size()) {
        const int hi = qMin(b, (int)cm.size());
        QRectF lineRect;
        for (int i = a; i < hi; ++i) {
            QRectF charRect = cm[i].bounds;
            // Merge characters on the same line (approximate Y coordinate)
            if (i == a || qAbs(charRect.top() - lineRect.top()) > 2.0) {
                if (!lineRect.isEmpty()) {
                    QRectF mapped = mapRectFromPage(lineRect);
                    if (!mapped.isEmpty()) painter.fillRect(mapped, QColor(0, 120, 215, 90));
                }
                lineRect = charRect;
            } else {
                lineRect = lineRect.united(charRect);
            }
        }
        if (!lineRect.isEmpty()) {
            QRectF mapped = mapRectFromPage(lineRect);
            if (!mapped.isEmpty()) painter.fillRect(mapped, QColor(0, 120, 215, 90));
        }
    }
    // Box fallback overlay (pages with no selectable text at all).
    if (cm.isEmpty() && m_selecting && !m_boxSelStart.isNull() &&
        !m_boxSelEnd.isNull()) {
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

void PdfViewerWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateViewport();
    requestRender();
    if (m_viewMode == ViewMode::Continuous) update();
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
        ++m_scrollVersion;
        update();
        refreshVisiblePages();
        event->accept();
    }
}

void PdfViewerWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_textSelectionEnabled && m_document && m_document->isLoaded() &&
            m_currentPage >= 0) {
            // Continuous mode: the click may land on a different page; switch
            // the tracked page to it before interpreting the gesture.
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
                    m_boxSelStart = m_boxSelEnd = event->pos();

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
        ++m_scrollVersion;
        m_panStart = event->pos();
        update();
        event->accept();
    } else if (m_selecting) {
        // Continuous mode: crossing into another page mid-drag re-anchors the
        // selection at the nearest char of the newly entered page.
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

void PdfViewerWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        if (m_panning) {
            m_panning = false;
            unsetCursor();
            event->accept();
        } else if (m_selecting) {
            m_selecting = false;
            // Plain drags are defined by the dragged rectangle: the widget
            // selection rect is mapped to PDF coordinates and its exact text is
            // extracted via extractTextInRect. Word/paragraph multi-clicks keep
            // the (more precise) char-range path.
            QString selectedText = this->selectedText();
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

void PdfViewerWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = buildContextMenu(event->pos());
    if (!menu) {
        QWidget::contextMenuEvent(event);
        return;
    }
    menu->exec(event->globalPos());
    delete menu;
}

QMenu* PdfViewerWidget::buildContextMenu(const QPoint& widgetPos) {
    if (!m_document || !m_document->isLoaded())
        return nullptr;
    const int page = pageUnderPoint(widgetPos);
    if (page < 0 || page >= m_pageCount)
        return nullptr;
    if (page != m_currentPage) {
        m_currentPage = page;
        clearTextSelectionState();
        emit pageChanged(page);
        update();
    }
    const int a = qMin(m_selectionStart, m_selectionEnd);
    const int b = qMax(m_selectionStart, m_selectionEnd);
    if (m_textSelectionEnabled && a >= 0 && b > a) {
        // Active selection: offer "Copy" so the user can copy it manually.
        QMenu* menu = new QMenu(this);
        QAction* copy = menu->addAction(tr("Copy"));
        connect(copy, &QAction::triggered, this, &PdfViewerWidget::copySelectedText);
        return menu;
    }
    // No active selection: reposition the caret at the right-click point so a
    // subsequent drag selects from here.
    if (m_textSelectionEnabled) {
        const QPointF pagePos = mapToPage(widgetPos);
        const int idx = m_document->findNearestCharIndex(m_currentPage, pagePos);
        if (idx >= 0) {
            m_cursorIndex = idx;
            m_selectionStart = m_selectionEnd = idx;
            m_boxSelStart = m_boxSelEnd = QPoint();
            update();
        }
    }
    return nullptr;
}

void PdfViewerWidget::copySelectedText() {
    if (!m_document || m_currentPage < 0)
        return;
    const QString text = selectedText();
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
    emit textSelected(text);
    emit statusMessage("Text copied to clipboard");
}

QRectF PdfViewerWidget::widgetToPageRect(const QRect& widgetRect) const {
    if (!m_document || m_currentPage < 0)
        return QRectF();
    const QPointF p1 = mapToPage(widgetRect.topLeft());
    const QPointF p2 = mapToPage(widgetRect.bottomRight());
    return QRectF(p1, p2).normalized();
}

QString PdfViewerWidget::selectedText() const {
    if (!m_document || !m_document->isLoaded() || m_currentPage < 0)
        return QString();

    const int a = qMin(m_selectionStart, m_selectionEnd);
    const int b = qMax(m_selectionStart, m_selectionEnd);
    const bool plainDrag = m_clickCount <= 1;

    // Plain drag: define the selection by the dragged rectangle.
    QString text;
    if (plainDrag && !m_boxSelStart.isNull() && !m_boxSelEnd.isNull()) {
        const QRectF pageSel = widgetToPageRect(
            QRect(m_boxSelStart, m_boxSelEnd).normalized());
        if (!pageSel.isEmpty()) {
            text = m_document->extractTextInRect(m_currentPage, pageSel);
        }
    }
    // Finger stretch / no-rect or empty-result fallback: char-range selection.
    if (text.isEmpty() && a >= 0 && b > a) {
        text = m_document->textForRange(m_currentPage, a, b);
    }
    return text;
}

void PdfViewerWidget::keyPressEvent(QKeyEvent* event) {
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
                // Copy the active selection (rect- or char-based), falling back
                // to the whole page when nothing is selected.
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
                        // A whole-page selection is a char-range selection; drop
                        // any stale drag rect so selectedText() doesn't reuse it.
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

void PdfViewerWidget::leaveEvent(QEvent* event) {
    if (m_panning) {
        m_panning = false;
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

void PdfViewerWidget::onRenderFinished(int pageIndex, QImage image, bool success) {
    m_pendingPages.remove(pageIndex);
    // Single-page mode ignores renders for pages other than the current one
    // (they are not requested there, but may trail after a fast page flip).
    if (m_viewMode == ViewMode::SinglePage && pageIndex != m_currentPage) return;
    
    if (success && !image.isNull()) {
        if (m_renderCache.size() >= MAX_CACHE_SIZE) {
            // Remove oldest (first) entry
            auto it = m_renderCache.begin();
            m_renderCache.erase(it);
        }
        m_renderCache[pageIndex] = {image, image.size(), m_rotation};
        if (pageIndex == m_currentPage) {
            m_currentImage = image;
        }
    } else if (pageIndex == m_currentPage) {
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
    requestRender(m_currentPage);
}

void PdfViewerWidget::requestRender(int pageIndex) {
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) return;
    
    QRectF pRect = pageRectOf(pageIndex);
    if (pRect.isEmpty()) return;
    
    QSize renderSize = pRect.size().toSize();
    renderSize = renderSize.boundedTo(QSize(8192, 8192)); // Limit max size
    if (renderSize.isEmpty()) return;
    
    // Check cache
    auto it = m_renderCache.find(pageIndex);
    if (it != m_renderCache.end() && it.value().renderSize == renderSize && it.value().rotation == m_rotation) {
        if (pageIndex == m_currentPage) m_currentImage = it.value().image;
        return;
    }
    
    // A render for this page is already in flight.
    if (m_pendingPages.contains(pageIndex)) return;
    m_pendingPages.insert(pageIndex);
    
    // Opportunistically drop completed futures so the list cannot grow without
    // bound on long documents.
    if (m_renderFutures.size() > 128) {
        m_renderFutures.erase(std::remove_if(m_renderFutures.begin(), m_renderFutures.end(),
            [](const QFuture<void>& f) { return f.isFinished(); }),
            m_renderFutures.end());
    }
    m_renderFutures.append(m_document->requestRender(pageIndex, renderSize, m_rotation));
}

void PdfViewerWidget::waitForRenders() {
    for (QFuture<void>& future : m_renderFutures) {
        if (!future.isFinished()) future.waitForFinished();
    }
    m_renderFutures.clear();
    m_pendingPages.clear();
}

void PdfViewerWidget::refreshVisiblePages() {
    if (!m_document || !m_document->isLoaded() || m_pageCount <= 0) return;
    if (m_viewMode != ViewMode::Continuous) return;
    
    // Request renders for every page intersecting the viewport.
    const QRect viewport = rect();
    for (int p = 0; p < m_pageCount; ++p) {
        QRectF pr = pageRectOf(p);
        if (pr.intersects(viewport)) requestRender(p);
    }
    updateCurrentPageForViewport();
}

void PdfViewerWidget::updateCurrentPageForViewport() {
    if (m_viewMode != ViewMode::Continuous || m_pageCount <= 0) return;
    // Only re-track when the strip actually moved (wheel/pan/key/setPage), so
    // ordinary repaints never steal the page away from an in-progress or
    // completed interaction (e.g. clicking a page below the viewport center).
    if (m_scrollVersion == m_trackedScrollVersion) return;
    m_trackedScrollVersion = m_scrollVersion;
    const int p = pageAtY(rect().center().y(), true);
    if (p >= 0 && p != m_currentPage) {
        m_currentPage = p;
        clearTextSelectionState();
        requestRender(p);
        emit pageChanged(p);
    }
}

QRectF PdfViewerWidget::pageRectOf(int pageIndex) const {
    if (m_viewMode == ViewMode::Continuous) {
        QSizeF size = pagePixelSize(pageIndex);
        if (size.isEmpty()) return QRectF();
        qreal x = (rect().width() - size.width()) / 2.0;
        return QRectF(x, m_scrollOffset.y() + continuousY(pageIndex),
                      size.width(), size.height());
    }
    
    // Single page: kept byte-identical to the previous centered layout.
    QSize size;
    if (pageIndex == m_currentPage && !m_currentImage.isNull()) {
        size = m_currentImage.size();
    } else if (m_document && pageIndex >= 0 && pageIndex < m_pageCount) {
        QSizeF pageSize = m_document->pageSize(pageIndex);
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

QSizeF PdfViewerWidget::pagePixelSize(int pageIndex) const {
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

qreal PdfViewerWidget::continuousY(int pageIndex) const {
    qreal y = kTopMargin;
    for (int i = 0; i < pageIndex; ++i)
        y += pagePixelSize(i).height() + kPageGap;
    return y;
}

int PdfViewerWidget::pageAtY(qreal y, bool clamp) const {
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

int PdfViewerWidget::pageUnderPoint(const QPoint& widgetPos) const {
    if (m_viewMode == ViewMode::Continuous)
        return pageAtY(widgetPos.y(), true);
    return m_currentPage;
}

QRectF PdfViewerWidget::pageRect() const {
    return pageRectOf(m_currentPage);
}

QRectF PdfViewerWidget::pageRect(int pageIndex) const {
    return pageRectOf(pageIndex);
}

QPointF PdfViewerWidget::mapFromPageF(const QPointF& pagePos) const {
    return mapFromPageOnPage(pagePos, m_currentPage);
}

QPointF PdfViewerWidget::mapFromPageOnPage(const QPointF& pagePos, int pageIndex) const {
    QRectF pRect = pageRectOf(pageIndex);
    if (pRect.isEmpty() || !m_document || pageIndex < 0 || pageIndex >= m_pageCount)
        return QPointF();

    // PDFium-native mapping into the page's device rectangle (origin at the
    // bitmap's pixel origin), then offset by the page rect origin in the view.
    return m_document->pageToDevice(pageIndex, pagePos, QPoint(0, 0),
                                    pRect.size().toSize(), m_rotation)
        + pRect.topLeft();
}

QPoint PdfViewerWidget::mapFromPage(const QPointF& pagePos) const {
    return mapFromPageF(pagePos).toPoint();
}

QPointF PdfViewerWidget::mapToPage(const QPoint& widgetPos) const {
    return mapToPageOnPage(widgetPos, m_currentPage);
}

QPointF PdfViewerWidget::mapToPageOnPage(const QPoint& widgetPos, int pageIndex) const {
    QRectF pRect = pageRectOf(pageIndex);
    if (pRect.isEmpty() || !m_document || pageIndex < 0 || pageIndex >= m_pageCount)
        return QPointF();

    // Relative to the page rect's origin, then inverted by PDFium.
    return m_document->deviceToPage(pageIndex,
                                    QPointF(widgetPos) - pRect.topLeft(),
                                    QPoint(0, 0), pRect.size().toSize(),
                                    m_rotation);
}

QRectF PdfViewerWidget::mapRectFromPage(const QRectF& pdfRect) const {
    return mapRectFromPageOnPage(pdfRect, m_currentPage);
}

QRectF PdfViewerWidget::mapRectFromPageOnPage(const QRectF& pdfRect, int pageIndex) const {
    QRectF r = pdfRect.normalized();
    if (r.isEmpty() || !m_document || pageIndex < 0 || pageIndex >= m_pageCount)
        return QRectF();
    
    // Rotating the 4 corners produces a rotated rectangle whose axis-aligned
    // bounding box is the visible highlight. Use explicit min/max: QRectF::united
    // treats degenerate (point) rects as no-ops, so it cannot be used here.
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

QRectF PdfViewerWidget::mappedMatchRect(const QRectF& pageMatch) const {
    if (!m_document || m_currentPage < 0) return QRectF();
    return mapRectFromPageOnPage(pageMatch.normalized(), m_currentPage);
}

bool PdfViewerWidget::loadFile(const QString& filePath, const QString& password) {
    if (!m_document) return false;

    // The document handles are about to be reused for a different file; all
    // in-flight render workers still point at the old FPDF handles.
    waitForRenders();

    m_currentPage = -1;
    m_currentImage = QImage();
    m_renderCache.clear();
    m_scrollOffset = QPoint(0, 0);
    m_scrollVersion = 0;
    m_trackedScrollVersion = -1;
    
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