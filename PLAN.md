## 11. 新增需求：PDF Reader 全流程 Skia 渲染 (Full-Pipeline Skia Rendering)

### 需求背景
目前项目中的 Skia 集成分为两个阶段：一是底层 PDFium 内部使用 Skia 栅格化（`pdf_use_skia=true`），二是前端使用 `SkiaPdfViewerWidget` 通过 GPU 绘制位图。为了实现“全流程渲染”，需要将这两者结合，使 `SkiaPdfViewerWidget` 成为一个从底层栅格化到顶层显示的完整 Skia 链路。同时，`PdfViewerWidget` 需保留其原有的 CPU 渲染效果，作为兼容性回退方案。

### 涉及修改的文件列表
- `build_scripts/build_pdfium.py`: 实现 `pdf_use_skia` 参数的自动化配置。
- `qt_app/src/abstractpdfviewer.h` (新): 定义统一的视图接口，消除 CPU/GPU 视图在 `MainWindow` 中的冗余逻辑。
- `qt_app/src/pdfviewerwidget.h/cpp`: 适配基类接口，维持原有的 CPU 栅格化 $\rightarrow$ QPainter 绘制流程。
- `qt_app/src/skiapdfviewerwidget.h/cpp`: 适配基类接口，实现“内部 Skia 栅格化 $\rightarrow$ GPU 绘制”的全链路路径。
- `qt_app/src/mainwindow.h/cpp`: 重构视图管理，默认使用 `SkiaPdfViewerWidget`。

### 具体执行步骤

#### 步骤 1: 统一底层构建管线 (`build_pdfium.py`)
- **实现内容**:
    1. 在 `BuildConfig` 中增加 `use_skia` 字段。
    2. 在 `gn_args` 中将 `pdf_use_skia` 设置为动态值（由 `--use-skia` 参数控制）。
    3. 构建 PDFium 时开启 `pdf_use_skia = true`。这意味着所有 Viewer 共享的 `libpdfium` 内部将使用 Skia 进行高性能栅格化。
- **校验方式**: 执行构建脚本，检查生成的 `args.gn` 文件中 `pdf_use_skia = true`。

#### 步骤 2: 构建多态视图架构 (`AbstractPdfViewer`)
- **实现内容**:
    1. 实现 `AbstractPdfViewer` 抽象基类，定义 `setDocument`, `setPage`, `setZoom`, `renderCurrentPage` 等纯虚接口。
    2. 让 `PdfViewerWidget` 和 `SkiaPdfViewerWidget` 继承自 `AbstractPdfViewer`。
    3. 统一信号定义（`pageChanged`, `zoomChanged` 等）到基类中。
- **校验方式**: 编译通过，确保两个 Viewer 能够通过 `AbstractPdfViewer*` 指针进行操作。

#### 步骤 3: 重构 `MainWindow` 为全流程模式
- **实现内容**:
    1. **删除冗余实例**: 移除 `m_gpuViewer` 和 `m_gpuActive` 标志位。
    2. **统一视图变量**: 使用 `AbstractPdfViewer* m_viewer` 指向当前的视图实例。
    3. **默认 GPU 路径**: 在 `MainWindow` 初始化时，默认创建 `SkiaPdfViewerWidget`，从而激活“内部 Skia $\rightarrow$ GPU 显示”的全链路效果。
    4. **简化指令分发**: 删除所有 `if (m_gpuActive)` 分支，所有导航和缩放指令直接调用 `m_viewer->...`。
    5. **保留回退能力**: 在“视图”菜单中保留一个“禁用 GPU 加速”的选项，点击后动态将 `m_viewer` 替换为 `PdfViewerWidget`。此时，虽然底层库仍是 Skia 编译版，但显示链路回退为原有的 CPU QPainter 模式。
- **校验方式**: 运行程序 $\rightarrow$ 确认启动即进入 GPU 渲染模式 $\rightarrow$ 验证基础操作（翻页/缩 uma 响应迅速且正确。

#### 步骤 4: 全链路渲染质量验证
- **实现内容**:
    1. 使用构建了 `pdf_use_skia=true` 的 PDFium 库。
    2. 在 `SkiaPdfViewerWidget` 中观察渲染产物。
- **校验方式**: 
    - **视觉对比**: 比较 `SkiaPdfViewerWidget` (全链路) 与 `PdfViewerWidget` (CPU 绘制) 的渲染细节，验证前者的抗锯齿和路径平滑度明显更优。
    - **功能回退验证**: 切换到 `PdfViewerWidget` $\rightarrow$ 验证其依然维持原有的 CPU 渲染表现，无视觉异常。
    - **性能压测**: 在大文件/高缩放倍数下，验证从 `PDFium-Skia` $\rightarrow$ `Skia-GPU` 的全链路延迟是否在可接受范围内（目标 60FPS）。

### 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **构建链路** | `build_pdfium.py --use-skia` $\rightarrow$ `cmake` $\rightarrow$ `build` | 编译通过，产出包含 Skia 支持的 PDFium 库 |
| **默认行为** | 启动应用 $\rightarrow$ 加载文档 | 默认使用 `SkiaPdfViewerWidget`，激活全链路渲染，质量达到 Chrome 级别 |
| **指令一致性** | 操作工具栏（缩放/翻页/旋转） | 所有的视图操作通过基类接口正确分发，无论在哪个 Viewer 下均正常 |
| **动态回退** | 切换到“禁用 GPU 加速” | 视图平滑切换至 `PdfViewerWidget`，维持原有的 CPU 渲染效果，状态同步正确 |
| **压力测试** | 快速连续滚动 100 页 | 界面无卡顿，内存占用稳定，无渲染闪烁 |

## 12. 详细实现计划：pdfreader 将 Qt 中的 SkCanvas 传入到 PDFium 中，直接在 SkCanvas 上渲染正文元素

### 1. 需求背景
目前的 GPU 渲染路径为：`PDFium (内部 Skia) -> 栅格化为位图 (QImage) -> 上传到 GPU 纹理 -> 使用 Skia 绘制纹理到屏幕`。
该路径存在两次冗余：一是 PDFium 内部已使用 Skia，但最终输出的是像素阵列；二是前端再次使用 Skia 将像素阵列绘制到屏幕。
目标是实现真正的“直通”渲染：`PDFium (内部 Skia) -> 直接在 Qt 提供的 SkCanvas 上执行绘制指令 -> 屏幕`。

### 2. 涉及修改的文件列表

#### PDFium 扩展层
- `pdfium_extensions/core/fpdf_ext_skia.h` (新): 定义 `FPDF_RenderPageToCanvas` 接口。
- `pdfium_extensions/core/fpdf_ext_skia.cpp` (新): 实现调用 PDFium 内部 `CPDF_PageRenderContext` 的逻辑。

#### 包装层 (pdfium_wrapper)
- `pdfium_wrapper/include/pdfium_wrapper.h`: 在 `IPdfPage` 接口中增加 `RenderToCanvas` 虚函数。
- `pdfium_wrapper/src/pdfium_wrapper_impl.cpp`: 实现 `IPdfPage::RenderToCanvas`，调用上述扩展接口。

#### Qt 应用层 (qt_app)
- `qt_app/src/skiapdfviewerwidget.cpp`: 修改 `drawPageOnCanvas` 函数，由绘制缓存位图改为直接调用 `RenderToCanvas`。

### 3. 具体执行步骤

#### 步骤 1: 实现 PDFium 内部直接渲染扩展
**目标**: 在 PDFium 内部创建一个能够接受 `SkCanvas*` 并执行页面渲染的 C-API。
- **实现内容**: 
    1. 参考 `fx_skia_device_embeddertest.cpp` 中的 `RenderPageToSkCanvas` 实现。
    2. 创建 `CPDF_PageRenderContext`。
    3. 使用 `CFX_RenderDevice::CreateForSkiaCanvas(canvas)` 创建渲染设备。
    4. 调用 `CPDFSDK_RenderPageWithContext` 执行渲染。
- **校验方式**: 编写简单的单元测试或使用 `skia_gl_harness` 验证 `FPDF_RenderPageToCanvas` 能正确在 Canvas 上产生绘制指令。

#### 步骤 2: 扩展 Wrapper 接口
**目标**: 将底层 Skia 渲染能力暴露给 Qt 应用。
- **实现内容**:
    1. 在 `IPdfPage` 中定义 `virtual bool RenderToCanvas(SkCanvas* canvas, int width, int height, int rotation, int flags) = 0;`。
    2. 在 `PdfPageImpl` 中实现该函数，负责将参数传递给 `FPDF_RenderPageToCanvas`。
- **校验方式**: 编译 `pdfium_wrapper` 确保无错误。

#### 步骤 3: 重构 `SkiaPdfViewerWidget` 的绘制链路
**目标**: 消除位图缓存，实现直接绘制。
- **实现内容**:
    1. 修改 `drawPageOnCanvas`：删除 `m_renderedPages` 缓存查询逻辑 $\rightarrow$ 直接调用 `m_document->getPage(pageIndex)->RenderToCanvas(canvas, ...)`。
    2. **优化缓存逻辑**: 由于不再需要异步渲染位图，可以移除 `requestRender` 异步循环、`m_pendingPages` 集合以及 `m_renderFutures` 管理逻辑。
    3. **坐标同步**: 确保传递给 `RenderToCanvas` 的 `width` 和 `height` 与 `widgetRect` 完全一致。
- **校验方式**: 运行应用 $\rightarrow$ 观察页面显示 $\rightarrow$ 确认不再有 "Rendering page..." 的占位符，页面瞬间加载。

#### 步骤 4: 性能与质量终验
**目标**: 验证全链路 Skia 渲染的优势。
- **实现内容**:
    1. 对比 `PdfViewerWidget` (CPU) 和 `SkiaPdfViewerWidget` (Direct GPU) 的文字边缘锐利度和路径平滑度。
    2. 测试快速缩放（Zoom）时的响应速度，验证是否消除了位图更新的闪烁感。
- **校验方式**: 
    - 视觉确认：文字抗锯齿效果达到 Chrome 级别。
    - 帧率确认：滚动时维持稳定 60FPS。
