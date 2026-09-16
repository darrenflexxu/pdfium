#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <iostream>
#include <vector>
#include <QMainWindow>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDockWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QModelIndex>

struct IPdfOutline;
class AbstractPdfViewer;
class PdfViewerWidget;
class SkiaPdfViewerWidget;
class QProgressDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    // [PROBE] temp: auto-open + toggle GPU after a delay (diagnostics).
    void probeAutoToggle(const QString& path);
    
private slots:
    void openFile();
    void saveAs();
    void saveOptimized();
    void printDocument();
    void aboutPdfium();
    
    // New features
    void showCompressionDialog();
    void showElementInspector();
    void extractPageText();
    void extractPageImages();
    void showDocumentStructure();
    
    void onPageChanged(int page);
    void onZoomChanged(qreal zoom);
    void onRotationChanged(int rotation);
    void onPageCountChanged(int count);
    void onStatusMessage(const QString& message);
    void onTextSelected(const QString& text);
    void onOptimizeFinished(bool success, const QString& outputPath, const QString& error);
    void onSaveCompressedFinished(bool success, const QString& filePath, const QString& error);
    
    // Document-wide search
    void startSearch();
    void searchNextMatch();
    void searchPrevMatch();
    void toggleSearchBar(bool visible);
    void updateSearchAfterNavigation();

    // Bookmarks
    void rebuildBookmarks();
    void onBookmarkClicked(const QModelIndex& index);
    
    void updateActions();
    void updateNavigationActions();

    // GPU acceleration (runtime CPU/GPU viewer toggle).
    void toggleGpuAcceleration(bool checked);
    void onViewModeChanged(int index);
    void onSelectionToolToggled(bool checked);

    // Active-viewer dispatch slots (the central widget is the single m_viewer;
    // its concrete type depends on the GPU Acceleration toggle).
    void goToFirstPageSlot();
    void goToPrevPageSlot();
    void goToNextPageSlot();
    void goToLastPageSlot();
    void zoomInSlot();
    void zoomOutSlot();
    void zoomToFitSlot();
    void zoomToWidthSlot();
    void rotateCwSlot();
    void rotateCcwSlot();
    void setActivePage(int page);
    
private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createStatusBar();
    void createBookmarksPanel();

    // Recursively appends the sibling chain starting at `first` to
    // `parentItem` (or the model root when null). Consumes and releases the
    // refs of `first` and all its siblings. `total` is a tree-wide counter of
    // processed nodes that hard-caps the walk against malformed/circular
    // outlines.
    void addOutlineChildren(IPdfOutline* first, QStandardItem* parentItem, int depth, int& total);

    // Connects the unified viewer signal set (identical on every concrete
    // backend) to MainWindow's slots. Called whenever the active viewer is
    // (re)created, so switching renderers can reuse one wiring path.
    void setupViewerConnections(AbstractPdfViewer* viewer);
    AbstractPdfViewer* createViewer(bool gpu);

    AbstractPdfViewer* m_viewer = nullptr;
    QAction* m_gpuAction = nullptr;

    // Whether the currently installed viewer is the Skia GPU backend. Derived
    // from m_viewer's concrete type so the state can never drift out of sync
    // with the widget actually installed (no redundant m_gpuActive flag).
    bool usingGpuViewer() const;

    int activePageCount() const;
    int activeCurrentPage() const;
    void emitStatus(const QString& message);
    
    // Actions
    QAction* m_openAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_saveOptimizedAction = nullptr;
    QAction* m_printAction = nullptr;
    QAction* m_exitAction = nullptr;
    
    QAction* m_prevPageAction = nullptr;
    QAction* m_nextPageAction = nullptr;
    QAction* m_firstPageAction = nullptr;
    QAction* m_lastPageAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_zoomFitAction = nullptr;
    QAction* m_zoomWidthAction = nullptr;
    QAction* m_rotateCwAction = nullptr;
    QAction* m_rotateCcwAction = nullptr;
    QAction* m_selectionToolAction = nullptr;
    QComboBox* m_viewModeCombo = nullptr;

    // New feature actions
    QAction* m_compressAction = nullptr;
    QAction* m_elementInspectorAction = nullptr;
    QAction* m_extractTextAction = nullptr;
    QAction* m_extractImagesAction = nullptr;
    QAction* m_docStructureAction = nullptr;
    
    QAction* m_fullScreenAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_aboutPdfiumAction = nullptr;
    
    // Search
    QAction* m_searchAction = nullptr;
    QToolBar* m_searchToolBar = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchPrevButton = nullptr;
    QPushButton* m_searchNextButton = nullptr;
    QLabel* m_searchLabel = nullptr;
    
    // Bookmarks panel
    QDockWidget* m_bookmarksDock = nullptr;
    QTreeView* m_bookmarksTree = nullptr;
    QStandardItemModel* m_bookmarksModel = nullptr;
    
    // Status bar
    QLabel* m_pageLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_rotationLabel = nullptr;

    // Compression progress dialog (owned via WA_DeleteOnClose)
    QProgressDialog* m_compressProgress = nullptr;
};

#endif // MAINWINDOW_H