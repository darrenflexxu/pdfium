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

## 6. 新增需求：PDFium Python 扩展

### 需求背景
需要为 `pdfium_wrapper` 提供 Python 绑定，使得开发者可以通过 Python 脚本调用 PDF 文档的加载、渲染、文本提取、结构分析及压缩等功能。由于 `pdfium_wrapper` 基于 COM-style 接口（vtable），直接使用 `ctypes` 过于繁琐，拟采用 `pybind11` 构建高性能的编译型 Python 扩展模块。

### 涉及修改的文件列表
- `CMakeLists.txt`: 配置 `pybind11` 依赖并添加 `pdfium_python` 子目录。
- `pdfium_python/CMakeLists.txt` (新): 定义 Python 模块构建目标。
- `pdfium_python/bindings.cpp` (新): 实现 C++ 接口到 Python 类的映射逻辑。
- `pdfium_python/test_pdfium.py` (新): 用于功能验证的 Python 测试脚本。

### 具体执行步骤

#### 步骤 1: 集成 pybind11 依赖
- 在根目录 `CMakeLists.txt` 中使用 `FetchContent` 引入 `pybind11`。
- 配置 Python 解释器路径，确保能够正确生成 `.so` (Linux/macOS) 或 `.pyd` (Windows) 文件。

#### 步骤 2: 实现 Python 类绑定 (`bindings.cpp`)
- **生命周期管理**: 为所有接口 (`IPdfDocument`, `IPdfPage` 等) 定义 `std::shared_ptr` 包装器，并自定义 deleter 调用 `Release()`，确保 Python 对象被回收时能正确释放 PDFium 句柄。
- **接口映射**:
    - `IPdfDocument` $\rightarrow$ `PdfDocument`: 映射 `GetPageCount`, `GetPage`, `GetDocumentStructure`, `Optimize`, `SaveWithCompression` 等。
    - `IPdfPage` $\rightarrow$ `PdfPage`: 映射 `Render`, `GetText`, `SearchText`, `GetCharBox`, `GetPageElement` 等。
    - `IPdfElement` $\rightarrow$ `PdfElement`: 映射 `GetType`, `GetBounds`, `GetElementText` 等。
    - `IPdfOutline` $\rightarrow$ `PdfOutline`: 映射 `GetTitle`, `GetFirstChild`, `GetNextSibling`, `GetDestinationPage`。
- **工厂函数**: 封装 `PDF_CreateDocument` 为 Python 类的构造函数或静态方法 `PdfDocument.create(path, password)`。
- **库初始化**: 在 `PYBIND11_EMBEDDED_MODULE` 或模块初始化段调用 `PDF_InitLibrary`，并在模块卸载时调用 `PDF_DestroyLibrary`。

#### 步骤 3: 构建系统集成
- 创建 `pdfium_python/CMakeLists.txt`，使用 `pybind11_add_module` 定义目标。
- 链接 `pdfium_wrapper` 库。
- 确保构建产物被放置在 Python 可导入的路径下。

#### 步骤 4: API 适配与优化
- **类型转换**: 将 `PDF_CompressOptions` 等 C 结构体映射为 Python 简单的 `dict` 或 `dataclass`。
- **内存安全**: 处理 `PDF_FreeString` 等内存释放函数，确保 Python 端的 `str` 在获取后无需用户手动释放。

### 校验方式
- **编译验证**: 执行 `cmake --build . --target pdfium_python` 成功生成模块文件。
- **功能验证**: 编写 `test_pdfium.py` 执行以下流程：
    1. `import pdfium` $\rightarrow$ `doc = pdfium.PdfDocument.create("test.pdf")`。
    2. 验证 `doc.get_page_count()` 与预期一致。
    3. 获取第一页 `page = doc.get_page(0)` $\rightarrow$ 调用 `page.get_text()` 并打印。
    4. 调用 `doc.save_with_compression("out.pdf", options)` $\rightarrow$ 验证文件生成且可打开。
- **内存泄漏测试**: 在循环中创建/销毁大量 `PdfDocument` 对象，观察内存占用是否稳定（验证 `Release()` 是否被正确触发）。

### 完成情况
- **步骤 1（pybind11 依赖）**：根 `CMakeLists.txt` 新增 `PDFIUM_ENABLE_PYTHON` 选项，`PDFIUM_ENABLE_PYTHON=ON` 时挂载 `pdfium_python` 子目录；`pdfium_python/CMakeLists.txt` 先 `find_package(pybind11 CONFIG QUIET)`（可用 `-Dpybind11_DIR` 指定，实测 `pybind11 3.1.0` + Homebrew Python 3.14），未找到时经 `FetchContent` 拉取 `pybind11 v3.1.0` 兜底。
- **步骤 2（类绑定 `bindings.cpp`）**：
  - 生命周期：所有接口以 `std::shared_ptr` + 自定义 deleter（`Release()`）持有，Python 对象回收即释放 PDFium 句柄；工厂 `PdfDocument.create(path, password="")` 封装 `PDF_CreateDocument`。
  - 映射：`PdfDocument`（get_page_count/get_page/get_document_structure/get_outline_root/optimize/save_with_compression/get_last_compress_stats）、`PdfPage`（render→BGRA bytes、get_text、search_text、get_char_count/get_char_unicode/get_char_box、count_page_elements/get_page_element/find_elements_by_type）、`PdfElement`（type/bounds/text）、`PdfOutline`（title/first_child/next_sibling/destination_page）。
  - 初始化：模块加载调用 `PDF_InitLibrary()`（失败抛异常），`Py_AtExit` 注册 `PDF_DestroyLibrary()` 于解释器退出。
- **步骤 3（构建集成）**：`pybind11_add_module(pdfium_python)` 链接 `pdfium_wrapper`，输出重命名为 `pdfium`（避让根目录 `pdfium` INTERFACE 目标），产物 `build_release/bin/pdfium.cpython-314-darwin.so`（含指向 `bin/` 的 LC_RPATH，可被 `python` 直接 `import`）。
- **步骤 4（类型转换与内存安全）**：`CompressOptions` 绑定为 Python 类（默认值对齐 `FPDF_CompressOptionsInit`）；模块级常量暴露压缩/渲染/搜索/元素标志位；`GetText/GetElementText/GetTitle` 返回的分配字符串立即 `PDF_FreeString` 后转 `std::string`。
- **校验**：
  - `cmake --build ... --target pdfium_python` 成功；`import pdfium` 通过。
  - `test_pdfium.py`（604 B 手工生成 PDF）：pages=1、structure 完整、`get_text()` 29 字符、`render()` 60000 B(100×150×4)、`search_text("PDFium")` 1 命中、`optimize=True`、`save_with_compression` 成功且统计 `compressed_size` 与文件实际字节一致、输出重开页数一致、60 次创建/销毁生命周期压力测试通过，退出码 0。
  - 说明：`find_elements_by_type(ELEMENT_TEXT)` 因元素提取扩展依赖 `BUILD_PDFIUM_FROM_SOURCE` 而定，若无源码构建可能返回空（接口调用本身正常）。

## 7. 新增需求：交互式文本选择 (Interactive Text Selection)

### 需求背景
目前的文本选择仅支持简单的框选，且无法精确提取选区内的文本内容。用户需要实现类似于浏览器的交互式文本选择：将用户在视图上的拖拽选区映射为 PDF 坐标，并精确提取该区域内的文本，同时提供紧贴文字边缘的高亮视觉反馈。

### 涉及修改的文件列表
- `qt_app/src/pdfdocument.h`: 定义 `extractTextInRect` 接口。
- `qt_app/src/pdfdocument.cpp`: 实现基于元素边界过滤的文本提取逻辑。
- `qt_app/src/pdfviewerwidget.cpp`: 重构文本选择流程，将视图选择区域映射为 PDF 坐标并提取文本。

### 具体执行步骤与校验方式

#### 步骤 1: 实现区域文本提取逻辑 (`PdfDocument`)
- **实现内容**:
    1. 新增接口 `QString extractTextInRect(int pageIndex, const QRectF& rect) const`。
    2. 调用 `findElementsByType(pageIndex, PDF_ElementType::Text)` 获取所有文本元素。
    3. 过滤出与 `rect` 相交的元素，并按阅读顺序（Y 轴从上到下，X 轴从左到右）排序。
    4. 拼接元素文本，并在 Y 坐标显著变化时插入 `\n` 换行符。
- **校验方式**: 编写单元测试或小型 harness，传入一个覆盖部分文本的矩形，验证输出字符串仅包含该区域内的文本且顺序正确。

#### 步骤 2: 重构 `PdfViewerWidget` 的选择流程
- **实现内容**:
    1. 修改 `mouseReleaseEvent`，使用 `mapToPage` 将视图选区转换为 PDF 坐标矩形。
    2. 调用 `m_document->extractTextInRect(m_currentPage, selectionRect)` 提取文本。
    3. 将提取到的文本通过 `textSelected` 信号发出，并同步至系统剪贴板。
- **校验方式**: 运行程序 $\rightarrow$ 拖拽选择页面上的一段特定文字 $\rightarrow$ 验证剪贴板内容与选区完全一致，且不包含页面的其余部分。

#### 步骤 3: 增强视觉反馈与交互
- **实现内容**:
    1. 优化 `paintEvent`：不再绘制简单的蓝色半透明矩形，而是基于提取出的文本元素边界绘制高亮区域，使高亮紧贴文字边缘。
    2. 快捷键同步：确保 `Ctrl+C` 能够触发上述提取流程并复制选中文本。
- **校验方式**: 
1. 视觉验证：观察选中文字时的高亮区域是否精准贴合文字。
     2. 功能验证：使用 `Ctrl+C` 快捷键验证复制内容正确。

### 完成情况
- **步骤 1（区域文本提取 `extractTextInRect`）**：`PdfDocument` 新增 `QString extractTextInRect(int pageIndex, const QRectF& rect) const`。因元素提取接口（`findElementsByType`）依赖 `BUILD_PDFIUM_FROM_SOURCE`（本环境返回空，见第 6 节说明），实现以**字符图（charMap）为权威文本来源**做等价过滤：取与 `rect` 相交的字符（charMap 已按阅读顺序排序：行↘、行内从左至右），同行为**连续字符**，行间插入 `\n`，水平间距超过字形高度 45% 时插入空格（词/分栏分隔）；`rect` 与字符框同一坐标系（y-up、指向 `deviceToPage`/`mapToPage`）。
- **步骤 2（重建视图选择流程）**：`PdfViewerWidget` 在按下/拖动时同步维护 `m_boxSelStart/m_boxSelEnd`；松开时经 `selectedText()` 统一取文本——**普通拖动**按计划使用 `mapToPage` 将视图选区映射为 PDF 坐标矩形 `widgetToPageRect()` $\rightarrow$ `extractTextInRect` 提取并经 `textSelected` 发出 + 写入剪贴板；**双击/三击**（词/段）与**无文本页回退**继续走精确的字符区间路径（`textForRange`）。
- **步骤 3（高亮与快捷键）**：
  - 高亮沿用已有的**逐字符贴合文字边缘**的线合并绘制（比“简单半透明矩形”更精细）；无文本页保留虚线框 overlay。
  - `Ctrl+C` 统一走 `selectedText()`（矩形/区间任一路径），无选中时回退整页；`Ctrl+A` 全选时清除残留拖拽矩形，避免 `selectedText()` 误用旧矩形（harness 暴露此 bug 已修复）。
- **校验**：新建 `rect_harness`（offscreen Qt 应用，3 行文本 PDF `sel_3lines.pdf`），全部 12 项通过：
  - 文档层：每行精确匹配、两行矩形拼接 `'行0\n行1'`、整页矩形三段文本、半行矩形只含前半段、页外矩形为空。
  - 视图层：跨行 0 拖拽 → `textSelected('First line of testing')`、剪贴板一致；`Ctrl+C` 重复制同一选区；`Ctrl+A`+`Ctrl+C` 复制整页；退出码 0。

## 8. 新增需求：跨端 GPU 加速渲染 (Cross-Platform GPU Acceleration via Skia)

### 需求背景
当前 PDF 渲染依赖于 CPU 栅格化生成 `QImage`，在移动端（iOS/Android）性能较差且不支持高质量的实时缩放和平滑滚动。拟引入 Skia 绘图引擎，利用 GPU 加速（Metal/Vulkan/OpenGL）实现高性能渲染，并将渲染管线升级为 `PDFium $\rightarrow$ Skia $\rightarrow$ Screen`。

### 涉及修改的文件列表
- `CMakeLists.txt`: 配置 Skia 依赖、跨平台库路径及硬件加速标志。
- `qt_app/src/pdfviewerwidget.h`: 将基类由 `QWidget` 修改为 `QOpenGLWidget`，引入 Skia 上下文管理。
- `qt_app/src/pdfviewerwidget.cpp`: 实现 `initializeGL`, `resizeGL`, `paintGL` 逻辑。
- `qt_app/src/pdfdocument.cpp`: 优化图像数据传递，确保 bitmap 能高效转换为 `SkImage`。

### 具体执行步骤

#### 步骤 1: Skia 构建与依赖集成
- **构建**: 为 Android (NDK) 和 iOS (Xcode Toolchain) 分别交叉编译 Skia 静态库。
- **CMake 配置**:
    1. 定义 `SKIA_DIR` 变量。
    2. 引入 Skia 头文件路径。
    3. 链接 `libskia.a` 及其平台依赖（iOS: Metal/QuartzCore; Android: EGL/GLESv2/Vulkan）。
- **校验方式**: 执行 `cmake` 成功找到 Skia 库且能够通过链接阶段。

#### 步骤 2: 实现 GPU 加速视图组件 (`SkiaPdfViewerWidget`)
- **上下文初始化**:
    1. 在 `initializeGL` 中通过 `GrGLMakeNativeInterface()` 获取 OpenGL 接口。
    2. 初始化 `GrDirectContext` (Skia GPU Context)。
- **Surface 管理**:
    1. 在 `resizeGL` 中获取当前 Framebuffer ID 和格式。
    2. 创建 `GrBackendRenderTarget` 并构建 `SkSurface`。
- **绘制循环 (`paintGL`)**:
    1. 清除背景。
    2. 遍历可见页面 $\rightarrow$ 从 `PdfDocument` 获取渲染好的 bitmap $\rightarrow$ 封装为 `SkImage`。
    3. 应用变换矩阵（缩放、平移）调用 `canvas->drawImage`。
    4. 使用 `SkPaint` 绘制高精度的矢量叠加层（如文本选区、注释）。
    5. 执行 `m_skiaContext->flush()` 提交 GPU 指令。
- **校验方式**: 运行程序 $\rightarrow$ 验证页面能够正确显示 $\rightarrow$ 验证缩放和滚动是否达到 60FPS 的流畅度。

#### 步骤 3: 跨平台适配与性能优化
- **后端适配**:
    1. **iOS**: 配置 `skia_use_metal=true`，通过 Metal Backend 提升渲染效能。
    2. **Android**: 配置 Vulkan/OpenGL ES 后端，处理不同设备间的 DPI 缩放。
- **纹理缓存**:
    1. 实现 `SkImage` 缓存机制，避免每帧重复创建纹理。
    2. 根据缩放级别分级缓存，减少 PDFium 的重复重绘次数。
- **校验方式**: 在实际 iOS/Android 设备上运行 $\rightarrow$ 验证内存占用稳定 $\rightarrow$ 验证在极端缩放（极小/极大）时无明显卡顿。

### 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **渲染正确性** | 打开复杂 PDF 文档 | 页面显示与原版一致，无花屏或缺失 |
| **交互流畅度** | 快速缩放或连续滚动 | 帧率稳定在 60FPS，无可见掉帧 |
| **平台兼容性** | 在 iOS (Metal) 和 Android (Vulkan) 运行 | 均能正常启动且 GPU 硬件加速生效 |
| **内存压力** | 连续加载多个大文档并快速翻页 | 内存增长受控，`SkImage` 缓存正确释放 |
