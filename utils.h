#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <windows.h>
#include "codeblock.h"

// 获取字符串中最后一行非空内容
std::string GetLastNonEmptyLine(const std::string& str);

// 将 UTF-8 转换为宽字符串
std::wstring Utf8ToWstring(const std::string& str);

// 读取管道输出
std::string ReadPipeOutput(HANDLE hPipe);

// 通用代码执行
bool ExecuteCodeWithInterpreter(const CodeBlock& block, std::string& resultMessage,
    const std::string& fileExt, const std::string& interpreter, const std::string& interpreterArgs);

#endif // UTILS_H
