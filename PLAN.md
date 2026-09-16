## 10. 新增需求：PDFium 内部启用 Skia 渲染 (Enable Internal PDFium Skia Rendering)

### 需求背景
目前 `SkiaPdfViewerWidget` 实现的是“GPU 加速显示”，即：PDFium (CPU 渲染) $\rightarrow$ `QImage` $\rightarrow$ Skia (GPU 绘制)。这仅仅是利用 GPU 提高了图像的绘制和缩放速度，但 PDF 的实际栅格化（Rasterization）依然由 PDFium 的 CPU 渲染器完成。

真正的“PDFium 使用 Skia 渲染”是指在编译 PDFium 时启用 `pdf_use_skia = true` 选项。启用后，PDFium 内部将使用 Skia 的绘制指令来替代其原有的 `FPDF_Canvas` 实现。这能带来更高的渲染质量（更好的抗锯齿、更精准的路径绘制）以及潜在的内部性能提升。

### 涉及修改的文件列表
- `build_scripts/build_pdfium.py`: 增加对 `pdf_use_skia` GN 参数的支持。

### 具体执行步骤

#### 步骤 1: 扩展 `build_pdfium.py` 的配置选项
- **实现内容**:
    1. 在 `BuildConfig` 类中新增 `use_skia: bool = False` 参数。
    2. 修改 `BuildConfig.gn_args` 属性，将硬编码的 `'pdf_use_skia = false'` 修改为动态赋值：`f'pdf_use_skia = {str(self.use_skia).lower()}'`。
    3. 在 `main()` 函数的 `argparse` 配置中，新增 `--use-skia` 命令行参数（action="store_true"）。
    4. 将该参数传递给 `BuildConfig` 实例。
- **校验方式**: 运行 `python build_scripts/build_pdfium.py --help`，验证 `--use-skia` 参数已存在。

#### 步骤 2: 执行 Skia 增强版 PDFium 构建
- **实现内容**:
    1. 使用 `--use-skia` 标志重新构建 PDFium：
       `python build_scripts/build_pdfium.py --target-os mac --target-cpu arm64 --use-skia`
    2. 确保构建流程（GN $\rightarrow$ Ninja $\rightarrow$ Artifact Extraction）全部成功。
- **校验方式**: 验证 `third_party/pdfium/lib/libpdfium.a` 已更新且文件大小有明显变化（启用 Skia 后库体积通常会增加）。

#### 步骤 3: 渲染质量对比验证
- **实现内容**:
    1. 启动 `pdf_reader` 应用，加载包含复杂矢量路径或透明度效果的 PDF 文档。
    2. 对比开启 `pdf_use_skia` 前后的渲染结果（尤其是边缘平滑度和文本细节）。
    3. 验证在 `SkiaPdfViewerWidget` (GPU 模式) 下，渲染结果依然正确且流畅。
- **校验方式**: 视觉验证 $\rightarrow$ 确认复杂页面无渲染异常，且在视觉上更趋向于 Chrome 的渲染效果（因为 Chrome 正是使用 PDFium + Skia）。

### 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **构建验证** | 运行 `build_pdfium.py --use-skia` | Ninja 成功完成构建，生成新的 `libpdfium.a` |
| **启动验证** | 启动应用 $\rightarrow$ 加载 PDF | 应用能正常启动，文档加载速度与之前相当 |
| **质量对比** | 观察精细线条或渐变区域 | 渲染边缘更加平滑，抗锯齿效果提升 |
| **功能回归** | 执行翻页、缩放、文本选择 | 所有基础功能均未受影响 |
