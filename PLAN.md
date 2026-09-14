## 9. 新增需求：统一 PDF 视图接口 (Unified Viewer Interface)

### 需求背景
目前 `MainWindow` 同时持有 `PdfViewerWidget` (CPU) 和 `SkiaPdfViewerWidget` (GPU) 两个实例，并在切换 GPU 加速时通过 `if (m_gpuActive)` 分支手动分发所有指令。这种做法导致 `MainWindow` 中存在大量冗余的逻辑分支，且难以维护。为了提高代码可维护性和可扩展性，需要提取一套统一的视图接口，使 `MainWindow` 能够通过多态机制操作当前的视图组件。

### 涉及修改的文件列表
- `qt_app/src/abstractpdfviewer.h` (新): 定义 `AbstractPdfViewer` 抽象基类。
- `qt_app/src/pdfviewerwidget.h`: 修改继承关系为 `AbstractPdfViewer`。
- `qt_app/src/pdfviewerwidget.cpp`: 适配基类接口。
- `qt_app/src/skiapdfviewerwidget.h`: 修改继承关系为 `AbstractPdfViewer`。
- `qt_app/src/skiapdfviewerwidget.cpp`: 适配基类接口。
- `qt_app/src/mainwindow.h`: 将 `m_viewer` 和 `m_gpuViewer` 合并为单个 `AbstractPdfViewer* m_viewer`。
- `qt_app/src/mainwindow.cpp`: 重构视图初始化、信号连接及指令分发逻辑。

### 具体执行步骤

#### 步骤 1: 定义抽象基类 `AbstractPdfViewer`
- **实现内容**:
    1. 创建 `abstractpdfviewer.h`，定义一个继承自 `QWidget` 的抽象类。
    2. **提取公共 API**: 将所有两个 viewer 共有的公开方法定义为纯虚函数 (`= 0`)，包括：
        - 文档管理：`setDocument()`, `document()`
        - 页面导航：`setPage()`, `currentPage()`, `pageCount()`, `goToNextPage()`, `goToPrevPage()`, `goToFirstPage()`, `goToLastPage()`
        - 视图控制：`setZoom()`, `zoom()`, `zoomIn()`, `zoomOut()`, `zoomToFit()`, `zoomToWidth()`, `setRotation()`, `rotation()`, `rotateClockwise()`, `rotateCounterClockwise()`
        - 视图模式：定义 `enum class ViewMode { SinglePage, Continuous }`，并定义 `setViewMode()`, `viewMode()`
        - 文本选择：`setTextSelectionEnabled()`, `isTextSelectionEnabled()`
        - 坐标映射：`mapToPage()`, `pageRect()`, `pageRect(int)`
    3. **统一信号**: 将所有公共信号（如 `pageChanged`, `zoomChanged`, `statusMessage` 等）定义在基类中。
- **校验方式**: 编译通过，确保基类定义了所有必要的接口且不包含具体实现。

#### 步骤 2: 实现接口继承与适配
- **实现内容**:
    1. 修改 `PdfViewerWidget` 和 `SkiaPdfViewerWidget` 的头文件，使其继承自 `AbstractPdfViewer`。
    2. 在 `.cpp` 文件中，确保所有纯虚函数得到了正确实现（大部分只需保留现有实现，但需检查函数签名是否与基类完全一致）。
    3. 移除子类中重复定义的 `ViewMode` 枚举，统一使用基类的枚举。
- **校验方式**: 编译通过，验证两个子类都能正确实例化并调用基类接口。

#### 步骤 3: 重构 `MainWindow` 视图管理
- **实现内容**:
    1. **成员变量修改**: 删除 `m_gpuViewer`，将 `m_viewer` 的类型改为 `AbstractPdfViewer*`。
    2. **初始化逻辑**:
        - 根据 `m_gpuActive` 初始状态，仅实例化一个对应的 Viewer。
        - 实现一个私有方法 `setupViewerConnections(AbstractPdfViewer* viewer)`，用于统一连接视图信号到 `MainWindow` 的槽函数。
    3. **指令分发简化**: 遍历 `MainWindow` 中所有 `goTo...`, `zoom...`, `rotate...` 等 Slot，删除 `if (m_gpuActive)` 分支，直接调用 `m_viewer->method()`。
    4. **重构 `toggleGpuAcceleration`**:
        - 记录当前视图的状态（Page, Zoom, Rotation, Mode）。
        - 销毁旧视图 $\rightarrow$ 根据 `checked` 状态创建新的 `PdfViewerWidget` 或 `SkiaPdfViewerWidget`。
        - 调用 `setupViewerConnections` 重新绑定信号。
        - 恢复之前记录的状态到新视图。
        - 调用 `setCentralWidget(m_viewer)`。
- **校验方式**: 
    1. **功能验证**: 验证打开文件、翻页、缩放等基础功能在两种模式下均正常工作。
    2. **切换验证**: 切换 GPU 加速 $\rightarrow$ 验证当前页面、缩放级别、旋转角度和视图模式被正确保留且无视觉跳变。
    3. **信号验证**: 验证翻页后状态栏的页码更新、缩放后比例更新等信号链路依然畅通。

### 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **基础导航** | 点击“下一页”/“上一页” | 无论处于 CPU 还是 GPU 模式，页面均正确跳转 |
| **视图控制** | 调整缩放级别 $\rightarrow$ 旋转页面 | 视图正确响应，状态栏显示更新 |
| **无缝切换** | 缩放到 150% $\rightarrow$ 翻到第 5 页 $\rightarrow$ 切换 GPU 加速 | 切换后仍处于第 5 页，缩放保持 150%，无明显闪烁 |
| **信号一致性** | 更改视图模式为 Continuous | 无论哪个 Viewer，`MainWindow` 都能收到通知并更新 UI |
| **内存检查** | 频繁切换 GPU 加速 $\rightarrow$ 观察内存 | 切换时旧 Viewer 被正确销毁，无内存泄漏 |
