# 实现计划：文档压缩功能 (底层实现与 UI 集成)

## 1. 需求背景
用户需要实现 PDF 文档的压缩与优化功能。该功能应允许用户选择不同的压缩策略（如：图像重压缩、删除未使用对象、移除元数据/注释等），并通过 UI 界面配置参数，最终生成一个体积更小的 PDF 文件。

目前项目已具备基础的接口定义 (`fpdf_ext_compression.h` 和 `pdfium_wrapper`)，但底层实现较为简单，且 UI 层仅为占位逻辑。

## 2. 涉及修改的文件列表

### 底层扩展 (`pdfium_extensions`)
- `pdfium_extensions/core/fpdf_ext_compression.cpp`: 实现具体的压缩算法、对象剔除和流优化逻辑。

### 包装层 (`pdfium_wrapper`)
- (无需修改) `pdfium_wrapper` 已提供 `Optimize` 和 `SaveWithCompression` 接口，且支持 `PDF_CompressOptions` 结构体。

### Qt 应用层 (`qt_app`)
- `qt_app/src/pdfdocument.h`: 更新压缩选项的定义，使 API 支持传递详细的配置参数而非简单的标志位。
- `qt_app/src/pdfdocument.cpp`: 实现调用包装层压缩接口的逻辑。
- `qt_app/src/mainwindow.cpp`: 实现压缩对话框的参数收集、调用触发及结果展示。

## 3. 具体执行步骤

### 步骤 1: 增强底层压缩实现 (`pdfium_extensions`)
**目标**: 使 `FPDF_OptimizeDocument` 真正能够根据 `FPDF_CompressOptions` 执行优化。

- **实现内容**:
    1. **流压缩增强**: 在 `CompressStream` 中，针对 `FPDF_COMPRESS_FLATE` 使用 `Z_BEST_COMPRESSION` 级别，并确保所有可压缩流（包括 XRef 流）都被处理。
    2. **图像优化**: 实现 `ProcessImages` 中的图像处理，除了 zlib 压缩，增加对图像分辨率的检查（基于 `image_dpi_threshold`），对超高分图像进行下采样（Downsampling）。
    3. **冗余剔除**: 
       - 实现 `RemoveUnusedObjects`：虽然 `FPDF_SaveAsCopy` 会自动剔除不可达对象，但在此步骤中可以显式清除文档中的损坏引用。
       - 实现元数据/注释/表单移除：遍历文档根字典及页面字典，根据 `options` 清除 `/Annots`, `/Form`, `/Info` 等项。
    4. **保存集成**: 确保 `FPDF_SaveWithCompression` 正确调用 `FPDF_SaveAsCopy` 以触发 PDFium 的内置线性化 (Linearization) 和对象流压缩。

**校验方式**: 使用一个包含大量图像和冗余元数据的 PDF，运行 `FPDF_SaveWithCompression`，对比优化前后文件的二进制大小。

- **完成情况**:
  - `FPDF_OptimizeDocument` 现按选项执行：每页图片经 `FPDF_LoadPage` 枚举（`CPDFPageFromFPDFPage`），按 `image_dpi_threshold`（默认 300 DPI）双线性下采样，其余图片流一律 Flate 重压缩。
  - 组件移除在**文档根字典 / trailer 字典上生效**（`/Outlines`、`/AcroForm`、`/Metadata`、`/Info` 均已在产物中消失）；页面 `/Annots` 需保证池中已修改对象不被删除、最终由 SaveAsCopy 写出（实测产物中已无 `/Annots`）。
  - `RemoveUnusedObjects` 仅作**可达性统计**（`objects_removed=8`：注释、书签项、书签树、表单字段、AcroForm、元数据、Info），**不删除池对象**——删除会导致保存时按源文件恢复原对象，使已移除的键“复活”。
  - `FPDF_SaveWithCompression` 调用 `FPDF_OptimizeDocument` 后以 `FPDF_SaveAsCopy(FPDF_NO_INCREMENTAL)` 重写全文档，统计原始/压缩后字节数与压缩比。
  - 校验：`compress_src.pdf`（15,962,018 B，2 页、2 张 1400×1900 未压缩 RGB 图、书签/注释/表单/XMP/Info）→ `compress_dst.pdf` **235,622 B（压缩比 0.0148）**，重开成功、双页渲染正常、`images_recompressed=2`。

### 步骤 2: 升级 `PdfDocument` 压缩 API
**目标**: 将底层 `PDF_CompressOptions` 映射到 Qt 层，允许 UI 传递详细配置。

- **修改内容**:
    1. 在 `PdfDocument` 中定义 `CompressOptions` 结构体，成员与 `pdfium_wrapper.h` 中的 `PDF_CompressOptions` 一一对应。
    2. 重构 `optimizeDocument` 和 `saveWithCompression` 函数签名，将 `CompressFlags` 参数替换为 `CompressOptions` 结构体。
    3. 在实现中，将 `PdfDocument::CompressOptions` 转换为 `PDF_CompressOptions` 并传递给 `m_interface`。

**校验方式**: 编译通过，且能够通过代码调用 `saveWithCompression` 并传递自定义的质量参数（如 `imageQuality`）。

- **完成情况**:
  - `qt_app/src/pdfdocument.h` 新增 `struct CompressOptions`（`flags`、`imageQuality`、`imageDpiThreshold`、`minImageDpi`、`fontSubsetThreshold`、`removeAnnotations/Forms/Bookmarks/Metadata`），默认 `flags=CompressFlate, imageQuality=90, imageDpiThreshold=300, minImageDpi=150, fontSubsetThreshold=80`；签名改为 `optimizeDocument(const CompressOptions&, const QString&)` 与 `saveWithCompression(const QString&, const CompressOptions&)`。
  - `qt_app/src/pdfdocument.cpp` 以匿名命名空间辅助函数 `toPdfCompressOptions()` 做 `CompressOptions → PDF_CompressOptions` 映射；`optimizeDocument` 将 `IPdfDocument::Optimize` 的 `PDF_OK`（`out_handle==nullptr`，原地优化）视为成功。
  - **顺带修复**：多个 `PdfDocument` 同时存活时，析构会各自 `PDF_DestroyLibrary()`+`unload()`，导致先销毁的实例卸载 dylib、后销毁实例的 `Release()` 段错误（退出码 139）。现通过文件静态 `QWeakPointer<LibraryState>` + 共享 `QSharedPointer` 引用计数，使包装库全局只初始化一次、仅在最后一个实例释放时卸载；`compress_qt` harness 实测退出码 0。
  - 校验：`compress_qt` harness（质量 60、移除注释+元数据、`CompressRemoveUnused`）→ 统计 `original=15962018 compressed=235982 ratio=0.0148 removed=3 images=2`，产物中 `/Annots`、`/Metadata`、`/Info` 全部为 0，重开 2 页成功；`removed=0/3` 由 `FPDF_COMPRESS_REMOVE_UNUSED` 标志位控制是否统计（SaveAsCopy 无论是否统计都会剔除不可达对象）。

### 步骤 3: 实现 UI 集成 (`MainWindow`)
**目标**: 完成从“用户配置 $\rightarrow$ 调用底层 $\rightarrow$ 显示统计”的闭环。

- **修改内容**:
    1. **对话框逻辑**: 修改 `MainWindow::saveOptimized`，将对话框中 `QCheckBox` 的勾选状态和可能的数值输入（如质量百分比）填充到 `PdfDocument::CompressOptions` 实例中。
    2. **异步调用**: 调用 `m_document->saveWithCompression(path, options)`。
    3. **结果回显**: 
       - 连接 `PdfDocument::saveCompressedFinished` 信号。
       - 在回调中调用 `m_document->getLastCompressStats()` 获取压缩统计数据（原始大小、压缩后大小、压缩比）。
       - 使用 `QMessageBox` 向用户展示详细的压缩报告。

**校验方式**: 
1. 点击“优化 PDF”按钮 $\rightarrow$ 弹出对话框 $\rightarrow$ 勾选“重压缩图像”并点击确定。
2. 验证状态栏显示“Optimizing PDF...”。
3. 验证最终弹出对话框，显示正确的压缩比例（例如 "Original: 1MB $\rightarrow$ Compressed: 600KB, Ratio: 60%"）。

- **完成情况**:
  - `MainWindow::saveOptimized` 的对话框现收集 Flate/图像重压缩勾选 + 图像质量 `QSpinBox`(1-100) + 移除注释/表单/书签/元数据勾选，全部填入 `PdfDocument::CompressOptions` 后调用异步 `saveWithCompression(fileName, options)`；始终启用 `CompressRemoveUnused` 以在统计中报告被剔除对象数。
  - 回调 `MainWindow::onSaveCompressedFinished` 通过 `getLastCompressStats()` 弹出 `QMessageBox`，展示原始/压缩后字节数、压缩比、剔除对象数、重压缩图像数与字体子集数。
  - 校验：`pdf_reader` 编译通过；`compress_qt` harness 全链路（加载→异步保存→信号→统计→重开验证）退出码 0；回归套件 `bookmarks_test / selection_test / context_menu_test / continuous_test / wheel_iso / wheel_widget / two_docs_test / mainwin_test` 全部 PASS。

## 4. 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **极简压缩** | 仅勾选 Flate 压缩 $\rightarrow$ 保存 | 文件体积略微下降，文档内容完全一致 |
| **深度压缩** | 勾选图像重压缩 + 移除元数据 + 移除注释 $\rightarrow$ 保存 | 文件体积显著下降，且 PDF 中不再包含注释/元数据 |
| **质量对比** | 分别设置 Low/High 图像质量 $\rightarrow$ 保存两个副本 | Low 质量的文件体积更小，但图像可见度下降 |
| **异常处理** | 选择一个只读文件进行优化保存 | 触发 `saveCompressedFinished(false, ...)` 并在 UI 显示错误信息 |

## 5. 新增需求：压缩进度报告

### 涉及修改的文件列表
- `pdfium_extensions/public/fpdf_ext_compression.h`: 定义进度回调函数类型。
- `pdfium_extensions/core/fpdf_ext_compression.cpp`: 在压缩关键节点触发回调。
- `pdfium_wrapper/include/pdfium_wrapper.h`: 将回调传递至包装接口。
- `pdfium_wrapper/src/pdfium_wrapper.cpp`: 实现回调桥接。
- `qt_app/src/pdfdocument.h`: 新增 `compressionProgress` 信号。
- `qt_app/src/pdfdocument.cpp`: 将 C 回调转换为 Qt 信号。
- `qt_app/src/mainwindow.cpp`: 集成 `QProgressDialog` 显示进度。

### 执行步骤

#### 步骤 1: 底层回调机制
- **定义**: 在 `fpdf_ext_compression.h` 中定义 `typedef void (*FPDF_CompressProgressCallback)(int progress, const char* status, void* user_data);`。
- **接口更新**: `FPDF_OptimizeDocument` 和 `FPDF_SaveWithCompression` 增加回调参数。
- **触发逻辑**: 
    - 0%: 开始优化。
    - 1%~60%: 图像处理阶段，每处理一页更新一次 `(currentPage / totalPages) * 60`。
    - 70%: 完成对象剔除。
    - 90%: 开始执行 `FPDF_SaveAsCopy`（最终写盘）。
    - 100%: 保存完成。

#### 步骤 2: 包装层透传
- 更新 `IPdfDocument::Optimize` 和 `IPdfDocument::SaveWithCompression` 接口以接受回调函数。
- 在 `pdfium_wrapper.cpp` 中实现简单的指针透传，确保 C 风格回调能正确传回给调用者。

#### 步骤 3: Qt 信号桥接
- 在 `PdfDocument` 中声明信号 `void compressionProgress(int percentage, const QString& status);`。
- 在 `saveWithCompression` 的异步任务中，定义一个静态回调函数，通过 `user_data` 传递 `PdfDocument` 指针，从而在回调中 `emit compressionProgress(...)`。

#### 步骤 4: UI 进度展示
- 修改 `MainWindow::saveOptimized`：
    - 创建一个模态的 `QProgressDialog`。
    - 将 `PdfDocument::compressionProgress` 信号连接至 `QProgressDialog::setValue`。
    - 在压缩完成后关闭对话框。

### 校验方式
- **视觉验证**: 启动压缩 $\rightarrow$ 观察进度条平滑增长 $\rightarrow$ 状态文字在“处理图像...”、“剔除冗余...”、“保存文件...”之间切换。
- **逻辑验证**: 确保在大文件（多页）压缩时，进度条能够真实反映处理进度而非直接跳至 100%。

### 完成情况
- **步骤 1（底层回调）**：`fpdf_ext_compression.h` 新增 `FPDF_CompressProgressCallback(progress, status, user_data)`；`FPDF_OptimizeDocument` 与 `FPDF_SaveWithCompression` 增加回调与 `user_data` 参数。进度点严格按计划：0%“开始优化...” → 图片阶段每页 `(i+1)/total*60`（实测 2 页 → 30%/60%）→ 70%“剔除冗余...” → 90%“保存文件...” → 100%“保存完成”（状态串为 UTF-8 常量，`ReportProgress` 统一钳制 0..100）。
- **步骤 2（包装层透传）**：`pdfium_wrapper.h` 定义 `PDF_CompressProgressCallback`，`IPdfDocument::Optimize`/`SaveWithCompression` 新增回调参数；`pdfium_wrapper.cpp` 的 extern 声明与实际调用同步更新（`FPDF_OptimizeDocument(doc, opts, cb, user, nullptr)`）。
- **步骤 3（Qt 信号桥接）**：`PdfDocument` 新增 `compressionProgress(int, QString)`；`pdfdocument.cpp` 静态 `CompressionProgressThunk` 以 `this` 为 `user_data`，把 C 回调转为 `emit compressionProgress`（`QString::fromUtf8`）。
- **步骤 4（UI 展示）**：`MainWindow::saveOptimized` 创建 `QProgressDialog`（WindowModal、无取消键、`WA_DeleteOnClose`），`compressionProgress → setValue/setLabelText`；`onSaveCompressedFinished` 中关闭进度框后弹出统计报告。
- **校验**：
  - C 层 `compress_iso`：`0% 开始优化... → 30% 处理图像... → 60% 处理图像... → 70% 剔除冗余... → 90% 保存文件... → 100% 保存完成`，`last_progress=100`，统计/压缩比与回归基线一致（ratio 0.0148、removed=8、images=2），重开双页渲染正常，退出码 0。
  - Qt 层 `compress_qt`：`compressionProgress` 序列 0/30/60/70/90/100、起止复查通过，保存产物 235,982 B 重开 2 页成功，错误路径（不可写目录）正确走失败信号，退出码 0。
  - 回归套件全部 PASS：`bookmarks/selection/context_menu/continuous/wheel_iso/wheel_widget/two_docs/mainwin_test`。

