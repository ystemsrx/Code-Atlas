#include "utils.h"
#include <sstream>
#include <fstream>
#include <iostream>

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

std::wstring Utf8ToWstring(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstrTo[0], size_needed);
    return wstrTo;
}

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

bool ExecuteCodeWithInterpreter(const CodeBlock& block, std::string& resultMessage,
    const std::string& fileExt, const std::string& interpreter, const std::string& interpreterArgs) {
    char tempPath[MAX_PATH];
    char tempFileName[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    GetTempFileNameA(tempPath, "code", 0, tempFileName);

    bool success = false;

    // 创建临时代码文件
    std::string tempCodeFile = std::string(tempFileName) + fileExt;
    std::ofstream outFile(tempCodeFile, std::ios::binary);
    if (!outFile) {
        resultMessage = "无法创建临时代码文件。";
        return false;
    }
    outFile << block.code;
    outFile.close();

    // 创建匿名管道
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

    // 构建命令行
    std::string cmdLine;
    if (!interpreter.empty()) {
        cmdLine = interpreter + " ";
        if (!interpreterArgs.empty()) {
            cmdLine += interpreterArgs + " ";
        }
        cmdLine += "\"" + tempCodeFile + "\"";
    }
    else {
        cmdLine = "\"" + tempCodeFile + "\"";
    }

    // 设置启动信息
    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdErrWrite;
    si.hStdInput = NULL;
    si.wShowWindow = SW_HIDE;

    // 创建进程
    if (CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        // 关闭不需要的句柄
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrWrite);

        // 等待进程结束
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
            success = (exitCode == 0);
        }
        else {
            success = false;
        }

        // 读取标准输出和错误输出
        std::string outputMsg = ReadPipeOutput(hStdOutRead);
        std::string errorMsg = ReadPipeOutput(hStdErrRead);

        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // 提取标准输出和错误输出的最后一行非空内容
        std::string lastOutputLine = GetLastNonEmptyLine(outputMsg);
        std::string lastErrorLine = GetLastNonEmptyLine(errorMsg);

        // 构造结果消息
        if (!success) {
            if (!lastErrorLine.empty()) {
                resultMessage = "代码执行失败: " + lastErrorLine;
            }
            else {
                resultMessage = "代码执行失败";
            }
        }
        else {
            if (!lastOutputLine.empty()) {
                resultMessage = "代码执行成功: " + lastOutputLine;
            }
            else {
                resultMessage = "代码执行成功";
            }
        }
    }
    else {
        // 如果进程创建失败，也应视为执行失败
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrWrite);
        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);
        resultMessage = "无法启动子进程。";
        success = false;
    }

    // 清理临时文件
    DeleteFileA(tempCodeFile.c_str());
    DeleteFileA(tempFileName);

    return success;
}
