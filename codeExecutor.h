#ifndef CODEEXECUTOR_H
#define CODEEXECUTOR_H

#include <string>
#include <memory>
#include "codeblock.h"

// 代码执行器
class CodeExecutor {
public:
    virtual ~CodeExecutor() {}
    virtual bool Execute(const CodeBlock& block, std::string& resultMessage) = 0;
};

// Python 执行器
class PythonExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// Batch 执行器
class BatchExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// PowerShell 执行器
class PowerShellExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// Shell/Bash 执行器
class ShellExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// 根据语言创建对应的执行器
class ExecutorFactory {
public:
    static CodeExecutor* CreateExecutor(const std::string& language);
};

// ExecuteCode
bool ExecuteCode(const CodeBlock& block, std::string& resultMessage);

#endif // CODEEXECUTOR_H
