# Implementation Plan: PDF Bookmarks (Outline) Navigation

## 1. 涉及修改的文件列表

- `pdfium_wrapper/include/pdfium_wrapper.h`: 定义 `IPdfOutline` 接口。
- `pdfium_wrapper/src/pdfium_wrapper.cpp`: 实现 `PdfOutlineImpl` 类。
- `qt_app/src/pdfdocument.h`: 添加获取书签根节点 `getOutlineRoot()` 的接口。
- `qt_app/src/pdfdocument.cpp`: 实现书签根节点的获取逻辑。
- `qt_app/src/mainwindow.h`: 声明书签侧边栏组件（`QDockWidget`, `QTreeView`, `QStandardItemModel`）。
- `qt_app/src/mainwindow.cpp`: 实现书签面板的创建、书签树的递归构建以及点击跳转逻辑。

## 2. 具体执行步骤

### 步骤 1: 在 Wrapper 层实现书签接口
**目标**: 封装 PDFium 的书签遍历 API。

1. **定义 `IPdfOutline` 接口**:
   - `virtual QString GetTitle() = 0;`
   - `virtual IPdfOutline* GetFirstChild() = 0;`
   - `virtual IPdfOutline* GetNextSibling() = 0;`
   - `virtual int GetDestinationPage() = 0;`
   - `virtual void Release() = 0;`
2. **实现 `PdfOutlineImpl`**:
   - 使用 `FPDF_OutlineGetTitle` 获取标题。
   - 使用 `FPDF_OutlineFirstChild` 和 `FPDF_OutlineNextSibling` 进行树遍历。
   - 使用 `FPDF_OutlineGetDestination` $\rightarrow$ `FPDF_DestinationGetPageIndex` 获取目标页码。
3. **在 `IPdfDocument` 中添加接口**:
   - `virtual IPdfOutline* GetOutlineRoot() = 0;` $\rightarrow$ 调用 `FPDF_LoadOutlineRoot`。

**校验方式**: 使用调试器验证 `GetOutlineRoot` 能正确返回根节点，且可以通过 `GetFirstChild` 遍历到子节点。

### 步骤 2: 在 `PdfDocument` 中构建转发逻辑
**目标**: 为 UI 层提供获取书签结构的入口。

1. **实现 `getOutlineRoot()`**:
   - 调用 Wrapper 的 `GetOutlineRoot()` 并返回。
2. **(可选) 实现树形数据转换**:
   - 提供一个辅助方法将 `IPdfOutline` 递归转换为 Qt 的 `QStandardItemModel` 结构。

**校验方式**: 打印书签根节点的标题，确认能正确读取到文档的书签内容。

### 步骤 3: 实现 `MainWindow` 书签侧边栏 UI
**目标**: 提供可交互的书签浏览界面。

1. **创建侧边栏**:
   - 在 `MainWindow` 中添加一个 `QDockWidget` (名称: "Bookmarks")。
   - 在 DockWidget 中放入 `QTreeView`。
2. **构建书签树**:
   - 当文档加载完成后，递归遍历 `IPdfOutline` 树。
   - 为每个书签创建 `QStandardItem`，将其标题设为书签名称，将页码存入 `Qt::UserRole`。
   - 将其添加到 `QStandardItemModel` 中并设置给 `QTreeView`。
3. **实现跳转逻辑**:
   - 连接 `QTreeView::clicked` 信号。
   - 从选中的 Item 中提取页码 $\rightarrow$ 调用 `m_viewer->setPage(pageIndex)`。

**校验方式**:
- 运行程序 $\rightarrow$ 打开带书签的 PDF $\rightarrow$ 检查侧边栏是否显示完整的书签树。
- 点击书签 $\rightarrow$ 验证视图是否立即跳转到对应的页面。

### 步骤 4: 细节优化与鲁棒性处理
**目标**: 提升用户体验和稳定性。

1. **空书签处理**: 如果文档没有书签，在侧边栏显示 "No bookmarks available"。
2. **内存管理**: 确保递归遍历过程中创建的 `IPdfOutline` 对象在构建完模型后被正确 `Release()`。
3. **UI 同步**: 每次加载新文档时，清空并重新构建书签树。

**校验方式**:
- 测试无书签的 PDF 文件，验证界面显示正常。
- 使用内存检测工具确认没有 `IPdfOutline` 对象的内存泄漏。
