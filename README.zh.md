[English](README.md)

# Code Atlas

## 概述

本项目是一个基于 C++ 开发的轻量级解释器，灵感来源于 [Open Interpreter](https://github.com/OpenInterpreter/open-interpreter)。项目支持多语言代码片段的执行，同时具备对部分 Markdown 的渲染功能。与 Open Interpreter 相比，本项目占用资源显著更少，更适合资源有限的设备使用。此外，项目利用 `llama.cpp` 实现本地大模型推理，为用户提供高效的执行环境。

---

## 功能

1. **代码执行**：
   - 支持 Python、Batch、PowerShell 和 Shell/Bash 等脚本语言。
   - 可执行的代码块会以 **紫色** 高亮，其他代码块以 **黄色** 显示。

2. **Markdown 渲染**：
   - 支持以下 Markdown 元素：
     - **标题**（支持 1–4 级）
     - **加粗**、**斜体**
     - **内联代码** 和 **代码块**
   - 输出经过颜色编码，增强可读性。

3. **本地 AI 推理**：
   - 使用 `llama.cpp` 实现本地大语言模型推理。
   - 兼容 `gguf` 格式的模型，例如专为此任务微调的 **[Qwen2.5-Interpreter](https://huggingface.co/ystemsrx/Qwen2.5-Interpreter)**，该模型基于 **Qwen2.5-0.5B** 进行微调。

4. **资源效率**：
   - 相较于 Open Interpreter，本项目对内存和计算资源的需求显著减少。

---

## 使用方法

1. 获取项目源码并使用兼容的 C++ 编译器自行编译，或者下载已编译好的可执行文件。[(发布页)](https://github.com/ystemsrx/Code-Atlas/releases)

```
g++ -std=c++17 main.cpp constants.cpp codeexecutor.cpp utils.cpp -o code-atlas.exe
```

2. 前往 [llama.cpp 发布页面](https://github.com/ggerganov/llama.cpp/releases)，下载或自行编译适合版本的 `llama.cpp`，确保您拥有以下文件：
   - `llama.cli.exe`
   - 所需的 `.dll` 文件
   - 兼容的 `gguf` 模型（如 **Qwen2.5-Interpreter**，专为此任务微调）

3. 将以下文件放置在同一目录下：
   - 本项目的已编译可执行文件
   - `llama.cli.exe`
   - 必需的 `.dll` 文件
   - 您选择的 `gguf` 模型文件

4. 运行程序。请注意，部分系统可能需要管理员权限。

---

## 系统要求

- Windows 操作系统
- `llama.cpp` 可执行文件及其依赖
- 兼容的 `gguf` 模型（例如 **Qwen2.5-Interpreter**，基于 Qwen2.5-0.5B 微调）
- 本项目的已编译可执行文件

---

## 待办事项

- [ ] 支持更多平台（例如 Linux、macOS 等）。
- [ ] 提供 API 支持以便与其他系统集成。
- [ ] 开发图形界面（GUI）以提升用户体验。
- [ ] 允许用户自由选择代码执行使用的编程语言。
- [ ] 扩展支持的编程语言类型，超越 Python、Batch、PowerShell 和 Shell/Bash。
- [ ] 增加插件支持以扩展功能。
- [ ] 支持其他 AI 模型 API，例如 OpenAI，为用户提供更多选择。
- [ ] 提供更强的沙箱机制，提升代码执行的安全性。
- [ ] 扩展 Markdown 渲染功能，支持更多元素。
- [ ] 支持语音模式，解放双手

---

## 灵感来源

本项目深受 [Open Interpreter](https://github.com/OpenInterpreter/open-interpreter) 启发。我们非常喜欢这个项目，认为它是一个极具创意的作品，可以改变我们与电脑交互的方式。将自然语言和编程无缝结合来自动化任务的概念令人耳目一新，也为许多其他想法奠定了基础。本项目在此基础上进行改进，使用 C++ 实现，以提高在 Windows 系统上的性能和效率，同时保留了让电脑交互更加直观和强大的核心愿景。此外，**Qwen2.5-Interpreter** 模型是专为此任务基于 **Qwen2.5-0.5B** 微调的模型，展示了本项目优化任务性能的能力。

---
