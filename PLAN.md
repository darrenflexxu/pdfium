# 实现计划：主视图页面滚动与单页/连续显示模式

## 1. 需求背景
目前 PDF 查看器仅支持单页显示，且页面在视图中居中显示。用户需要实现两种显示模式：
- **单页模式 (Single Page)**：保持当前行为，一次显示一页，通过 `setPage()` 切换。
- **连续滚动模式 (Continuous)**：所有页面垂直排列，用户可以通过滚动条/鼠标滚轮在所有页面间平滑滚动。

## 2. 涉及修改的文件列表
- `qt_app/src/pdfviewerwidget.h`: 定义 `ViewMode` 枚举及相关成员变量/接口。
- `qt_app/src/pdfviewerwidget.cpp`: 重构渲染循环、坐标映射逻辑及页面跟踪。
- `qt_app/src/mainwindow.h` & `.cpp`: 添加模式切换的 UI 控件。

## 3. 具体执行步骤

### 步骤 1: 引入视图模式管理 (`PdfViewerWidget`)
**目标**: 在视图层支持模式切换。

- **修改点**:
    - 定义 `enum class ViewMode { SinglePage, Continuous };`。
    - 添加私有成员 `ViewMode m_viewMode = ViewMode::SinglePage;`。
    - 提供公共接口 `void setViewMode(ViewMode mode);`。
- **校验方式**: 调用 `setViewMode` 后验证成员变量正确更新。

### 步骤 2: 重构渲染逻辑 (`PdfViewerWidget::paintEvent`)
**目标**: 实现连续页面的绘制。

- **修改点**:
    - **单页模式**: 保留现有逻辑，仅绘制 `m_currentPage`。
    - **连续模式**: 
        1. 遍历所有页面索引 $0 \dots \text{pageCount}-1$。
        2. 计算每页在当前缩放/旋转下的像素尺寸。
        3. 根据 `m_scrollOffset` 和页面高度，计算每一页在视图中的 $\text{Y}$ 坐标。
        4. 检查该页面矩形是否与当前可见视口 (Viewport) 相交。
        5. 如果可见，调用 `requestRender(pageIndex, ...)` 并绘制图像。
- **校验方式**: 切换到 `Continuous` 模式 $\rightarrow$ 滚动页面 $\rightarrow$ 验证可见区域内正确显示了多个相邻页面的内容。

### 步骤 3: 更新坐标映射系统 (`PdfViewerWidget`)
**目标**: 使坐标转换支持多页偏移。

- **修改点**:
    - **重构 `pageRect(int pageIndex)`**: 使其能够根据索引返回该页在连续模式下的绝对位置矩形。
    - **更新 `mapToPage(QPoint widgetPos)`**: 
        - 在连续模式下，先通过累加页面高度找到点击点落在哪个页面内。
        - 将坐标相对于该页面的左上角进行偏移，再调用 PDFium 转换。
    - **更新 `mapFromPage(QPointF pagePos, int pageIndex)`**: 
        - 根据 `pageIndex` 计算其在连续布局中的 $\text{Y}$ 偏移量。
- **校验方式**: 在连续模式下进行文本选择 $\rightarrow$ 验证选区高亮块正确落在对应页面的文字上，无偏移。

### 步骤 4: 实现自动页面跟踪 (`PdfViewerWidget`)
**目标**: 在滚动时自动更新 `m_currentPage` 索引，以保持状态栏同步。

- **修改点**:
    - 在 `paintEvent` 或 `wheelEvent` 结束后，计算视口中心点所在的页面索引。
    - 如果索引发生变化，调用 `emit pageChanged(newPageIndex);`。
- **校验方式**: 滚动页面 $\rightarrow$ 验证主界面状态栏的 "Page X of Y" 能随滚动实时更新。

### 步骤 5: UI 控件集成 (`MainWindow`)
**目标**: 为用户提供模式切换入口。

- **修改点**: 
    - 在工具栏中增加一个 `QComboBox` 或 `QActionGroup` (单页/连续)。
    - 将信号连接至 `m_viewer->setViewMode(...)`。
- **校验方式**: 点击 UI 按钮 $\rightarrow$ 查看界面实时在单页和连续滚动之间切换。

## 4. 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **单页模式验证** | 切换至单页 $\rightarrow$ 翻页 | 界面仅显示当前页，翻页时页面瞬间切换 |
| **连续模式滚动** | 切换至连续 $\rightarrow$ 滚轮向下滚动 | 页面平滑向上移动，下一页逐渐进入视口 |
| **坐标一致性** | 连续模式 $\rightarrow$ 选中第二页文本 $\rightarrow$ 复制 | 复制内容正确，高亮位置精准 |
| **页面同步** | 连续模式 $\rightarrow$ 滚动至第三页 | 状态栏正确显示 "Page 3 of ..." |
| **缩放兼容性** | 连续模式 $\rightarrow$ 放大页面 $\rightarrow$ 滚动 | 所有页面尺寸同步缩放，间距保持一致 |
