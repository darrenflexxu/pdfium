# Implementation Plan: Advanced Text Selection (Linear Cursor & Box Selection)

## 1. 涉及修改的文件列表

- `qt_app/src/pdfdocument.h`: 定义 `CharInfo` 结构体以及页面级字符映射缓存 `m_charMaps`。
- `qt_app/src/pdfdocument.cpp`: 实现字符坐标映射表的生成逻辑（Character Mapping）。
- `qt_app/src/pdfviewerwidget.h`: 添加光标索引、选区索引范围及光标状态变量。
- `qt_app/src/pdfviewerwidget.cpp`: 重构鼠标事件，实现点击定位光标、线性拖拽选择及精准的字符级高亮渲染。

## 2. 具体执行步骤

### 步骤 1: 构建字符级映射表 (`PdfDocument`)
**目标**: 将 PDF 页面离散的元素转化为一个线性的、带有坐标信息的字符流。

1. **定义 `CharInfo` 结构**:
   - 包含字符编码 (codepoint)、该字符的 PDF 坐标边界 (`QRectF bounds`) 以及它在页中的线性索引 (`int index`)。
2. **实现 `generateCharMap(int pageIndex)`**:
   - 遍历页面所有文本元素 $\rightarrow$ 提取字符串 $\rightarrow$ 基于字符宽度和位置估算每个字符的 `bounds`。
   - 将所有字符按阅读顺序（先 Y 后 X）存入 `QVector<CharInfo>` 并缓存至 `m_charMaps`。
3. **提供查询接口**:
   - `int findNearestCharIndex(int pageIndex, const QPointF& pos)`: 根据 PDF 坐标查找距离最近的字符索引。

**校验方式**: 打印页面的字符索引表，验证每个字符的坐标是否正确，且索引顺序与阅读顺序一致。

### 步骤 2: 实现光标定位与状态管理 (`PdfViewerWidget`)
**目标**: 在视图中引入文本编辑器的“光标”概念。

1. **引入状态变量**:
   - `int m_cursorIndex`: 当前光标所在的字符位置。
   - `int m_selectionStart`: 选区起始索引。
   - `int m_selectionEnd`: 选区结束索引。
2. **更新 `mousePressEvent`**:
   - 将点击点 $\rightarrow$ `mapToPage` $\rightarrow$ `findNearestCharIndex` $\rightarrow$ 更新 `m_cursorIndex`。
   - 将 `m_selectionStart = m_selectionEnd = m_cursorIndex`。
3. **更新 `mouseMoveEvent`**:
   - 在拖拽时，实时更新 `m_selectionEnd` 为当前鼠标所在位置对应的字符索引。

**校验方式**: 在界面上通过调试标签显示当前的 `m_cursorIndex`，验证点击不同字符时索引能正确跳转。

### 步骤 3: 实现两种选择模式的混合渲染 (`PdfViewerWidget`)
**目标**: 兼顾“线性光标选择”的精确度和“矩形框选”的便捷性。

1. **视觉渲染 (`paintEvent`)**:
   - **光标渲染**: 在 `m_cursorIndex` 对应字符的左边界绘制一条 1px 宽的垂直线（Caret）。
   - **线性高亮**: 遍历 `CharMap`，将 `m_selectionStart` 和 `m_selectionEnd` 之间的所有字符通过 `mapFromPage` 转换为视图矩形并绘制。
   - **框选 fallback**: 若用户在无文本区域拖拽，依然保留原有的半透明矩形覆盖层。
2. **文本提取逻辑**:
   - 根据 `[start, end]` 索引范围，直接从 `CharMap` 中拼接字符，无需再次进行区域过滤。

**校验方式**: 
- 拖拽选择文本 $\rightarrow$ 验证高亮块精准包裹字符 $\rightarrow$ 跨行选择时，高亮应在行末折返至下一行行首。

### 步骤 4: 增强交互细节 (UX)
**目标**: 提供专业 PDF 阅读器的交互体验。

1. **双击/三击扩展**:
   - 双击 $\rightarrow$ 扩展选区至包含当前字符的整个单词。
   - 三击 $\rightarrow$ 扩展选区至当前段落。
2. **快捷键支持**:
   - `Ctrl+A` $\rightarrow$ 选中全页文本索引 `[0, totalChars - 1]`。
3. **清除逻辑**: 在点击空白区域时，保留光标位置但清除选区。

**校验方式**: 执行双击/三击操作 $\rightarrow$ 验证选区范围自动扩展的准确性。

## 3. 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **精准点击** | 点击某个单词的第三个字符 | 光标准确落在该字符之前 |
| **线性跨行选择** | 从第一行末尾拖拽到第二行开头 | 选中第一行剩余部分 $\rightarrow$ 换行 $\rightarrow$ 选中第二行起始部分 |
| **视觉一致性** | 旋转/缩放页面 | 光标和字符高亮块依然紧贴文字，无漂移 |
| **复制内容** | 线性选择 $\rightarrow$ `Ctrl+C` | 复制的字符串与视觉选中的字符序列完全一致 |
| **边界处理** | 在页首或页尾点击 | 索引正确处理为 0 或 `totalChars`，不产生越界崩溃 |
