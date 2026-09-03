#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QAction>
#include <QToolBar>
#include <QStatusBar>

class PdfViewerWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    
private slots:
    void openFile();
    void saveAs();
    void printDocument();
    void aboutPdfium();
    
    void onPageChanged(int page);
    void onZoomChanged(qreal zoom);
    void onRotationChanged(int rotation);
    void onPageCountChanged(int count);
    void onStatusMessage(const QString& message);
    void onTextSelected(const QString& text);
    
    void updateActions();
    void updateNavigationActions();
    
private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createStatusBar();
    
    PdfViewerWidget* m_viewer = nullptr;
    
    // Actions
    QAction* m_openAction = nullptr;
    QAction* m_saveAsAction = nullptr;
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
    
    QAction* m_fullScreenAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_aboutPdfiumAction = nullptr;
    
    // Status bar
    QLabel* m_pageLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
};

#endif // MAINWINDOW_H