# Implementation Plan: Coordinate Transformation via PDFium Native APIs

## 1. 涉及修改的文件列表

- `pdfium_wrapper/include/pdfium_wrapper.h`: 在 `IPdfPage` 接口中添加 `PageToDevice` 和 `DeviceToPage` 虚函数。
- `pdfium_wrapper/src/pdfium_wrapper.cpp`: 在 `PdfPageImpl` 中实现上述函数，调用 PDFium 原生 API。
- `qt_app/src/pdfdocument.h`: 添加转发接口 `pageToDevice` 和 `deviceToPage`。
- `qt_app/src/pdfdocument.cpp`: 实现转发逻辑，将请求传递给 Wrapper 层的 `IPdfPage`。
- `qt_app/src/pdfviewerwidget.cpp`: 重构 `mapFromPage` 和 `mapToPage`，删除手动计算旋转/缩放的逻辑，改为调用 `PdfDocument` 的新接口。

## 2. 具体执行步骤

### 步骤 1: 扩展 Wrapper 接口与实现
**目标**: 将 PDFium 内部的坐标转换能力暴露给上层。

1. **修改 `pdfium_wrapper.h`**:
   - 在 `IPdfPage` 类中增加两个纯虚函数：
     - `virtual void PageToDevice(double px, double py, double* dx, double* dy) = 0;`
     - `virtual void DeviceToPage(double dx, double dy, double* px, double* py) = 0;`
2. **修改 `pdfium_wrapper.cpp`**:
   - 在 `PdfPageImpl` 中实现上述函数：
     - `PageToDevice` $\rightarrow$ 调用 `FPDF_PageToDevice(m_page, px, py, dx, dy)`。
     - `DeviceToPage` $\rightarrow$ 调用 `FPDF_DeviceToPage(m_page, dx, dy, px, py)`。

**校验方式**: 编译 Wrapper DLL，使用调试器在 `PdfPageImpl` 中设置断点，验证调用路径是否正确到达 `FPDF_PageToDevice/DeviceToPage`。

### 步骤 2: 在 `PdfDocument` 中构建转发层
**目标**: 为 Qt 视图提供便捷的坐标转换访问点。

1. **修改 `pdfdocument.h`**:
   - 添加公开接口：
     - `QPointF pageToDevice(int pageIndex, const QPointF& pagePos);`
     - `QPointF deviceToPage(int pageIndex, const QPointF& devicePos);`
2. **修改 `pdfdocument.cpp`**:
   - 实现 `pageToDevice`: 获取对应页面的 `IPdfPage` $\rightarrow$ 调用 `PageToDevice` $\rightarrow$ 将结果封装为 `QPointF`。
   - 实现 `deviceToPage`: 获取对应页面的 `IPdfPage` $\rightarrow$ 调用 `DeviceToPage` $\rightarrow$ 将结果封装为 `QPointF`。

**校验方式**: 在 `PdfDocument` 中添加临时日志，输出输入点和输出点的对比，验证数值是否随页面旋转/缩放而变化。

### 步骤 3: 重构 `PdfViewerWidget` 的坐标映射
**目标**: 彻底移除手动计算矩阵的代码，实现“调用 $\rightarrow$ 偏移”的简化逻辑。

1. **修改 `mapFromPage`**:
   - 调用 `m_document->pageToDevice(m_currentPage, pagePos)` 获得设备坐标。
   - 最终坐标 = `devicePos + pageRect().topLeft()`。
2. **修改 `mapToPage`**:
   - 相对坐标 = `widgetPos - pageRect().topLeft()`。
   - 调用 `m_document->deviceToPage(m_currentPage, relativePos)` 获得 PDF 坐标。
3. **更新 `mapRectFromPage` (若存在)**:
   - 将矩形的左上角和右下角分别通过 `mapFromPage` 转换，重新构造视图矩形。

**校验方式**:
- **旋转校验**: 旋转页面 $90^\circ, 180^\circ, 270^\circ$，验证 `mapFromPage(0, 0)` 始终准确对应视图中页面的左上角。
- **缩放校验**: 改变 Zoom 级别，验证转换后的像素坐标与 PDF 坐标保持线性比例。

### 步骤 4: 综合端到端验证
**目标**: 确保整个转换链路在实际场景中无误差。

1. **往返测试 (Round-trip)**: 选取随机点 $P \rightarrow$ `mapFromPage` $\rightarrow$ `mapToPage` $\rightarrow$ 验证结果是否回到 $P$（允许 1 像素误差）。
2. **交互测试**: 实现一个简单的点击反馈，点击页面某点，通过 `mapToPage` 获取 PDF 坐标并打印，验证与 PDF 内部坐标一致。

**校验方式**: 运行应用程序 $\rightarrow$ 随机旋转/缩放 $\rightarrow$ 执行往返测试 $\rightarrow$ 验证坐标一致性。
