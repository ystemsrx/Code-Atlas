[中文](README.zh.md)

# Code-Atlas

## Overview

This project is a lightweight C++-based interpreter inspired by [Open Interpreter](https://github.com/OpenInterpreter/open-interpreter), designed to execute code snippets across multiple languages with support for Markdown rendering. Compared to Open Interpreter, this project requires significantly fewer resources, making it suitable for devices with limited capacity. It leverages `llama.cpp` for local large model inference and delivers a highly optimized execution environment.

---

## Features

1. **Code Execution**:
   - Supports Python, Batch, PowerShell, and Shell/Bash scripts.
   - Highlights executable code blocks in **purple** and non-executable ones in **yellow**.

2. **Markdown Rendering**:
   - Handles the following Markdown elements:
     - **Headings** (levels 1–4)
     - **Bold**, **Italic**
     - **Inline code** and **block code**
   - Outputs are color-coded and formatted for readability.

3. **Local AI Inference**:
   - Uses `llama.cpp` for large language model inference.
   - Compatible with `gguf` models, including **[Qwen2.5-Interpreter](https://huggingface.co/ystemsrx/Qwen2.5-Interpreter)**, which is specifically fine-tuned for this task based on the **Qwen2.5-0.5B** model.

4. **Resource Efficiency**:
   - Consumes significantly less memory and computational resources compared to Open Interpreter.

---

## Usage

1. Obtain the source code and compile it using a compatible C++ compiler, or download a precompiled binary if available. [(Release Page)](https://github.com/ystemsrx/Code-Atlas/releases)

```
g++ -std=c++17 main.cpp constants.cpp codeexecutor.cpp utils.cpp -o my_program.exe
```

2. Obtain `llama.cpp` from its [releases page](https://github.com/ggerganov/llama.cpp/releases). Ensure you have:
   - `llama.cli.exe`
   - Necessary `.dll` files
   - A compatible `gguf` model (e.g., **Qwen2.5-Interpreter**, fine-tuned for this task)

3. Place the following files in the same directory:
   - The compiled executable from this project
   - `llama.cli.exe`
   - Required `.dll` files
   - Your selected `gguf` model

4. Run the program. Note that administrator privileges may be required on some systems.

---

## System Requirements

- Windows OS
- `llama.cpp` executable and dependencies
- Compatible `gguf` model (e.g., **Qwen2.5-Interpreter**, based on Qwen2.5-0.5B)
- Compiled executable for this project

---

## To-Do List

- [ ] Expand support to platforms beyond Windows (Linux, macOS, etc.).
- [ ] Introduce API support for integration into other systems.
- [ ] Develop a GUI for user-friendly interaction.
- [ ] Allow users to select their preferred programming language for code execution.
- [ ] Expand language support beyond Python, Batch, PowerShell, and Shell/Bash.
- [ ] Add plugin support for extended functionality.
- [ ] Support other AI model APIs, such as OpenAI, for enhanced flexibility.
- [ ] Implement enhanced sandboxing mechanisms for improved security.
- [ ] Extend Markdown rendering capabilities for additional elements.

---

## Inspiration

This project is deeply inspired by [Open Interpreter](https://github.com/OpenInterpreter/open-interpreter), a truly innovative and creative project that has the potential to change the way we interact with computers. The concept of seamlessly integrating natural language and programming to automate tasks is groundbreaking and has set the foundation for many other ideas. This project seeks to build upon those ideas by implementing them in C++ for improved performance and efficiency on Windows systems, while maintaining the core inspiration of making computer interaction more intuitive and powerful.

---
