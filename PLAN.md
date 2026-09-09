# 实现计划：文本选择右键复制功能

## 1. 需求背景
目前文本选择在释放鼠标时会自动复制到剪贴板，但缺乏标准的 UI 交互。用户希望在选中一段文本后，可以通过右键点击弹出上下文菜单，并从中选择“复制”操作来手动复制选中的文本。

## 2. 涉及修改的文件列表
- `qt_app/src/pdfviewerwidget.h`: 声明 `contextMenuEvent` 覆盖函数。
- `qt_app/src/pdfviewerwidget.cpp`: 实现右键菜单逻辑及文本复制调用。

## 3. 具体执行步骤

### 步骤 1: 声明上下文菜单事件 (`PdfViewerWidget`)
**目标**: 让 `PdfViewerWidget` 能够拦截并处理右键点击事件。

- **修改点**: 在 `protected` 区域添加 `void contextMenuEvent(QContextMenuEvent* event) override;`。
- **校验方式**: 编译检查，确保无语法错误。

### 步骤 2: 实现右键菜单逻辑 (`PdfViewerWidget`)
**目标**: 根据当前选择状态动态显示菜单。

- **实现细节**:
    1. **检查选区**: 在 `contextMenuEvent` 中检查 `m_selectionStart` 和 `m_selectionEnd` 是否不相等且均 $\ge 0$。
    2. **创建菜单**: 实例化一个 `QMenu`。
    3. **添加“复制”项**: 如果存在有效选区，添加一个 `addAction(tr("Copy"))`。
    4. **绑定动作**: 为“复制”动作绑定一个 Lambda 表达式或槽函数，执行以下操作：
        - 调用 `m_document->textForRange(m_currentPage, a, b)` 获取选中文本。
        - 使用 `QApplication::clipboard()->setText(text)` 将文本写入剪贴板。
        - 发出 `textSelected(text)` 信号以通知 UI（如状态栏显示）。
    5. **显示菜单**: 调用 `menu.exec(event->globalPos())` 在鼠标位置弹出菜单。
- **校验方式**: 
    - 选中一段文字 $\rightarrow$ 右键点击 $\rightarrow$ 验证弹出菜单包含“复制”选项 $\rightarrow$ 点击“复制” $\rightarrow$ 验证剪贴板内容正确。
    - 未选中任何文字 $\rightarrow$ 右键点击 $\rightarrow$ 验证不弹出“复制”选项（或弹出空菜单）。

### 步骤 3: 优化交互体验 (UX)
**目标**: 确保右键操作与现有选择逻辑协调。

- **细节调整**:
    - 如果用户在没有选中状态下右键点击，可以考虑调用 `mousePressEvent` 的逻辑来将光标定位到点击位置。
    - 确保菜单弹出时不会意外触发 `mouseReleaseEvent` 中的自动复制逻辑（如果以后决定取消自动复制）。
- **校验方式**: 快速连续点击、在页面边缘点击等边界测试。

## 4. 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **选中复制** | 选中一段文本 $\rightarrow$ 右键 $\rightarrow$ 点击“复制” | 剪贴板内容与选中文字一致 |
| **未选中右键** | 在空白区域右键点击 | 不显示“复制”选项 |
| **跨行选中复制** | 跨行选中 $\rightarrow$ 右键 $\rightarrow$ 点击“复制” | 复制结果包含正确的换行符 |
| **状态同步** | 点击右键“复制” | 状态栏显示“Text copied to clipboard” |
