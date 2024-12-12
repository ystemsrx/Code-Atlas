#include "code_executor.h"
#include "markdown_processor.h"
#include <algorithm>

extern HANDLE hChildStdInWrite;

// 使用给定解释器和参数执行代码块，不改变逻辑
bool ExecuteCodeWithInterpreter(const CodeBlock& block, std::string& resultMessage,
    const std::string& fileExt, const std::string& interpreter, const std::string& interpreterArgs) {
    char tempPath[MAX_PATH];
    char tempFileName[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    GetTempFileNameA(tempPath, "code", 0, tempFileName);

    bool success = false;

    std::string tempCodeFile = std::string(tempFileName) + fileExt;
    std::ofstream outFile(tempCodeFile, std::ios::binary);
    if (!outFile) {
        resultMessage = "无法创建临时代码文件。";
        return false;
    }
    outFile << block.code;
    outFile.close();

    HANDLE hStdOutRead, hStdOutWrite;
    HANDLE hStdErrRead, hStdErrWrite;
    SECURITY_ATTRIBUTES saAttr = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) {
        resultMessage = "无法创建标准输出管道。";
        DeleteFileA(tempCodeFile.c_str());
        DeleteFileA(tempFileName);
        return false;
    }

    if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &saAttr, 0)) {
        resultMessage = "无法创建错误输出管道。";
        CloseHandle(hStdOutRead);
        CloseHandle(hStdOutWrite);
        DeleteFileA(tempCodeFile.c_str());
        DeleteFileA(tempFileName);
        return false;
    }

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

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdErrWrite;
    si.hStdInput = NULL;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrWrite);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
            success = (exitCode == 0);
        } else {
            success = false;
        }

        std::string outputMsg = ReadPipeOutput(hStdOutRead);
        std::string errorMsg = ReadPipeOutput(hStdErrRead);

        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        std::string lastOutputLine = GetLastNonEmptyLine(outputMsg);
        std::string lastErrorLine = GetLastNonEmptyLine(errorMsg);

        if (!success) {
            if (!lastErrorLine.empty()) {
                resultMessage = "代码执行失败: " + lastErrorLine + "\n";
            } else {
                resultMessage = "代码执行失败\n";
            }
        } else {
            if (!lastOutputLine.empty()) {
                resultMessage = "代码执行成功: " + lastOutputLine + "\n";
            } else {
                resultMessage = "代码执行成功\n";
            }
        }
    } else {
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrWrite);
        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);
        resultMessage = "无法启动子进程。";
        success = false;
    }

    DeleteFileA(tempCodeFile.c_str());
    DeleteFileA(tempFileName);
    return success;
}

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
        return new PowerShellExecutor(); // 原代码如此
    } else {
        return nullptr;
    }
}

bool ExecuteCode(const CodeBlock& block, std::string& resultMessage) {
    std::unique_ptr<CodeExecutor> executor(ExecutorFactory::CreateExecutor(block.language));
    if (executor) {
        return executor->Execute(block, resultMessage);
    } else {
        resultMessage = "不支持的语言: " + block.language;
        return false;
    }
}
