#ifndef CODEEXECUTOR_H
#define CODEEXECUTOR_H

#include <string>
#include <memory>
#include "codeblock.h"

// 基类：代码执行器
class CodeExecutor {
public:
    virtual ~CodeExecutor() {}
    virtual bool Execute(const CodeBlock& block, std::string& resultMessage) = 0;
};

// 派生类：Python 执行器
class PythonExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// 派生类：Batch 执行器
class BatchExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// 派生类：PowerShell 执行器
class PowerShellExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// 派生类：Shell/Bash 执行器
class ShellExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// 执行器工厂，用于根据语言创建对应的执行器
class ExecutorFactory {
public:
    static CodeExecutor* CreateExecutor(const std::string& language);
};

// ExecuteCode 函数声明
bool ExecuteCode(const CodeBlock& block, std::string& resultMessage);

#endif // CODEEXECUTOR_H
