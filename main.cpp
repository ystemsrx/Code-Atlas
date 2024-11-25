#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <sstream>
#include <fstream>
#include "constants.h"
#include "codeexecutor.h"
#include "utils.h"
#include "codeblock.h"

// 全局变量
HANDLE hChildStdInWrite;
std::atomic<bool> modelLoaded(false);
std::vector<CodeBlock> pendingCodeBlocks;
std::atomic<bool> isExecuting(false);
std::mutex codeBlocksMutex;

// 新增变量用于计时
std::chrono::steady_clock::time_point lastChunkTime;
std::mutex lastChunkMutex;

// 修改 ReadOutputThread 函数
void ReadOutputThread(HANDLE hPipe) {
    char buffer[4096];
    DWORD bytesRead;
    std::string accumulator;
    bool inCodeBlock = false;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    // Existing variables for code blocks
    std::string startBackTickBuffer;
    std::string endBackTickBuffer;
    int startBackTickCount = 0;
    int endBackTickCount = 0;
    bool hasEndingBackticks = false;

    std::string firstLineBuffer;
    bool collectingFirstLine = false;

    std::string codeBuffer; // 用于收集代码内容

    // 当前代码块的颜色
    WORD currentCodeColor = CODE_COLOR;

    // Variables for markdown formatting
    bool inBold = false;
    bool inItalic = false;
    bool atLineStart = true;
    bool inHeading = false;
    int headingLevel = 0;

    // 新增变量，用于处理单反引号包裹的内容
    bool inInlineCode = false;
    // 定义颜色，确保不与已有颜色冲突
    const WORD INLINE_CODE_COLOR = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // 青色

    // 新增变量，用于缓冲标题行
    std::string headingBuffer;
    bool isBufferingHeading = false;
    int currentHeadingLevel = 0;

    auto printWithColor = [&](const std::string& text, WORD color) {
        SetConsoleTextAttribute(hConsole, color);
        std::cout << text;
        std::cout.flush();
    };

    auto printWithCurrentAttributes = [&](const std::string& text) {
        WORD color = DEFAULT_COLOR;
        if (inCodeBlock) {
            color = currentCodeColor;
        }
        else if (inHeading) {
            // 根据级别设置颜色
            switch (currentHeadingLevel) {
            case 1: color = HEADING1_COLOR; break;
            case 2: color = HEADING2_COLOR; break;
            case 3: color = HEADING3_COLOR; break;
            case 4: color = HEADING4_COLOR; break;
            default: color = DEFAULT_COLOR; break;
            }
        }
        else if (inBold) {
            color = FOREGROUND_RED | FOREGROUND_INTENSITY;
        }
        else if (inItalic) {
            color = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        }
        else if (inInlineCode) {
            color = INLINE_CODE_COLOR;
        }

        SetConsoleTextAttribute(hConsole, color);
        std::cout << text;
        std::cout.flush();
    };

    while (true) {
        if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
            if (hasEndingBackticks) {
                SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
            }
            if (!startBackTickBuffer.empty()) {
                printWithCurrentAttributes(startBackTickBuffer);
            }
            if (!endBackTickBuffer.empty()) {
                printWithCurrentAttributes(endBackTickBuffer);
            }
            break;
        }

        accumulator.append(buffer, bytesRead);

        // 更新最后接收消息块的时间
        {
            std::lock_guard<std::mutex> lock(lastChunkMutex);
            lastChunkTime = std::chrono::steady_clock::now();
        }

        if (!modelLoaded && accumulator.find("Must avoid hallucinations.") != std::string::npos) {
            modelLoaded = true;
            printWithColor("\n\n模型加载完成!\n", DEFAULT_COLOR);
            SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
            inItalic = false;
            accumulator.clear();
            continue;
        }

        size_t pos = 0;
        while (pos < accumulator.length()) {
            // 在循环开始时确定是否在行首
            if (pos == 0) {
                atLineStart = true;
            }
            else {
                atLineStart = (accumulator[pos - 1] == '\n');
            }

            // 1. 修改标题处理部分
            if (!isBufferingHeading && !inCodeBlock) {
                if (accumulator[pos] == '#' && atLineStart) {
                    isBufferingHeading = true;
                    currentHeadingLevel = 0; // 重置级别计数

                    // 计算标题级别
                    while (pos < accumulator.length() && accumulator[pos] == '#' && currentHeadingLevel < 4) {
                        currentHeadingLevel++;
                        pos++;
                    }

                    // 跳过空格
                    if (pos < accumulator.length() && accumulator[pos] == ' ') {
                        pos++;
                    }

                    continue; // 继续下一个字符处理
                }
            }

            // 2. 修改输出标题行部分
            if (isBufferingHeading) {
                // 缓冲标题内容直到行尾
                while (pos < accumulator.length() && accumulator[pos] != '\n') {
                    headingBuffer += accumulator[pos];
                    pos++;
                }

                if (pos < accumulator.length() && accumulator[pos] == '\n') {
                    // 设置颜色
                    WORD titleColor;
                    switch (currentHeadingLevel) {
                    case 1: titleColor = HEADING1_COLOR; break;
                    case 2: titleColor = HEADING2_COLOR; break;
                    case 3: titleColor = HEADING3_COLOR; break;
                    case 4: titleColor = HEADING4_COLOR; break;
                    default: titleColor = DEFAULT_COLOR; break;
                    }

                    // 直接使用颜色打印内容
                    SetConsoleTextAttribute(hConsole, titleColor);
                    std::cout << headingBuffer << '\n';
                    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

                    // 重置状态
                    headingBuffer.clear();
                    isBufferingHeading = false;
                    currentHeadingLevel = 0;
                    pos++; // 移动到下一个字符
                    continue;
                }

                // 如果没有读取到完整的标题行，跳出循环等待更多数据
                break;
            }

            if (!inCodeBlock) {
                if (accumulator[pos] == '`' && !inInlineCode) {
                    // Existing triple backtick detection
                    startBackTickBuffer += "`";
                    startBackTickCount++;
                    pos++;

                    if (startBackTickCount == 3) {
                        collectingFirstLine = true;
                        firstLineBuffer.clear();
                        codeBuffer.clear(); // 重置 codeBuffer
                        startBackTickBuffer.clear();
                        startBackTickCount = 0;
                        inCodeBlock = true;
                    }
                }
                else {
                    if (!startBackTickBuffer.empty() && startBackTickCount < 3) {
                        // Handle inline code detection separately
                        if (startBackTickCount == 1) {
                            // Single backtick detected, enter or exit inline code
                            inInlineCode = !inInlineCode;
                            if (inInlineCode) {
                                // 可根据需要添加处理逻辑
                            }
                            else {
                                // 可根据需要添加处理逻辑
                            }
                        }
                        else {
                            // 打印累积的反引号
                            printWithCurrentAttributes(startBackTickBuffer);
                        }
                        startBackTickBuffer.clear();
                        startBackTickCount = 0;
                    }

                    // 处理 Markdown 元素
                    if (inInlineCode) {
                        if (accumulator[pos] == '`') {
                            // 检测内联代码结束
                            inInlineCode = false;
                            pos++;
                        }
                        else {
                            // 打印内联代码内容
                            printWithColor(std::string(1, accumulator[pos]), INLINE_CODE_COLOR);
                            pos++;
                        }
                    }
                    else {
                        // 处理粗体和斜体
                        if (accumulator[pos] == '*' || accumulator[pos] == '_') {
                            char marker = accumulator[pos];
                            size_t markerCount = 0;
                            size_t tempPos = pos;

                            while (tempPos < accumulator.length() && accumulator[tempPos] == marker) {
                                markerCount++;
                                tempPos++;
                            }

                            if (markerCount >= 2) {
                                pos += 2; // 跳过两个标记符
                                inBold = !inBold;
                                continue;
                            }
                            else {
                                pos++; // 跳过单个标记符
                                inItalic = !inItalic;
                                continue;
                            }
                        }

                        // 打印普通字符
                        std::string ch(1, accumulator[pos]);
                        printWithCurrentAttributes(ch);

                        pos++;
                    }
                }
            }
            else {
                if (collectingFirstLine) {
                    if (accumulator[pos] == '\n') {
                        collectingFirstLine = false;
                        // 判断语言类型并设置颜色
                        std::string lowerFirstLine = firstLineBuffer;
                        std::transform(lowerFirstLine.begin(), lowerFirstLine.end(),
                            lowerFirstLine.begin(), ::tolower);

                        currentCodeColor = YELLOW_COLOR; // 默认颜色

                        for (const auto& pair : languageColorMap) {
                            if (lowerFirstLine.find(pair.first) != std::string::npos) {
                                currentCodeColor = pair.second;
                                break;
                            }
                        }
                        pos++;
                    }
                    else {
                        firstLineBuffer += accumulator[pos];
                        pos++;
                    }
                }
                else if (accumulator[pos] == '`') {
                    endBackTickBuffer += "`";
                    endBackTickCount++;
                    pos++;

                    if (endBackTickCount == 3) {
                        inCodeBlock = false;
                        endBackTickBuffer.clear();
                        endBackTickCount = 0;
                        hasEndingBackticks = true;
                        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

                        // 收集代码块
                        CodeBlock block;
                        block.code = codeBuffer; // 使用 codeBuffer 作为代码内容
                        block.language = "";
                        std::string lowerFirstLine = firstLineBuffer;
                        std::transform(lowerFirstLine.begin(), lowerFirstLine.end(),
                            lowerFirstLine.begin(), ::tolower);
                        for (const auto& pair : languageColorMap) {
                            if (lowerFirstLine.find(pair.first) != std::string::npos) {
                                block.language = pair.first;
                                break;
                            }
                        }

                        if (!block.language.empty()) {
                            // 保护 pendingCodeBlocks 的访问
                            {
                                std::lock_guard<std::mutex> lock(codeBlocksMutex);
                                pendingCodeBlocks.push_back(block);
                            }
                        }

                        codeBuffer.clear(); // 清空代码缓冲
                        firstLineBuffer.clear(); // 清空语言行缓冲
                    }
                }
                else {
                    // 收集并打印代码行
                    codeBuffer += accumulator[pos];
                    printWithColor(std::string(1, accumulator[pos]), currentCodeColor);
                    pos++;
                }
            }
        }

        accumulator.clear();
    }

    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
}

// 新增 TimerThread 函数
void TimerThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 每100ms检查一次

        std::chrono::steady_clock::time_point lastTime;
        {
            std::lock_guard<std::mutex> lock(lastChunkMutex);
            lastTime = lastChunkTime;
        }
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();

        if (duration >= 1000) { // 超过1秒未接收到新的消息块
            // 检查是否需要执行代码
            if (modelLoaded && !isExecuting) {
                std::vector<CodeBlock> blocksToExecute;
                {
                    std::lock_guard<std::mutex> lock(codeBlocksMutex);
                    if (pendingCodeBlocks.empty()) {
                        continue;
                    }
                    isExecuting = true;
                    blocksToExecute.swap(pendingCodeBlocks);
                }

                // 在新线程中执行代码
                std::thread execThread([blocksToExecute]() {
                    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                    std::string lastResultMessage;
                    bool lastSuccess = false;

                    for (auto& blk : blocksToExecute) {
                        std::string resultMessage;
                        bool success = ExecuteCode(blk, resultMessage);

                        // 首先在终端显示结果
                        if (success) {
                            SetConsoleTextAttribute(hConsole, GREEN_COLOR);
                        }
                        else {
                            SetConsoleTextAttribute(hConsole, RED_COLOR);
                        }
                        std::cout << resultMessage << std::endl;
                        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

                        // 保存最后一个代码块的执行结果
                        lastResultMessage = resultMessage;
                        lastSuccess = success;
                    }

                    // 在所有代码块执行完成后，发送最后一个结果给模型
                    if (!lastResultMessage.empty()) {
                        // 不使用 ANSI 颜色编码
                        std::string messageToModel = lastResultMessage + "\n"; // 添加换行符，以确保模型正确解析输入
                        WriteFile(hChildStdInWrite, messageToModel.c_str(), messageToModel.length(), NULL, NULL);
                    }

                    isExecuting = false;
                });
                execThread.detach();
            }
        }
    }
}

int main() {
    // 首先重置终端颜色为默认白色
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    SECURITY_ATTRIBUTES saAttr = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hChildStdInRead, hChildStdInWriteLocal;
    HANDLE hChildStdOutRead, hChildStdOutWrite;

    if (!CreatePipe(&hChildStdInRead, &hChildStdInWriteLocal, &saAttr, 0) ||
        !CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &saAttr, 0)) {
        std::cout << "创建管道失败" << std::endl;
        return 1;
    }

    hChildStdInWrite = hChildStdInWriteLocal; // 赋值给全局变量

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.hStdError = hChildStdOutWrite;
    si.hStdOutput = hChildStdOutWrite;
    si.hStdInput = hChildStdInRead;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    char cmdline[] = "llama-cli -m Qwen2.5-interpreter-f16-master.gguf -p \"**Identity Setup**: \n- You are **Open Interpreter**, operating on the user's Windows computer.\n\n**Execution Capability**: \n- Complete tasks using **Batch scripts** or **Python code**.\n\n**Operation Process**: \n1. **Receive Request**: The user submits an operation request.\n2. **Develop Plan**: Plan the steps and required resources.\n3. **Choose Language**: Select Batch or Python.\n4. **Generate and Output Code**: Provide executable code to the user.\n\n**Code Requirements**: \n- **No User Interaction**: No user input required.\n- **Path Handling**: Use the current directory by default, ensure paths are valid and secure.\n- **Execution Result Handling**: Obtain, parse, and succinctly feedback the results.\n\n**Multi-step Tasks**: \n- Execute complete code snippets step-by-step, maintaining solution consistency. For the same problem, only one solution can be used.\n\n**Security and Efficiency**: \n- Code is safe and harmless, follows best programming practices, ensuring efficiency and maintainability.\n- Must avoid hallucinations.\" -n 4096 -c 16384 --temp 0.2 --top-k 40 -cnv";

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::cout << "启动进程失败: " << GetLastError() << std::endl;
        return 1;
    }

    // 关闭父进程中不需要的管道句柄
    CloseHandle(hChildStdOutWrite);
    CloseHandle(hChildStdInRead);

    // 初始化 lastChunkTime
    {
        std::lock_guard<std::mutex> lock(lastChunkMutex);
        lastChunkTime = std::chrono::steady_clock::now();
    }

    std::thread outputThread(ReadOutputThread, hChildStdOutRead);
    outputThread.detach();

    std::thread timerThread(TimerThread);
    timerThread.detach();

    // 等待模型加载完成,增加超时检测
    std::cout << "正在加载模型，请稍候...\n";
    int timeout = 0;
    while (!modelLoaded && timeout < 300) { // 30秒超时
        Sleep(100);
        timeout++;
    }

    if (!modelLoaded) {
        std::cout << "模型加载超时!\n";
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hChildStdInWrite);
        CloseHandle(hChildStdOutRead);
        return 1;
    }

    std::string input;
    DWORD bytesWritten;
    char ch;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    while (true) {
        std::cout.flush();

        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hStdin, &mode);
        SetConsoleMode(hStdin, mode & ~ENABLE_LINE_INPUT);

        // 获取初始光标位置
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int cursorStartX = 2;
        input.clear();
        while (true) {
            if (ReadConsoleA(hStdin, &ch, 1, &bytesWritten, NULL)) {
                if (ch == '\r') {  // 回车键
                    std::cout << '\n';
                    std::cout << '\n';
                    break;
                }
                else if (ch == '\b') {  // 退格键
                    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);

                    if (!input.empty() && csbi.dwCursorPosition.X > cursorStartX) {
                        if ((unsigned char)input.back() >= 0x80) {
                            if (csbi.dwCursorPosition.X >= cursorStartX + 2) {
                                input.pop_back();
                                if (!input.empty()) input.pop_back();
                                std::cout << "\b\b  \b\b";
                            }
                        }
                        else {
                            input.pop_back();
                            std::cout << "\b \b";
                        }
                    }
                    continue;
                }
                input += ch;
                std::cout << ch;  // 回显字符
            }
        }

        // 恢复输入缓冲
        SetConsoleMode(hStdin, mode);

        if (input == "quit") break;

        input += "\n";
        if (!WriteFile(hChildStdInWrite, input.c_str(), input.length(), &bytesWritten, NULL)) {
            std::cout << "写入失败" << std::endl;
            break;
        }
        FlushFileBuffers(hChildStdInWrite);
    }

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hChildStdInWrite);
    CloseHandle(hChildStdOutRead);

    return 0;
}
