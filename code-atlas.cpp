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
 * 
 * This function scans the input string line by line. It will return the 
 * last line that is not empty. This is useful for displaying the final 
 * meaningful line of output or error messages.
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
 * 
 * This is used for writing text to files in UTF-16 (for PowerShell or batch scripts) 
 * or for Windows API calls that require wide strings.
 */
std::wstring Utf8ToWstring(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstrTo[0], size_needed);
    return wstrTo;
}

/**
 * @brief Read the output from a given pipe handle (e.g., child's stdout).
 * 
 * This function reads the data from the pipe in an ANSI code page, converts it 
 * to wide string, and then re-encodes it as UTF-8. This ensures that we are 
 * eventually dealing with UTF-8 strings in our program.
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
 * 
 * @param block The code block to be executed (contains code text and language).
 * @param resultMessage A string to hold execution results or errors.
 * @param fileExt The file extension used for the temporary script file (e.g. ".py", ".bat").
 * @param interpreter The interpreter executable (e.g. "python", "cmd.exe").
 * @param interpreterArgs Additional command-line arguments (e.g. "/C", "-ExecutionPolicy Bypass -File").
 * @return true if the code execution is successful, false otherwise.
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

    // If it's PowerShell or Batch, we write the file in UTF-16 LE. Otherwise, write in normal text form.
    if (isPowerShell) {
        std::wstring wTempCodeFile = Utf8ToWstring(tempCodeFile);
        std::ofstream outFile(wTempCodeFile.c_str(), std::ios::binary);
        if (!outFile) {
            resultMessage = "Failed to create temporary code file.";
            return false;
        }
        // Write UTF-16 LE BOM
        unsigned char bom[] = { 0xFF, 0xFE };
        outFile.write(reinterpret_cast<char*>(bom), sizeof(bom));
        // Convert code to wide string and write
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

    // Build command line: interpreter + args + script file
    std::string cmdLine;
    if (!interpreter.empty()) {
        cmdLine = interpreter + " ";
        if (!interpreterArgs.empty()) {
            cmdLine += interpreterArgs + " ";
        }
        cmdLine += "\"" + tempCodeFile + "\"";
    } else {
        // If no interpreter is specified, we just run the file
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

        // Wait until the process finishes
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

// Specialized executor classes for different interpreters
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

/**
 * @brief A factory class that creates appropriate CodeExecutor objects based on the language string.
 */
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
    // Note: In the original logic, "shell"/"bash"/"sh" also uses PowerShellExecutor.
    // We keep this behavior unchanged for compatibility.
    return ExecuteCodeWithInterpreter(block, resultMessage, ".sh", "bash", "");
}

/**
 * @brief Factory method that decides which executor to use based on the language string.
 * 
 * If the language is not recognized, it returns a nullptr to indicate unsupported language.
 */
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
        // The original code returns PowerShellExecutor here, 
        // but per its definition we actually call ShellExecutor's logic with "bash".
        // We'll keep the logic as is to avoid changing behavior.
        return new PowerShellExecutor();
    } else {
        return nullptr;
    }
}

/**
 * @brief Execute a code block by creating the appropriate executor. 
 * 
 * If the language is unsupported, a suitable error message is returned.
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
 * @brief Handles parsing and rendering of Markdown-style fenced code blocks (```) 
 *        and collects them into CodeBlock structs for later execution.
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
    /**
     * @brief Processes each character to determine whether we are inside a code block.
     *        It checks for triple backticks (```), collects code, and identifies language.
     */
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
                // If the buffer contains backticks but not enough for a code block, we print them out
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
                // The first line after ``` is often used to specify language
                if (ch == '\n') {
                    collectingFirstLine = false;
                    std::string lowerFirstLine = firstLineBuffer;
                    std::transform(lowerFirstLine.begin(), lowerFirstLine.end(), 
                                   lowerFirstLine.begin(), ::tolower);

                    // Default to a color if no matching language is found
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

                    // Once code block ends, store it in pendingCodeBlocks
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
                // Append the character to the code buffer and print in code color
                codeBuffer += ch;
                SetConsoleTextAttribute(hConsole, currentCodeColor);
                std::cout << ch;
                pos++;
                return true;
            }
        }
    }

    /**
     * @brief Reset internal states to default.
     */
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

    /**
     * @brief Check if currently inside a code block.
     */
    bool isActive() const override {
        return inCodeBlock;
    }
};

// Holds non-code-block markdown states, such as heading, bold, italic
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
 * @brief This class handles character processing for standard Markdown lines (headers, bold, italic).
 */
class MarkdownLineProcessor : public LineProcessor {
public:
    MarkdownLineProcessor(HANDLE hConsole) : hConsole(hConsole) {
        resetLineState();
    }

    /**
     * @brief Reset the line state (e.g., heading detection, bold/italic flags).
     */
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

    /**
     * @brief Process a single character for Markdown syntax (headings, bold, italic).
     */
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

        // If at line start, check for heading syntax (#)
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
                    // If we see a space after #..., that indicates a heading
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
                        // If it's not a space, print out the # we collected
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

        // Normal character output
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

    /**
     * @brief Toggle bold/italic states according to the marker.
     * 
     * If we see the same marker twice in a row (like "**"), we interpret it 
     * as toggling bold. Otherwise, we interpret a single marker as toggling italic.
     */
    void handleFormattingMarker(char marker) {
        if (!lastMarkerUsed && lastMarkerChar == marker) {
            // The same marker repeated -> toggle bold
            state.inBold = !state.inBold;
            lastMarkerUsed = true;
            lastMarkerChar = '\0';
        } else {
            // Single marker -> toggle italic
            state.inItalic = !state.inItalic;
            lastMarkerChar = marker;
            lastMarkerUsed = false;
        }
        SetColor(hConsole, state.currentColor());
    }
};

// Global pointer for line processor so other parts can reference it if needed
MarkdownLineProcessor* gLineProcessor = nullptr;

/**
 * @brief Abstract class for model calling. This defines the interface to initialize, 
 *        start input loops, process responses, and execute code blocks.
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
 * @brief Local model caller, which starts a local process (e.g. llama-cli) and streams data.
 */
class LocalModelCaller : public ModelCaller {
private:
    PROCESS_INFORMATION pi;
    HANDLE hChildStdOutRead = nullptr;
    SECURITY_ATTRIBUTES saAttr;
    std::string systemEndMarker; // Used to detect end of system prompt

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

    /**
     * @brief Initialize the local model by starting the child process with the correct parameters.
     */
    bool initialize(const json& configJson) override {
        std::string systemPrompt = configJson["system"]["prompt"];
        systemEndMarker = systemPrompt.length() >= 20 
                          ? systemPrompt.substr(systemPrompt.length() - 20) 
                          : systemPrompt;
        std::string modelName = configJson["model"]["name"];
        json parameters = configJson["model"]["parameters"];

        // Collect command-line parameters
        std::string TOP_P         = std::to_string(parameters["top_p"].get<double>());
        std::string TOP_K         = std::to_string(parameters["top_k"].get<int>());
        std::string MAX_LENGTH    = std::to_string(parameters["max_length"].get<int>());
        std::string TEMPERATURE   = std::to_string(parameters["temperature"].get<double>());
        std::string CONTEXT_WINDOW= std::to_string(parameters["context_window"].get<int>());

        // Build command line for the local model
        std::string cmdline = "llama-cli -m " + modelName + " -p \"" + systemPrompt + "\"";
        cmdline += " -n " + MAX_LENGTH;
        cmdline += " -c " + CONTEXT_WINDOW;
        cmdline += " --temp " + TEMPERATURE;
        cmdline += " --top-k " + TOP_K;
        cmdline += " --top-p " + TOP_P;
        cmdline += " -cnv";

        // Convert to char array
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

        // Create the process
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

        // Close pipes we don't need
        CloseHandle(hChildStdOutWrite);
        CloseHandle(hChildStdInReadLocal);

        // Initialize the time for the last chunk
        {
            std::lock_guard<std::mutex> lock(lastChunkMutex);
            lastChunkTime = std::chrono::steady_clock::now();
        }

        // Start a thread to read the child's output
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

                // Otherwise, parse the chunk for Markdown
                size_t pos = 0;
                while (pos < chunk.size()) {
                    char ch = chunk[pos];
                    if (codeBlockElement.isActive()) {
                        codeBlockElement.process(ch, pos, chunk, hConsole, currentColor);
                    } else {
                        size_t oldPos = pos;
                        if (codeBlockElement.process(ch, pos, chunk, hConsole, currentColor)) {
                            continue;
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

        // Start the timer thread for periodic code block execution
        std::thread timerThreadObj(TimerThread);
        timerThreadObj.detach();

        std::cout << "Loading model, please wait...\n";

        // Wait for model to load or time out
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

    /**
     * @brief Start the input loop to pass user input to the child process.
     */
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
            // Turn off line input so we can manually handle backspaces etc.
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
                            // Handle backspace for possible multi-byte chars
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

            // Restore console mode
            SetConsoleMode(hStdin, mode);

            if (input == "quit") {
                break;
            }

            // Send input to the model
            input += "\n";
            if (!WriteFile(hChildStdInWrite, input.c_str(), (DWORD)input.length(), &bytesWritten, NULL)) {
                std::cout << "Write failure" << std::endl;
                break;
            }
            FlushFileBuffers(hChildStdInWrite);
        }
    }

    /**
     * @brief Process response from the model. 
     *        (Not used in local mode since we directly read from the child process.)
     */
    void processResponse(const std::string& response) override {}

    /**
     * @brief Execute pending code blocks. 
     *        (In local mode, we rely on TimerThread to handle the actual logic.)
     */
    void executePendingCodeBlocks() override {}
};

/**
 * @brief Size callback for the streaming data from the remote API (used by CURL).
 */
size_t StreamCallback(char* ptr, size_t size, size_t nmemb, void* userdata);

/**
 * @brief This class handles API-based model calling (remote server).
 */
class APIModelCaller : public ModelCaller {
public:
    std::vector<json> messages;
    APIModelCaller() : isNewResponse(true) {}
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

    CodeBlockElement codeBlockElement;
};

/**
 * @brief Sends a request to the remote API in streaming mode, receiving partial responses via the callback.
 */
void APIModelCaller::doOneRequest() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Unable to init CURL" << std::endl;
        return;
    }

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    struct curl_slist* headers = NULL;
    std::string auth_header = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    json request_body = {
        {"model", model_name},
        {"messages", messages},
        {"stream", true}
    };

    for (auto& el : parameters.items()) {
        request_body[el.key()] = el.value();
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
 * @brief The timer thread that periodically checks if code blocks need execution and handles it.
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

        // If more than 1 second has passed since last chunk, we consider executing pending code blocks
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

                // Run execution in a separate thread
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

                        // std::string lastLine = GetLastNonEmptyLine(resultMessage);
                        // std::cout << lastLine << std::endl;
                        std::cout << resultMessage << std::endl;
                        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

                        lastResultMessage = resultMessage;
                        lastSuccess       = success;
                    }

                    // After finishing all blocks, send the result back to the model
                    if (!lastResultMessage.empty()) {
                        std::string messageToModel;
                        if (lastSuccess) {
                            messageToModel = fullOutput;
                        } else {
                            messageToModel = lastResultMessage;
                        }

                        if (caller) {
                            // If we are in API mode, we add a new user message and request
                            APIModelCaller* apiCaller = static_cast<APIModelCaller*>(caller);
                            apiCaller->messages.push_back({
                                {"role", "user"},
                                {"content", messageToModel}
                            });
                            apiCaller->doOneRequest();
                            std::cout << "> " << std::flush;
                        } else {
                            // If we are in local mode, we simply write the result to the child process
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
 * @brief The CURL stream callback for handling the partial responses from the remote API.
 *        This function is invoked repeatedly as data arrives.
 */
size_t StreamCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    APIModelCaller* caller = reinterpret_cast<APIModelCaller*>(userdata);
    size_t totalSize = size * nmemb;
    std::string chunk(ptr, totalSize);
    caller->processResponse(chunk);
    return totalSize;
}

/**
 * @brief Initialize the API caller by reading configuration such as base_url, API key, and model parameters.
 */
bool APIModelCaller::initialize(const json& configJson) {
    try {
        api_base     = configJson["api"]["base_url"];
        api_key      = configJson["api"]["key"];
        model_name   = configJson["model"]["name"];
        system_role  = configJson["system"]["prompt"];
        parameters   = configJson["model"]["parameters"];

        // The first message is usually a "system" role message
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

/**
 * @brief Set the console to UTF-8 encoding for input and output.
 */
void setUTF8Console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

/**
 * @brief Start an input loop for the remote API scenario. 
 *        This reads user input from console and sends it as messages to the API.
 */
void APIModelCaller::startInputOutputLoop() {
    setUTF8Console();
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

        // Send request and stream response
        doOneRequest();
    }
}

/**
 * @brief Process incoming response chunks from the API in streaming mode. 
 *        It detects the "data: [DONE]" sentinel indicating end of response.
 */
void APIModelCaller::processResponse(const std::string& chunk) {
    static std::string currentResponse;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    MarkdownLineProcessor& lineProcessor = *gLineProcessor;

    std::istringstream stream(chunk);
    std::string line;
    while (std::getline(stream, line)) {
        if (line == "data: [DONE]") {
            // End of data
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
            // Each line is prefixed with "data: " according to OpenAI streaming protocol
            std::string jsonData = line.substr(6);
            try {
                json response = json::parse(jsonData);
                if (response["choices"][0].contains("delta") && 
                    response["choices"][0]["delta"].contains("content")) 
                {
                    std::string messageContent = response["choices"][0]["delta"]["content"];
                    currentResponse += messageContent;

                    {
                        std::lock_guard<std::mutex> lock(lastChunkMutex);
                        lastChunkTime = std::chrono::steady_clock::now();
                    }

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
                                continue;
                            } else {
                                lineProcessor.processChar(ch);
                                pos++;
                            }
                        }
                    }
                }
            } 
            catch (json::exception&) {
                // Ignore JSON parsing errors here
            }
        }
    }
}

/**
 * @brief Execute any pending code blocks for the API scenario. 
 *        After execution, we send the results back to the model as a user message.
 */
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
            // Print only the last line of success message
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

// Create a global unique_ptr<ModelCaller> so we can switch between local/remote
std::unique_ptr<ModelCaller> modelCaller;

/**
 * @brief Program entry point.
 */
int main() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

    // Set console to UTF-8 for proper I/O
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Load config from file
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

    // Decide whether to use local or API-based model calling
    if (useAPI) {
        modelCaller = std::make_unique<APIModelCaller>();
    } else {
        modelCaller = std::make_unique<LocalModelCaller>();
    }

    // Initialize the chosen model caller
    if (!modelCaller->initialize(configJson)) {
        return 1;
    }

    // Start the input/output loop
    modelCaller->startInputOutputLoop();

    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
    return 0;
}
