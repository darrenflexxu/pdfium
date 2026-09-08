# 实现计划：PDFium Debug/Release 库版本自动切换

## 1. 需求背景
目前项目在编译时仅链接一个 PDFium 静态库文件。为了在 Xcode 中进行高效调试，需要实现在 Debug 配置下链接带调试符号的 PDFium 库（以便 Step Into 源代码），而在 Release 配置下链接经过优化且无符号的 PDFium 库。

## 2. 涉及修改的文件列表
- `build_scripts/build_pdfium.py`: 修改产物提取逻辑，区分命名。
- `CMakeLists.txt`: 重构构建目标与链接逻辑。

## 3. 具体执行步骤

### 步骤 1: 修改 PDFium 产物提取逻辑 (`build_pdfium.py`)
**目标**: 确保 Debug 和 Release 版本的库文件在提取到 `third_party/pdfium/lib` 时不会互相覆盖。

- **修改点**: 在 `_extract_windows_artifacts`, `_extract_macos_artifacts`, `_extract_linux_artifacts` 函数中：
    - 在复制主库文件（如 `libpdfium.a` 或 `pdfium.lib`）时，检查 `self.config.is_debug`。
    - 如果是 Debug 版本，将目标文件名改为 `libpdfium_debug.a` / `pdfium_debug.lib`。
    - 如果是 Release 版本，保持原名 `libpdfium.a` / `pdfium.lib`。
- **校验方式**: 执行 `python3 build_scripts/build_pdfium.py --debug` 和 `python3 build_scripts/build_pdfium.py --release` 后，检查 `third_party/pdfium/lib/` 目录下同时存在 `libpdfium.a` 和 `libpdfium_debug.a`。

### 步骤 2: 定义多配置库路径 (`CMakeLists.txt`)
**目标**: 在 CMake 配置阶段定义两套不同的库路径。

- **修改点**: 
    - 定义 `PDFIUM_LIB_RELEASE` 变量，指向标准库路径。
    - 定义 `PDFIUM_LIB_DEBUG` 变量，指向带 `_debug` 后缀的库路径。
- **校验方式**: 运行 `cmake ..`，通过 `cmake-gui` 或命令行检查这两个缓存变量是否正确定义。

### 步骤 3: 创建分离的 PDFium 构建目标 (`CMakeLists.txt`)
**目标**: 允许开发者通过 CMake 目标分别触发 Debug 和 Release 版本的 PDFium 编译。

- **修改点**:
    - 将原有的单一 `pdfium_build` 目标拆分为 `pdfium_build_debug` 和 `pdfium_build_release`。
    - `pdfium_build_debug` 调用 `build_pdfium.py --debug`。
    - `pdfium_build_release` 调用 `build_pdfium.py --release`。
    - 创建一个元目标 `pdfium_build`，使其依赖于上述两个目标，实现一次性生成两套库。
- **校验方式**: 执行 `cmake --build . --target pdfium_build_debug`，验证是否生成了调试版库。

### 步骤 4: 实现配置感知链接 (`CMakeLists.txt`)
**目标**: 使 Xcode 的 Build Configuration 决定链接哪个库。

- **修改点**:
    - 使用 CMake 生成器表达式 (Generator Expressions) 修改 `pdfium` 接口库的链接指令：
      `target_link_libraries(pdfium INTERFACE $<$<CONFIG:Debug>:${PDFIUM_LIB_DEBUG}> $<$<NOT:$<CONFIG:Debug>>:${PDFIUM_LIB_RELEASE}>)`
- **校验方式**: 
    1. 在 Xcode 中选择 **Debug** 方案 $\rightarrow$ 编译 $\rightarrow$ 使用 `otool -L` 或检查链接日志，确认链接的是 `libpdfium_debug.a`。
    2. 在 Xcode 中选择 **Release** 方案 $\rightarrow$ 编译 $\rightarrow$ 确认链接的是 `libpdfium.a`。

## 4. 综合校验方案

| 测试场景 | 操作步骤 | 预期结果 |
| :--- | :--- | :--- |
| **双版本产出** | 执行 `cmake --build . --target pdfium_build` | `lib/` 目录下同时存在 `.a` 和 `_debug.a` 文件 |
| **Debug 符号验证** | Xcode 选择 Debug $\rightarrow$ 运行 $\rightarrow$ 在 PDFium 代码中打断点/单步执行 | 能够正常进入 PDFium 内部函数，变量值可见 |
| **Release 优化验证** | Xcode 选择 Release $\rightarrow$ 编译 $\rightarrow$ 检查二进制大小 | 最终 App 体积较小，PDFium 部分无调试符号 |
| **路径灵活性** | 修改 `PDFIUM_LIB_DEBUG` 变量路径 $\rightarrow$ 重新编译 | 编译器正确使用新路径下的库文件 |
