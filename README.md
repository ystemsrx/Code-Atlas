[中文](README.zh.md)

# Code Atlas

## Overview

This project is a lightweight interpreter developed in C++, inspired by [Open Interpreter](https://github.com/OpenInterpreter/open-interpreter). It supports the execution of multi-language code snippets and partial Markdown rendering. Compared to Open Interpreter, this project has significantly lower resource consumption, making it more suitable for resource-limited devices. Additionally, it leverages `llama.cpp` for local large-model inference, providing an efficient and secure execution environment. It also supports cloud-based large-model APIs, configurable via [config.json](https://github.com/ystemsrx/Code-Atlas/blob/main/config.json).

---

## Features

1. **Code Execution**:
   - Supports scripting languages like Python, Batch, PowerShell, and Shell/Bash.
   - Executable code blocks are highlighted in **purple**, while others are shown in **yellow**.

2. **Markdown Rendering**:
   - Supports the following Markdown elements:
     - **Headings** (levels 1–4)
     - **Bold** and *Italic*
     - **Code blocks**
   - Output is color-coded for enhanced readability.

3. **Local AI Inference**:
   - Uses `llama.cpp` for efficient local large-language model inference.
   - Compatible with `gguf` models, such as the **[Qwen2.5-Interpreter](https://huggingface.co/ystemsrx/Qwen2.5-Interpreter)**, fine-tuned for this task.
   - Configuration: Set `API` to `False` in [config.json](https://github.com/ystemsrx/Code-Atlas/blob/main/config.json).

4. **Cloud-Based Large Models**:
   - Supports HTTPS API-based cloud models like OpenAI.
   - Configuration: Set `API` to `True` in [config.json](https://github.com/ystemsrx/Code-Atlas/blob/main/config.json) and provide the base URL and API key.

5. **Resource Efficiency**:
   - Significantly reduces memory and computational resource requirements compared to Open Interpreter.

---

## Usage

1. Download the project source code and compile it using a compatible C++ compiler, or download the precompiled executable ([Releases](https://github.com/ystemsrx/Code-Atlas/releases)).

```
g++ code-atlas.cpp -o code-atlas.exe -std=c++17 -lcurl -lws2_32
```

2. Download or compile [llama.cpp](https://github.com/ggerganov/llama.cpp/releases), ensuring the following files are available:
   - `llama.cli.exe`
   - Necessary `.dll` files
   - A compatible `gguf` model (e.g., **Qwen2.5-Interpreter**) or an accessible API.

3. Place the following files in the same directory:
   - The project's executable
   - `llama.cli.exe`
   - Required `.dll` files
   - Your chosen `gguf` model
   - `config.json`

4. Run the program. Note that administrator permissions might be required on some systems.

---

## To-Do List

- [ ] Support more platforms (e.g., Linux, macOS).
- [ ] Develop a graphical user interface (GUI) for enhanced usability.
- [ ] Allow users to choose the programming language for code execution.
- [ ] Expand supported programming languages beyond Python, Batch, PowerShell, and Shell/Bash.
- [ ] Add plugin support to extend functionality.
- [x] Support additional AI model APIs, such as OpenAI.
- [ ] Enhance sandboxing mechanisms for improved execution security.
- [ ] Extend Markdown rendering to support more elements.
- [ ] Support voice mode for hands-free operation.

---

## Inspiration

This project is inspired by [Open Interpreter](https://github.com/OpenInterpreter/open-interpreter), which we admire for its innovative approach to integrating natural language and programming. By optimizing performance, this project is implemented in C++ with specific enhancements for Windows systems while retaining the intuitive and powerful core design.

Additionally, the **Qwen2.5-Interpreter** model, fine-tuned from **Qwen2.5-0.5B**, showcases the project's potential for task optimization and performance enhancement.

---

