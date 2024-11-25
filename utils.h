#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <windows.h>
#include "codeblock.h"

// 添加辅助函数，用于获取字符串中最后一行非空内容
std::string GetLastNonEmptyLine(const std::string& str);

// 添加辅助函数，用于将 UTF-8 编码的字符串转换为宽字符串
std::wstring Utf8ToWstring(const std::string& str);

// 读取管道输出的辅助函数
std::string ReadPipeOutput(HANDLE hPipe);

// 通用的代码执行函数
bool ExecuteCodeWithInterpreter(const CodeBlock& block, std::string& resultMessage,
    const std::string& fileExt, const std::string& interpreter, const std::string& interpreterArgs);

#endif // UTILS_H
