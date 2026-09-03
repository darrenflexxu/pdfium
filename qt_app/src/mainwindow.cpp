#include "mainwindow.h"
#include "pdfviewerwidget.h"
#include "pdfdocument.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPainter>
#include <QSettings>
#include <QScreen>
#include <QStyle>
#include <QLabel>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QDebug>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // Central widget: PDF Viewer
    m_viewer = new PdfViewerWidget(this);
    setCentralWidget(m_viewer);
    
    // Document model
    PdfDocument* doc = new PdfDocument(this);
    m_viewer->setDocument(doc);
    
    createActions();
    createMenus();
    createToolBars();
    createStatusBar();
    
    // Connect viewer signals
    connect(m_viewer, &PdfViewerWidget::pageChanged, this, &MainWindow::onPageChanged);
    connect(m_viewer, &PdfViewerWidget::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(m_viewer, &PdfViewerWidget::rotationChanged, this, &MainWindow::onRotationChanged);
    connect(m_viewer, &PdfViewerWidget::pageCountChanged, this, &MainWindow::onPageCountChanged);
    connect(m_viewer, &PdfViewerWidget::statusMessage, this, &MainWindow::onStatusMessage);
    connect(m_viewer, &PdfViewerWidget::textSelected, this, &MainWindow::onTextSelected);
    
    // Window settings
    setWindowTitle("PDF Reader");
    resize(1024, 768);
    
    // Restore geometry
    QSettings settings("PdfReader", "PdfReader");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    
    updateActions();
}

MainWindow::~MainWindow() {
    QSettings settings("PdfReader", "PdfReader");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
}

void MainWindow::createActions() {
    // File actions
    m_openAction = new QAction(QIcon::fromTheme("document-open"), tr("&Open..."), this);
    m_openAction->setShortcuts(QKeySequence::Open);
    m_openAction->setStatusTip(tr("Open a PDF file"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);
    
    m_saveAsAction = new QAction(QIcon::fromTheme("document-save-as"), tr("Save &As..."), this);
    m_saveAsAction->setShortcuts(QKeySequence::SaveAs);
    m_saveAsAction->setStatusTip(tr("Save document as..."));
    m_saveAsAction->setEnabled(false); // Not implemented yet
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);
    
    m_printAction = new QAction(QIcon::fromTheme("document-print"), tr("&Print..."), this);
    m_printAction->setShortcuts(QKeySequence::Print);
    m_printAction->setStatusTip(tr("Print document"));
    connect(m_printAction, &QAction::triggered, this, &MainWindow::printDocument);
    
    m_exitAction = new QAction(tr("E&xit"), this);
    m_exitAction->setShortcuts(QKeySequence::Quit);
    m_exitAction->setStatusTip(tr("Exit the application"));
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);
    
    // Navigation actions
    m_prevPageAction = new QAction(QIcon::fromTheme("go-previous"), tr("&Previous Page"), this);
    m_prevPageAction->setShortcut(QKeySequence::MoveToPreviousPage);
    m_prevPageAction->setStatusTip(tr("Go to previous page"));
    connect(m_prevPageAction, &QAction::triggered, m_viewer, &PdfViewerWidget::goToPrevPage);
    
    m_nextPageAction = new QAction(QIcon::fromTheme("go-next"), tr("&Next Page"), this);
    m_nextPageAction->setShortcut(QKeySequence::MoveToNextPage);
    m_nextPageAction->setStatusTip(tr("Go to next page"));
    connect(m_nextPageAction, &QAction::triggered, m_viewer, &PdfViewerWidget::goToNextPage);
    
    m_firstPageAction = new QAction(QIcon::fromTheme("go-first"), tr("&First Page"), this);
    m_firstPageAction->setShortcut(Qt::Key_Home);
    m_firstPageAction->setStatusTip(tr("Go to first page"));
    connect(m_firstPageAction, &QAction::triggered, m_viewer, &PdfViewerWidget::goToFirstPage);
    
    m_lastPageAction = new QAction(QIcon::fromTheme("go-last"), tr("&Last Page"), this);
    m_lastPageAction->setShortcut(Qt::Key_End);
    m_lastPageAction->setStatusTip(tr("Go to last page"));
    connect(m_lastPageAction, &QAction::triggered, m_viewer, &PdfViewerWidget::goToLastPage);
    
    // View actions
    m_zoomInAction = new QAction(QIcon::fromTheme("zoom-in"), tr("Zoom &In"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAction->setStatusTip(tr("Zoom in"));
    connect(m_zoomInAction, &QAction::triggered, m_viewer, &PdfViewerWidget::zoomIn);
    
    m_zoomOutAction = new QAction(QIcon::fromTheme("zoom-out"), tr("Zoom &Out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAction->setStatusTip(tr("Zoom out"));
    connect(m_zoomOutAction, &QAction::triggered, m_viewer, &PdfViewerWidget::zoomOut);
    
    m_zoomFitAction = new QAction(QIcon::fromTheme("zoom-fit-best"), tr("Fit &Page"), this);
    m_zoomFitAction->setShortcut(Qt::CTRL | Qt::Key_0);
    m_zoomFitAction->setStatusTip(tr("Fit page to window"));
    connect(m_zoomFitAction, &QAction::triggered, m_viewer, &PdfViewerWidget::zoomToFit);
    
    m_zoomWidthAction = new QAction(QIcon::fromTheme("zoom-fit-width"), tr("Fit &Width"), this);
    m_zoomWidthAction->setShortcut(Qt::CTRL | Qt::Key_9);
    m_zoomWidthAction->setStatusTip(tr("Fit width to window"));
    connect(m_zoomWidthAction, &QAction::triggered, m_viewer, &PdfViewerWidget::zoomToWidth);
    
    m_rotateCwAction = new QAction(QIcon::fromTheme("object-rotate-right"), tr("Rotate &Clockwise"), this);
    m_rotateCwAction->setShortcut(Qt::CTRL | Qt::Key_R);
    m_rotateCwAction->setStatusTip(tr("Rotate page clockwise"));
    connect(m_rotateCwAction, &QAction::triggered, m_viewer, &PdfViewerWidget::rotateClockwise);
    
    m_rotateCcwAction = new QAction(QIcon::fromTheme("object-rotate-left"), tr("Rotate &Counterclockwise"), this);
    m_rotateCcwAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_R);
    m_rotateCcwAction->setStatusTip(tr("Rotate page counterclockwise"));
    connect(m_rotateCcwAction, &QAction::triggered, m_viewer, &PdfViewerWidget::rotateCounterClockwise);
    
    m_fullScreenAction = new QAction(tr("&Full Screen"), this);
    m_fullScreenAction->setShortcut(Qt::Key_F11);
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setStatusTip(tr("Toggle full screen mode"));
    connect(m_fullScreenAction, &QAction::toggled, this, [this](bool checked) {
        if (checked) showFullScreen();
        else showNormal();
    });
    
    // Help actions
    m_aboutAction = new QAction(tr("&About"), this);
    m_aboutAction->setStatusTip(tr("Show about dialog"));
    connect(m_aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About PDF Reader"),
            tr("<h3>PDF Reader</h3>"
               "<p>Version 1.0</p>"
               "<p>A simple PDF viewer built with Qt6 and PDFium.</p>"
               "<p>Copyright &copy; 2024</p>"));
    });
    
    m_aboutPdfiumAction = new QAction(tr("About &PDFium"), this);
    m_aboutPdfiumAction->setStatusTip(tr("Show PDFium version info"));
    connect(m_aboutPdfiumAction, &QAction::triggered, this, &MainWindow::aboutPdfium);
}

void MainWindow::createMenus() {
    QMenuBar* menuBar = this->menuBar();
    
    // File menu
    QMenu* fileMenu = menuBar->addMenu(tr("&File"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_printAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exitAction);
    
    // Navigation menu
    QMenu* navMenu = menuBar->addMenu(tr("&Navigation"));
    navMenu->addAction(m_firstPageAction);
    navMenu->addAction(m_prevPageAction);
    navMenu->addAction(m_nextPageAction);
    navMenu->addAction(m_lastPageAction);
    navMenu->addSeparator();
    navMenu->addAction(m_zoomFitAction);
    navMenu->addAction(m_zoomWidthAction);
    navMenu->addSeparator();
    navMenu->addAction(m_rotateCwAction);
    navMenu->addAction(m_rotateCcwAction);
    
    // View menu
    QMenu* viewMenu = menuBar->addMenu(tr("&View"));
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fullScreenAction);
    
    // Help menu
    QMenu* helpMenu = menuBar->addMenu(tr("&Help"));
    helpMenu->addAction(m_aboutAction);
    helpMenu->addAction(m_aboutPdfiumAction);
}

void MainWindow::createToolBars() {
    // File toolbar
    QToolBar* fileToolBar = addToolBar(tr("File"));
    fileToolBar->setObjectName("fileToolBar");
    fileToolBar->addAction(m_openAction);
    fileToolBar->addAction(m_saveAsAction);
    fileToolBar->addAction(m_printAction);
    
    // Navigation toolbar
    QToolBar* navToolBar = addToolBar(tr("Navigation"));
    navToolBar->setObjectName("navigationToolBar");
    navToolBar->addAction(m_firstPageAction);
    navToolBar->addAction(m_prevPageAction);
    
    m_pageLabel = new QLabel("Page: - / -");
    m_pageLabel->setMinimumWidth(120);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    navToolBar->addWidget(m_pageLabel);
    
    navToolBar->addAction(m_nextPageAction);
    navToolBar->addAction(m_lastPageAction);
    
    // View toolbar
    QToolBar* viewToolBar = addToolBar(tr("View"));
    viewToolBar->setObjectName("viewToolBar");
    viewToolBar->addAction(m_zoomOutAction);
    
    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setMinimumWidth(60);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    viewToolBar->addWidget(m_zoomLabel);
    
    viewToolBar->addAction(m_zoomInAction);
    viewToolBar->addSeparator();
    viewToolBar->addAction(m_zoomFitAction);
    viewToolBar->addAction(m_zoomWidthAction);
    viewToolBar->addSeparator();
    viewToolBar->addAction(m_rotateCcwAction);
    viewToolBar->addAction(m_rotateCwAction);
}

void MainWindow::createStatusBar() {
    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setMinimumWidth(200);
    statusBar()->addWidget(m_statusLabel, 1);
    
    // Permanent widgets
    QLabel* rotationLabel = new QLabel("Rotation: 0°");
    rotationLabel->setMinimumWidth(100);
    statusBar()->addPermanentWidget(rotationLabel);
    
    connect(m_viewer, &PdfViewerWidget::rotationChanged, this, [rotationLabel](int rot) {
        rotationLabel->setText(QString("Rotation: %1°").arg(rot));
    });
}

void MainWindow::openFile() {
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open PDF File"),
        QDir::homePath(),
        tr("PDF Files (*.pdf);;All Files (*)")
    );
    
    if (!fileName.isEmpty()) {
        m_statusLabel->setText(tr("Loading %1...").arg(QFileInfo(fileName).fileName()));
        QApplication::processEvents();
        
        if (m_viewer->loadFile(fileName)) {
            m_statusLabel->setText(tr("Loaded: %1").arg(QFileInfo(fileName).fileName()));
        } else {
            m_statusLabel->setText(tr("Failed to load: %1").arg(QFileInfo(fileName).fileName()));
            QMessageBox::warning(this, tr("Open Failed"), 
                tr("Could not open the PDF file:\n%1").arg(fileName));
        }
        
        updateActions();
    }
}

void MainWindow::saveAs() {
    // Not implemented
    QMessageBox::information(this, tr("Save As"), tr("Save As not implemented yet."));
}

void MainWindow::printDocument() {
    if (!m_viewer || m_viewer->pageCount() == 0) return;
    
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print Document"));
    
    if (dialog.exec() == QDialog::Accepted) {
        // TODO: Implement actual printing using PDFium
        QMessageBox::information(this, tr("Print"), tr("Printing not fully implemented yet."));
    }
}

void MainWindow::aboutPdfium() {
    QMessageBox::about(this, tr("About PDFium"),
        tr("<h3>PDFium</h3>"
           "<p>PDFium is an open-source PDF rendering engine developed by Google.</p>"
           "<p>Used in Chrome, Android, and many other projects.</p>"
           "<p><a href=\"https://pdfium.googlesource.com/pdfium/\">https://pdfium.googlesource.com/pdfium/</a></p>"));
}

void MainWindow::onPageChanged(int page) {
    updateNavigationActions();
    if (m_pageLabel) {
        m_pageLabel->setText(QString("Page: %1 / %2").arg(page + 1).arg(m_viewer->pageCount()));
    }
}

void MainWindow::onZoomChanged(qreal zoom) {
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1%").arg(int(zoom * 100)));
    }
}

void MainWindow::onRotationChanged(int rotation) {
    // Handled by status bar connection
}

void MainWindow::onPageCountChanged(int count) {
    updateNavigationActions();
    if (m_pageLabel) {
        m_pageLabel->setText(QString("Page: %1 / %2").arg(m_viewer->currentPage() + 1).arg(count));
    }
}

void MainWindow::onStatusMessage(const QString& message) {
    if (m_statusLabel) {
        m_statusLabel->setText(message);
    }
}

void MainWindow::onTextSelected(const QString& text) {
    if (m_statusLabel) {
        m_statusLabel->setText(tr("Text copied to clipboard"));
    }
}

void MainWindow::updateActions() {
    bool hasDoc = m_viewer && m_viewer->pageCount() > 0;
    m_saveAsAction->setEnabled(hasDoc);
    m_printAction->setEnabled(hasDoc);
    m_zoomInAction->setEnabled(hasDoc);
    m_zoomOutAction->setEnabled(hasDoc);
    m_zoomFitAction->setEnabled(hasDoc);
    m_zoomWidthAction->setEnabled(hasDoc);
    m_rotateCwAction->setEnabled(hasDoc);
    m_rotateCcwAction->setEnabled(hasDoc);
    updateNavigationActions();
}

void MainWindow::updateNavigationActions() {
    if (!m_viewer) return;
    
    int current = m_viewer->currentPage();
    int count = m_viewer->pageCount();
    
    m_firstPageAction->setEnabled(count > 0 && current > 0);
    m_prevPageAction->setEnabled(count > 0 && current > 0);
    m_nextPageAction->setEnabled(count > 0 && current < count - 1);
    m_lastPageAction->setEnabled(count > 0 && current < count - 1);
}