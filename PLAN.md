# Implementation Plan: Full-Featured Text Search

## 1. 涉及修改的文件列表

- `qt_app/src/pdfdocument.h`: 定义搜索状态变量和会话管理接口。
- `qt_app/src/pdfdocument.cpp`: 实现跨页搜索逻辑、结果缓存和索引追踪。
- `qt_app/src/mainwindow.h`: 声明搜索界面组件和相关 Slot。
- `qt_app/src/mainwindow.cpp`: 实现搜索 UI 布局、`Ctrl+F` 快捷键以及 UI 与逻辑的绑定。
- `qt_app/src/pdfviewerwidget.cpp`: 实现搜索结果在页面上的视觉高亮显示。

## 2. 具体执行步骤

### 步骤 1: 在 `PdfDocument` 中实现搜索状态管理
**目标**: 将单页搜索升级为文档级搜索会话。

- **修改 `pdfdocument.h`**:
    - 添加私有成员：`m_currentSearchTerm` (QString), `m_currentMatchIndex` (int), `m_searchCaseSensitive` (bool), `m_searchMatches` (QMap<int, QList<QRectF>>)。
    - 添加公开接口：
        - `void startSearch(const QString& text, bool caseSensitive)`
        - `int findNextMatch()`
        - `int findPrevMatch()`
        - `int totalMatches() const`
        - `QList<QRectF> matchesForPage(int pageIndex) const`
        - `int currentMatchPage() const`
        - `QRectF currentMatchRect() const`
- **修改 `pdfdocument.cpp`**:
    - 实现 `startSearch`: 遍历所有页面调用 `searchText` 并将结果存入 `m_searchMatches`。
    - 实现 `findNextMatch`/`findPrevMatch`: 更新 `m_currentMatchIndex` 并处理环形跳转。
    - 实现辅助查询函数。

**校验方式**: 编写单元测试或在调试模式下调用 `startSearch`，检查 `m_searchMatches` 是否正确记录了所有页面的匹配矩形。

### 步骤 2: 在 `MainWindow` 中构建搜索界面
**目标**: 提供用户输入搜索词和导航结果的 UI。

- **修改 `mainwindow.h`**:
    - 声明搜索面板组件（`QLineEdit`, `QPushButton` x2, `QLabel`）。
- **修改 `mainwindow.cpp`**:
    - 在 `createToolBars` 或单独的布局中创建搜索栏。
    - 绑定 `Ctrl+F` 快捷键以显示/隐藏搜索栏并聚焦输入框。
    - 连接 `QLineEdit::returnPressed` 到 `PdfDocument::startSearch`。
    - 连接“上一个/下一个”按钮到 `PdfDocument::findNextMatch/PrevMatch`，并触发 `PdfViewerWidget::setPage`。

**校验方式**: 运行程序，按下 `Ctrl+F` 检查搜索栏是否出现，输入文字并回车，检查状态栏或标签是否显示正确的匹配总数。

### 步骤 3: 在 `PdfViewerWidget` 中实现视觉高亮
**目标**: 在 PDF 页面上实时绘制搜索结果。

- **修改 `pdfviewerwidget.cpp`**:
    - 修改 `paintEvent`：在绘制完页面图像后，调用 `m_document->matchesForPage(m_currentPage)`。
    - 遍历匹配矩形，使用 `mapFromPage` 将 PDF 坐标转换为 Widget 坐标。
    - 使用 `painter.fillRect` 绘制半透明黄色矩形。
    - 根据 `m_document->currentMatchRect()` 绘制当前选中的匹配项（使用不同颜色，如橙色）。

**校验方式**: 搜索一个已知存在的词，检查页面上是否出现了黄色高亮块；点击“下一个”，检查橙色高亮是否正确移动。

### 步骤 4: 整体集成与优化
**目标**: 确保搜索体验流畅且在缩放/旋转时依然准确。

- **优化**: 确保 `PdfViewerWidget::update()` 在匹配索引改变时被调用。
- **健壮性**: 处理搜索词为空或无匹配结果的情况。

**校验方式**: 执行端到端测试：打开文档 $\rightarrow$ `Ctrl+F` $\rightarrow$ 输入词 $\rightarrow$ 连续点击“下一个” $\rightarrow$ 缩放页面 $\rightarrow$ 验证高亮位置依然准确。
