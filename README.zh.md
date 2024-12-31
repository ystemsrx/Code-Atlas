[English](README.md)

# Code Atlas

## 概述

本项目是一个基于 C++ 开发的轻量级解释器，灵感来源于 [Open Interpreter](https://github.com/OpenInterpreter/open-interpreter)。项目支持多语言代码片段的执行，同时具备对部分 Markdown 的渲染功能。与 Open Interpreter 相比，本项目占用资源更少，更适合资源有限的设备。此外，项目利用 `llama.cpp` 实现本地大模型推理，以提供高效、安全的执行环境。当然也支持通过 API 接口调用云端大模型。（请在 [config.json](https://github.com/ystemsrx/Code-Atlas/blob/main/config.json) 中修改相应设置）

---

## 功能

1. **代码执行**：
   - 支持多种脚本语言，包括 Python、Batch、PowerShell 和 Shell/Bash。
   - 可执行代码块以 **紫色** 高亮，其他代码块以 **黄色** 显示。

2. **Markdown 渲染**：
   - 支持以下 Markdown 元素：
     - **标题**（支持 1–4 级）
     - **加粗**、**斜体**
     - **代码块**
   - 输出颜色编码，提升可读性。

3. **本地 AI 推理**：
   - 使用 `llama.cpp` 实现高效的本地大语言模型推理。
   - 兼容 `gguf` 格式的模型，例如 **[Qwen2.5-Interpreter](https://huggingface.co/ystemsrx/Qwen2.5-Interpreter)**，专为此任务微调。
   - 配置方法：在 [config.json](https://github.com/ystemsrx/Code-Atlas/blob/main/config.json) 中将 `API` 设置为 `False`。

4. **云端大模型支持**：
   - 支持调用 OpenAI 等提供 HTTPS API 的云端模型。
   - 配置方法：在 [config.json](https://github.com/ystemsrx/Code-Atlas/blob/main/config.json) 中将 `API` 设置为 `True`，并填写 API URL 和 API Key。

5. **资源效率**：
   - 相较于 Open Interpreter，内存和计算资源需求显著降低。

---

## 使用方法

1. 下载项目源码并使用兼容的 C++ 编译器进行编译，或者下载已编译的可执行文件（[发布页](https://github.com/ystemsrx/Code-Atlas/releases)）。

```
g++ code-atlas.cpp -o code-atlas.exe -std=c++17 -lcurl -lws2_32
```

2. 下载或编译 [llama.cpp](https://github.com/ggerganov/llama.cpp/releases)，确保以下文件齐备：
   - `llama.cli.exe`
   - 必需的 `.dll` 文件
   - 兼容的 `gguf` 模型（如 **Qwen2.5-Interpreter**）或可用的 API。

3. 将以下文件放在同一目录下：
   - 本项目的可执行文件
   - `llama.cli.exe`
   - 必需的 `.dll` 文件
   - 所选的 `gguf` 模型文件
   - `config.json`

4. 运行程序。部分系统可能需要管理员权限。

---

## 待办事项

- [ ] 支持更多平台（例如 Linux 和 macOS）。
- [ ] 开发图形用户界面（GUI）以提升用户体验。
- [ ] 允许用户自由选择代码执行语言。
- [ ] 扩展支持的编程语言，增加对 Python、Batch、PowerShell 和 Shell/Bash 之外的语言支持。
- [ ] 增加插件支持以扩展功能。
- [x] 支持其他 AI 模型 API（如 OpenAI）。
- [ ] 提供更强的沙箱机制，增强代码执行的安全性。
- [ ] 扩展 Markdown 渲染功能，支持更多元素。
- [ ] 支持语音模式，解放双手。

---

## 灵感来源

本项目受 [Open Interpreter](https://github.com/OpenInterpreter/open-interpreter) 启发。我们欣赏其创新的自然语言与编程结合方式，能够显著提升人机交互体验。为优化性能，本项目使用 C++ 实现，特别针对 Windows 系统进行性能优化，同时保留直观强大的核心设计理念。

此外，**Qwen2.5-Interpreter** 模型基于 **Qwen2.5-0.5B** 微调，专为此任务设计，展示了本项目在任务优化上的潜力。

---
