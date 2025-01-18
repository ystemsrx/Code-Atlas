#include <algorithm>
#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windows.h>

// Using nlohmann::json to handle JSON-related tasks
using json = nlohmann::json;

// Forward declarations to ensure unique_ptr<ModelCaller> is valid
class ModelCaller;
class APIModelCaller;
extern std::unique_ptr<ModelCaller> modelCaller;

// Global variables for child process input handle
HANDLE hChildStdInWrite = nullptr;

// Console color definitions
const WORD DEFAULT_COLOR  = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;  
const WORD CODE_COLOR     = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  
const WORD YELLOW_COLOR   = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;  
const WORD HEADING1_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY;  
const WORD HEADING2_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;  
const WORD HEADING3_COLOR = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  
const WORD HEADING4_COLOR = FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  

const WORD GREEN_COLOR  = FOREGROUND_GREEN | FOREGROUND_INTENSITY;  
const WORD RED_COLOR    = FOREGROUND_RED | FOREGROUND_INTENSITY;  
const WORD ITALIC_COLOR = FOREGROUND_BLUE | FOREGROUND_INTENSITY;  
const WORD BOLD_COLOR   = FOREGROUND_RED | FOREGROUND_INTENSITY;  

// Heading color array
const WORD HEADING_COLORS[4] = {
    HEADING1_COLOR, 
    HEADING2_COLOR, 
    HEADING3_COLOR, 
    HEADING4_COLOR
};

// Maps source code language name to the console color used for code blocks
std::unordered_map<std::string, WORD> languageColorMap = {
    {"python",    CODE_COLOR},
    {"py",        CODE_COLOR},
    {"batch",     CODE_COLOR},
    {"bat",       CODE_COLOR},
    {"sh",        CODE_COLOR},
    {"shell",     CODE_COLOR},
    {"bash",      CODE_COLOR},
    {"cmd",       CODE_COLOR},
    {"powershell",CODE_COLOR},
    {"ps1",       CODE_COLOR},
};

// Indicates whether the model is loaded or not
std::atomic<bool> modelLoaded(false);

// Struct to hold code block information extracted from the text
struct CodeBlock {
    std::string code;
    std::string language;
};

// Shared data for code block execution
std::vector<CodeBlock> pendingCodeBlocks;
std::atomic<bool> isExecuting(false);
std::mutex codeBlocksMutex;
std::chrono::steady_clock::time_point lastChunkTime;
std::mutex lastChunkMutex;

/**
 * @brief Retrieve the last non-empty line from a given string.
 */
std::string GetLastNonEmptyLine(const std::string& str) {
    std::istringstream stream(str);
    std::string line, lastLine;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            lastLine = line;
        }
    }
    return lastLine;
}

/**
 * @brief Convert a UTF-8 encoded std::string into a wide-character std::wstring.
 */
std::wstring Utf8ToWstring(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstrTo[0], size_needed);
    return wstrTo;
}

/**
 * @brief Read the output from a given pipe handle (e.g., child's stdout).
 */
std::string ReadPipeOutput(HANDLE hPipe) {
    const int bufferSize = 4096;
    char buffer[bufferSize];
    DWORD bytesRead;
    std::wstring wOutput;

    while (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        int wchars_num = MultiByteToWideChar(CP_ACP, 0, buffer, bytesRead, NULL, 0);
        std::wstring wstr(wchars_num, 0);
        MultiByteToWideChar(CP_ACP, 0, buffer, bytesRead, &wstr[0], wchars_num);
        wOutput.append(wstr);
    }

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wOutput.c_str(), -1, NULL, 0, NULL, NULL);
    std::string utf8_str(utf8_len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wOutput.c_str(), -1, &utf8_str[0], utf8_len, NULL, NULL);
    return utf8_str;
}

/**
 * @brief Execute a code block with a specified interpreter and arguments, capturing the output.
 */
bool ExecuteCodeWithInterpreter(const CodeBlock& block, 
                                std::string& resultMessage, 
                                const std::string& fileExt, 
                                const std::string& interpreter, 
                                const std::string& interpreterArgs) 
{
    char tempPath[MAX_PATH];
    char tempFileName[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    GetTempFileNameA(tempPath, "code", 0, tempFileName);

    bool success = false;
    std::string tempCodeFile = std::string(tempFileName) + fileExt;

    bool isPowerShell = (interpreter == "powershell");
    bool isBatch      = (interpreter == "cmd.exe");

    // 写文件时，如果是 PowerShell 或批处理脚本，则用 UTF-16 LE
    if (isPowerShell) {
        std::wstring wTempCodeFile = Utf8ToWstring(tempCodeFile);
        std::ofstream outFile(wTempCodeFile.c_str(), std::ios::binary);
        if (!outFile) {
            resultMessage = "Failed to create temporary code file.";
            return false;
        }
        unsigned char bom[] = { 0xFF, 0xFE };
        outFile.write(reinterpret_cast<char*>(bom), sizeof(bom));
        std::wstring wcode = Utf8ToWstring(block.code);
        outFile.write(reinterpret_cast<const char*>(wcode.data()), wcode.size() * sizeof(wchar_t));
        outFile.close();
    } else {
        std::ofstream outFile(tempCodeFile, std::ios::binary);
        if (!outFile) {
            resultMessage = "Failed to create temporary code file.";
            return false;
        }
        outFile << block.code;
        outFile.close();
    }

    // Create pipes for stdout and stderr
    HANDLE hStdOutRead, hStdOutWrite;
    HANDLE hStdErrRead, hStdErrWrite;
    SECURITY_ATTRIBUTES saAttr = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) {
        resultMessage = "Failed to create stdout pipe.";
        DeleteFileA(tempCodeFile.c_str());
        DeleteFileA(tempFileName);
        return false;
    }

    if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &saAttr, 0)) {
        resultMessage = "Failed to create stderr pipe.";
        CloseHandle(hStdOutRead);
        CloseHandle(hStdOutWrite);
        DeleteFileA(tempCodeFile.c_str());
        DeleteFileA(tempFileName);
        return false;
    }

    // Build command line
    std::string cmdLine;
    if (!interpreter.empty()) {
        cmdLine = interpreter + " ";
        if (!interpreterArgs.empty()) {
            cmdLine += interpreterArgs + " ";
        }
        cmdLine += "\"" + tempCodeFile + "\"";
    } else {
        cmdLine = "\"" + tempCodeFile + "\"";
    }

    // Set up startup info and process info for CreateProcessA
    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdOutWrite;
    si.hStdError  = hStdErrWrite;
    si.hStdInput  = NULL;
    si.wShowWindow = SW_HIDE;

    // Execute the process
    if (CreateProcessA(
            NULL, 
            (LPSTR)cmdLine.c_str(), 
            NULL, 
            NULL, 
            TRUE,
            CREATE_NO_WINDOW, 
            NULL, 
            NULL, 
            &si, 
            &pi
        )) 
    {
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrWrite);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
            success = (exitCode == 0);
        } else {
            success = false;
        }

        // Capture stdout and stderr
        std::string outputMsg = ReadPipeOutput(hStdOutRead);
        std::string errorMsg  = ReadPipeOutput(hStdErrRead);

        // Close handles
        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // Prepare final success/error messages
        if (!success) {
            if (!errorMsg.empty()) {
                resultMessage = "Execution failed: " + errorMsg + "\n";
            } else {
                resultMessage = "Execution failed.\n";
            }
        } else {
            if (!outputMsg.empty()) {
                resultMessage = "Execution succeeded:\n" + outputMsg + "\n";
            } else {
                resultMessage = "Execution succeeded.\n";
            }
        }
    } 
    else {
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrWrite);
        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);
        resultMessage = "Unable to start the child process.";
        success = false;
    }

    // Clean up temporary files
    DeleteFileA(tempCodeFile.c_str());
    DeleteFileA(tempFileName);

    return success;
}

/**
 * @brief Abstract class for code execution. Each derived class handles a specific language/interpreter.
 */
class CodeExecutor {
public:
    virtual ~CodeExecutor() {}
    virtual bool Execute(const CodeBlock& block, std::string& resultMessage) = 0;
};

class PythonExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

class BatchExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

class PowerShellExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

class ShellExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

class ExecutorFactory {
public:
    static CodeExecutor* CreateExecutor(const std::string& language);
};

bool PythonExecutor::Execute(const CodeBlock& block, std::string& resultMessage) {
    return ExecuteCodeWithInterpreter(block, resultMessage, ".py", "python", "");
}

bool BatchExecutor::Execute(const CodeBlock& block, std::string& resultMessage) {
    return ExecuteCodeWithInterpreter(block, resultMessage, ".bat", "cmd.exe", "/C");
}

bool PowerShellExecutor::Execute(const CodeBlock& block, std::string& resultMessage) {
    return ExecuteCodeWithInterpreter(block, resultMessage, ".ps1", "powershell", "-ExecutionPolicy Bypass -File");
}

bool ShellExecutor::Execute(const CodeBlock& block, std::string& resultMessage) {
    // 与原逻辑保持一致：对于 "shell"/"bash"/"sh" 也用 PowerShellExecutor 的方式，
    // 这里做一个简单的 bash 执行示例
    return ExecuteCodeWithInterpreter(block, resultMessage, ".sh", "bash", "");
}

CodeExecutor* ExecutorFactory::CreateExecutor(const std::string& language) {
    std::string lang = language;
    std::transform(lang.begin(), lang.end(), lang.begin(), ::tolower);
    if (lang == "python" || lang == "py") {
        return new PythonExecutor();
    } else if (lang == "batch" || lang == "bat" || lang == "cmd") {
        return new BatchExecutor();
    } else if (lang == "powershell" || lang == "ps1") {
        return new PowerShellExecutor();
    } else if (lang == "shell" || lang == "bash" || lang == "sh") {
        // 保持原有兼容
        return new PowerShellExecutor();
    } else {
        return nullptr;
    }
}

/**
 * @brief Execute a code block by creating the appropriate executor.
 */
bool ExecuteCode(const CodeBlock& block, std::string& resultMessage) {
    std::unique_ptr<CodeExecutor> executor(ExecutorFactory::CreateExecutor(block.language));
    if (executor) {
        return executor->Execute(block, resultMessage);
    } else {
        resultMessage = "Unsupported language: " + block.language;
        return false;
    }
}

/**
 * @brief Abstract class to define line-by-line character processing behavior.
 */
class LineProcessor {
public:
    virtual ~LineProcessor() {}
    virtual void processChar(char ch) = 0;
    virtual void resetLineState() = 0;
};

/**
 * @brief Abstract class to handle different Markdown elements (like code blocks).
 */
class MarkdownElement {
public:
    virtual ~MarkdownElement() {}
    virtual bool process(char ch, size_t& pos, const std::string& accumulator, 
                         HANDLE hConsole, WORD& currentColor) = 0;
    virtual void reset() = 0;
    virtual bool isActive() const = 0;
};

/**
 * @brief Handles parsing and rendering of Markdown-style fenced code blocks (```).
 */
class CodeBlockElement : public MarkdownElement {
private:
    bool inCodeBlock = false;
    std::string codeBuffer;
    std::string startBackTickBuffer;
    int startBackTickCount = 0;
    std::string endBackTickBuffer;
    int endBackTickCount = 0;
    bool collectingFirstLine = false;
    std::string firstLineBuffer;
    WORD currentCodeColor = CODE_COLOR;

public:
    bool process(char ch, size_t& pos, const std::string& accumulator, 
                 HANDLE hConsole, WORD& currentColor) override 
    {
        // If we are not in a code block yet, look for ``` opening
        if (!inCodeBlock) {
            if (ch == '`') {
                startBackTickBuffer += '`';
                startBackTickCount++;
                pos++;
                if (startBackTickCount == 3) {
                    collectingFirstLine = true;
                    firstLineBuffer.clear();
                    codeBuffer.clear();
                    startBackTickBuffer.clear();
                    startBackTickCount = 0;
                    inCodeBlock = true;
                    return true;
                }
                return true;
            } else {
                // 如果之前累积了一些 ` 但不足3个，就把它们打印出来
                if (!startBackTickBuffer.empty()) {
                    for (char c : startBackTickBuffer) {
                        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
                        std::cout << c;
                    }
                    startBackTickBuffer.clear();
                    startBackTickCount = 0;
                }
                return false;
            }
        } 
        else {
            // We are inside a code block
            if (collectingFirstLine) {
                // 第一行通常是语言声明
                if (ch == '\n') {
                    collectingFirstLine = false;
                    std::string lowerFirstLine = firstLineBuffer;
                    std::transform(lowerFirstLine.begin(), lowerFirstLine.end(), 
                                   lowerFirstLine.begin(), ::tolower);

                    currentCodeColor = YELLOW_COLOR;
                    for (const auto& pair : languageColorMap) {
                        if (lowerFirstLine.find(pair.first) != std::string::npos) {
                            currentCodeColor = pair.second;
                            break;
                        }
                    }
                    pos++;
                    return true;
                } else {
                    firstLineBuffer += ch;
                    pos++;
                    return true;
                }
            } 
            else if (ch == '`') {
                // Check for code block ending: ```
                endBackTickBuffer += '`';
                endBackTickCount++;
                pos++;
                if (endBackTickCount == 3) {
                    inCodeBlock = false;
                    endBackTickBuffer.clear();
                    endBackTickCount = 0;
                    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

                    // 存储 CodeBlock
                    CodeBlock block;
                    block.code = codeBuffer;
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
                        std::lock_guard<std::mutex> lock(codeBlocksMutex);
                        pendingCodeBlocks.push_back(block);
                    }
                    codeBuffer.clear();
                    firstLineBuffer.clear();
                    return true;
                }
                return true;
            } 
            else {
                codeBuffer += ch;
                SetConsoleTextAttribute(hConsole, currentCodeColor);
                std::cout << ch;
                pos++;
                return true;
            }
        }
    }

    void reset() override {
        inCodeBlock = false;
        codeBuffer.clear();
        startBackTickBuffer.clear();
        startBackTickCount = 0;
        endBackTickBuffer.clear();
        endBackTickCount = 0;
        collectingFirstLine = false;
        firstLineBuffer.clear();
    }

    bool isActive() const override {
        return inCodeBlock;
    }
};

// Holds non-code-block markdown states
struct MarkdownState {
    bool headingDetected = false;
    int headingLevel = 0;
    bool inBold = false;
    bool inItalic = false;
    WORD baseColor = DEFAULT_COLOR;

    WORD currentColor() const {
        if (inBold) {
            return BOLD_COLOR;
        }
        if (inItalic) {
            return ITALIC_COLOR;
        }
        return baseColor;
    }
};

/**
 * @brief Helper function to set console text color.
 */
inline void SetColor(HANDLE hConsole, WORD color) {
    SetConsoleTextAttribute(hConsole, color);
}

/**
 * @brief This class handles character processing for standard Markdown lines.
 */
class MarkdownLineProcessor : public LineProcessor {
public:
    MarkdownLineProcessor(HANDLE hConsole) : hConsole(hConsole) {
        resetLineState();
    }

    void resetLineState() override {
        state.headingDetected = false;
        state.headingLevel    = 0;
        state.inBold          = false;
        state.inItalic        = false;
        state.baseColor       = DEFAULT_COLOR;
        lineStart             = true;
        collectingHash        = false;
        hashCount             = 0;
        lastMarkerChar        = '\0';
        lastMarkerUsed        = true;
    }

    void processChar(char ch) override {
        if (ch == '\n') {
            // Reset format at line end
            state.inBold  = false;
            state.inItalic= false;
            if (!state.headingDetected) {
                state.baseColor = DEFAULT_COLOR;
            }
            SetColor(hConsole, DEFAULT_COLOR);
            std::cout << ch;
            resetLineState();
            return;
        }

        if (lineStart) {
            if (hashCount == 0 && ch == '#') {
                collectingHash = true;
                hashCount = 1;
                return;
            } else if (collectingHash) {
                if (ch == '#') {
                    if (hashCount < 4) {
                        hashCount++;
                    } else {
                        hashCount++;
                    }
                    return;
                } else {
                    if (ch == ' ') {
                        state.headingDetected = true;
                        if (hashCount > 4) {
                            hashCount = 4;
                        }
                        state.headingLevel = hashCount;
                        state.baseColor = HEADING_COLORS[state.headingLevel - 1];
                        SetColor(hConsole, state.currentColor());
                        lineStart = false;
                        collectingHash = false;
                        hashCount = 0;
                        return;
                    } else {
                        // Not a space, print out the # we collected
                        SetColor(hConsole, DEFAULT_COLOR);
                        for (int i = 0; i < hashCount; i++) {
                            std::cout << '#';
                        }
                        collectingHash = false;
                        hashCount = 0;
                        lineStart = false;
                    }
                }
            }
            if (!collectingHash) {
                lineStart = false;
            }
        }

        // Detect bold (**) or italic (*) usage
        if (ch == '*' || ch == '_') {
            handleFormattingMarker(ch);
            return;
        }

        std::cout << ch;
    }

private:
    HANDLE hConsole;
    MarkdownState state;
    bool lineStart = true;
    bool collectingHash = false;
    int hashCount = 0;
    char lastMarkerChar = '\0';
    bool lastMarkerUsed = true;

    void handleFormattingMarker(char marker) {
        if (!lastMarkerUsed && lastMarkerChar == marker) {
            // **: toggle bold
            state.inBold = !state.inBold;
            lastMarkerUsed = true;
            lastMarkerChar = '\0';
        } else {
            // *: toggle italic
            state.inItalic = !state.inItalic;
            lastMarkerChar = marker;
            lastMarkerUsed = false;
        }
        SetColor(hConsole, state.currentColor());
    }
};

MarkdownLineProcessor* gLineProcessor = nullptr;

/**
 * @brief Abstract class for model calling.
 */
class ModelCaller {
public:
    virtual ~ModelCaller() {}
    virtual bool initialize(const json& configJson) = 0;
    virtual void startInputOutputLoop() = 0;
    virtual void processResponse(const std::string& response) = 0;
    virtual void executePendingCodeBlocks() = 0;
};

/**
 * @brief Timer thread function that periodically checks and executes pending code blocks.
 */
void TimerThread();

/**
 * @brief Local model caller, which starts a local process (e.g. llama-cli).
 */
class LocalModelCaller : public ModelCaller {
private:
    PROCESS_INFORMATION pi;
    HANDLE hChildStdOutRead = nullptr;
    SECURITY_ATTRIBUTES saAttr;
    std::string systemEndMarker; 

public:
    LocalModelCaller() {
        ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
        ZeroMemory(&saAttr, sizeof(SECURITY_ATTRIBUTES));
        saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle       = TRUE;
        saAttr.lpSecurityDescriptor = NULL;
    }

    ~LocalModelCaller() {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hChildStdInWrite);
        CloseHandle(hChildStdOutRead);
    }

    bool initialize(const json& configJson) override {
        std::string systemPrompt = configJson["system"]["prompt"];
        systemEndMarker = systemPrompt.length() >= 20 
                          ? systemPrompt.substr(systemPrompt.length() - 20) 
                          : systemPrompt;
        std::string modelName = configJson["model"]["name"];
        json parameters = configJson["model"]["parameters"];

        std::string TOP_P         = std::to_string(parameters["top_p"].get<double>());
        std::string TOP_K         = std::to_string(parameters["top_k"].get<int>());
        std::string MAX_LENGTH    = std::to_string(parameters["max_length"].get<int>());
        std::string TEMPERATURE   = std::to_string(parameters["temperature"].get<double>());
        std::string CONTEXT_WINDOW= std::to_string(parameters["context_window"].get<int>());

        // llama-cli command line
        std::string cmdline = "llama-cli -m " + modelName + " -p \"" + systemPrompt + "\"";
        cmdline += " -n " + MAX_LENGTH;
        cmdline += " -c " + CONTEXT_WINDOW;
        cmdline += " --temp " + TEMPERATURE;
        cmdline += " --top-k " + TOP_K;
        cmdline += " --top-p " + TOP_P;
        cmdline += " -cnv";

        std::vector<char> cmdlineVec(cmdline.begin(), cmdline.end());
        cmdlineVec.push_back('\0');
        char* cmdlineCStr = cmdlineVec.data();

        HANDLE hChildStdInReadLocal, hChildStdOutWrite;
        if (!CreatePipe(&hChildStdInReadLocal, &hChildStdInWrite, &saAttr, 0) ||
            !CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &saAttr, 0)) {
            std::cout << "Pipe creation failed" << std::endl;
            return false;
        }

        STARTUPINFOA si = { sizeof(STARTUPINFOA) };
        si.hStdError  = hChildStdOutWrite;
        si.hStdOutput = hChildStdOutWrite;
        si.hStdInput  = hChildStdInReadLocal;
        si.dwFlags   |= STARTF_USESTDHANDLES;

        if (!CreateProcessA(
                NULL, 
                cmdlineCStr, 
                NULL, 
                NULL, 
                TRUE, 
                CREATE_NO_WINDOW, 
                NULL, 
                NULL, 
                &si, 
                &pi
            )) 
        {
            std::cout << "Process creation failed: " << GetLastError() << std::endl;
            return false;
        }

        CloseHandle(hChildStdOutWrite);
        CloseHandle(hChildStdInReadLocal);

        {
            std::lock_guard<std::mutex> lock(lastChunkMutex);
            lastChunkTime = std::chrono::steady_clock::now();
        }

        // 读取子进程输出的线程
        std::thread outputThread([this]() {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

            CodeBlockElement codeBlockElement;
            WORD currentColor = DEFAULT_COLOR;
            MarkdownLineProcessor lineProcessor(hConsole);
            gLineProcessor = &lineProcessor;

            char buffer[4096];
            DWORD bytesRead;

            while (true) {
                if (!ReadFile(this->hChildStdOutRead, buffer, sizeof(buffer), &bytesRead, NULL) 
                    || bytesRead == 0) 
                {
                    break;
                }

                {
                    std::lock_guard<std::mutex> lock(lastChunkMutex);
                    lastChunkTime = std::chrono::steady_clock::now();
                }

                std::string chunk(buffer, bytesRead);

                // Check if model has finished loading
                if (!modelLoaded && chunk.find(systemEndMarker) != std::string::npos) {
                    modelLoaded = true;
                    SetColor(hConsole, DEFAULT_COLOR);
                    std::cout << "\n\nModel loading complete!\n";
                    SetColor(hConsole, DEFAULT_COLOR);
                    codeBlockElement.reset();
                    lineProcessor.resetLineState();
                    continue;
                }

                // If the model is not loaded yet, print raw output
                if (!modelLoaded) {
                    std::cout << chunk;
                    continue;
                }

                // Otherwise, parse the chunk as markdown
                size_t pos = 0;
                while (pos < chunk.size()) {
                    char ch = chunk[pos];
                    if (codeBlockElement.isActive()) {
                        codeBlockElement.process(ch, pos, chunk, hConsole, currentColor);
                    } else {
                        size_t oldPos = pos;
                        if (codeBlockElement.process(ch, pos, chunk, hConsole, currentColor)) {
                            // 如果进入/退出 codeBlock，这里就处理完了
                        } else {
                            lineProcessor.processChar(ch);
                            pos++;
                        }
                    }
                }
            }

            lineProcessor.processChar('\n');
            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), DEFAULT_COLOR);
        });
        outputThread.detach();

        // 启动 TimerThread
        std::thread timerThreadObj(TimerThread);
        timerThreadObj.detach();

        std::cout << "Loading model, please wait...\n";

        // 等待模型加载
        int timeout = 0;
        while (!modelLoaded && timeout < 300) {
            Sleep(100);
            timeout++;
        }

        if (!modelLoaded) {
            std::cout << "Model loading timed out!\n";
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hChildStdInWrite);
            CloseHandle(hChildStdOutRead);
            return false;
        }

        return true;
    }

    void startInputOutputLoop() override {
        std::string input;
        DWORD bytesWritten;
        char ch;
        CONSOLE_SCREEN_BUFFER_INFO csbi;

        while (true) {
            std::cout.flush();

            HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
            DWORD mode;
            GetConsoleMode(hStdin, &mode);
            // 关闭行输入模式，手动处理回退
            SetConsoleMode(hStdin, mode & ~ENABLE_LINE_INPUT);

            GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
            int cursorStartX = 2;
            input.clear();

            while (true) {
                DWORD readCount;
                if (ReadConsoleA(hStdin, &ch, 1, &readCount, NULL)) {
                    if (ch == '\r') {
                        std::cout << "\n\n";
                        break;
                    } else if (ch == '\b') {
                        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
                        if (!input.empty() && csbi.dwCursorPosition.X > cursorStartX) {
                            if ((unsigned char)input.back() >= 0x80) {
                                if (csbi.dwCursorPosition.X >= cursorStartX + 2) {
                                    input.pop_back();
                                    if (!input.empty()) {
                                        input.pop_back();
                                    }
                                    std::cout << "\b\b  \b\b";
                                }
                            } else {
                                input.pop_back();
                                std::cout << "\b \b";
                            }
                        }
                        continue;
                    }
                    input += ch;
                    std::cout << ch;
                }
            }

            // 恢复控制台模式
            SetConsoleMode(hStdin, mode);

            if (input == "quit") {
                break;
            }

            input += "\n";
            if (!WriteFile(hChildStdInWrite, input.c_str(), (DWORD)input.length(), &bytesWritten, NULL)) {
                std::cout << "Write failure" << std::endl;
                break;
            }
            FlushFileBuffers(hChildStdInWrite);
        }
    }

    void processResponse(const std::string& /*response*/) override {
        // 本地模式直接从子进程 stdout 获取，不需要此函数
    }

    void executePendingCodeBlocks() override {
        // 本地模式依靠 TimerThread 来执行
    }
};

size_t StreamCallback(char* ptr, size_t size, size_t nmemb, void* userdata);

/**
 * @brief This class handles API-based model calling (remote server).
 */
class APIModelCaller : public ModelCaller {
public:
    std::vector<json> messages;
    APIModelCaller() : isNewResponse(true), isLocalHostApi(false) {}
    ~APIModelCaller() {}

    bool initialize(const json& configJson) override;
    void startInputOutputLoop() override;
    void processResponse(const std::string& response) override;
    void executePendingCodeBlocks() override;
    void doOneRequest();

private:
    std::string api_base;
    std::string api_key;
    std::string model_name;
    std::string system_role;
    json parameters;
    bool isNewResponse;

    // 用于区分是否是 localhost 模式
    bool isLocalHostApi;
    // 用于累积 localhost 响应的内容
    std::string localHostAccumulatedResponse;

    CodeBlockElement codeBlockElement;
};

void APIModelCaller::doOneRequest() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Unable to init CURL" << std::endl;
        return;
    }

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    struct curl_slist* headers = NULL;
    // 如果不是 localhost，就带上 Authorization 头
    if (!isLocalHostApi && !api_key.empty()) {
        std::string auth_header = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 根据需求，将参数放到 "options" 下，并忽略 key
    json request_body;
    request_body["model"] = model_name;

    // 将 context_window/max_length 重命名为 num_ctx/num_predict
    json opts;
    opts["temperature"] = parameters["temperature"];
    opts["top_p"]       = parameters["top_p"];
    opts["top_k"]       = parameters["top_k"];
    opts["num_ctx"]     = parameters["context_window"];
    opts["num_predict"] = parameters["max_length"];

    request_body["options"]  = opts;
    request_body["messages"] = messages;

    // 如果不是 localhost，但依旧想使用类似 OpenAI 的流式，可加 "stream": true
    // 反之 localhost 走自己的流式实现（本例子中不加 "stream"）
    if (!isLocalHostApi) {
        request_body["stream"] = true; 
    }

    std::string postData = request_body.dump();
    curl_easy_setopt(curl, CURLOPT_URL, api_base.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Request failed: " << curl_easy_strerror(res) << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

/**
 * @brief The timer thread that periodically checks if code blocks need execution.
 */
void TimerThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::chrono::steady_clock::time_point lastTime;
        {
            std::lock_guard<std::mutex> lock(lastChunkMutex);
            lastTime = lastChunkTime;
        }
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();

        // 如果超过1秒没有新输出，就执行代码块
        if (duration >= 1000) {
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

                ModelCaller* caller = nullptr;
                if (dynamic_cast<APIModelCaller*>(modelCaller.get())) {
                    caller = modelCaller.get();
                }

                // 异步执行代码
                std::thread execThread([blocksToExecute, caller]() {
                    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                    std::string fullOutput;
                    std::string lastResultMessage;
                    bool lastSuccess = false;

                    for (auto& blk : blocksToExecute) {
                        std::string resultMessage;
                        bool success = ExecuteCode(blk, resultMessage);

                        if (success) {
                            SetConsoleTextAttribute(hConsole, GREEN_COLOR);
                            fullOutput += resultMessage;
                        } else {
                            SetConsoleTextAttribute(hConsole, RED_COLOR);
                        }

                        std::cout << resultMessage << std::endl;
                        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

                        lastResultMessage = resultMessage;
                        lastSuccess       = success;
                    }

                    if (!lastResultMessage.empty()) {
                        std::string messageToModel = lastSuccess ? fullOutput : lastResultMessage;

                        if (caller) {
                            // API 模式下，把结果当做 user 消息再发给模型
                            APIModelCaller* apiCaller = static_cast<APIModelCaller*>(caller);
                            apiCaller->messages.push_back({
                                {"role", "user"},
                                {"content", messageToModel}
                            });
                            apiCaller->doOneRequest();
                            std::cout << "> " << std::flush;
                        } else {
                            // 本地模式，直接写入子进程 stdin
                            DWORD bytesWritten;
                            WriteFile(hChildStdInWrite, messageToModel.c_str(), 
                                      (DWORD)messageToModel.length(), &bytesWritten, NULL);
                            FlushFileBuffers(hChildStdInWrite);
                        }
                    }

                    isExecuting = false;
                });
                execThread.detach();
            }
        }
    }
}

/**
 * @brief The CURL stream callback for partial responses.
 */
size_t StreamCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    APIModelCaller* caller = reinterpret_cast<APIModelCaller*>(userdata);
    size_t totalSize = size * nmemb;
    std::string chunk(ptr, totalSize);
    caller->processResponse(chunk);
    return totalSize;
}

bool APIModelCaller::initialize(const json& configJson) {
    try {
        api_base    = configJson["api"]["base_url"];
        api_key     = configJson["api"]["key"];
        model_name  = configJson["model"]["name"];
        system_role = configJson["system"]["prompt"];
        parameters  = configJson["model"]["parameters"];

        // 如果检测到 base_url 中包含 "localhost"，则忽略 key 并进入本地 API 模式
        if (api_base.find("localhost") != std::string::npos) {
            isLocalHostApi = true;
            api_key.clear();
        }

        // 第一次对话通常是 system role
        messages.push_back({
            {"role", "system"},
            {"content", system_role}
        });
    } 
    catch (json::exception& e) {
        std::cerr << "Configuration parse error: " << e.what() << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(lastChunkMutex);
        lastChunkTime = std::chrono::steady_clock::now();
    }
    std::thread timerThreadObj(TimerThread);
    timerThreadObj.detach();

    return true;
}

void APIModelCaller::startInputOutputLoop() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    MarkdownLineProcessor lineProcessor(hConsole);
    gLineProcessor = &lineProcessor;

    while (true) {
        std::string user_input;
        std::cout << "> ";
        std::getline(std::cin, user_input);

        if (user_input.empty()) {
            std::cout << "Empty input, exiting." << std::endl;
            break;
        }

        messages.push_back({
            {"role", "user"},
            {"content", user_input}
        });

        doOneRequest();
    }
}

/**
 * @brief 处理来自远程 API 的流式响应或 JSON 对象。
 */
void APIModelCaller::processResponse(const std::string& chunk) {
    // 记录最后一次接收的时间，用于 TimerThread
    {
        std::lock_guard<std::mutex> lock(lastChunkMutex);
        lastChunkTime = std::chrono::steady_clock::now();
    }

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    MarkdownLineProcessor& lineProcessor = *gLineProcessor;

    // 如果是 localhost API，则逐行解析 JSON 对象
    if (isLocalHostApi) {
        static std::string buffer;
        buffer += chunk;

        size_t pos = 0;
        while (true) {
            size_t newlinePos = buffer.find('\n', pos);
            if (newlinePos == std::string::npos) break;
            std::string line = buffer.substr(pos, newlinePos - pos);
            pos = newlinePos + 1;

            if (!line.empty()) {
                try {
                    json j = json::parse(line);
                    // 如果 done=true，表示回复结束
                    if (j.contains("done") && j["done"].is_boolean() && j["done"].get<bool>() == true) {
                        // 将累积的回复放入 messages
                        if (!localHostAccumulatedResponse.empty()) {
                            messages.push_back({
                                {"role", "assistant"},
                                {"content", localHostAccumulatedResponse}
                            });
                            localHostAccumulatedResponse.clear();
                        }
                        std::cout << "\n\n";
                        isNewResponse = true;
                    } else {
                        // 正在流式输出
                        if (j.contains("message") && j["message"].contains("content")) {
                            std::string partial = j["message"]["content"].get<std::string>();
                            localHostAccumulatedResponse += partial;

                            // 按原先 Markdown/代码块的方式解析输出
                            size_t p = 0;
                            while (p < partial.size()) {
                                char c = partial[p];
                                if (codeBlockElement.isActive()) {
                                    codeBlockElement.process(c, p, partial, hConsole, *(new WORD(DEFAULT_COLOR)));
                                } else {
                                    size_t oldPos = p;
                                    if (codeBlockElement.process(c, p, partial, hConsole, *(new WORD(DEFAULT_COLOR)))) {
                                        // 如果进入/退出 code block，直接 continue
                                    } else {
                                        lineProcessor.processChar(c);
                                        p++;
                                    }
                                }
                            }
                        }
                    }
                } catch (...) {
                    // 忽略解析错误（有可能不是 JSON 或者不完整）
                }
            }
        }
        buffer.erase(0, pos);
    } 
    else {
        // 如果不是 localhost，则按原先类似 OpenAI 的 "data: " 方式解析
        static std::string currentResponse;
        std::istringstream stream(chunk);
        std::string line;
        while (std::getline(stream, line)) {
            // 如果兼容 OpenAI 接口返回 "data: [DONE]"
            if (line == "data: [DONE]") {
                if (!currentResponse.empty()) {
                    messages.push_back({
                        {"role", "assistant"},
                        {"content", currentResponse}
                    });
                    currentResponse.clear();
                }
                lineProcessor.processChar('\n');
                std::cout << "\n\n";
                SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
                isNewResponse = true;
                return;
            }

            if (line.find("data: ") == 0) {
                std::string jsonData = line.substr(6);
                try {
                    json response = json::parse(jsonData);
                    if (response["choices"][0].contains("delta") && 
                        response["choices"][0]["delta"].contains("content")) 
                    {
                        std::string messageContent = response["choices"][0]["delta"]["content"];
                        currentResponse += messageContent;

                        if (!modelLoaded && !messageContent.empty()) {
                            modelLoaded = true;
                        }

                        if (isNewResponse) {
                            std::cout << "\n";
                            isNewResponse = false;
                        }

                        size_t pos = 0;
                        while (pos < messageContent.size()) {
                            char ch = messageContent[pos];
                            if (codeBlockElement.isActive()) {
                                codeBlockElement.process(ch, pos, messageContent, 
                                    hConsole, *(new WORD(DEFAULT_COLOR)));
                            } else {
                                size_t oldPos = pos;
                                if (codeBlockElement.process(ch, pos, messageContent, 
                                    hConsole, *(new WORD(DEFAULT_COLOR)))) 
                                {
                                    // code block 切换
                                } else {
                                    lineProcessor.processChar(ch);
                                    pos++;
                                }
                            }
                        }
                    }
                } 
                catch (json::exception&) {
                    // 忽略 JSON 解析错误
                }
            }
        }
    }

    // 更新 modelLoaded
    if (!modelLoaded) {
        modelLoaded = true;
    }
}

void APIModelCaller::executePendingCodeBlocks() {
    if (pendingCodeBlocks.empty()) {
        return;
    }

    std::vector<CodeBlock> blocksToExecute;
    {
        std::lock_guard<std::mutex> lock(codeBlocksMutex);
        blocksToExecute.swap(pendingCodeBlocks);
    }

    std::string fullOutput;
    std::string lastResultMessage;
    bool lastSuccess = false;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    for (auto& blk : blocksToExecute) {
        std::string resultMessage;
        bool success = ExecuteCode(blk, resultMessage);

        if (success) {
            SetConsoleTextAttribute(hConsole, GREEN_COLOR);
            fullOutput += resultMessage;
            std::string lastLine = GetLastNonEmptyLine(resultMessage);
            std::cout << lastLine << std::endl;
        } else {
            SetConsoleTextAttribute(hConsole, RED_COLOR);
            std::cout << resultMessage << std::endl;
        }
        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

        lastResultMessage = resultMessage;
        lastSuccess       = success;
    }

    if (!lastResultMessage.empty()) {
        std::string messageToModel = lastSuccess ? fullOutput : lastResultMessage;
        messages.push_back({
            {"role", "user"},
            {"content", messageToModel}
        });
        doOneRequest();
    }
}

// Global unique_ptr<ModelCaller>
std::unique_ptr<ModelCaller> modelCaller;

int main() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

    // Set console to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Load config
    std::ifstream configFile("config.json");
    if (!configFile.is_open()) {
        std::cout << "Unable to open config.json" << std::endl;
        return 1;
    }

    nlohmann::json configJson;
    try {
        configFile >> configJson;
    } 
    catch (const nlohmann::json::parse_error& e) {
        std::cout << "Failed to parse config.json: " << e.what() << std::endl;
        return 1;
    }

    bool useAPI = false;
    if (configJson["api"]["enabled"] == true) {
        useAPI = true;
    }

    if (useAPI) {
        modelCaller = std::make_unique<APIModelCaller>();
    } else {
        modelCaller = std::make_unique<LocalModelCaller>();
    }

    if (!modelCaller->initialize(configJson)) {
        return 1;
    }

    modelCaller->startInputOutputLoop();

    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
    return 0;
}
