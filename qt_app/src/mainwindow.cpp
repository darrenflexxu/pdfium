#include "mainwindow.h"
#include "pdfviewerwidget.h"
#include "skiapdfviewerwidget.h"
#include "pdfdocument.h"
#include <QApplication>
#include <QClipboard>
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
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QProgressDialog>
#include <QDialogButtonBox>
#include <QDialog>
#include <QTextEdit>
#include <QFontDatabase>
#include <QDebug>
#include <QTimer> // [PROBE] temp

#include "pdfium_wrapper.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // Central widget: single viewer, switched via the same AbstractPdfViewer*
    // when GPU acceleration is toggled. The PDF document is shared and owned
    // by this window, and is (re)attached on every renderer switch.
    m_viewer = createViewer(/* gpu */ true);
    setCentralWidget(m_viewer->widget());
    m_gpuActive = true;
    
    PdfDocument* doc = new PdfDocument(this);
    m_viewer->setDocument(doc);
    
    createActions();
    createMenus();
    createToolBars();
    createStatusBar();
    createBookmarksPanel();

    setupViewerConnections(m_viewer);
    
    // Connect document signals for new features
    PdfDocument* currentDoc = m_viewer->document();
    if (currentDoc) {
        connect(currentDoc, &PdfDocument::optimizeFinished, this, &MainWindow::onOptimizeFinished);
        connect(currentDoc, &PdfDocument::saveCompressedFinished, this, &MainWindow::onSaveCompressedFinished);
    }
    
    // Window settings
    setWindowTitle("PDF Reader");
    resize(1024, 768);
    
    // Restore geometry
    QSettings settings("PdfReader", "PdfReader");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    
    updateActions();
}

void MainWindow::setupViewerConnections(AbstractPdfViewer* viewer) {
    connect(viewer->widget(), SIGNAL(pageChanged(int)), this, SLOT(onPageChanged(int)));
    connect(viewer->widget(), SIGNAL(zoomChanged(qreal)), this, SLOT(onZoomChanged(qreal)));
    connect(viewer->widget(), SIGNAL(rotationChanged(int)), this, SLOT(onRotationChanged(int)));
    connect(viewer->widget(), SIGNAL(pageCountChanged(int)), this, SLOT(onPageCountChanged(int)));
    connect(viewer->widget(), SIGNAL(statusMessage(QString)), this, SLOT(onStatusMessage(QString)));
    connect(viewer->widget(), SIGNAL(textSelected(QString)), this, SLOT(onTextSelected(QString)));

    // Rebuild the bookmarks tree whenever a document is (re)loaded
    connect(viewer->widget(), SIGNAL(pageCountChanged(int)), this, SLOT(rebuildBookmarks()));
}

AbstractPdfViewer* MainWindow::createViewer(bool gpu) {
#ifdef SKIA_AVAILABLE
    if (gpu)
        return new SkiaPdfViewerWidget(this);
#endif
    return new PdfViewerWidget(this);
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
    
    m_saveOptimizedAction = new QAction(QIcon::fromTheme("document-save"), tr("Save &Optimized..."), this);
    m_saveOptimizedAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_S);
    m_saveOptimizedAction->setStatusTip(tr("Save document with compression/optimization"));
    m_saveOptimizedAction->setEnabled(false);
    connect(m_saveOptimizedAction, &QAction::triggered, this, &MainWindow::saveOptimized);
    
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
    connect(m_prevPageAction, &QAction::triggered, this, &MainWindow::goToPrevPageSlot);
    
    m_nextPageAction = new QAction(QIcon::fromTheme("go-next"), tr("&Next Page"), this);
    m_nextPageAction->setShortcut(QKeySequence::MoveToNextPage);
    m_nextPageAction->setStatusTip(tr("Go to next page"));
    connect(m_nextPageAction, &QAction::triggered, this, &MainWindow::goToNextPageSlot);
    
    m_firstPageAction = new QAction(QIcon::fromTheme("go-first"), tr("&First Page"), this);
    m_firstPageAction->setShortcut(Qt::Key_Home);
    m_firstPageAction->setStatusTip(tr("Go to first page"));
    connect(m_firstPageAction, &QAction::triggered, this, &MainWindow::goToFirstPageSlot);
    
    m_lastPageAction = new QAction(QIcon::fromTheme("go-last"), tr("&Last Page"), this);
    m_lastPageAction->setShortcut(Qt::Key_End);
    m_lastPageAction->setStatusTip(tr("Go to last page"));
    connect(m_lastPageAction, &QAction::triggered, this, &MainWindow::goToLastPageSlot);
    
    // View actions
    m_zoomInAction = new QAction(QIcon::fromTheme("zoom-in"), tr("Zoom &In"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAction->setStatusTip(tr("Zoom in"));
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomInSlot);
    
    m_zoomOutAction = new QAction(QIcon::fromTheme("zoom-out"), tr("Zoom &Out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAction->setStatusTip(tr("Zoom out"));
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOutSlot);
    
    m_zoomFitAction = new QAction(QIcon::fromTheme("zoom-fit-best"), tr("Fit &Page"), this);
    m_zoomFitAction->setShortcut(Qt::CTRL | Qt::Key_0);
    m_zoomFitAction->setStatusTip(tr("Fit page to window"));
    connect(m_zoomFitAction, &QAction::triggered, this, &MainWindow::zoomToFitSlot);
    
    m_zoomWidthAction = new QAction(QIcon::fromTheme("zoom-fit-width"), tr("Fit &Width"), this);
    m_zoomWidthAction->setShortcut(Qt::CTRL | Qt::Key_9);
    m_zoomWidthAction->setStatusTip(tr("Fit width to window"));
    connect(m_zoomWidthAction, &QAction::triggered, this, &MainWindow::zoomToWidthSlot);
    
    m_rotateCwAction = new QAction(QIcon::fromTheme("object-rotate-right"), tr("Rotate &Clockwise"), this);
    m_rotateCwAction->setShortcut(Qt::CTRL | Qt::Key_R);
    m_rotateCwAction->setStatusTip(tr("Rotate page clockwise"));
    connect(m_rotateCwAction, &QAction::triggered, this, &MainWindow::rotateCwSlot);
    
    m_rotateCcwAction = new QAction(QIcon::fromTheme("object-rotate-left"), tr("Rotate &Counterclockwise"), this);
    m_rotateCcwAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_R);
    m_rotateCcwAction->setStatusTip(tr("Rotate page counterclockwise"));
    connect(m_rotateCcwAction, &QAction::triggered, this, &MainWindow::rotateCcwSlot);

    m_selectionToolAction = new QAction(QIcon::fromTheme("edit-select"), tr("&Selection Tool"), this);
    m_selectionToolAction->setCheckable(true);
    m_selectionToolAction->setStatusTip(tr("Toggle between panning and text selection"));
    connect(m_selectionToolAction, &QAction::toggled, this, &MainWindow::onSelectionToolToggled);

#ifdef SKIA_AVAILABLE
    m_gpuAction = new QAction(tr("&GPU Acceleration"), this);
    m_gpuAction->setCheckable(true);
    m_gpuAction->setStatusTip(tr("Render pages through Skia GPU (OpenGL) instead of the CPU painter"));
    m_gpuAction->setChecked(m_gpuActive);
    m_gpuAction->setVisible(true);
    connect(m_gpuAction, &QAction::toggled, this, &MainWindow::toggleGpuAcceleration);
#endif

    // Tools actions (new features)
    m_compressAction = new QAction(QIcon::fromTheme("document-compress"), tr("&Optimize PDF..."), this);
    m_compressAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_O);
    m_compressAction->setStatusTip(tr("Optimize and compress PDF"));
    m_compressAction->setEnabled(false);
    connect(m_compressAction, &QAction::triggered, this, &MainWindow::showCompressionDialog);
    
    m_elementInspectorAction = new QAction(QIcon::fromTheme("view-details"), tr("Element &Inspector"), this);
    m_elementInspectorAction->setShortcut(Qt::CTRL | Qt::Key_I);
    m_elementInspectorAction->setStatusTip(tr("Inspect page elements"));
    m_elementInspectorAction->setEnabled(false);
    connect(m_elementInspectorAction, &QAction::triggered, this, &MainWindow::showElementInspector);
    
    m_extractTextAction = new QAction(QIcon::fromTheme("edit-copy"), tr("Extract &Page Text"), this);
    m_extractTextAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_T);
    m_extractTextAction->setStatusTip(tr("Extract all text from current page"));
    m_extractTextAction->setEnabled(false);
    connect(m_extractTextAction, &QAction::triggered, this, &MainWindow::extractPageText);
    
    m_extractImagesAction = new QAction(QIcon::fromTheme("image-x-generic"), tr("Extract &Images"), this);
    m_extractImagesAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_I);
    m_extractImagesAction->setStatusTip(tr("Extract images from current page"));
    m_extractImagesAction->setEnabled(false);
    connect(m_extractImagesAction, &QAction::triggered, this, &MainWindow::extractPageImages);
    
    m_docStructureAction = new QAction(QIcon::fromTheme("text-x-preview"), tr("Document &Structure"), this);
    m_docStructureAction->setShortcut(Qt::CTRL | Qt::Key_D);
    m_docStructureAction->setStatusTip(tr("Show document structure information"));
    m_docStructureAction->setEnabled(false);
    connect(m_docStructureAction, &QAction::triggered, this, &MainWindow::showDocumentStructure);
    
    m_fullScreenAction = new QAction(tr("&Full Screen"), this);
    m_fullScreenAction->setShortcut(Qt::Key_F11);
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setStatusTip(tr("Toggle full screen mode"));
    connect(m_fullScreenAction, &QAction::toggled, this, [this](bool checked) {
        if (checked) showFullScreen();
        else showNormal();
    });
    
    // Search action (Ctrl+F)
    m_searchAction = new QAction(QIcon::fromTheme("edit-find"), tr("&Find..."), this);
    m_searchAction->setShortcut(Qt::CTRL | Qt::Key_F);
    m_searchAction->setCheckable(true);
    m_searchAction->setStatusTip(tr("Search for text in the document"));
    connect(m_searchAction, &QAction::toggled, this, &MainWindow::toggleSearchBar);
    
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
    fileMenu->addAction(m_saveOptimizedAction);
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
#ifdef SKIA_AVAILABLE
    if (m_gpuAction)
        viewMenu->addAction(m_gpuAction);
#endif
    viewMenu->addSeparator();
    viewMenu->addAction(m_searchAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fullScreenAction);
    
    // Tools menu (new features)
    QMenu* toolsMenu = menuBar->addMenu(tr("&Tools"));
    toolsMenu->addAction(m_compressAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_elementInspectorAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_extractTextAction);
    toolsMenu->addAction(m_extractImagesAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_docStructureAction);
    
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
    fileToolBar->addAction(m_saveOptimizedAction);
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
    viewToolBar->addAction(m_selectionToolAction);
    viewToolBar->addAction(m_zoomFitAction);
    viewToolBar->addAction(m_zoomWidthAction);
    viewToolBar->addSeparator();
    viewToolBar->addAction(m_rotateCcwAction);
    viewToolBar->addAction(m_rotateCwAction);
    viewToolBar->addSeparator();
    
    m_viewModeCombo = new QComboBox(this);
    m_viewModeCombo->setObjectName("viewModeCombo");
    m_viewModeCombo->addItem(tr("Single Page"));
    m_viewModeCombo->addItem(tr("Continuous"));
    m_viewModeCombo->setToolTip(tr("Display mode"));
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onViewModeChanged);
    viewToolBar->addWidget(m_viewModeCombo);
    
    // Tools toolbar (new features)
    QToolBar* toolsToolBar = addToolBar(tr("Tools"));
    toolsToolBar->setObjectName("toolsToolBar");
    toolsToolBar->addAction(m_compressAction);
    toolsToolBar->addSeparator();
    toolsToolBar->addAction(m_elementInspectorAction);
    toolsToolBar->addSeparator();
    toolsToolBar->addAction(m_extractTextAction);
    toolsToolBar->addAction(m_extractImagesAction);
    toolsToolBar->addSeparator();
    toolsToolBar->addAction(m_docStructureAction);
    
    // Search toolbar
    m_searchToolBar = addToolBar(tr("Search"));
    m_searchToolBar->setObjectName("searchToolBar");
    m_searchToolBar->setMovable(false);
    
    m_searchEdit = new QLineEdit(m_searchToolBar);
    m_searchEdit->setPlaceholderText(tr("Search text..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(200);
    m_searchToolBar->addWidget(m_searchEdit);
    
    m_searchPrevButton = new QPushButton(QIcon::fromTheme("go-up"), tr("Previous"), m_searchToolBar);
    m_searchToolBar->addWidget(m_searchPrevButton);
    
    m_searchNextButton = new QPushButton(QIcon::fromTheme("go-down"), tr("Next"), m_searchToolBar);
    m_searchToolBar->addWidget(m_searchNextButton);
    
    m_searchLabel = new QLabel(tr("No search"), m_searchToolBar);
    m_searchLabel->setMinimumWidth(80);
    m_searchLabel->setAlignment(Qt::AlignCenter);
    m_searchToolBar->addWidget(m_searchLabel);
    
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MainWindow::startSearch);
    connect(m_searchEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (text.isEmpty()) {
            m_searchLabel->setText(tr("No search"));
        }
    });
    connect(m_searchPrevButton, &QPushButton::clicked, this, &MainWindow::searchPrevMatch);
    connect(m_searchNextButton, &QPushButton::clicked, this, &MainWindow::searchNextMatch);
    
    m_searchToolBar->setVisible(false);
}

void MainWindow::createStatusBar() {
    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setMinimumWidth(200);
    statusBar()->addWidget(m_statusLabel, 1);
    
    // Permanent widgets
    m_rotationLabel = new QLabel("Rotation: 0°");
    m_rotationLabel->setMinimumWidth(100);
    statusBar()->addPermanentWidget(m_rotationLabel);
}

void MainWindow::createBookmarksPanel() {
    m_bookmarksDock = new QDockWidget(tr("Bookmarks"), this);
    m_bookmarksDock->setObjectName("bookmarksDock");
    m_bookmarksDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    m_bookmarksTree = new QTreeView(m_bookmarksDock);
    m_bookmarksTree->setObjectName("bookmarksTree");
    m_bookmarksModel = new QStandardItemModel(m_bookmarksTree);
    m_bookmarksTree->setModel(m_bookmarksModel);
    m_bookmarksTree->setHeaderHidden(true);
    m_bookmarksTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    m_bookmarksDock->setWidget(m_bookmarksTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_bookmarksDock);
    
    connect(m_bookmarksTree, &QTreeView::clicked, this, &MainWindow::onBookmarkClicked);
}

void MainWindow::rebuildBookmarks() {
    if (!m_bookmarksModel) return;
    m_bookmarksModel->clear();
    
    if (!m_viewer || m_viewer->pageCount() == 0) return;
    PdfDocument* doc = m_viewer->document();
    if (!doc) return;
    
    IPdfOutline* root = doc->getOutlineRoot();
    if (!root) {
        QStandardItem* empty = new QStandardItem(tr("No bookmarks available"));
        empty->setEnabled(false);
        m_bookmarksModel->appendRow(empty);
        return;
    }
    
    int total = 0;
    addOutlineChildren(root, nullptr, 0, total);
}

void MainWindow::addOutlineChildren(IPdfOutline* first, QStandardItem* parentItem, int depth, int& total) {
    const int kMaxDepth = 64;
    const int kMaxBookmarks = 5000;
    
    IPdfOutline* cur = first;
    while (cur) {
        if (++total > kMaxBookmarks) {
            cur->Release();
            break;
        }
        
        int titleLen = 0;
        const char* rawTitle = cur->GetTitle(&titleLen);
        QString title = (rawTitle && titleLen > 0)
                            ? QString::fromUtf8(rawTitle, titleLen)
                            : QString();
        if (rawTitle) PDF_FreeString(rawTitle);
        
        int page = cur->GetDestinationPage();
        
        QStandardItem* item = new QStandardItem(title.isEmpty() ? tr("(Untitled)") : title);
        item->setData(page, Qt::UserRole);
        if (page < 0) item->setEnabled(false);
        
        if (depth < kMaxDepth) {
            IPdfOutline* child = cur->GetFirstChild();
            if (child) {
                addOutlineChildren(child, item, depth + 1, total);
            }
        }
        
        if (parentItem) {
            parentItem->appendRow(item);
        } else {
            m_bookmarksModel->appendRow(item);
        }
        
        IPdfOutline* next = cur->GetNextSibling();
        cur->Release();
        cur = next;
    }
}

void MainWindow::onBookmarkClicked(const QModelIndex& index) {
    if (!m_viewer) return;
    int page = index.data(Qt::UserRole).toInt();
    if (page >= 0 && page < activePageCount()) {
        setActivePage(page);
    }
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
        
        bool ok = m_viewer->loadFile(fileName);
        if (ok) {
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
    if (!m_viewer || activePageCount() == 0) return;
    
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
        m_pageLabel->setText(QString("Page: %1 / %2").arg(page + 1).arg(activePageCount()));
    }
}

void MainWindow::onZoomChanged(qreal zoom) {
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1%").arg(int(zoom * 100)));
    }
}

void MainWindow::onRotationChanged(int rotation) {
    if (m_rotationLabel) {
        m_rotationLabel->setText(QString("Rotation: %1°").arg(rotation));
    }
}

void MainWindow::onPageCountChanged(int count) {
    updateNavigationActions();
    if (m_pageLabel) {
        m_pageLabel->setText(QString("Page: %1 / %2").arg(activeCurrentPage() + 1).arg(count));
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

void MainWindow::toggleSearchBar(bool visible) {
    if (!m_searchToolBar) return;
    m_searchToolBar->setVisible(visible);
    if (visible) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void MainWindow::startSearch() {
    if (!m_viewer || activePageCount() == 0) return;
    
    QString term = m_searchEdit->text();
    if (term.isEmpty()) {
        m_searchLabel->setText(tr("No search"));
        return;
    }
    
    PdfDocument* doc = m_viewer->document();
    if (!doc) return;
    
    doc->startSearch(term, false);
    int total = doc->totalMatches();
    
    if (total == 0) {
        m_searchLabel->setText(tr("No matches"));
        return;
    }
    
    doc->findNextMatch();
    updateSearchAfterNavigation();
    emitStatus(tr("Found %1 match(es) for \"%2\"").arg(total).arg(term));
}

void MainWindow::searchNextMatch() {
    if (!m_viewer || !m_viewer->document()) return;
    
    PdfDocument* doc = m_viewer->document();
    if (doc->totalMatches() == 0) return;
    
    doc->findNextMatch();
    updateSearchAfterNavigation();
}

void MainWindow::searchPrevMatch() {
    if (!m_viewer || !m_viewer->document()) return;
    
    PdfDocument* doc = m_viewer->document();
    if (doc->totalMatches() == 0) return;
    
    doc->findPrevMatch();
    updateSearchAfterNavigation();
}

void MainWindow::updateSearchAfterNavigation() {
    PdfDocument* doc = m_viewer->document();
    if (!doc) return;
    
    int total = doc->totalMatches();
    int currentPage = doc->currentMatchPage();
    if (currentPage >= 0 && currentPage != activeCurrentPage()) {
        setActivePage(currentPage);
    }
    
    m_searchLabel->setText(tr("%1 / %2").arg(doc->currentMatchIndex() + 1).arg(total));
    m_viewer->widget()->update();
}

void MainWindow::updateActions() {
    bool hasDoc = m_viewer && activePageCount() > 0;
    m_saveAsAction->setEnabled(hasDoc);
    m_saveOptimizedAction->setEnabled(hasDoc);
    m_printAction->setEnabled(hasDoc);
    m_zoomInAction->setEnabled(hasDoc);
    m_zoomOutAction->setEnabled(hasDoc);
    m_zoomFitAction->setEnabled(hasDoc);
    m_zoomWidthAction->setEnabled(hasDoc);
    m_rotateCwAction->setEnabled(hasDoc);
    m_rotateCcwAction->setEnabled(hasDoc);
    
    // New feature actions
    m_compressAction->setEnabled(hasDoc);
    m_elementInspectorAction->setEnabled(hasDoc);
    m_extractTextAction->setEnabled(hasDoc);
    m_extractImagesAction->setEnabled(hasDoc);
    m_docStructureAction->setEnabled(hasDoc);
    m_searchAction->setEnabled(hasDoc);
    
    updateNavigationActions();
}

void MainWindow::updateNavigationActions() {
    if (!m_viewer) return;
    
    int current = activeCurrentPage();
    int count = activePageCount();
    
    m_firstPageAction->setEnabled(count > 0 && current > 0);
    m_prevPageAction->setEnabled(count > 0 && current > 0);
    m_nextPageAction->setEnabled(count > 0 && current < count - 1);
    m_lastPageAction->setEnabled(count > 0 && current < count - 1);
}

// ============ Active-viewer dispatch ============

int MainWindow::activePageCount() const {
    // Single source of truth is the shared document, which both viewers poll.
    // A hidden viewer's cached m_pageCount can lag behind a just-loaded doc;
    // the document's count is always authoritative for enabling/denominators.
    if (!m_viewer) return 0;
    PdfDocument* doc = m_viewer->document();
    return doc ? doc->pageCount() : 0;
}

int MainWindow::activeCurrentPage() const {
    return m_viewer ? m_viewer->currentPage() : -1;
}

void MainWindow::emitStatus(const QString& message) {
    if (m_statusLabel) m_statusLabel->setText(message);
}

void MainWindow::goToFirstPageSlot() {
    m_viewer->goToFirstPage();
}

void MainWindow::goToPrevPageSlot() {
    m_viewer->goToPrevPage();
}

void MainWindow::goToNextPageSlot() {
    m_viewer->goToNextPage();
}

void MainWindow::goToLastPageSlot() {
    m_viewer->goToLastPage();
}

void MainWindow::zoomInSlot() {
    m_viewer->zoomIn();
}

void MainWindow::zoomOutSlot() {
    m_viewer->zoomOut();
}

void MainWindow::zoomToFitSlot() {
    m_viewer->zoomToFit();
}

void MainWindow::zoomToWidthSlot() {
    m_viewer->zoomToWidth();
}

void MainWindow::rotateCwSlot() {
    m_viewer->rotateClockwise();
}

void MainWindow::rotateCcwSlot() {
    m_viewer->rotateCounterClockwise();
}

void MainWindow::setActivePage(int page) {
    m_viewer->setPage(page);
}

void MainWindow::onViewModeChanged(int index) {
    m_viewer->setViewMode(index == 0
        ? AbstractPdfViewer::ViewMode::SinglePage
        : AbstractPdfViewer::ViewMode::Continuous);
}

void MainWindow::onSelectionToolToggled(bool checked) {
    m_viewer->setTextSelectionEnabled(checked);
}

void MainWindow::toggleGpuAcceleration(bool checked) {
#ifndef SKIA_AVAILABLE
    Q_UNUSED(checked);
    return;
#else
    if (!m_viewer || checked == m_gpuActive) return;

    // Capture the current view state from the viewer being replaced.
    const int srcPage = m_viewer->currentPage();
    const qreal srcZoom = m_viewer->zoom();
    const int srcRot = m_viewer->rotation();
    const int srcMode = static_cast<int>(m_viewer->viewMode());
    const bool srcSelection = m_selectionToolAction ? m_selectionToolAction->isChecked() : false;
    PdfDocument* doc = m_viewer->document();

    // [PROBE] temp: log the new GPU surface geometry after the swap.
    qDebug().noquote() << "[PROBE] toggleGpuAcceleration checked=" << checked
                       << "srcPage=" << srcPage << "srcZoom=" << srcZoom;

    // Tear down the old viewer and build the new backend.
    QWidget* oldWidget = m_viewer->widget();
    setCentralWidget(nullptr);

    m_viewer = createViewer(checked);
    AbstractPdfViewer* next = m_viewer;
    next->setDocument(doc);

    // Restore the view state on the fresh viewer.
    next->setPage(srcPage);
    next->setZoom(srcZoom);
    next->setRotation(srcRot);
    next->setViewMode(srcMode == 0
        ? AbstractPdfViewer::ViewMode::SinglePage
        : AbstractPdfViewer::ViewMode::Continuous);
    next->setTextSelectionEnabled(srcSelection);

    setupViewerConnections(next);
    setCentralWidget(next->widget());
    next->widget()->show();

    if (oldWidget)
        oldWidget->deleteLater();

    m_gpuActive = checked;

    if (m_viewModeCombo) {
        if (m_viewModeCombo->currentIndex() != srcMode)
            m_viewModeCombo->setCurrentIndex(srcMode);
    }

    emitStatus(tr("Renderer switched to %1").arg(
        checked ? tr("GPU (Skia OpenGL)") : tr("CPU")));
    updateActions();
#endif
}

// ============ New Feature Slots ============

void MainWindow::probeAutoToggle(const QString& path) {
    if (!m_viewer) return;
    m_viewer->loadFile(path);
    QTimer::singleShot(1500, this, [this]() {
#if !defined(SKIA_AVAILABLE)
        qDebug().noquote() << "[PROBE] SKIA_AVAILABLE not defined - toggle is a no-op";
#else
        if (m_gpuAction)
            m_gpuAction->setChecked(true);
        // [PROBE] temp: repaint again after the relayout paint.
        QTimer::singleShot(400, this, [this]() {
            if (m_viewer) m_viewer->widget()->update();
        });
#endif
    });
}

void MainWindow::saveOptimized() {
    if (!m_viewer || activePageCount() == 0) return;

    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save Optimized PDF"),
        QDir::homePath(),
        tr("PDF Files (*.pdf);;All Files (*)")
    );

    if (fileName.isEmpty()) return;

    // Show compression options dialog
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Optimize PDF"));
    dialog.resize(400, 340);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QGroupBox* optionsGroup = new QGroupBox(tr("Compression Options"));
    QVBoxLayout* optionsLayout = new QVBoxLayout(optionsGroup);

    QCheckBox* cbFlate = new QCheckBox(tr("Flate compression (lossless)"), &dialog);
    cbFlate->setChecked(true);
    optionsLayout->addWidget(cbFlate);

    QCheckBox* cbImages = new QCheckBox(tr("Recompress images"), &dialog);
    cbImages->setChecked(true);
    optionsLayout->addWidget(cbImages);

    QHBoxLayout* qualityRow = new QHBoxLayout;
    qualityRow->addWidget(new QLabel(tr("Image quality (1-100):"), &dialog));
    QSpinBox* spQuality = new QSpinBox(&dialog);
    spQuality->setRange(1, 100);
    spQuality->setValue(90);
    qualityRow->addWidget(spQuality);
    optionsLayout->addLayout(qualityRow);

    QCheckBox* cbAnnotations = new QCheckBox(tr("Remove annotations"), &dialog);
    optionsLayout->addWidget(cbAnnotations);

    QCheckBox* cbForms = new QCheckBox(tr("Remove form fields"), &dialog);
    optionsLayout->addWidget(cbForms);

    QCheckBox* cbBookmarks = new QCheckBox(tr("Remove bookmarks"), &dialog);
    optionsLayout->addWidget(cbBookmarks);

    QCheckBox* cbMetadata = new QCheckBox(tr("Remove metadata"), &dialog);
    optionsLayout->addWidget(cbMetadata);

    layout->addWidget(optionsGroup);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) return;

    // Build compression options from the dialog state.
    PdfDocument::CompressOptions options;
    if (cbFlate->isChecked()) options.flags |= PdfDocument::CompressFlate;
    if (cbImages->isChecked()) options.flags |= PdfDocument::CompressImages;
    // Unreachable objects are dropped on save regardless; this flag only
    // enables reporting how many in the stats message.
    options.flags |= PdfDocument::CompressRemoveUnused;
    options.imageQuality = spQuality->value();
    options.removeAnnotations = cbAnnotations->isChecked();
    options.removeForms = cbForms->isChecked();
    options.removeBookmarks = cbBookmarks->isChecked();
    options.removeMetadata = cbMetadata->isChecked();

    PdfDocument* doc = m_viewer->document();
    if (!doc) return;

    // Create a modal progress dialog driven by the async compression task.
    QProgressDialog* progress = new QProgressDialog(tr("开始优化..."),
                                                    QString(), 0, 100, this);
    progress->setWindowTitle(tr("压缩进度"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setCancelButton(nullptr);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    connect(doc, &PdfDocument::compressionProgress, progress,
            [progress](int pct, const QString& status) {
                progress->setValue(pct);
                progress->setLabelText(status);
            });
    m_compressProgress = progress;

    m_statusLabel->setText(tr("Optimizing PDF..."));
    QApplication::processEvents();

    doc->saveWithCompression(fileName, options);
}

void MainWindow::showCompressionDialog() {
    saveOptimized(); // Same for now
}

void MainWindow::showElementInspector() {
    if (!m_viewer || activePageCount() == 0) return;
    
    int pageIndex = activeCurrentPage();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Element Inspector - Page %1").arg(pageIndex + 1));
    dialog.resize(600, 400);
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    QListWidget* elementList = new QListWidget(&dialog);
    layout->addWidget(elementList);
    
    // Get elements from current page
    // This would use the new element extraction API
    // For now, show placeholder
    elementList->addItem(tr("Element inspection would show page elements here"));
    elementList->addItem(tr("Text elements, images, paths, forms, etc."));
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);
    
    dialog.exec();
}

void MainWindow::extractPageText() {
    if (!m_viewer || activePageCount() == 0) return;
    
    int pageIndex = activeCurrentPage();
    
    // Get text from the document
    // This would use the new element extraction API
    QString text = tr("Text extraction from page %1 would be performed here.").arg(pageIndex + 1);
    
    // Copy to clipboard
    QApplication::clipboard()->setText(text);
    m_statusLabel->setText(tr("Page text copied to clipboard"));
}

void MainWindow::extractPageImages() {
    if (!m_viewer || activePageCount() == 0) return;
    
    int pageIndex = activeCurrentPage();
    
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"), QDir::homePath());
    if (dir.isEmpty()) return;
    
    // This would use the new element extraction API to get images
    // For now, show placeholder
    QMessageBox::information(this, tr("Extract Images"), 
        tr("Image extraction from page %1 would save images to:\n%2").arg(pageIndex + 1).arg(dir));
    
    m_statusLabel->setText(tr("Images extracted to %1").arg(dir));
}

void MainWindow::showDocumentStructure() {
    if (!m_viewer || activePageCount() == 0) return;
    
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Document Structure"));
    dialog.resize(500, 400);
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    QTextEdit* structureText = new QTextEdit(&dialog);
    structureText->setReadOnly(true);
    structureText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(structureText);
    
    // This would use the new document structure API
    // For now, show placeholder
    structureText->setHtml(tr(
        "<h3>Document Structure</h3>"
        "<p><b>Pages:</b> %1</p>"
        "<p><b>Objects:</b> N/A</p>"
        "<p><b>Fonts:</b> N/A</p>"
        "<p><b>Images:</b> N/A</p>"
        "<p><b>Form Fields:</b> N/A</p>"
        "<p><b>Annotations:</b> N/A</p>"
        "<p><b>Bookmarks:</b> N/A</p>"
        "<p><b>File Size:</b> N/A</p>"
        "<p><b>Producer:</b> N/A</p>"
        "<p><b>Creator:</b> N/A</p>"
        "<p><b>Creation Date:</b> N/A</p>"
        "<p><b>Modification Date:</b> N/A</p>"
    ).arg(activePageCount()));
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);
    
    dialog.exec();
}

void MainWindow::onOptimizeFinished(bool success, const QString& outputPath, const QString& error) {
    if (success) {
        m_statusLabel->setText(tr("Optimization complete: %1").arg(outputPath));
        PDF_CompressStats stats = m_viewer->document()->getLastCompressStats();
        QString details = tr("Original: %1 bytes\nCompressed: %2 bytes\nRatio: %3%\nObjects removed: %4")
            .arg(stats.original_size)
            .arg(stats.compressed_size)
            .arg(QString::number(stats.compression_ratio * 100, 'f', 1))
            .arg(stats.objects_removed);
        QMessageBox::information(this, tr("Optimization Complete"), details);
    } else {
        m_statusLabel->setText(tr("Optimization failed: %1").arg(error));
        QMessageBox::warning(this, tr("Optimization Failed"), error);
    }
}

void MainWindow::onSaveCompressedFinished(bool success, const QString& filePath, const QString& error) {
    if (m_compressProgress) {
        m_compressProgress->close();
        m_compressProgress = nullptr;
    }
    if (success) {
        m_statusLabel->setText(tr("Saved compressed: %1").arg(filePath));
        PdfDocument* doc = m_viewer ? m_viewer->document() : nullptr;
        PDF_CompressStats stats = doc ? doc->getLastCompressStats()
                                      : PDF_CompressStats{};
        const double ratioPct = stats.original_size > 0
            ? stats.compression_ratio * 100.0
            : 0.0;
        QString report = tr("Original: %1 bytes\n"
                            "Compressed: %2 bytes\n"
                            "Ratio: %3%\n"
                            "Objects removed: %4\n"
                            "Images recompressed: %5\n"
                            "Fonts subset: %6")
            .arg(static_cast<qulonglong>(stats.original_size))
            .arg(static_cast<qulonglong>(stats.compressed_size))
            .arg(QString::number(ratioPct, 'f', 1))
            .arg(stats.objects_removed)
            .arg(stats.images_recompressed)
            .arg(stats.fonts_subsets);
        QMessageBox::information(this, tr("Compression Complete"), report);
    } else {
        m_statusLabel->setText(tr("Save failed: %1").arg(error));
        QMessageBox::warning(this, tr("Save Failed"), error);
    }
}
